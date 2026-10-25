# NOVA Native: Generationstickets für vorbereitete ELF-Programmseiten

**Stand:** 25. Oktober 2026 · **Status:** Quellcode-/Host-Test-Meilenstein, keine Freigabe

## Ziel und Bedrohungsmodell

Die physische Speicherverwaltung (PMM) unterscheidet bereits zwischen
Programmseiten, Seitentabellen und Stackseiten. Bei Wiederverwendung einer
physischen Seite für dieselbe Rolle ist die reine Kombination aus
Speicheradresse und Rolle jedoch nicht ausreichend: Ein alter Zeiger kann
nach Freigabe und Neuzuteilung auf einen anderen Besitzer zeigen (ABA).

Dieser Meilenstein bindet **die ELF-Programmseiten** an die bei der
Zuteilung ausgegebene PMM-Generation. Ein alter ELF-Deskriptor darf
einen später neu vergebenen IMAGE-Frame nicht mehr freigeben.

## Umsetzung

- `kernel/process_image.h`: Jede `nova_process_page` speichert optional
  die ursprüngliche `generation`. Ein `nova_process_image` kennzeichnet
  die abgesicherte Variante mit `guarded`.
- Die neue Callback-Struktur `nova_process_guard_ops` enthält eine
  generationserhaltende Zuteilung, eine physische Adressprüfung mit
  Originalgeneration und eine generationserhaltende Freigabe.
- `nova_process_stage_guarded()` verwendet diese Callbacks bei der
  Vorbereitung jeder ELF-Seite. Vor dem ersten Schreiben werden die
  Originalgeneration und die physische Identität geprüft. Auch bereits
  bekannte Seiten werden bei der Aliasprüfung überprüft.
- `nova_process_drop_guarded()` prüft vor der ersten Freigabe alle
  beteiligten Frame-Identitäten. Wird eine alte Generation erkannt,
  bleibt der wiederverwendete Frame unangetastet.
- Legacy-Freigaben verweigern den Zugriff auf abgesicherte Images,
  **ohne deren Deskriptor und Originalgeneration zu verlieren**.
- `kernel/main.c`: Die nativen Shell-Pfade `PSTAGE` und `PDROP`
  einschließlich ihrer Fehler-Rollbacks nutzen nun die abgesicherten
  ELF-Callbacks, welche die bestehenden `nova_frame_ticket`-Funktionen
  der PMM verwenden.

Die alten APIs `nova_process_stage()` und `nova_process_drop()` bleiben
für vorhandene Host-Tests und Kompatibilität verfügbar. Sie sind ausdrücklich
**nicht** gegen ABA bei erneuter Zuteilung derselben Frame-Rolle geschützt.
Dies ist keine kryptografische Eigentumsprüfung.

## Automatisierte Tests

`tests/test_image_tickets.c` verwendet die **echte PMM-Rollen- und
Generationstabelle** mit simuliertem, seitenweise ausgerichtetem RAM.
Der CI-Job `image-tickets` kompiliert und testet mit GCC sowie Clang
unter AddressSanitizer und UndefinedBehaviorSanitizer:

1. Erfolgreiches ELF-Staging und Freigabe mit Originalgenerationen.
2. Verweigerung einer Legacy-Freigabe ohne Verlust der Generationstickets.
3. Veraltetes Ticket nach Freigabe und Neuzuteilung als IMAGE-Frame:
   kein Überschreiben und keine Freigabe des neuen Besitzers.
4. Manipulierte Generation, fehlende Generation und veränderte physische
   Identität: keine partielle Freigabe.
5. Rückabwicklung bei Dateilesefehlern und Speichermangel.
6. Ein vom Allocator ausgegebenes, ungültiges Generationsticket darf
   keine ELF-Daten in den verdächtigen Frame schreiben.

Zusätzlich laufen weiterhin die vorhandenen Tests für PMM, Paging,
ELF/VFS, Dateisystem, Berechtigungen und Kernel-Kompilierung.

## Echte Einschränkungen

- **Nur ELF-Programmseiten sind jetzt an Generationstickets gebunden.**
  Stackseiten und Seitentabellen verwenden weiterhin teilweise
  zeigerbasierte Callback-Pfade und benötigen eine eigene Migration.
- Die PMM und die Callbacks laufen nur auf einem CPU-Kern und sind
  nicht für parallele oder reentrante Nutzung ausgelegt.
- Wenn Verwaltungsdaten beschädigt sind, werden verdächtige Seiten
  absichtlich nicht freigegeben. Das verhindert möglicherweise eine
  falsche Freigabe, kann aber zu Speicherverlust führen.
- Ein kompromittierter Kernel kann weiterhin Metadaten manipulieren.
  Generationstickets ersetzen keine hardwareseitige Speicherisolation.
- Die Offline-Seitentabellen und die Debug-Lesefunktionen überprüfen
  die gespeicherten ELF-Generationen noch nicht überall erneut.
- Keine Ring-3-Ausführung, keine aktivierten Prozessadressräume,
  keine sicheren Hardware-Systemaufrufe, keine belastbaren Gaming-Treiber.
- Die Tests sind Host- und Kompilierungstests, **keine vollständigen
  QEMU-/VirtualBox-Laufzeit- oder Hardwaretests**.

## Sicherheits- und Release-Grenzen

Es wurden nur Quellcode, Host-Tests und Dokumentation geändert.
Keine ISO, kein Release, keine Installation, keine Änderungen an Windows
oder realen Datenträgern. Die Workflow-Sperre
`workflow_dispatch.inputs.final_release == true` bleibt unverändert;
ein finales Abbild erfordert weiterhin eine ausdrückliche manuelle Freigabe.

**Nächster sinnvoller Meilenstein:** Originalgenerationen in die
gespeicherten Seitentabellen- oder Stack-Deskriptoren integrieren,
einschließlich überprüfter Rückabwicklung bei beschädigten Tickets.
