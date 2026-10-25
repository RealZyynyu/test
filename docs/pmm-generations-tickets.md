# NOVA Native: Generationstickets für physische Frames

**Stand: 25. Oktober 2026 · Meilenstein: PMM-Generationstickets (Host-Tests, keine ISO)**

## Sicherheitsproblem

Die bisherige rollenbasierte PMM-Verwaltung verhindert, dass eine
ELF-Programmseite versehentlich als Stack- oder Seitentabellenseite
freigegeben wird. Sie erkennt jedoch **keine veralteten Zeiger nach
Wiederverwendung derselben physischen Seite für dieselbe Rolle** (ABA).
Ein altes `NOVA_FRAME_IMAGE`-Handle kann sonst auf eine inzwischen neu
zugeteilte `NOVA_FRAME_IMAGE`-Seite zeigen.

## Implementierter Umfang

`kernel/pmm.h` führt für jeden der maximal 2048 Frames eine
`generations[]`-Kennung und einen monotonen `next_generation`-Zähler.
Bei jeder Zuteilung – auch über die bestehenden rollenbasierten und
allgemeinen PMM-Funktionen – wird eine neue Generation vergeben. Bei
Freigabe wird die Generation des Frames auf null zurückgesetzt. Eine
erschöpfte Generationsfolge wird nicht umgebrochen: Neue Zuteilungen
werden dann verweigert.

Die neue **optionale** API lautet:

- `nova_page_alloc_ticket(rolle, &ticket)`: gibt Adresse, Rolle und
  aktuelle Generation gemeinsam zurück.
- `nova_pmm_ticket_valid(&ticket)`: akzeptiert nur einen aktuell
  belegten Frame mit exakt passender Rolle und Generation.
- `nova_page_free_ticket(&ticket)`: verweigert das Löschen einer
  erneut vergebenen Seite mit einem alten Ticket.
- `nova_pmm_physical_ticket(&ticket, &phys)`: gibt die physische Adresse
  nur für ein gültiges Ticket zurück.

Die PMM-Metadatenprüfung verweigert Operationen, wenn ein belegter Frame
keine plausible Generation hat oder freie Frames eine Generation tragen.
Auch ein beschädigter globaler Generationszähler wird erkannt.

## Automatisierte Tests

`tests/test_frame_tickets.c` prüft auf künstlichem Host-RAM:

1. Wiedervergabe derselben physischen Seite mit **derselben Rolle** und
   Ablehnung eines veralteten Tickets ohne Veränderung der neuen Daten.
2. Rollenwechsel und Rückkehr zur ursprünglichen Rolle.
3. Fortlaufende Generationen auch bei Verwendung der alten PMM-API.
4. Gefälschte Generationen, falsche Rollen, unzulässige Adressen,
   ungültige Ausgabeparameter und Null-Tickets.
5. Beschädigte Generationsfelder, unbenutzte Frames mit falschen
   Metadaten und ungültige globale Zähler.
6. Sicheres Scheitern bei Erschöpfung des 64-Bit-Generationszählers.

GitHub Actions führt diese Tests mit GCC sowie Clang mit ASan/UBSan
zusätzlich zu allen bestehenden Quellcodeprüfungen und der
Kernel-Kompilierung aus. Der Build erstellt **keine ISO**.

## Tatsächliche Grenzen

**Noch keine durchgehende ABA-Sicherung:** Bestehende
`nova_page_free_role`-, `nova_pmm_physical_role`- und Callback-Pfade
in ELF-Staging, Seitentabellen und Stackverwaltung verwenden weiterhin
**nur Zeiger und Rollen**. Sie speichern noch keine Tickets. Eine
veraltete Referenz in diesen Pfaden kann daher trotz dieser Grundlage
weiterhin gefährlich sein. Das ist bewusst nicht als gelöste
Prozessisolation zu bewerten.

Die Kennungen sind weder kryptografisch authentifiziert noch
prozesseindeutig. Sie schützen nicht vor beliebigen Kernel-Schreibzugriffen,
manipulierten PMM-Metadaten oder einem Aufrufer, der absichtlich ein neues
Ticket zu einer fremden Seite erhält. Einmal initialisierter PMM-Zustand
wird nur in Host-Testfixtures zurückgesetzt; Generationen sind daher
nicht über einen Neustart hinweg eindeutig. Die Implementierung bleibt
single-core und nicht reentrant. Doppelte Generationen infolge
zusammenhängender Metadatenmanipulation werden nicht unabhängig
authentifiziert.

Es fehlen weiterhin aktivierte Adressräume, Ring-3-Prozessausführung,
sichere Systemaufrufe, vollständige VM-Laufzeittests sowie
hardwareseitige Prozessisolation. Keine Änderungen an echten
Datenträgern, Windows, Releases oder der manuellen
`final_release`-Sperre.

**Nächster sinnvoller Schritt:** Tickets als unveränderliche
Zuteilungsidentitäten in den gespeicherten ELF-/Stack-/Seitentabellen-
Deskriptoren führen und sie bei jeder Freigabe mitprüfen. Erst
anschließend kann der alte zeigerbasierte Freigabepfad reduziert werden.
