# NOVA Native: Lebenszyklus für physische Frame-Eigentümer

**Stand: 25. Oktober 2026 · Abgegrenzter Sicherheitsmeilenstein**

## Ziel und Implementierung

Bisher akzeptierte der PMM beliebige Nichtnull-Owner-IDs. Jetzt verwaltet `kernel/pmm.h` ein Register für höchstens 64 gleichzeitig aktive Eigentümer. `nova_owner_create(&id)` vergibt eine monoton steigende, nicht-null Kennung. `nova_page_alloc_owned_ticket` akzeptiert nur registrierte aktive Kennungen. `nova_owner_retire(id)` verweigert die Stilllegung, solange mindestens ein physischer Frame dieser Kennung gehört. Stillgelegte IDs werden bis zur nächsten vollständigen PMM-Initialisierung nicht wiederverwendet. Bei vollem Register oder Zählererschöpfung wird die Vergabe verweigert.

Die PMM-Konsistenzprüfung erkennt doppelte Registereinträge, Kennungen außerhalb des Zählerbereichs sowie belegte Frames mit nicht mehr aktiven Eigentümern. Fehler führen zur Verweigerung weiterer Speicheroperationen, nicht zum Überschreiben fremder Frames. Legacy-Zuteilungen mit `owner=0` bleiben kompatibel.

## Automatisierte Tests

`tests/test_frame_owners.c` bezieht Eigentümerkennungen jetzt über die Registrierung. `tests/test_owner_lifecycle.c` prüft mit synthetischem Host-RAM Registrierung, Stilllegung, verweigerte Stilllegung bei lebenden Frames, ABA-Wiederverwendung, 64-Slot-Grenze, Zählererschöpfung und fünf korrupte Registerzustände. Die CI führt GCC und Clang mit AddressSanitizer und UndefinedBehaviorSanitizer aus. Das ist keine VM-Laufzeitprüfung.

## Verbleibende Einschränkungen

**Owner-IDs sind keine PIDs oder geheimen Zugriffstoken.** Kernel-Aufrufer können weiterhin andere gültige IDs angeben. Es gibt noch keine Bindung an `nova_process_table`, keine automatische Bereinigung bei Prozessende und keine eigentümergebundenen ELF-/Stack-/Seitentabellen-Callbacks. Ein kompromittierter Kernel kann konsistent gefälschte Metadaten herstellen. Der PMM ist weiterhin single-core und nicht reentrant. Das Register ist auf 64 gleichzeitige Eigentümer begrenzt.

Ring-3-Ausführung, sichere Syscalls, aktivierte getrennte CR3-Adressräume und vollständige VM-Laufzeittests fehlen. Eine erfolgreiche CI belegt nur die getesteten Host-Eigenschaften und Kernel-Kompilierbarkeit.

**Unveränderte Sicherheitsgrenzen:** Keine ISO, kein Release, keine Installation, keine Änderungen an Windows oder echten Datenträgern. Die manuelle `final_release`-Sperre bleibt bestehen.
