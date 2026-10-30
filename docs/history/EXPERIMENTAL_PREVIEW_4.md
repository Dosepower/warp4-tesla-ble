# WARP4 Tesla BLE – Experimental Preview 4

**Inoffizielle experimentelle Firmware ausschließlich für WARP4. Keine stabile Release und keine Tinkerforge-Freigabe.**

Firmware `2.13.5+6ad9fd3c`. Angefügtes OTA ist exakt der auf meiner WARP4 installierte und bootvalidierte Build. Separat aus Quellen gebautes, angepasstes NimBLE/Secure-BLE-SDK; kein unverändertes Hersteller-SDK.

## Änderungen seit Preview 3

- Grüne eindeutige Tesla-Trefferkarte mit tatsächlich gesuchter VIN-Endung, gefundener Bluetooth-Adresse und Fundalter seit Abschluss der Suche.
- Klarer Hinweis: Übernehmen speichert die Bluetooth-Zuordnung; Kopplung folgt separat.
- Zweiminütige, auch im Backend erzwungene Gültigkeit. Abgelaufene, uneindeutige, ungültige oder veraltete Treffer sind nicht übernehmbar. Keine erfundenen Modell-/Fahrzeugnamen.
- Bei ausgeschaltetem oder noch unbekanntem globalem Bluetooth verschwindet die gesamte Tesla-Erweiterung unter Fahrzeuge einschließlich Überschrift, Trennlinie und reserviertem Platz. Standardfahrzeuge und gespeicherte Daten bleiben unverändert.
- Keine automatische Suche, Verbindung, Kopplung oder Abfrage durch Seitenaufruf, Sichtbarkeitswechsel oder Update. Laufzeit-/Neustart- und Sicherheitsprüfungen bleiben bestehen.

## Tatsächlich getestet

Dieser exakte Build wurde auf eigener WARP4 installiert und automatisch bootvalidiert. Originalfirmware 2.13.4 bleibt im zweiten gültigen Slot erhalten. Sicherheitsfunktionen aktiv, kein EVSE-Fehler. Bei der anschließenden echten Browserprüfung war Bluetooth ausgeschaltet und die Tesla-Überschrift nicht mehr vorhanden. Keine Bluetoothaktivierung oder echte Suche/Übernahme als Teil dieses Updates ausgeführt.

Offline: 58 Prüfskripte bestanden; unveränderte zehn historische Ausschlüsse sind keine bestandenen Tests. Normal-/ASan-/UBSan-/Frontend- und Mobil-/Desktopprüfungen bestanden. Tatsächliche WARP-Fahrzeugverwaltung bei DE/EN und 360/390/1280px geprüft: aus/an/fehlende Konfiguration, gewöhnliche Fahrzeuge unverändert, kein Leerraum und keine automatischen Aktionen. Echte Produktionsfrontendtests und unabhängige finale Astra-Prüfung mit 20 frisch ausgeführten Kommandos bestanden. Firmware-ELF-Webinhalt und daraus abgeleitete App/OTA geprüft. Suchalter/TTL/Mehrdeutigkeit/Revisions-/Profilwechsel wurden mit synthetischen Daten offline geprüft, nicht als neue RF-Geräteprüfung ausgegeben. Keine GitHub-CI-Ausführung behauptet.

## Noch nicht bestätigt

**Erfolgreiches Tesla-Pairing und realer SoC-Abruf sind weiterhin nicht hardwarebestätigt.** Gleiches gilt für Wiederverbindung/Mehrfahrzeugwechsel und Langzeit-/WLAN-/BLE-/Ladekoexistenz. Entkoppeln mit echten Fahrzeugschlüsseln wurde nicht destruktiv getestet; dazu dienen bisher isolierte Profil-/Backend- und UI-Tests. Der bestätigende Bluetooth-Aus-/An-Neustartablauf ist offline getestet, am Gerät wurde der Dialog bislang nur abgebrochen. Keine vollständige Fahrzeugabnahme.

## Installation

1. Nur WARP4; nicht WARP1/2/3 oder beliebige ESP32-Boards. Lokaler Zugriff und akzeptiertes Ausfallrisiko erforderlich.
2. Einstellungen vorab dokumentieren und vorhandene Sicherungsmöglichkeiten nutzen. Ein Originalslot ist kein Backup gemeinsam genutzter Konfigurationsdaten; vollständiger Restore/Fernrettung nicht garantiert.
3. `warp4-tesla-ble-2.13.5-6ad9fd3c-experimental_ota.bin` und SHA256SUMS herunterladen und Prüfsumme vergleichen.
4. Ausschließlich den normalen Web-Firmware-Updater verwenden. Keine esptool-/Fullflash-/Bootloader-/Partitionsinstallation. Keine Sicherheitsfunktionen deaktivieren. Warnung zur fehlenden Herstellersignatur bewusst nur für diesen Experimentalbuild bestätigen.
5. Nach Neustart automatische Boot-Validierung abwarten (im verwendeten Stand ungefähr sieben Minuten), danach Browser vollständig neu laden. Bootvalidierung ist kein Tesla-Funktionstest.
6. Bluetooth explizit unter Schnittstellen aktivieren und den Neustart bestätigen. Fahrzeugeinrichtung bleibt unter Fahrzeuge.

### Hinweis zum Upload

Bei der Installation dieses Builds brachen mehrere Uploads in der Originalfirmware 2.13.4 mit einem Webserver-/WebSocket-Watchdog ab. Nach einem ausdrücklich ausgelösten normalen Neustart der Originalfirmware ließ sich **dasselbe unveränderte Paket** erfolgreich installieren. Der genaue Auslöser ist nicht bewiesen. Bei Hängern nicht fortlaufend blind erneut hochladen; gültigen laufenden Slot prüfen und keine ungültige Teilinstallation starten. Ein normaler Neustart ist kein Werksreset. Kein pauschales Kompatibilitätsversprechen daraus ableiten.

## Quellen und Rückmeldungen

Der Release-Tag enthält den zugehörigen Anwendungsquellstand, Tests und vorhandene SDK-Overlays/Patches. Historische Source-only-Texte beschreiben frühere Übergaben oder gesperrte Review-Buildziele, nicht die Freigabe dieser experimentellen Anlage. Ein byteidentischer Ein-Kommando-Rebuild wird nicht zugesagt; private lokale Buildnachweise werden nicht veröffentlicht.

Fehler bitte mit Firmwareversion, Bedienfolge und sichtbarer Meldung berichten. Keine echten VINs, MAC-Adressen, Schlüssel, Zugangsdaten, vollständigen Debugreports oder Coredumps öffentlich posten. Testidentitäten im Quellcode sind synthetisch.
