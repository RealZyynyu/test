# NOVA OS Native – Integritätsprüfung vorbereiteter Seitentabellen

**Stand:** 25. Oktober 2026  
**Meilenstein:** Offline-Adressprüfung verweigert manipulierte Seitentabelleneinträge
und erkennt tatsächlich belegte Stack-Guard-Seiten.

## Ausgangslage und Risiko

Die bisherigen Funktionen `nova_pt_lookup` und
`nova_user_span_accessible` lasen die PTEs vorbereiteter Adressräume.
Wurde ein PTE nach der Konstruktion verändert, konnte die Offline-Prüfung
fälschlich eine andere physische Seite oder zusätzliche Schreibrechte
akzeptieren. Ein manipuliertes PTE im eigentlich freien Stack-Guard-Bereich
konnte dagegen von `nova_pt_lookup` abgelehnt werden, obwohl der Eintrag
physisch **nicht** leer war. Der Guard-Test prüfte zuvor nur die oberste
der vier Stackseiten.

## Umsetzung

- `kernel/paging.h` führt pro zugeordneter Blattseite ein begrenztes
  **Zuordnungsmanifest** aus virtueller Adresse, physischer Frame-Adresse
  und ursprünglich erzeugtem PTE. Maximal 64 Zuordnungen bleiben erlaubt.
- `nova_pt_lookup` verlangt eine eindeutige Manifestzuordnung und die
  **exakte Übereinstimmung** des PTEs. Manipulierte Schreib-/NX-Rechte,
  ausgetauschte physische Frames, unbekannte Zuordnungen und doppelte
  Manifestadressen werden abgelehnt.
- Auch die drei übergeordneten Seitentabellen-Einträge werden auf
  erwartete Offline-Bits und gültige Tabellenframes geprüft. Unplausible
  Tabellen- und Mapping-Zähler werden vor Arrayzugriffen abgewiesen.
- `nova_pt_slot_empty` kontrolliert den tatsächlichen Seitentabellen-Slot:
  Nur ein **Null-Eintrag** gilt als frei. Ein unbekannter, vorhandener oder
  nichtpräsenter, aber von Null verschiedener PTE wird nicht als
  sichere Guard-Seite eingestuft.
- `nova_user_stack_guard_is_unmapped` verlangt eine wirklich freie
  Guard-Seite und kontrolliert **alle vier Stack-PTEs** auf die vorgesehenen
  physischen Frames sowie `P|RW|US|NX`.
- `tests/test_paging.c` simuliert PTE-Manipulationen an RX-Code, RW-Daten,
  physischen Frames, Guard-Seiten, Stackseiten und übergeordneten
  Seitentabelleneinträgen. Es prüft außerdem korrupte Metadaten und
  die Ablehnung von Pufferzugriffen über `nova_user_span_accessible`.
  Der vorhandene Paging-CI-Job führt diese Tests mit GCC sowie Clang
  AddressSanitizer/UndefinedBehaviorSanitizer aus.

## Sicherheitsgrenzen und offene Arbeiten

Das Manifest beschreibt ausschließlich den **ursprünglichen Offline-Zustand**.
Es ist weder kryptografisch geschützt noch von Kernel-Speicher getrennt.
Ein Angreifer, der Manifest und Seitentabellen gleichermaßen verändern kann,
wird dadurch nicht zuverlässig aufgehalten. Auch ein fehlerhafter
Allocator kann weiterhin falsche Eigentumsangaben machen. Es gibt keine
tatsächlich geschalteten Prozess-Adressräume, keinen CR3-Wechsel,
keine Ring-3-Ausführung, keine Syscalls und keine hardwareseitig
durchgesetzten Schutzgrenzen.

Die Host-Tests und der freestanding Kernel-Build sind **keine** Laufzeit-
oder Sicherheitsfreigabe. VM-Tests mit realen Page Faults, TSS/IST,
ein verlässlicher Frame-Eigentumsnachweis und eine abgesicherte
Kernel/User-Trennung bleiben erforderlich.

**Release-Sperre:** Kein ISO-Bau, kein Release, keine Installation,
keine Änderungen an echten Datenträgern oder Windows. Die
`final_release`-Freigabe bleibt ausschließlich manuell.
