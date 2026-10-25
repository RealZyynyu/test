# NOVA OS — Produktziele und Freigabekriterien

**Produktziel:** Ein eigenständiges, übersichtliches Desktop-Betriebssystem,
das sich ähnlich unkompliziert wie macOS bedienen lässt, Sicherheit vor
experimentellen Funktionen priorisiert und langfristig auch zum Spielen taugt.
Es bleibt ein **eigener NOVA-Kernel**, keine Linux-Distribution und keine
macOS-Kopie. Icons, Branding und Grafiken bleiben original.

> **Status:** Entwicklungsziele, keine bereits vorhandenen Eigenschaften.
> Der jetzige Prototyp eignet sich ausschließlich für eine virtuelle Maschine.
> Weder normale Spiele noch fremde Apps können bislang gestartet werden.

## 1. Sicherheit geht vor Spielekompatibilität

### Zielarchitektur
- Standardmäßig nur normale Benutzerrechte; administrative Änderungen
  benötigen klare Bestätigung und minimale, kurzlebige Berechtigungen.
- Getrennte Prozesse und virtuelle Adressräume; im Kernel kein ausführbarer
  oder beschreibbarer Benutzercode. CPU-erzwungene W^X-/NX-Speicherrechte.
- Verlässlicher Exception-/Interrupt-/Timer-Pfad, eigener Kernel-Stack pro
  Ausführungskontext, TSS und kontrollierte Systemaufrufe.
- Spiele und Anwendungen als unprivilegierte Prozesse, restriktiver Zugriff
  auf Dateien, Mikrofon, Netzwerk und Eingabegeräte; Berechtigungsdialoge
  nur für tatsächlich sensible Zugriffe.
- Geprüfte Anwendungspakete und Updates, Signaturprüfung,
  atomare Installation, Rollback und Wiederherstellung.
- Persistente Dateisysteme erst freigeben, nachdem Stromausfall- und
  Dateisystembeschädigungs-Tests zuverlässig bestanden wurden.
- Privatsphäre: Telemetrie standardmäßig aus, klare Kontrolle über
  Netzwerkzugriffe und keine versteckten Hintergrundinstallationen.

### Bedrohungsmodell
Schadprogramme in Spielen/Downloads, beschädigte Spieldateien,
fehlerhafte Treiber, fehlerhafte Updates, Prozess-Abstürze und
Netzwerkangriffe dürfen nicht das ganze System oder Benutzerdaten
gefährden. **Keine Freigabe als sicheres System ohne externe Prüfung.**

### Was bereits als Grundlage existiert
ELF-Validierung, vorbereitete (noch nicht aktivierte) Seitentabellen,
nicht ausführbare geplante Stackseiten, FAT16-Read-only und RAM-VFS,
begrenzt große Programmabbilder und Fehler-Rollback.
Diese Komponenten **beweisen keine aktuelle Sandbox oder Isolation**.

## 2. Gaming — realistischer Stufenplan

| Stufe | Voraussetzung | Abnahmetest |
|---|---|---|
| Native 2D-Spiele | Stabiler Usermode, Dateisystem, Audio, Eingabe und zuverlässiger Frame-Timer | Spiel startet, speichert sicher, Ton/Eingabe stabil |
| Virtualisierte Grafikhardware | VirtIO-GPU/VirGL oder anderer konkret unterstützter virtueller Grafikpfad | Wiederholbare Framezeiten und Treibertests |
| Gamecontroller | USB/xHCI/HID und Controller-Zuordnung | An-/Abstecken, Belegung, Pausieren, keine verlorenen Eingaben |
| 3D-/GPU-Spiele | Grafiktreiber, API/Shader-Compiler, GPU-Speicherverwaltung | Gemessene Performance auf explizit unterstützter Hardware |
| Windows-/Steam-Spiele | Breite API-/ABI-/Treiberkompatibilität (separates Großprojekt) | Spiel-für-Spiel-Kompatibilitätsliste mit Tests |

**Wichtig:** Proton ist keine drop-in-Lösung für einen neuen Nicht-Linux-Kernel.
Wir garantieren weder Steam-Spiele noch Multiplayer-Anti-Cheat-Kompatibilität.
Zuerst native NOVA-Spiele, später unterstützte Portierungen, danach
gegebenenfalls aufwendige Windows-Kompatibilität.

