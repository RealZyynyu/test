# NOVA Native: konsistente Offline-Prozessfreigabe

Stand: 25. Oktober 2026. **Quellcode-/Host-Meilenstein, keine Prozessausführung.**

## Ziel und Umsetzung

Der native Befehl `PDROP` darf Berechtigungen und Speicherregistrierungen
nicht entfernen und keine Frames freigeben, solange die Metadaten des
vorbereiteten Offline-Prozesses widersprüchlich sind.

Die neue Funktion `nova_offline_drop_preflight()` prüft vor der ersten
Änderung alle Image-Frame-Tickets (auch gegenüber anderen Images), die
Freigabefähigkeit von Seitentabellen- und Stackframes, identische
Eigentümerkennungen und physische ELF-Identitäten in beiden Deskriptoren,
die erwartete Anzahl an Mappings sowie die exakten PTE-Rechte für
ELF-Blätter, vier Stack-Blätter und die tatsächlich leere Guard-Seite.

Bei einer Abweichung meldet `PDROP` `OFFLINE PROCESS OWNERSHIP INVALID`.
Image-, Stack- und Seitentabellen-Deskriptoren und Registrierungen bleiben
zur Diagnose bestehen.

## Tests

`tests/test_owned_images.c` überprüft beschädigte Stack-Generationen,
ELF-Identitäten, fremde Eigentümer, PTE-Rechte und veraltete ELF-Tickets.
Bei Ablehnung bleiben alle Frames und Deskriptoren unverändert. Nach
Wiederherstellung gelingt die Freigabe zweier voneinander unabhängiger
Offline-Images. Die vorhandene CI führt GCC und Clang/ASan/UBSan sowie
die native Kernel-Kompilierung aus. Ergebnisse müssen am konkreten
GitHub-Actions-Lauf geprüft werden.

## Einschränkungen

Die Vorabprüfung ist **keine atomare Transaktion**. Ein regulär fehlschlagender
Ticket-Callback lässt die nicht freigegebenen Frames samt Tickets für einen
Wiederholungsversuch erhalten. Ein Callback, der entgegen seinem Vertrag
`false` meldet, obwohl er den Frame freigegeben hat, oder Metadaten beschädigt,
kann weiterhin einen nicht automatisch behebbaren Zustand erzeugen. Es gibt
keine SMP-Synchronisation, keine Ring-3-Ausführung, keine aktivierbaren
Prozessadressräume und keine hardwareseitige Isolation. Keine ISO, kein Release,
keine Installation; `final_release` bleibt manuell gesperrt.

## Retrybare Freigabe

Seitentabellen-Frames werden von den Blättern zur Wurzel freigegeben; der
Root-Frame bleibt erhalten, bis alle Kinder freigegeben sind. Stack- und
ELF-Frames werden ebenfalls vom Listenende freigegeben. Erfolgreich
freigegebene Tickets werden sofort aus dem Deskriptor entfernt. Meldet ein
generationserhaltender Callback einen Fehler, bleiben der fehlgeschlagene
Frame und alle noch nicht freigegebenen Tickets für einen erneuten
`PDROP` erhalten. Teilweise zerstörte Adressräume können nicht mehr für
Mappings oder Lookups verwendet werden.

Host-Regressionstests injizieren einmalige Fehler nach bereits erfolgreichen
Freigaben und prüfen Ticket-Erhalt, Frame-Zähler und den erfolgreichen
Wiederholungsversuch. Legacy-Freigabe-Callbacks mit `void` können Fehler
prinzipbedingt nicht melden; der native Eigentümerpfad verwendet boolesche
PMM-Ticket-Callbacks.

## Retrybare Eigentümerstilllegung

Die Freigabe aller Image-Frames und die Stilllegung des PMM-Eigentümers sind zwei
getrennte Operationen: `nova_owner_retire()` verweigert die Stilllegung, solange
noch irgendein Frame diesem Eigentümer gehört. Wenn die Image-Frames bereits
freigegeben sind, ein zusätzlicher oder verwaister Frame aber noch existiert,
darf der Prozessdeskriptor deshalb nicht einfach verschwinden.

`nova_process_drop_owned_guarded()` hält in diesem Fall einen expliziten
`owner_retirement_pending`-Tombstone mit PID und Eigentümer-ID im bisherigen
Prozessslot. Dieser Tombstone enthält keine Image-Frames und wird nicht erneut
als ausführbares Image freigegeben. `PDROP` versucht beim nächsten Aufruf nur noch
die Eigentümerstilllegung. Der Slot wird erst gelöscht, wenn die Stilllegung
erfolgreich ist. Vor einem Retry werden außerdem alle Tombstone-Felder und
alle Image-Seiteneinträge auf den erwarteten leeren Zustand geprüft; ein
beschädigter Tombstone kann den Eigentümer nicht stilllegen. Dieselbe zentrale Teardown-Routine wird für Setup-Rollbacks
verwendet; fehlgeschlagene Stack-/Tabellenfreigaben stoppen den Rollback, bevor
Image-Frames oder der Eigentümer freigegeben werden.

