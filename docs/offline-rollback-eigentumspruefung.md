# NOVA OS Native – Abgesicherte Freigabe vorbereiteter Adressräume

**Stand:** 25. Oktober 2026  
**Meilenstein:** Vor dem Freigeben von Offline-Seitentabellen und Stackframes
wird die gesamte bekannte Frame-Eigentumszuordnung überprüft.

## Konkretes Problem

Die bisherigen Aufräumfunktionen `nova_pt_destroy` und
`nova_user_space_destroy` vertrauten unmittelbar auf veränderbare Zähler
und Zeiger ihrer Deskriptoren. Ein beschädigter `tables_used`- oder
`stack_count`-Wert konnte zu einem Arrayzugriff außerhalb der Grenzen
führen. Doppelte Einträge oder vertauschte Zeiger konnten dieselbe
Speicherseite mehrfach oder sogar eine fremde ELF-Seite freigeben.

Das ist besonders kritisch bei Fehlerbehandlung und Rollback: Ein
vermeintlich harmloser Aufräumpfad darf die Isolation künftiger Prozesse
nicht unterlaufen.

## Umsetzung

In `kernel/paging.h` prüft `nova_pt_tables_releasable` vor **jeder**
Freigabe die vollständige Tabelle:

- Begrenzung von Tabellen-, geschützten Frame- und Mapping-Zählern;
- gültige, ausgerichtete und eindeutig identifizierte Tabellenframes;
- Übereinstimmung der gespeicherten physischen Adressen mit der
  `physical_address`-Rückfrage des Allocators;
- keine Zeiger- oder physischen Aliase zwischen Tabellen sowie
  geschützten ELF-Frames und eingetragenen Blattseiten;
- konsistente PML4-Wurzel und keine noch ausstehende Blattreservierung.

`kernel/user_space.h` kontrolliert zusätzlich alle höchstens vier
Stackframes, ihre physischen Adressen sowie Aliase mit Tabellen,
ELF-Frames und anderen Stackframes. Erst wenn **beide** Prüfungen
erfolgreich sind, beginnt die Freigabe. Andernfalls werden die
Deskriptoren verworfen, ohne einen möglicherweise fremden Frame
freizugeben. Dies kann absichtlich Speicher verlieren (Fail-Closed).

## Automatisierte Regressionstests

`tests/test_paging.c` enthält zusätzliche Host-Szenarien für:

- übergroße Tabellen-, Manifest-, Schutz- und Stackzähler;
- doppelte Tabellen- oder Stackzeiger und physische Frame-Aliase;
- versehentlich eingetragene ELF-Frames oder Seitentabellen als Stack;
- beschädigte PML4-Wurzel und nicht aufgelöste Reservierungen;
- fehlende physische Eigentumsprüfung beim Aufräumen;
- erfolgreichen Normalpfad, der alle eigenen Seiten wieder freigibt.

Der bestehende CI-Paging-Job führt diese Tests mit GCC sowie Clang
AddressSanitizer und UndefinedBehaviorSanitizer aus. Der Kernel-Job
kompiliert und linkt nur die ELF-Kerneldatei, **keine ISO**.

## Echte Grenzen

Dies ist eine defensive **Offline-Aufräumprüfung**, keine unabhängige
Authentisierung des Frame-Eigentums. Ein kompromittierter Allocator
kann weiterhin falsche physische Adressen bestätigen; absichtliche
gleichzeitige Änderungen von Metadaten und Speicherverwaltung sind
nicht abgesichert. Eine Prüfung kann bei fehlerhaften Rückgaben
Speicher absichtlich nicht freigeben. Sie ist nicht atomar gegenüber
einem parallel arbeitenden Allocator; NOVA bleibt ein
Single-Core-Entwicklungsprototyp.

Die vorbereiteten Seitentabellen werden nicht mit CR3 aktiviert.
Es gibt weiterhin keine Ring-3-Prozesse, keine hardwareseitig
durchgesetzte Kernel/User-Trennung, keine sichere Syscall-Grenze
und keine vollständigen VM-Laufzeittests. Die Host-Sanitizer und
der Kernel-Build ersetzen weder einen Sicherheits-Audit noch
einen tatsächlichen Boot-/Fault-Test.

**Release-Grenze:** Keine ISO-Erstellung, kein Release, keine
Installation, keine Änderungen an echten Datenträgern oder Windows.
Die GitHub-Aktion zur finalen ISO benötigt unverändert eine explizite
manuelle `final_release`-Freigabe.
