# NOVA Native: konsistente Offline-Prozessfreigabe

Stand: 25. Oktober 2026. **Quellcode-/Host-Meilenstein, keine Prozessausführung.**

## Ziel und Umsetzung

Der native Befehl `PDROP` darf Berechtigungen und Speicherregistrierungen
nicht entfernen und keine Frames freigeben, solange die Metadaten des
vorbereiteten Offline-Prozesses widersprüchlich sind.

Die neue Funktion `nova_offline_drop_preflight()` prüft vor der ersten
Änderung alle Image-Frame-Tickets (auch gegenüber anderen Images), die
Freigabefähigkeit von Seitentabellen- und Stackframes, identische
Eigentümerkennungen und physische ELF-Identitäten in beiden Deskriptoren,
die erwartete Anzahl an Mappings sowie die exakten PTE-Rechte für
ELF-Blätter, vier Stack-Blätter und die tatsächlich leere Guard-Seite.

Bei einer Abweichung meldet `PDROP` `OFFLINE PROCESS OWNERSHIP INVALID`.
Image-, Stack- und Seitentabellen-Deskriptoren und Registrierungen bleiben
zur Diagnose bestehen.

## Tests

`tests/test_owned_images.c` überprüft beschädigte Stack-Generationen,
ELF-Identitäten, fremde Eigentümer, PTE-Rechte und veraltete ELF-Tickets.
Bei Ablehnung bleiben alle Frames und Deskriptoren unverändert. Nach
Wiederherstellung gelingt die Freigabe zweier voneinander unabhängiger
Offline-Images. Die vorhandene CI führt GCC und Clang/ASan/UBSan sowie
die native Kernel-Kompilierung aus. Ergebnisse müssen am konkreten
GitHub-Actions-Lauf geprüft werden.

## Einschränkungen

Die Vorabprüfung ist **keine atomare Transaktion**. Nachträglich
fehlschlagende oder fehlerhafte Freigabe-Callbacks können partielle
Freigaben und Speicherlecks verursachen. Es gibt keine SMP-Synchronisation,
keine Ring-3-Ausführung, keine aktivierbaren Prozessadressräume und keine
hardwareseitige Isolation. Eine kompromittierte Kernel-Komponente kann
Metadaten verändern. Keine ISO, kein Release, keine Installation.
`final_release` bleibt manuell gesperrt.