Die Host-Regression `test_retryable_owner_retirement` hält absichtlich einen
zusätzlichen, owner-markierten Frame zurück. Sie prüft, dass die erste Stilllegung
fehlschlägt, der Tombstone erhalten bleibt, der fremde Frame unberührt bleibt und
ein späterer `PDROP`-Versuch nach Freigabe des Orphan-Frames den Eigentümer und
Prozessslot korrekt entfernt.

## PSTAGE-Rollback mit reserviertem Wiederherstellungszustand

Der zuvor offene Fehlerpfad ist im Quellcode adressiert. Wenn `nova_process_stage_impl()` nach einem Fehler nicht alle bereits zugeteilten Frames freigeben kann, liefert es `NOVA_PROCESS_ROLLBACK_PENDING` zurück und markiert den Slot mit `rollback_pending`. Der Slot bleibt ausdrücklich **nicht gestaged**, erhält keine PID und wird weder von der normalen Prozesssuche gefunden noch von einer neuen Staging-Anfrage überschrieben. Die verbliebenen Frames samt Generationstickets bleiben im Deskriptor erhalten.

Die Freigabe- und Zuteilungsprüfungen berücksichtigen diese ausstehenden Slots auch bei der Suche nach physischen Aliassen. Vor dem nächsten `PSTAGE` versucht der Kernel, alle ausstehenden Rollbacks erneut auszuführen. Der Retry gibt nur die noch gespeicherten Tickets frei. Falls die Eigentümerstilllegung anschließend noch scheitert, bleibt ein nullseitiger Pending-Eintrag mit der Eigentümer-ID erhalten; der nächste Retry versucht dann ausschließlich die Stilllegung. Ein Fehler bei der Freigabe darf somit weder den Slot noch die einzige bekannte Eigentümer-ID verlieren.

Ein vom Allocator zurückgegebener verdächtiger Frame wird weiterhin absichtlich nicht freigegeben. Bei owner-gebundenen Zuteilungen bleibt in diesem Fall die Eigentümer-ID auch dann erhalten, wenn die übrigen Image-Frames zurückgerollt wurden. Das verhindert eine vergessene Eigentümerstilllegung, garantiert aber keine automatische Wiederherstellung des verdächtigen Frames.

Der Host-Regressionstest `test_retryable_stage_rollback` injiziert einen Lese- und einen einmaligen Freigabefehler während `PSTAGE`. Er prüft den nicht gestaged reservierten Slot, die Ablehnung beschädigter Pending-Metadaten, die Möglichkeit eines zweiten unabhängigen Images in einem anderen Slot, den Erhalt aller Tickets und die getrennte Wiederholung der Eigentümerstilllegung nach Beseitigung eines zusätzlichen Frames. Zusätzlich simuliert er einen Allocator mit falscher Generation: Der verdächtige Frame wird nicht beschrieben oder freigegeben, und seine Eigentümer-ID bleibt bis zur sicheren Auflösung erhalten. Die Implementierung ist dokumentiert; ihr tatsächlicher Ausführungsstatus muss am CI-Lauf des konkreten Commits verifiziert werden.

## Verbleibende Grenzen

Ein Callback muss garantieren, dass `false` bedeutet: der aktuelle Frame
wurde **nicht** freigegeben. Callback-Verhalten außerhalb dieses Vertrags
ist nicht absicherbar. Die Bereinigung ist single-core und nicht SMP-atomar.
Sie verwaltet Offline-Metadaten, bietet aber keine laufende Prozessisolation.
Keine ISO, kein Release, keine Installation; `final_release` bleibt manuell
gesperrt.

## Trennung aktiver Images und Cleanup-Tombstones

Ein Eintrag mit `owner_retirement_pending` hält nur noch die PMM-Eigentümer-ID
für einen späteren Freigabeversuch fest; seine Image-Frames sind bereits
freigegeben. Er darf daher nicht als lebendes, vorbereitetes Programm
erscheinen. `nova_process_find()` liefert ausschließlich aktive staged Images,
und `nova_process_count()` zählt weder Eigentümer-Tombstones noch ausstehende
PSTAGE-Rollbacks. `nova_process_find_any()` ist für die interne
Lebenszyklusverwaltung vorgesehen, damit `PDROP` den Tombstone weiter
erreichen und nach erfolgreicher Eigentümerstilllegung entfernen kann.

Der Host-Regressionstest prüft, dass ein Tombstone nicht mehr über die normale
Prozesssuche gefunden oder in der Anzahl aktiver Images gezählt wird, während
der gezielte Cleanup-Pfad ihn weiterhin sicher abschließen kann. Das ändert
keine Ausführungs- oder Isolationsgarantie: NOVA führt diese Images weiterhin
nicht in Ring 3 aus.
