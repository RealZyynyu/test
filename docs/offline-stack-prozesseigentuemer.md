# Offline-Stackframes mit Prozesseigentümer

Stand: 25.10.2026; Branch `nova-os-native`.

## Meilenstein
Bei einem vorbereiteten ELF-Image mit registriertem PMM-Eigentümer werden
nun auch die vier Stackframes über eigentümergebundene Generationstickets
angelegt, vor dem Nullsetzen geprüft und beim Abbau erneut geprüft.
Der Owner wird vor dem Zurücksetzen der Seitentabellen gesichert. Fehlt
einer der drei `stack_owned_ticket_*`-Callbacks, wird der gesamte
Owned-Prepare-Pfad abgelehnt. Legacy-Hostpfade ohne Eigentümer bleiben
kompatibel. Die Stackframes, ELF-Seiten und Seitentabellen eines Images
teilen nun dieselbe Eigentümerkennung.

## Tests
`tests/test_owned_images.c` enthält zusätzliche Host-Regressionstests
für getrennte Eigentümer, fehlende Callbacks, manipulierte Owner und
Generationen, ABA-Wiederzuteilung, Speichermangel-Rollback und gefälschte
Tickets ohne Beschreiben verdächtiger Frames. Der bestehende Workflow
kompiliert diesen Test mit GCC und Clang mit ASan/UBSan. Der konkrete
CI-Erfolg ist anhand des Commit-Laufs zu prüfen.

## Echte Grenzen
Nur Offline-Datenstrukturen: kein Ring 3, keine Aktivierung von CR3,
keine sicheren Syscalls, kein Prozessscheduler, keine SMP-Synchronisierung
und keine hardwareseitige Isolation. Eigentümerkennungen sind keine
authentifizierten Prozess-IDs. Kompromittierter Kernel-Code und
manipulierte Allocator-Callbacks bleiben außerhalb dieses Schutzes.
Bei zweifelhafter Frame-Identität werden verdächtige Frames absichtlich
nicht freigegeben, was Speicherverluste verursachen kann.

Keine ISO, kein Release, keine Installation, keine Datenträgeränderungen.
Die manuelle `final_release`-Sperre bleibt unverändert.
