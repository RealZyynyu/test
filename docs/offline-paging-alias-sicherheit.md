# Entwicklungsstand: Physische Alias-Sicherheit in Offline-Seitentabellen

**Datum:** 25. Oktober 2026  
**Meilenstein:** Fehlersichere Zuordnung von Prozessabbildern zu vorbereiteten
x86-64-Seitentabellen (ohne Prozessausführung).

## Sicherheitsproblem

Die bisherige Seitentabellen-Vorbereitung prüfte zwar virtuelle Adressen,
Seitentabellenstruktur und W^X-Flags, konnte jedoch physische Frame-Aliase
nicht umfassend ausschließen. Ein fehlerhafter Speicherallocator hätte
beispielsweise eine bereits belegte Programmseite als neue Seitentabellenseite
zurückgeben können. Das Initialisieren der Tabelle hätte dann Programmdaten
überschrieben. Zwei virtuelle Adressen auf dieselbe physische Seite mit
unterschiedlichen Rechten könnten später W^X umgehen.

## Umsetzung

- `kernel/paging.h` prüft **alle physischen ELF-Abbildseiten vor der ersten
  Seitentabellen-Allokation**. Doppelte Frame-Pointer und doppelte physische
  Frame-Adressen werden mit `NOVA_PT_BAD_FRAME` abgelehnt.
- Beim Erzeugen einer Seitentabelle wird ein physischer Alias zu einer
  reservierten Programmseite, einer anderen Tabelle oder einer bereits
  abgebildeten Datenseite abgelehnt. Potenziell fremde Frames werden dabei
  weder genullt noch freigegeben.
- `nova_pt_map_one` verweigert mehrfache Abbildungen derselben physischen
  Seite (auch bei verschiedenen RW-/NX-Rechten) und verhindert, dass eine
  Seitentabellenseite als Nutzer-Datenseite eingeblendet wird.
- Direkte Abbildungen sind auf **64 Blattseiten pro vorbereiteter Tabelle**
  begrenzt. Die bestehenden vorbereiteten ELF-Programme bleiben auf 32 Seiten
  zuzüglich vier geplanter Stack-Seiten begrenzt.
- `tests/test_paging.c` prüft identische Frame-Pointer, verschiedene
  Pointer mit derselben physischen Adresse, einen absichtlich fehlerhaften
  Allocator, RX/RW-Aliase und den Erhalt der Programmbytes bei Ablehnung.
  Die bestehenden Paging-, Stack-, Schutz- und Rollback-Tests laufen weiterhin.

## Grenzen und offene Arbeiten

Die Änderung betrifft ausschließlich **Offline-Datenstrukturen**. Es gibt
weiterhin **keinen CR3-Wechsel, keine Hardware-Durchsetzung der Seitenrechte,
keine Ring-3-Prozesse, keine Syscalls und keine echte Isolation**. Eine
ungültige Allokator-Rückgabe kann nicht zuverlässig als neue, freigebbare
Seite erkannt werden: Zur Vermeidung von Doppel-Freigaben wird sie im
Konfliktfall bewusst nicht freigegeben. Ein dauerhaft fehlerhafter Allocator
könnte deshalb Ressourcen verlieren; er muss separat gehärtet werden.

Die Host-Tests und der freestanding Kernel-Build beweisen **nicht**, dass
diese Pfade unter realen CPU-Ausnahmen oder beim Wechsel in Nutzerprozesse
sicher funktionieren. VM-Laufzeittests, TSS/IST, robuste Fehlerbehandlung
bei Speicherschutzverletzungen und vollständige Kernel/User-Adressraum-
Trennung sind vor einer Freigabe erforderlich.

**Release-Regel:** Kein automatischer ISO-Bau, keine Installation, keine
Datenträgeränderungen. Die explizite manuelle `final_release`-Freigabe
bleibt erforderlich.
