# NOVA OS — Systemaufruf-Schnittstelle v1 (noch nicht aktiv)

**Status:** Entwicklerprototyp eines **Kernel-intern simulierten Brokers**.
Dies ist **kein echter Systemaufruf**. NOVA hat weiterhin keine ring-3-
Prozesse, keinen sicheren Kernel-Stack-Wechsel und keinen laufenden
Syscall-Einstieg. Das Testen dieses Codes belegt keine Betriebssystemsicherheit.

## Warum NOVA diese Schnittstelle braucht

Spätere Apps und Spiele dürfen nicht direkt auf den Kernel, physischen
Speicher oder Dateitreiber zugreifen. Stattdessen stellen sie begrenzte,
klar benannte Anfragen. Der Kernel prüft dabei die **vom Scheduler
ermittelte Prozessidentität**, Rechte, Dateipfade und Datenlängen und
kopiert Ergebnisse in kontrollierten Speicher.

Das neue Modul `kernel/broker.h` implementiert dafür einen begrenzten
**C-Datenvertrag und einen synchronen Dispatcher**, noch ausschließlich
innerhalb von Ring 0 / in Hosttests.

| Anfrage | Geplantes Verhalten im Kernel-Broker |
| --- | --- |
| `VERSION` | Meldet die ABI-Version (1) für ein registriertes Programmabbild |
| `READ` | Liest mit RAM- oder FAT16-Leseberechtigung bis zu 64 Bytes |
| `WRITE_RAM` | Ändert kleine RAM-Dateien, nur mit RAM-Schreibrecht; nie Festplatten |
| `STAT` | Erfragt die Länge einer Datei nach positiver Leserechtsprüfung |

Regeln: Standardmäßig **keine Rechte**, niemals Schreiben auf die
virtuelle Festplatte, maximal **64 Bytes pro Dateioperation**,
maximal **32 Bytes im Pfadfeld**, kein Pointer im ABI-Paket,
keine Netzwerk-/Mikrofon-/Controller-Anfragen, kein dynamischer Speicher.
Antworten werden auch bei Fehlern vollständig zurückgesetzt.

Die Datei `kernel/user_buffer.h` enthält eine weitere Vorprüfung:
Eine virtuelle Adresse wird Seite für Seite anhand der **nur vorbereiteten
Seitentabellen** validiert. Kernel-Schreibzugriffe in einen zukünftigen
Benutzerpuffer werden bei schreibgeschützten Seiten verweigert; die
absichtlich ungemappte Stack-Guard-Page darf nicht durchschritten werden.
Es werden dabei **keine fremden Speicherinhalte ausgelesen oder verändert**.

## Befehle für Entwickler (nur im Quellcode, noch in keiner ISO)

```text
PSTAGE DISK:/APP.ELF    # ELF und Stack vorbereiten, keine Ausführung
PBROKER 1               # simulierte Broker-ABI-Version für PID 1 anzeigen
PBUF 1                  # offline Stack-Grenzen/Guard-Page prüfen
PACCESS 1               # Rechte der PID; anfangs immer 0
PDROP 1                 # Rechte und Speicher wieder freigeben
```

Es gibt bewusst **keinen** Befehl zum Rechtegewähren und
**keinen echten Syscall-Interrupt**. Der Entwickler-Broker bekommt
seine PID direkt aus dem vertrauenswürdigen Aufruferargument, nicht aus
einem vom Programm gelieferten Datenpaket. Dies ist noch kein
sicherer Prozessnachweis – dieser muss mit dem Scheduler und Ring 3
implementiert werden.

## Teststrategie

`tests/test_broker.c` testet zulässige und unzulässige Dateianfragen,
zu große Datenpakete, nicht abgeschlossene Pfade, fehlende Rechte,
widerrufene Rechte, ungültige PIDs, alte Ausgabedaten sowie
verbotene Schreibzugriffe. `tests/test_paging.c` testet die
adress- und seitenübergreifenden Speichergrenzen. GitHub Actions
prüft mit GCC, Clang, UBSan/ASan und kompiliert den Kernel,
**ohne eine neue ISO zu erzeugen**.

## Was noch zu implementieren und zu testen ist

1. Funktionierender Exception-/Interrupt-/Timerpfad und korrekte Kernel-
   und Benutzerseiten in einem aktivierbaren Adressraum.
2. GDT/TSS, Ring-3-Wechsel, isolierte Kernel-Stacks und ein geprüfter
   Syscall-Trap / IRQ-Rückweg.
3. Fehlerresistentes `copyin/copyout`, Schutz gegen ungültige
   Pointer während des Kopierens, vollständige Prozessidentitätsprüfung.
4. Echte Dateisystem-Zugriffskontrolle mit Besitzerzuordnung,
   private Spiele-Speicherstände, Benutzerkonten und permanente Daten.
5. Geprüfte App-Laufzeit, Audio/GPU/Input-Treiber, 2D-Spiele-API und
   später mögliche Kompatibilität mit anderen Spielplattformen.
6. Reproduzierbare VM-Runtime-Tests, Sicherheitsprüfung und bewusste
   Freigabe des finalen Installationsabbilds.

**Sicherheitsgrenze:** Der aktuelle Kernel läuft im Ring 0. Bis echte
Prozesse existieren, können Kernel-Module die simulierten Prüfungen
umgehen. Es gibt keine CPU-erzwungene Sandbox. Auf Windows-Systemen
nur eine Wegwerf-VM verwenden.
