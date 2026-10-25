# NOVA Native: Eigentümergebundene Offline-Seitentabellen

**Stand: 25. Oktober 2026 · Ein Sicherheitsmeilenstein · Keine ISO**

## Änderung

Für ein per `PSTAGE` vorbereitetes ELF mit registrierter PMM-Eigentümerkennung reserviert der Offline-Paging-Builder nun **alle PML4/PDPT/PD/PT-Frames** über eigentümergebundene Generationstickets. `nova_page_table.owner` speichert die ursprüngliche Kennung. Die drei neuen, vertrauenswürdigen Callbacks `table_owned_ticket_allocate`, `table_owned_ticket_physical_address` und `table_owned_ticket_release` prüfen Rolle `NOVA_FRAME_PAGE_TABLE`, Eigentümer und Zuteilungsgeneration **vor dem Nullsetzen neu zugeteilter Tabellenframes** und während der vollständigen Freigabevorprüfung. Ein gewöhnlicher PTE-Lookup validiert das Eigentum nicht erneut.

Fehlt bei einem eigentümergebundenen Image auch nur einer dieser Callbacks, verweigert `nova_pt_prepare` die Vorbereitung **vor jeder Tabellenallokation**. Bei beschädigtem Eigentümer oder veralteter Generation gibt die Teardown-Vorprüfung keine Tabellenframes frei. Dies kann Speicher zurückhalten, vermeidet aber die unberechtigte Freigabe eines inzwischen fremden Frames. Nicht eigentümergebundene synthetische Host-Images behalten den bisherigen Legacy-/Generationsticket-Pfad.

## Automatisierte Prüfungen

Die bestehenden GitHub-Actions-Hosttests in `tests/test_owned_images.c` decken die getrennten Eigentümer zweier ELF-Images, Eigentümerbindung der zugehörigen Tabellen, Verweigerung von Legacy-/Fremdeigentümer-Freigaben, fehlende oder unvollständige Callback-Sätze, manipulierte Eigentümerkennungen, Rollback bei Speichermangel, manipulierte Generationen vor dem ersten Schreibzugriff und ABA-Wiederverwendung eines Tabellenframes ab. Der bestehende Job führt den Test mit GCC sowie Clang-Sanitizern aus; der Kernel-Build und alle übrigen CI-Jobs bleiben aktiv. Ergebnisse dürfen erst nach vollständiger neuer CI-Prüfung als bestanden gelten.

## Grenzen und Sicherheitsstatus

**Keine Ring-3-Prozessisolation und keine ausführbaren Nutzerprozesse.** Die PML4-Strukturen werden niemals über CR3 aktiviert. Die Kennungen sind keine authentifizierten Prozessidentitäten; vertrauenswürdiger Kernel-Code kann sie weiterhin manipulieren. **Offline-Stackframes bleiben vorerst ungebunden**, obwohl sie Generationstickets besitzen. SMP-Synchronisation, Syscalls, sichere Kernel-Halbraum-Mappings, VM-Laufzeittests und ein vollständiger Prozesslebenszyklus fehlen. Zwischen Vorbereitung und Freigabe wird die Gültigkeit gespeicherter Tabellenframes nicht bei jedem PTE-Zugriff erneut geprüft. Bei beschädigten Metadaten kann Speicher absichtlich verloren gehen.

Keine ISO, kein Release, keine Installation, keine Änderungen an Windows oder Datenträgern. Die manuelle `final_release`-Sperre bleibt unverändert.
