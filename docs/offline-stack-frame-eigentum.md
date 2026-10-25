# NOVA OS Native: Eigentum an Offline-Stack-Frames

**Stand:** 25. Oktober 2026  
**Meilenstein:** Fehlerhafte Frame-Zuteilungen beim Vorbereiten von Nutzer-Stacks
sicher ablehnen, ohne fremde Speicherseiten zu überschreiben oder freizugeben.

## Ausgangsproblem

Die Offline-Vorbereitung der vier geplanten Nutzer-Stackseiten trug bisher
jeden vom Allocator gelieferten Pointer **vor** der Aliasprüfung als eigene
Stackseite ein. Bei einer Fehlermeldung führte das anschließende Rollback
`nova_user_space_destroy` aus und konnte dadurch eine bereits zum ELF-Abbild,
zur Seitentabelle oder zu einem früheren Stack gehörende Seite freigeben.
Das Risiko war ein Doppel-Free oder eine Beschädigung anderer Daten.

Zusätzlich konnte beim Anlegen einer neuen Seitentabelle dieselbe, gerade
angeforderte Stackseite als Tabellenframe zurückgegeben werden, **bevor**
ihre virtuelle Zuordnung in den PTEs sichtbar war.

## Umsetzung

- `kernel/user_space.h` prüft Pointer-Identität **vor** der physischen
  Adressauflösung und vergleicht anschließend validierte physische Adressen
  mit den ELF-Abbildseiten, Seitentabellen und früheren Stackseiten.
- Nur erfolgreich validierte, nicht überlappende Seiten werden in
  `stack_frames` und `stack_physical` als Eigentum des vorbereiteten
  Adressraums aufgenommen. Nur diese werden bei Fehlern freigegeben.
- Während der Zuordnung einer neuen Stackseite reserviert
  `kernel/paging.h` vorübergehend deren Pointer und physische Adresse.
  Neue Seitentabellen dürfen diesen Frame nicht wiederverwenden.
- Beim Anlegen von Seitentabellen werden bekannte fremde Pointer vor dem
  physischen Callback geprüft. Ist die Herkunft einer verdächtigen Seite
  nicht zuverlässig bestimmbar, wird sie **nicht** freigegeben.
- `tests/test_paging.c` simuliert sieben fehlerhafte Zuteilungen:
  geliehenes ELF-Frame, geliehenes Tabellenframe, geliehenes früheres
  Stackframe, Stackframe als neues Tabellenframe, falsche physische Adresse
  zu einem ELF-Frame, falsche physische Adresse zu einem früheren Stackframe
  und fehlgeschlagene physische Adressauflösung. Tests prüfen den
  unveränderten ELF-Inhalt und das Zurückrollen bestätigter Allokationen.

## Sicherheitsgrenzen

Diese Maßnahmen sind **Fail-Closed-Prüfungen für vorbereitete
Datenstrukturen**. Bei einem fehlerhaften Allocator, dessen Rückgabe nicht
zweifelsfrei einem neuen Frame zugeordnet werden kann, wird die verdächtige
Seite bewusst nicht freigegeben. Das kann einen **Speicherverlust** verursachen,
verhindert aber eine riskante Doppel-Freigabe. Ein absichtlich lügender
Allocator kann nicht allein durch seine eigenen Callback-Angaben sicher
validiert werden; echte Frame-Eigentumsnachweise und robuste
Allocator-Verträge bleiben erforderlich.

Es gibt weiterhin **keine aktivierten Prozess-Seitentabellen**, keinen
CR3-Wechsel, keine Ring-3-Ausführung, keine Syscalls und keine durch Hardware
durchgesetzte Prozessisolation. Der freestanding Kernel-Build und Host-Tests
ersetzen keine VM-Laufzeittests mit echten CPU-Ausnahmen. Der vorhandene
physische Speicherallocator ist ein begrenzter Entwicklungsprototyp.

**Release-Regel:** Keine automatische ISO-Erstellung, keine Änderungen an
echten Datenträgern, keine Installation. `final_release` bleibt manuell.
