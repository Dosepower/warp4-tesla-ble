# Hardware-Kompatibilität, Risiken und Wiederherstellung

## Bluetooth-Hardware der WARP-Generationen

Die netzwerkfähigen Smart-/Pro-Modelle der WARP-Generationen 1 bis 4 besitzen
grundsätzlich einen Bluetooth-fähigen ESP32-Controller:

- **WARP1 Smart/Pro:** ESP32 Brick; Tinkerforge nennt Bluetooth 4.2 BR/EDR und BLE.
- **WARP2 Smart/Pro:** ESP32 Ethernet Brick; Tinkerforge nennt ebenfalls Bluetooth 4.2 BR/EDR und BLE.
- **WARP3:** WARP ESP32 Ethernet Brick mit ESP32-WROVER-E.
- **WARP4:** WARP ESP32 Ethernet Brick 2.0 mit ESP32-WROVER-E.

Bei Varianten ohne ESP32-/Netzwerkcontroller steht diese Grundlage nicht zur
Verfügung. Vorhandene Bluetooth-Hardware bedeutet außerdem nicht, dass die
Standardfirmware Bluetooth bereits verwendet: Die offiziellen vorgebauten
ESP32-Bibliotheken der untersuchten WARP-Firmware haben Bluetooth deaktiviert.

**Wichtig:** Der in diesem Repository enthaltene Build ist ausschließlich für
WARP4 erstellt. Er darf nicht auf WARP1, WARP2 oder WARP3 installiert werden.
Eine Unterstützung älterer Generationen erfordert jeweils einen eigenen Build,
eine Prüfung der Ressourcen und vollständige Integrations- und Hardwaretests.

### Offizielle Hardwarequellen

- ESP32 Brick: https://www.tinkerforge.com/en/doc/Hardware/Bricks/ESP32_Brick.html
- ESP32 Ethernet Brick: https://www.tinkerforge.com/en/doc/Hardware/Bricks/ESP32_Ethernet_Brick.html
- WARP3-Controller: https://github.com/Tinkerforge/warp-esp32-ethernet-brick
- WARP4-Controller: https://github.com/Tinkerforge/warp-esp32-ethernet-v2-brick

## Kann eine angepasste Firmware die Wallbox beschädigen?

Eine dauerhafte physische Beschädigung allein durch ein korrekt übertragenes
Firmwareabbild ist eher unwahrscheinlich. Eine fehlerhafte oder für die falsche
Generation gebaute Firmware kann die Wallbox aber unbenutzbar machen oder ihre
Funktion erheblich stören, zum Beispiel durch:

- Bootschleifen, Abstürze oder Watchdog-Neustarts,
- Verlust von Netzwerkzugriff oder Weboberfläche,
- Speichererschöpfung oder Störungen zwischen WLAN und BLE,
- fehlerhafte Fahrzeugerkennung oder Ladefreigabe,
- Verlust beziehungsweise Zurücksetzen gespeicherter Konfiguration,
- notwendige Wiederherstellung über Recovery-Modus oder USB/UART.

Ein vollständiges Esptool-Abbild kann Bootloader und Partitionstabelle
überschreiben und ist deshalb riskanter als ein normales OTA-Update. Niemals ein
Abbild für eine andere WARP-Generation installieren.

Die Tesla-BLE-Erweiterung greift nicht direkt in Schütz- oder EVSE-Steuerung ein
und sendet keinen Tesla-BLE-Ladestart. Sie startet jedoch zusätzliche BLE- und
Protokollaufgaben im selben ESP32. Fehler bei Speicher, Task-Scheduling oder
Funk-Koexistenz könnten daher trotzdem den Hauptcontroller beeinträchtigen.

## Aktueller Validierungsstand

Bestätigt sind:

- Hosttests der Integrationsrichtlinien,
- 249 Tests der verwendeten TeslaBLE-Bibliothek,
- vollständiger WARP4-Web-/TypeScript-Build,
- vollständiger WARP4-Firmwarebuild,
- Image-Größe innerhalb der vorgesehenen WARP4-Anwendungspartition.

Noch **nicht** auf realer Hardware bestätigt sind:

- Boot und Langzeitstabilität auf einer WARP4,
- BLE-/WLAN-Koexistenz und Reichweite,
- Tesla-Schlüsselkoppelung,
- tatsächlicher SoC-Abruf und Schlaf-/Weckverhalten,
- normaler AC-Ladestart nach BLE-SoC-Abfrage.

Während der Entwicklung wurde keine WARP geflasht.

## Empfohlener kontrollierter Ersttest

1. WARP-Konfiguration sichern und die passende offizielle Firmware bereithalten.
2. Den offiziellen Recovery-Ablauf vor dem Test lesen und zugänglich halten.
3. Nur auf der vorgesehenen WARP4 testen; nie auf WARP1–3.
4. Wenn überhaupt, zunächst nur das OTA-Abbild verwenden, nicht das vollständige Esptool-Abbild.
5. Erster Start ohne angeschlossenes Fahrzeug.
6. Netzwerk, Weboberfläche, EVSE-Zustand, Zähler und Neustartstabilität prüfen.
7. Erst danach Tesla-Konfiguration und Kopplung durchführen.
8. Erste SoC-Abfragen und den ersten Ladevorgang beaufsichtigen.
9. Bei Bootschleife, ungewöhnlichem EVSE-Verhalten oder wiederholten Verbindungsabbrüchen sofort abbrechen und die offizielle Firmware wiederherstellen.

Die WARP4-Dokumentation beschreibt einen Recovery-Modus, der auch bei nicht
mehr erreichbarer Weboberfläche gestartet werden kann. Dieser reduziert das
Risiko eines dauerhaften Software-Bricks, ersetzt aber keine vorherige Sicherung
und garantiert keine Wiederherstellung nach einem falsch geschriebenen
Bootloader oder Hardwaredefekt.

- WARP4 Recovery Mode: https://docs.warp-charger.com/en/docs/warp4/troubleshooting/#recovery-mode
- WARP2 Recovery Mode: https://docs.warp-charger.com/en/docs/warp2/troubleshooting/#recovery-mode

## Status des veröffentlichten Releases

Das GitHub-Release ist ausdrücklich als **experimentell**, **ungetestet auf
realer Hardware** und **nicht offiziell von Tinkerforge unterstützt** markiert.
Die Binärdateien sind unsignierte Entwicklungsartefakte. Ihre Veröffentlichung
ist keine Empfehlung zur Installation auf einer produktiv verwendeten
Wallbox.
