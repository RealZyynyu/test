# NOVA OS Native – Schutz von ELF-Frames beim Offline-Staging

**Stand:** 25. Oktober 2026  
**Meilenstein:** Bereits verwendete Speicherframes dürfen beim Laden eines
weiteren ELF-Segments oder einer weiteren vorbereiteten Anwendung nicht
versehentlich überschrieben oder doppelt freigegeben werden.

## Ausgangslage

Der bisherige Staging-Pfad in `kernel/process_image.h` löschte jede
zurückgegebene Allocator-Seite sofort mit Nullen. Ein fehlerhafter
Allocator konnte aber einen Zeiger auf eine **bereits zugewiesene**
Seite erneut liefern. Damit hätte das Nullsetzen Code oder Daten einer
anderen vorbereiteten Anwendung zerstören können. Außerdem vertraute
die Freigabe blind auf `page_count` und die darin enthaltenen Zeiger:
beschädigte Zähler und doppelte Einträge waren eine Gefahr für
Arraygrenzen und die Frame-Eigentumsverwaltung.

## Umsetzung

Der Staging-Pfad prüft nun **vor jedem Schreibzugriff**:

- Die zurückgegebene Adresse ist nicht NULL und auf 4096 Byte ausgerichtet.
- Die neue 4096-Byte-Region überschneidet sich nicht mit einer bereits
  übernommenen Seite desselben ELF-Images.
- Sie überschneidet sich nicht mit Seiten anderer aktuell vorgemerkter
  Anwendungen (`staged`-Einträge der Prozesstabelle).
- Ein verdächtiger Rückgabewert wird weder beschrieben noch freigegeben.
  Bereits zweifelsfrei zugeordnete Seiten werden separat zurückgerollt.

Die Freigabe von ELF-Images kontrolliert **vor der ersten Freigabe** den
maximalen Seitenzähler, gültige Zeiger, die Ausrichtung und interne
Überschneidungen. Beim regulären Entfernen eines Images werden zusätzlich
die anderen aktiven Images geprüft. Bei widersprüchlichen Angaben
verweigert NOVA die Freigabe, verwirft den betroffenen Deskriptor und
meldet einen Fehlschlag. Der dadurch mögliche Speicherverlust ist
absichtlich sicherer als eine mögliche doppelte Freigabe.

Die Diagnose `NOVA_PROCESS_BAD_FRAME` unterscheidet einen verdächtigen
Allocator-Rückgabewert von gewöhnlichem Speichermangel. Der reine
Offline-Debug-Lesezugriff `nova_process_peek` lehnt beschädigte
Seitenzähler und fehlende Frame-Zeiger ab.

## Tests

`tests/test_process_image.c` simuliert insbesondere:

- denselben Frame bei einer späteren Allocation desselben Images;
- einen unzulässigen Zeiger mitten in eine bereits verwendete Seite;
- eine ungerichtete/verschobene Seite, die nicht beschrieben werden darf;
- eine Seite, die bereits einer anderen vorgemerkten Anwendung gehört;
- beschädigte `page_count`-Werte und doppelte Freigabeeinträge;
- manipulierte Frame-Zeiger, die auf eine andere Anwendung verweisen;
- weiterhin erfolgreiche normale Erstellung, Freigabe und Rollbacks.

Der CI-Job `staged-images` führt die Tests mit GCC und Clang
AddressSanitizer/UndefinedBehaviorSanitizer aus. Der separate
`elf64`-Job prüft den VFS-Integrationspfad mit ausgerichteten
simulierten Allocator-Seiten. Der vollständige Kernel wird als ELF
kompiliert und gelinkt, ohne eine ISO zu erstellen.

## Echte Grenzen

Die Prüfung beruht auf **virtuellen Kernel-Zeigern**. Sie kann keine
physischen Aliase erkennen, wenn ein kompromittierter Allocator zwei
verschiedene virtuelle Adressen für denselben physischen Frame liefert.
Es gibt noch keinen unabhängigen, manipulationsgeschützten
Frame-Eigentumsnachweis. Ein beschädigter Prozessdeskriptor kann
bewusst zu Speicherverlust führen. Die Prüfung ist nicht atomar
gegenüber nebenläufigen Änderungen; NOVA ist weiterhin ein
Single-Core-Prototyp.

Dies betrifft ausschließlich das **Offline-Staging**. Die vorbereiteten
Adressräume werden nicht aktiviert. Es fehlen Ring-3-Ausführung,
hardwareseitig durchgesetzte Prozessisolation, Systemaufruf-Grenzen,
zuverlässige TSS/IST-Ausnahmebehandlung und vollständige VM-Laufzeittests.
Host-Sanitizer und erfolgreicher Kernel-Build sind kein Nachweis
eines sicheren produktiven Betriebssystems.

**Release-Sperre:** Keine ISO, kein Release, keine Installation,
keine Schreibzugriffe auf reale Datenträger und keine Windows-Änderungen.
Die finale ISO-Erstellung bleibt ausdrücklich manuell über
`final_release` gesperrt.
