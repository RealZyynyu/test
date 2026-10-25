# NOVA OS Native: Zentrale Frame-Rollen im physischen Speicherpool

Stand: 25. Oktober 2026 · Entwicklungszweig: `nova-os-native`

## Ziel und Umfang dieses Meilensteins

Die bisherigen Prüfungen erkannten Aliase innerhalb bekannter vorbereiteter
ELF-Images und Seitentabellen. Der PMM führte jedoch nur eine Belegungsbitmap:
Ein belegter physischer Frame war nicht als **Programmseite, Seitentabelle oder
Stackseite** gekennzeichnet. Ein falscher Freigabe-Callback konnte daher einen
Frame einer anderen Nutzungsklasse freigeben.

Dieser Meilenstein führt eine **zentrale, begrenzte Rollenverwaltung** im
physischen Speicherallocator ein. Die Verwaltung ist Teil des bestehenden
PMM und gilt für seinen gesamten Pool von höchstens 2048 Seiten (8 MiB).

## Implementierung

`kernel/pmm.h` führt für jeden physischen Frame eine Rollenkennung:

- `NOVA_FRAME_FREE`: frei, Bitmap-Bit muss 0 sein.
- `NOVA_FRAME_GENERAL`: bisherige generische Kernel- und Selbsttestseiten.
- `NOVA_FRAME_IMAGE`: vorbereitete ELF-Programmseiten.
- `NOVA_FRAME_PAGE_TABLE`: offline konstruierte Seitentabellen.
- `NOVA_FRAME_STACK`: vorbereitete, nicht ausführbare Stackseiten.

`nova_page_alloc_role`, `nova_page_free_role` und
`nova_pmm_physical_role` prüfen die angeforderte Rolle. Die generischen
Funktionen `nova_page_alloc` und `nova_page_free` verwenden ausschließlich
`NOVA_FRAME_GENERAL`; sie können keine Programm- oder Stackseiten freigeben.

Die PMM-Konsistenzprüfung verlangt, dass jedes belegte Bitmap-Bit genau eine
gültige Nicht-FREI-Rolle hat und freie bzw. unbenutzte Einträge keine Rolle
tragen. Sie läuft **vor** Reservierungen, Freigaben, Löschoperationen und
Adressauskünften. Beim Freigeben wird der Frame weiterhin vollständig
überschrieben und erst anschließend als frei markiert.

Im nativen Kernel verwenden die ELF-Staging-Callbacks die Rolle IMAGE.
Die Offline-Seitentabellen verwenden PAGE_TABLE; die vier vorbereiteten
Stackseiten verwenden STACK. `nova_pt_ops` erlaubt dafür getrennte
physische Prüf- und Stack-Callbacks. Die bisherige generische Callback-
Schnittstelle bleibt für Host-Tests verfügbar. Eine nur teilweise
konfigurierte Stack-Override-Gruppe wird vor der ersten Zuteilung abgelehnt.

## Automatisierte Tests und Verifikation

`tests/test_frame_roles.c` verwendet ausschließlich simulierten Host-RAM.
Der Test prüft unter anderem:

- ungültige Rollen und Verweigerung rollenfremder Freigaben;
- keine Datenlöschung bei einem abgelehnten Freigabeversuch;
- Übereinstimmung von Bitmap und Rollenverwaltung einschließlich freier
  und außerhalb des Pools liegender Einträge;
- Recycling eines Frames unter **einer anderen Rolle**: ein alter
  rollenfremder Freigabeversuch muss scheitern;
- echtes Zusammenwirken von ELF-Staging, Offline-PML4 und vier Stackseiten
  mit rollengetrennten Callbacks;
- verweigerte Bereitstellung bei unvollständigen Stack-Callbacks;
- verweigerte Freigabe bei manipulierten Rollen in der Offline-Verwaltung;
- vollständige reguläre Freigabe und Nullbelegung nach dem positiven Test.

Die CI führt den Integrationstest mit GCC und mit Clang unter
AddressSanitizer/UndefinedBehaviorSanitizer aus. Zusätzlich bleiben
die bisherigen PMM-, ELF-, Paging-, Sicherheits- und Kernel-Compile-Jobs
unverändert aktiv. Ein CI-Erfolg ist ein **Host- und Compile-Nachweis**,
kein Nachweis einer erfolgreichen VM-Ausführung.

## Grenzen und offene Sicherheitsfragen

Die Rollen sind **keine authentifizierten Eigentumsnachweise**. Ein Fehler,
der Bitmap und Rollen gemeinsam konsistent manipuliert, kann unentdeckt
bleiben. Die Rolle unterscheidet nicht zwischen verschiedenen PIDs;
ein alter Zeiger kann nach Freigabe und erneuter Zuteilung **derselben Rolle**
nicht als veraltet erkannt werden. Generationstoken und eine zuverlässige
prozessbezogene Frame-Zuordnung fehlen weiterhin.

Die Rollenprüfung ist nicht threadsicher und setzt einen vertrauenswürdigen
PMM und einen einzigen CPU-Kern voraus. Im Fehlerfall können bewusst
nicht freigegebene Frames verloren gehen. Ein manipulierter Kernel selbst
kann diese Metadaten überschreiben; dies ist keine Sicherheitsgrenze gegen
Kernel-Code.

Vor allem sind die vorbereiteten Seitentabellen weiterhin **offline**:
Es gibt weder CR3-Aktivierung noch Ring-3-Prozessausführung, geschützte
Systemaufrufe, einen geprüften TSS/IST-Übergang oder hardwareseitig
durchgesetzte Prozessisolation. Der Kernel ist nicht produktionsreif.
Vor einer finalen Prüfung sind unter anderem VM-Laufzeittests,
Speicher-Eigentümer pro Prozess, sichere Adressraumwechsel und
systematische Fehlerbehandlung erforderlich.

## Release-Sperre

Es wurden nur Quellcode, Host-Tests, CI-Konfiguration und Dokumentation
geändert. **Keine ISO, kein Release, keine Installation und keine
Schreibzugriffe auf echte Datenträger oder Windows.** Der finale
`build-native-iso.yml`-Workflow bleibt ausschließlich manuell über
`workflow_dispatch` mit `final_release == true` freigeschaltet.
