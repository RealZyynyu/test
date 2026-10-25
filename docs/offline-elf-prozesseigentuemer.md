# NOVA Native: Eigentümergebundene ELF-Frames (Offline)

**Stand: 25. Oktober 2026 · Sicherheitsmeilenstein · Keine ISO**

## Implementierung

Die native `PSTAGE`-Route registriert vor dem ELF-Staging eine neue PMM-Eigentümerkennung und reserviert die ELF-Frames über `nova_page_alloc_owned_ticket`. Das vorbereitete `nova_process_image` speichert diese Kennung zusätzlich zu den bisherigen Frame-Generationen. Alle Eigentums- und physischen Identitätsprüfungen, einschließlich fremder vorbereiteter Images, verwenden jetzt die ursprüngliche Kennung des jeweiligen Images. Ein Ticket mit fremder Eigentümerkennung oder alter Generation berechtigt nicht zur Freigabe.

Die Offline-Seitentabellenprüfung nutzt für eigentümergebundene ELF-Frames einen separaten Callback, der Eigentümer **und** Generation gegen den PMM prüft. Ohne diesen Callback verweigert sie die Vorbereitung, statt auf die zeigerbasierte physische Adressauflösung zurückzufallen. Bestehende Legacy-Tests bleiben über die alten Guard-Callbacks kompatibel.

Nach erfolgreichem `PDROP` wird die Kennung stillgelegt. Bei fehlgeschlagenem Staging und regulär aufgeräumten Frames wird sie ebenfalls stillgelegt. Wenn ein verdächtiger Frame nicht sicher freigegeben werden kann, blockiert die PMM-Registrierung die Stilllegung; dadurch kann eine begrenzte Ressourcenleckage entstehen.

## Tests

`tests/test_owned_images.c` nutzt ausschließlich synthetischen Host-RAM und ein künstliches ELF64. Geprüft werden zwei getrennte Eigentümer, Verweigerung fremder Tickets und Legacy-Freigaben, vollständige Offline-PML4-Vorprüfung, fehlende Eigentümerprüfung, manipulierte Image-Eigentümer, unabhängige Freigabe, Rollback bei Lesefehlern/Speicherknappheit und Wiederverwendung eines Frames mit veralteter Generation. GitHub Actions führt GCC und Clang mit AddressSanitizer/UndefinedBehaviorSanitizer aus. Die übrigen Quellcode- und Kernel-Build-Jobs bleiben unverändert.

## Grenzen

**Das ist keine Ring-3-Prozessisolation.** Kennungen werden von vertrauenswürdigem Kernel-Code vergeben und sind nicht authentifiziert. Der native Pfad bindet **nur ELF-Programmframes** an Eigentümer; Stack- und Seitentabellenframes verwenden weiterhin ungebundene PMM-Tickets. Es gibt weder echte Prozesslebenszyklen noch aktive getrennte CR3-Adressräume, sichere Syscalls, SMP-Synchronisation oder vollständige VM-Laufzeittests. Bei beschädigten Metadaten kann bewusst Speicher verloren gehen. Ein kompromittierter Kernel kann die Schutzdaten manipulieren.

Keine ISO, kein Release, keine Installation und keine Datenträgeränderungen. Die manuelle `final_release`-Sperre bleibt unverändert.
