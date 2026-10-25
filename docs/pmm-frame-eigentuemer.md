# NOVA Native: Eigentümergebundene PMM-Generationstickets

**Stand: 25. Oktober 2026 · Einzelmeilenstein: Eigentümer-Ledger für physische Frames**

## Problem und abgegrenztes Ziel

Rollen und Generationen unterscheiden bisher Nutzungsart und Zuteilungszeitpunkt, nicht aber zwei gleichartige Zuteilungen für unterschiedliche zukünftige Prozesse. Dieser Meilenstein führt eine **zusätzliche Eigentümerkennung pro belegtem physischem Frame** und eine explizite, eigentümergebundene Ticket-API ein. Es ist eine Host-getestete Grundlage für spätere Prozessisolation, **keine bereits implementierte PID- oder Ring-3-Sicherheitsgrenze**.

## Umsetzung

In `kernel/pmm.h` enthält der begrenzte PMM-Pool nun `owners[NOVA_PAGE_POOL_CAP]` mit höchstens 2048 Einträgen. `0` steht ausschließlich für die bisherigen Kernel-/Legacy-Zuteilungen. Ein vertrauenswürdiger Kernel-Aufrufer kann über `nova_page_alloc_owned_ticket(rolle, owner, &ticket)` einen Frame mit einer **nicht-null** Eigentümerkennung reservieren. Das Ticket enthält Frame-Adresse, Rolle, Generation und Eigentümer.

`nova_pmm_ticket_valid`, `nova_page_free_ticket` und `nova_pmm_physical_ticket` prüfen die gesamte Kombination. Eine falsche Eigentümerkennung oder veraltete Generation verweigert die Operation. Zeigerbasierte Legacy-Freigaben und -Adressauskünfte dürfen eigentümermarkierte Frames nicht mehr freigeben oder als eigene Frames behandeln. Die PMM-Konsistenzprüfung verlangt, dass freie und ungenutzte Frames **keinen** Eigentümer besitzen. Beim Freigeben wird der Frame wie bisher gelöscht und der Eigentümer auf null zurückgesetzt.

Die bestehenden Zuteilungen, auch die aktuell vorbereiteten ELF-, Stack- und Seitentabellen-Frames, bleiben **bewusst Legacy-Zuteilungen mit owner = 0**. Es werden noch keine PIDs generiert, keine Prozesslebenszyklen verbunden und keine realen Adressräume aktiviert.

## Automatisierte Prüfungen

`tests/test_frame_owners.c` verwendet ausschließlich simulierten Host-RAM und prüft:

- Zwei Eigentümer mit identischer Frame-Rolle, korrekte physische Ticket-Adressen und Ablehnung fremder Eigentümertickets.
- Ablehnung von Legacy-Freigaben und Legacy-Adressabfragen für eigentümermarkierte Frames, ohne deren Daten zu verändern.
- Wiederverwendung derselben Seite mit anderer **und** gleicher Eigentümerkennung; alte Generationstickets bleiben ungültig.
- Ungültige Rollen und Eigentümer-ID 0; beschädigte Eigentümerfelder freier und unbenutzter Frames.
- Ablehnung bei veränderter Eigentümerzuordnung und erschöpftem Generationszähler; vollständiges Nullsetzen beim erfolgreichen Freigeben.
- Rückwärtskompatibilität der bisherigen Generationstickets.

Der neue Job `frame-owners` in `source-checks.yml` kompiliert und startet diese Tests mit GCC und Clang/ASan/UBSan. Die bestehenden CI-Jobs einschließlich Kernel-Kompilierung laufen weiterhin. Ein grüner CI-Lauf belegt **Host-Verhalten und Kompilierbarkeit**, keine VM-Laufzeit oder echte Prozessisolation.

## Echte Grenzen und nächste Arbeit

Eigentümer-IDs werden vom **vertrauenswürdigen Aufrufer** übergeben, nicht vom PMM kryptografisch authentifiziert. Ein fehlerhafter oder bösartiger Kernel-Aufrufer könnte IDs oder Ticketfelder fälschen; eine konsistent manipulierte Eigentümer-Tabelle ist nicht unabhängig nachweisbar. Die Legacy-Pfade bleiben für unmarkierte Frames verwendbar. Das System ist weiterhin **single-core, nicht reentrant**; ein paralleler Zugriff zwischen Validierung und Freigabe wäre unsicher. Bei inkonsistenten Daten verweigert der PMM vorsichtshalber die Freigabe, was zu Speicherverlust führen kann.

Für echte Isolation fehlen weiterhin ein vertrauenswürdiger Prozess-/Owner-Lebenszyklus, owner-gebundene ELF-/Stack-/Seitentabellen-Callbacks, aktivierte getrennte CR3-Adressräume, Ring-3-Ausführung, sichere Syscalls, Schutz der Kernel-Mappings und VM-Laufzeittests. Die vorhandenen Offline-Seitentabellen dürfen **nicht** aktiviert werden.

**Sicherheitsgrenze:** Keine ISO, kein Release, keine automatische Installation, keine Änderungen an Windows oder realen Datenträgern. Die bestehende manuelle `final_release`-Sperre wird nicht verändert.