**Aktuell implementiert:** Ein eigenes kleines Arcade-Spiel, PS/2-Tastatur,
eine vereinheitlichte Spielaktions-Schnittstelle für WASD/Pfeiltasten,
Tastenstatus und Pause. Sie hat einen Adapter für zukünftige Controller,
aber **noch keinen USB- oder Gamepad-Treiber**.

### Messbare Zukunftsziele (noch nicht erreicht)
- Ein native Testspiel hält möglichst 60 FPS mit protokollierten Framezeiten
  auf einer festgelegten Test-VM und Hardwarekonfiguration.
- Eingabe-zu-Bild-Latenz wird gemessen und dokumentiert, statt geschätzt.
- Spiel-Abstürze beenden nur das betreffende Spiel, nicht NOVA.
- Wechsel zwischen Vollbildspiel, Desktop und Einstellungen ohne Neustart.

## 3. Einfache Bedienung im macOS-Stil

- Einheitliche Menüleiste, Dock, Finder-artiger Dateimanager, Suchfunktion
  und konsistente Fenster (verschieben, vergrößern, schließen).
- **Einfacher Start:** Desktop statt Debug-Konsole; klare Einrichtung
  ohne komplizierte Terminal-Kommandos, leicht erkennbare Optionen.
- Systemweite Einstellungen für Design, Ton, Anzeige, Sprache,
  Eingabegeräte, Netzwerk, Speicher und Datenschutz.
- Unterstützung für Maus, Tastatur, Tastenkürzel und Controller
  ohne mehrere unterschiedliche Bedienkonzepte.
- Gut lesbare, skalierbare Texte, Fokuskennzeichnung, hohe Kontraste,
  Tastaturbedienung, Sprachen und ggf. Bildschirmleser.
- Fehlermeldungen sagen, was passiert ist, ob Daten betroffen sind
  und welche sichere Wiederherstellungsoption existiert.

**Aktuell:** Grafischer Desktop mit Dock, Maus-Support in VMs, Finder-artige
RAM-Dateiansicht, Notes, Settings, Arcade und Dunkelmodus. Noch fehlen
vollwertige Fensterverwaltung, persistente Einstellungen und Accessibility.

## 4. Reale Prioritäten vor dem ersten täglichen Einsatz

1. **Boot-/Kernel-Stabilität:** CPU-Ausnahmen, Interrupts, Timer,
   Crash-Diagnose, reproduzierbare VM-Integrationstests.
2. **Isolierte Apps:** geschütztes Paging mit korrekten Kernel-Mappings,
   TSS, Ring-3-Benutzermodus, Syscalls, Preemption, sicherer ELF-Start.
3. **Daten und Sicherheit:** persistentes Dateisystem, moderne
   Speichertreiber, Zugriffsrechte, sichere Installations-/Updatepfade.
4. **Grafik und Usability:** Fenster-Compositor, skalierte Schrift,
   Eingabegeräte, verständliche Systemverwaltung.
5. **Gaming-Grundlagen:** zuverlässiger Frame-Timer, native Game-API,
   Audio, USB-Controller, Grafiktreiber und Performanceprofiling.
6. **Breite Kompatibilität:** Native App-Ökosysteme, 3D-Grafik und
   experimentelle Windows-App-/Spiel-Kompatibilität.

## 5. Final-ISO-Gates

Ein **finaler Release** benötigt mindestens dokumentierte und bestandene
Kernel-/VM-Tests, sichere persistenten Datenspeicherung, Runtime-
Prozessisolierung, Crash-/Recovery-Tests, signierte/prüfbare Updates,
funktionierende grundlegende Benutzeroberfläche und eine klar definierte
Hardware-Kompatibilitätsliste. Vorher ist ein Windows-Ersatz nicht möglich.

Die ISO wird weiterhin **nicht automatisch gebaut**. Sie darf nur über
den manuell freigegebenen Release-Workflow nach ausdrücklicher Zustimmung
entstehen. Vorher kein Windows-Datenträger überschreiben oder auf echter
Hardware installieren.
