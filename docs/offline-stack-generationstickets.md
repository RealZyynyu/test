# Offline-Stack: Generationstickets gegen veraltete Frame-Referenzen

Stand: 25. Oktober 2026. **Nur Quellcode und Host-Tests; keine Ausführung von Nutzerprogrammen.**

## Abgegrenzter Meilenstein

Die vorbereiteten vier Stackseiten speichern jetzt zusätzlich zur virtuellen/physikalischen Adresse die **Generation ihrer PMM-Zuteilung**. Ein optionales, nur vollständig konfiguriert akzeptiertes Trio von Stack-Ticket-Callbacks (`allocate`, `physical`, `release`) prüft diese Generation vor dem ersten Beschreiben sowie vor jeder Freigabe. Der native Kernel (`kernel/main.c`) verwendet für Stackseiten nun `NOVA_FRAME_STACK`-Generationstickets. Synthetische Host-Allocatoren können den alten Pfad ohne Tickets weiterhin verwenden; dessen Grenzen bleiben ausdrücklich bestehen.

Der Freigabe-Preflight prüft **alle** Stack- und Seitentabellen-Frames, bevor die erste Seite freigegeben wird. Ist ein Ticket veraltet oder beschädigt, wird der ganze Offline-Adressraumdeskriptor verworfen und die verdächtigen Frames werden bewusst **nicht** freigegeben. Eine zurückgegebene Seite mit gefälschtem Ticket wird vor jeder Nullung abgelehnt. Teilweise konfigurierte Ticket-Callbacks sind ungültig.

## Automatisierte Prüfung

`tests/test_frame_roles.c` ergänzt reale PMM-Integrationstests für (1) erfolgreiche Stack-Vorbereitung und -Freigabe, (2) unvollständige Callback-Konfiguration, (3) verweigerte zeigerbasierte Freigabe geschützter Stacks, (4) erneute Vergabe derselben Stackseite mit anderer Generation (ABA), (5) manipulierte Generationen, (6) Speicherknappheit mit Rollback und (7) gefälschte Generation bei der Zuteilung ohne Überschreiben des betroffenen Frames. Der bestehende Workflow führt diesen Test mit GCC sowie Clang und AddressSanitizer/UndefinedBehaviorSanitizer aus; der vollständige CI-Lauf muss separat erfolgreich sein.

## Echte Grenzen

- Die **Seitentabellen-Frames** werden noch mit rollenbasierten, zeigergebundenen Callbacks verwaltet. Gleichrollen-ABA ist dort weiterhin möglich.
- Ticket-Validierung hängt vom unverfälschten PMM-Ledger und vertrauenswürdigen Kernel-Callbacks ab. Keine kryptografische Identität, kein unabhängiger Prozesseigentümer und kein Schutz vor bösartigem Kernel-Code.
- PMM und vorbereitete Deskriptoren sind nicht threadsicher; zwischen Preflight und Freigabe darf kein konkurrierender Eingriff erfolgen. Fehlgeschlagene Freigaben können Speicher bewusst zurücklassen.
- **Keine Ring-3-Ausführung, keine aktivierten separaten CR3-Adressräume, keine Systemaufrufe und kein Laufzeitschutz durch Hardware.** Die vorbereiteten PML4-Bäume dürfen nicht aktiviert werden.
- Kein ISO-Build, keine Veröffentlichung, keine Installation, kein Zugriff auf echte Datenträger. Die manuelle `final_release`-Sperre bleibt unverändert.

**Status:** Nur bei grünem vollständigem CI-Lauf als getestete Offline-Sicherheitsgrundlage zu bewerten; kein produktionsreifes Betriebssystem.
