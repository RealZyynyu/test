# NOVA OS Native: PMM-Metadatenprüfung (Host-/Quellcode-Meilenstein)

Stand: 25. Oktober 2026. Bereich: `kernel/pmm.h`, `tests/test_pmm.c`.

## Problem und Ziel

Der physische Seitenallocator (PMM) verwaltet maximal 2048 Seiten à 4096
Byte in einem Limine-`USABLE`-Speicherbereich. Die bisherigen Funktionen
verwendeten die veränderbaren Felder `pages`, `in_use` und `bitmap`
direkt. Bei beschädigten Zählern konnte die Bitmapsuche außerhalb des
Arrays laufen oder die Anzeige freier Seiten durch einen Unterlauf
falsche Werte liefern. Eine unzuverlässige Freigabe hätte zudem
Speicher löschen können, bevor die Verwaltungsdaten geprüft wurden.

**Ziel dieses einzelnen Meilensteins:** Offensichtlich widersprüchliche
PMM-Metadaten vor jedem Speicherzugriff durch den Allocator erkennen und
den betroffenen Vorgang ohne weitere Speicheränderung ablehnen.

## Umsetzung

`nova_pmm_metadata_valid()` prüft:

- Initialisierung, gültige Poolgröße (8 bis 2048 Seiten) und
  `in_use <= pages`.
- Seitenausrichtung der physischen Basis und des HHDM-Offsets sowie
  überlauffreie physische und HHDM-Adressgrenzen.
- Dass alle Bitmap-Bits außerhalb des aktiven Pools null sind,
  einschließlich ungenutzter Bits des letzten angebrochenen Bytes.
- Dass die Anzahl gesetzter Bits exakt dem Belegungszähler entspricht.

`nova_page_alloc()`, `nova_page_free()` und `nova_pmm_physical()`
verweigern bei einer fehlgeschlagenen Prüfung den Vorgang. Auch
`nova_free_pages()` liefert dann einen konservativen Wert von null;
`nova_pmm_self_test()` bricht ab. Die Initialisierung lehnt einen
nicht seitenausgerichteten HHDM-Offset ab. Bei beschädigter Verwaltung
wird **kein verdächtiger Frame freigegeben oder überschrieben**.

## Automatisierte Tests

`tests/test_pmm.c` enthält zehn gezielte Beschädigungen, darunter
überhöhte und zu kleine Poolgrößen, unplausible Belegungszähler,
entfernte Belegungsbits, gesetzte Bits außerhalb des Pools, überlaufende
Adressgrenzen und einen nicht ausgerichteten HHDM-Offset.
Die Tests prüfen, dass Allokation, Freigabe und physische Adressabfrage
fehlschlagen, die Belegungsanzeige konservativ bleibt und eine bereits
belegte Testseite **unverändert** bleibt. Nach Wiederherstellung
konsistenter Metadaten funktionieren Freigabe und Zero-on-free wieder.
Zusätzlich wird ein unzulässig ausgerichteter HHDM-Offset bei der
Initialisierung abgewiesen.

Die bestehende GitHub-Actions-Quelleprüfung führt den PMM-Test mit GCC
sowie Clang/ASan/UBSan aus und kompiliert den nativen Kernel **ohne ISO**.
Ein grüner CI-Lauf bestätigt ausschließlich diese geprüften Eigenschaften,
keine vollständige Systemsicherheit.

## Echte Grenzen und offene Aufgaben

- **Keine authentifizierte Eigentumsverwaltung:** Zusammen passend
  manipulierte Bitmap und Zähler, veränderte zulässige Basisadressen oder
  ein kompromittierter Allocator können diese Konsistenzprüfung umgehen.
- **Keine Prozessisolation:** Es werden keine neuen CPU-Adressräume
  aktiviert, kein Ring 3 eingerichtet und keine untrusted Apps gestartet.
- **Nur Single-Core-Prototyp:** Die Prüfung ist nicht atomar. Zwischen
  Prüfung und Änderung darf kein anderer Kern/Interrupt dieselbe
  PMM-Verwaltung verändern. SMP-Locking fehlt weiterhin.
- **Kosten:** Die vollständige Bitmap wird bei jeder PMM-Operation
  geprüft. Für den begrenzten 8-MiB-Pool ist dies akzeptabel, für eine
  produktive Speicherverwaltung wäre eine andere Strategie nötig.
- **Fail-closed bedeutet ggf. Speicherverlust:** Bei beschädigten
  Metadaten kann NOVA Seiten nicht sicher zurückgeben; es erfolgt
  keine automatische Reparatur.
- **Keine VM-Laufzeitprüfung:** Host-Tests und Kernel-Kompilierung
  ersetzen weder QEMU/VirtualBox-Bootprüfungen noch Fault-Injection
  auf echter CPU-Hardware.

## Freigabegrenzen

Die Änderung bleibt eine reine Quellcode-/Host-Test-Härtung.
Keine ISO-Erzeugung, kein Release, keine Installation, keine
Datenträger-Schreiboperation und keine Änderung an Windows.
Der Workflow für eine finale ISO bleibt ausdrücklich manuell und
an `final_release: true` gebunden.
