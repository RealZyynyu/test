# NOVA OS — Anwendungssicherheit und Berechtigungen (Entwicklungsstand)

## Ziel

NOVA soll Apps und Spiele später ohne Administratorrechte starten. Sie sollen
nicht automatisch Dateien anderer Anwendungen, das Netzwerk, Kamera/Mikrofon
oder Spielcontroller verwenden dürfen. Berechtigungen sollen verständlich
und bei Bedarf widerrufbar sein.

**Aktueller Stand ist ausdrücklich KEINE Sandbox:** NOVA besitzt noch keine
CPU-erzwungene Ring-3-Prozessisolation. Die APIs hier laufen aktuell nur
im Kernel-Modus; der Kernel kann die Prüfungen noch umgehen. Sie dienen als
Vorbereitung einer späteren geschützten Systemaufruf- und App-Laufzeit.

## Bereits umgesetzt im Kernel-Quellcode

Die Datei `kernel/capabilities.h` definiert eine begrenzte
Berechtigungsverwaltung für höchstens vier vorbereitete Programmabbilder.
Jedes neue Programmabbild bekommt bei `PSTAGE` **keinerlei Berechtigung**.
`PACCESS <PID>` zeigt die zugewiesene Bitmaske (initial: 0).
`PDROP` entfernt die Rechte, bevor Speicher und Seitentabellen freigegeben
werden. PID-Referenzen werden mit der aktuellen Prozesstabelle abgeglichen.

Der vorhandene privilegierte Kernel kann ausschließlich durch eine
**explizite interne Entscheidung** drei implementierte Dateirechte erteilen:

| Recht | Bereits vorhandene Prüfung |
| --- | --- |
| RAM-Dateien lesen | `NOVA_CAP_RAM_READ` |
| RAM-Dateien ändern | `NOVA_CAP_RAM_WRITE` |
| FAT16-Dateien vom virtuellen Datenträger lesen | `NOVA_CAP_DISK_READ` |

Über `nova_cap_read_file` und `nova_cap_write_ram` wird erst eine
Berechtigungsprüfung durchgeführt, bevor die kleine VFS-Schicht verwendet
wird. Der dateischreibende Wrapper verweigert geschützte RAM-Dateien und
**immer** Schreibzugriffe auf `DISK:/`. Speicherplätze für veraltete PIDs
können nicht mehr legitim auf Dateien zugreifen.

Die Bits für Netzwerk, Mikrofon und Controller sind **reserviert**, können
aber momentan nicht vergeben werden; dafür existieren noch weder
Systemaufrufe noch passende Treiber. Es gibt absichtlich **keinen**
Terminalbefehl `PGRANT` oder eine vorgetäuschte Berechtigungsdialogbox.

## Was noch fehlt, damit Berechtigungen tatsächlich schützen

1. CPU-erzwungene Isolation: korrekte Kernel-Mappings, Ring 3, TSS,
   Page-Fault-Behandlung, Interrupts, geschützte Syscalls.
2. Ein vollständig vermittelter Dateisystemzugriff. Ein späteres Programm
   darf **niemals** direkt `nova_vfs_read` oder Gerätetreiber aufrufen,
   sondern nur den berechtigungsprüfenden Broker.
3. App-Identitäten und Dateibesitz. Ein einziges globales RAM-Dateisystem
   ist noch keine sichere Aufteilung zwischen verschiedenen Apps.
4. Ein nutzerfreundlicher Berechtigungsdialog mit widerrufbaren,
   eng begrenzten Freigaben. Keine Aufforderungen für harmlose Funktionen.
5. Rechte für Netzwerk, Mikrofon, Kamera, Controller und native
   Spiele-Speicherstände sowie automatisierte Laufzeittests.
6. Sicherheitsprüfung, Recovery, sichere Updates und unabhängiges Audit.

## CI-Prüfungen ohne ISO

In `tests/test_elf_vfs.c` werden über einen synthetischen FAT16-Datenträger
und staged ELF-Dateien unter anderem diese Fälle geprüft:

- Standardmäßig verweigerter RAM-/Festplattenzugriff.
- Nur durch einen vertrauenswürdigen Kernelaufruf vergebene Dateirechte.
- Keine Rechte für noch nicht unterstützte Netzwerk-/Mikrofon-/Controller-APIs.
- RAM-Dateizugriff mit expliziter Freigabe; geschützte RAM-Dateien bleiben
  für App-Schreibzugriffe tabu.
- **Niemals** FAT16-Schreibzugriffe, auch nach einer RAM-Schreibfreigabe.
- Widerruf von Rechten, ungültige Dateipfade sowie abgelaufene Prozess-IDs.
- Neue Programmabbilder erben keinerlei Zugriffsrechte der alten PID.

`tests/test_safety_policy.py` kontrolliert wichtige statische Invarianten
und dass der ISO-Release nur nach manueller Freigabe läuft. Die Tests
beweisen keine Sicherheit einer laufenden Ring-3-Umgebung, da diese
noch nicht implementiert ist.

**Release-Sperre:** Keine neue ISO bis zur ausdrücklich freigegebenen
finalen Version. Bis dahin nur Quellcode-, Compiler- und Simulations-Tests.
