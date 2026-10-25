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

## Noch offener Fehlerpfad: fehlgeschlagener PSTAGE-Rollback

Die hier ergänzte Tombstone-Logik deckt die Freigabe nach erfolgreicher Image-Staging-Phase ab. Ein separater Randfall bleibt offen: Wenn bereits während `nova_process_stage_impl()` ein Fehler auftritt und dessen eigener Freigabe-Callback ebenfalls fehlschlägt, ignoriert der aktuelle Staging-Code den Rückgabewert des Rollbacks. Dann kann ein `staged=false`-Deskriptor mit verbleibenden Tickets zurückbleiben und später von einer neuen Staging-Anfrage als freier Slot angesehen werden. Das ist **nicht** durch den neuen Owner-Tombstone behoben und muss vor einer Finalprüfung separat korrigiert und mit Callback-Fehlerinjektion getestet werden.

## Verbleibende Grenzen

Ein Callback muss garantieren, dass `false` bedeutet: der aktuelle Frame
wurde **nicht** freigegeben. Callback-Verhalten außerhalb dieses Vertrags
ist nicht absicherbar. Die Bereinigung ist single-core und nicht SMP-atomar.
Sie verwaltet Offline-Metadaten, bietet aber keine laufende Prozessisolation.
Keine ISO, kein Release, keine Installation; `final_release` bleibt manuell
gesperrt.
