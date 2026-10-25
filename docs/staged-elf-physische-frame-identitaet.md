# NOVA OS Native: Physische Frame-Identität beim ELF-Staging

Stand: 25. Oktober 2026 · Branch: `nova-os-native`

## Abgegrenzter Meilenstein

Die bisherige ELF-Staging-Prüfung verglich Kernel-Zeiger auf 4-KiB-Seiten.
Das erkannte identische oder überlappende virtuelle Adressen, **nicht aber**
zwei unterschiedliche Zeiger, die auf denselben physischen Frame zeigen.
Eine solche Alias-Situation könnte bei einer fehlerhaften Speicherverwaltung
Programmcode oder Daten eines anderen vorbereiteten Programms verändern.

Für das rein offline ausgeführte Staging ist deshalb jetzt eine vom
Speicherallocator bereitgestellte **physische Adressauflösung verpflichtend**:

- `nova_process_stage` verlangt einen `nova_process_phys_fn`-Callback.
  Im nativen Kernel verwendet er `nova_pmm_physical`; die Host-Tests
  verwenden einen separaten, synthetischen Frame-Index.
- Vor dem Nullsetzen oder Beschreiben eines neu zugeteilten Frames wird
  seine physische Adresse ermittelt und auf 4-KiB-Ausrichtung, zulässige
  x86-64-PTE-Adressbits und Nichtnull geprüft.
- Bereits erfasste Frames desselben Images **und aller anderen gestagten
  Images** werden sowohl nach Zeigerüberlappung als auch nach physischer
  Adresse geprüft. Der Callback wird für bestehende Frames erneut aufgerufen
  und mit der beim Staging erfassten Adresse verglichen.
- Die bestätigte physische Adresse wird je Image-Seite gespeichert.
  `nova_pt_prepare` akzeptiert später nur die dazu passende PMM-Adresse.
- Vor dem Freigeben eines Images werden alle Frame-Identitäten erneut
  geprüft. Doppelte, widersprüchliche oder nicht mehr überprüfbare
  Zuordnungen verhindern die gesamte Freigabe. Verdächtige Frames
  werden bewusst nicht freigegeben, statt fremden Speicher zu beschädigen.

## Automatisierte Prüfungen

Die erweiterten Host-Regressionstests simulieren unterschiedliche virtuelle
Zeiger mit identischer gemeldeter physischer Adresse – innerhalb eines
Images und gegenüber einem anderen Image. Zusätzlich prüfen sie ungültige
physische Adressen, fehlgeschlagene Callback-Auflösung, manipulierte
Metadaten beim Freigeben und veränderte Frame-Identitäten beim
Seitentabellenaufbau. Die Tests prüfen ausdrücklich, dass ein verdächtiger
Frame vor dem Abbruch nicht durch das Staging überschrieben oder
versehentlich freigegeben wird.

Die vorhandenen GCC- und Clang-Sanitizer-Jobs sowie der freistehende
Kernel-Compile-Job bleiben im Quellcode-CI aktiv. Diese Prüfungen erzeugen
**keine ISO** und starten keine realen Datenträgerzugriffe.

## Echte Grenzen und Restrisiken

Diese Änderung stellt **keinen kryptografischen oder unabhängigen
Eigentumsnachweis** dar. Sie vertraut weiterhin dem PMM-Callback und seinen
Verwaltungsdaten. Ein fehlerhafter oder kompromittierter Callback kann
übereinstimmende, aber falsche physische Adressen melden. Physische Aliase
außerhalb der bekannten, vorbereiteten Images werden nicht global erfasst.

Im Fehlerfall können verdächtige, frisch zugeteilte Frames absichtlich
verloren gehen. Das ist ein Fail-Closed-Verhalten, keine vollständige
Speicherleck-Vermeidung. Es gibt keine parallelen CPU-Kerne, keinen
authentifizierten globalen Frame-Eigentümer und keine überprüfte
Laufzeitisolierung. `nova_process_peek` ist ein privilegierter Debug-Lesezugriff
und stellt keine Zugriffskontrolle für User-Prozesse dar.

**NOVA führt weiterhin keine gestagten ELF-Programme aus.** CR3-Wechsel,
Ring-3-Einstieg, sichere Systemaufrufe, echte getrennte Prozessadressräume
und VM-Laufzeittests fehlen. Die Änderungen sind Entwicklungsgrundlagen,
keine Produktionsfreigabe.

Die finale ISO-Pipeline bleibt ausschließlich über `workflow_dispatch`
mit `final_release == true` freischaltbar. Dieser Meilenstein erstellt
weder ISO noch Release und verändert weder Windows noch echte Datenträger.
