# Offline-Seitentabellen: PMM-Generationstickets

Stand: 25. Oktober 2026. **Nur Quellcode und Host-Tests, keine Aktivierung der Seitentabellen.**

## Abgegrenzter Meilenstein

Jeder beim Offline-Aufbau angelegte PML4-/PDPT-/PD-/PT-Frame erhält neben der geprüften physischen Adresse die **Zuteilungsgeneration** aus dem PMM. Die drei optionalen Table-Ticket-Callbacks (`table_ticket_allocate`, `table_ticket_physical_address`, `table_ticket_release`) werden nur als vollständiges Trio akzeptiert. Der native Kernel stellt diese mit der Rolle `NOVA_FRAME_PAGE_TABLE` bereit; ältere synthetische Host-Allocatoren dürfen für Kompatibilität ohne Tickets arbeiten.

Eine zurückgegebene Seitentabellenseite wird **vor dem ersten Nullschreiben** mit dem Originalticket und gegen bekannte Image-, Stack- und Tabellen-Aliase geprüft. Im Teardown werden **alle** Seitentabellen vor der ersten Freigabe geprüft. Ungültige, veraltete oder fehlende Tickets verhindern jede Freigabe über diesen Deskriptor. Die Stack-Freigabe setzt die erfolgreiche Prüfung des vollständigen Table-Sets voraus. Die reine Zeiger-API darf generationengeschützte Tabellen nicht freigeben.

## Automatisierte Tests

`tests/test_frame_roles.c` prüft: vollständige Tabellen- und Stack-Ticket-Lebenszyklen; Ablehnung unvollständiger Callback-Trios; verweigerte Freigabe ohne Tabellen-Tickets; Wiederzuteilung eines Tabellenframes mit neuer Generation (ABA) ohne Veränderung der neuen Eigentümerdaten; beschädigte Generation; Rollback bei Speicherknappheit; und gefälschte Ticketgeneration vor dem Nullschreiben. Bestehende Legacy-/Rollen-/Stack-Tests bleiben erhalten. Der Workflow `source-checks.yml` führt diesen Test mit GCC und mit Clang/ASan/UBSan aus; der vollständige CI-Lauf muss separat überprüft werden.

## Echte Grenzen

- Der Schutz gilt **nur** für den optional abgesicherten Pfad; synthetische Legacy-Callbacks ohne Tickets bleiben ABA-anfällig. Der native Pfad verwendet die Ticket-Callbacks.
- Die Prüfung vertraut dem unveränderten PMM-Ledger, den Kernel-Callbacks und nicht-paralleler Ausführung. Es gibt keine individuellen Prozesseigentümer, keine Kryptografie und keine Sicherheit gegenüber kompromittiertem Kernel-Code.
- Zwischen Preflight und Freigabe darf keine parallele Änderung erfolgen. Bei widersprüchlichen Angaben werden Frames vorsichtshalber nicht freigegeben; dadurch sind Speicherverluste möglich.
- **Kein Ring 3, keine hardwareseitige Prozessisolation, keine aktivierten CR3-Adressräume, keine Systemaufrufe, keine vollständigen VM-Laufzeittests.** Der PML4-Baum besitzt keine Kernel-Mappings und darf nicht aktiviert werden.
- Keine ISO, kein Release, keine Installation und keine Änderung an echten Datenträgern. Die manuelle `final_release`-Sperre bleibt unverändert.

**Status:** Erst nach vollständig grünem neuem CI-Lauf als getestete Offline-Sicherheitsgrundlage bewerten, niemals als fertiges Betriebssystem.
