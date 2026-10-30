# WARP4 Tesla BLE – Experimental Preview 3

**Inoffizielle experimentelle Firmware ausschließlich für WARP4. Keine stabile Release und keine Tinkerforge-Freigabe.**

Firmware `2.13.5+6ad9e156`. Angefügtes OTA ist exakt der auf meiner WARP4 installierte und bootvalidierte Build. Separat aus Quellen gebautes, angepasstes NimBLE/Secure-BLE-SDK; kein unverändertes Hersteller-SDK.

## Änderungen seit Preview 2

- Zusätzlicher Neustart-Wiederholungsbutton bleibt während erfolgreichem Bluetooth-Speichern/Readback/Neustart einschließlich nachfolgender Statusmeldungen verborgen. Bei tatsächlichem Fehler oder offener Änderung bleibt die Wiederholungsmöglichkeit erhalten.
- Bestehender Button unter Tesla → Details heißt jetzt „VIN bearbeiten / Zuordnung löschen“, vollflächig rot mit weißer Schrift, unveränderte Position und Diagnosefelder.
- Bestätigtes Zurücksetzen entfernt die gespeicherte Bluetooth-Zuordnung und alte Such-/Aktionsfreigaben. VIN und lokaler Schlüssel bleiben erhalten; Entkoppeln ist separat. Keine automatische Funkaktion.
- Bei gekoppeltem Fahrzeug ist diese Aktion gesperrt, mit Hinweis zuerst zu entkoppeln; Backend erzwingt dieselbe Regel. Unklare Schlüsselzustände bleiben gesperrt, vorhandene Schlüssel dürfen nicht einer anderen VIN zugeordnet werden.
- Ersteinrichtung und zusätzlich angelegte WARP-Fahrzeugprofile erhalten autoritativ gelesene Leernamensraum-Nachweise. Nur nachweislich leere Profile dürfen erstmalig ihre VIN eingeben; fehlende Daten gelten nicht pauschal als schlüsselfrei. Diese Korrektur schließt einen in der unabhängigen Abschlussprüfung gefundenen Zwischenbuildfehler. Der blockierte Zwischenbuild wurde nicht installiert oder veröffentlicht.

## Tatsächlich getestet

Auf eigener WARP4: Installation, Start und automatische Boot-Validierung dieses exakten Builds. Originalfirmware bleibt auf dem anderen gültigen Slot erhalten. Aktuelle Oberfläche live geprüft: Bluetooth-Deaktivierungsdialog öffnen und abbrechen lässt den Schalter und die gespeicherte Einstellung an; kein separater Speichern-Button. Fahrzeugseite bei Bluetooth aus ohne permanente blaue Info. Sicherheit aktiv, kein EVSE-Fehler.

Suche und Speichern eines eindeutigen VIN-Treffers waren im Vorgängerbuild am Gerät erfolgreich; dies ist kein behaupteter neuer Fahrzeugtest dieses Builds. Der frühere Stackoverflow beim Speichern wurde zuvor gezielt behoben.

Offline: 57 Prüfskripte (56 bestehende plus neuer Bootstrap-Test), 15 Qualifikationskommandos einschließlich normaler und ASan/UBSan-Tests sowie echte Produktionsfrontend-Tests bestanden. Unveränderte zehn historische Ausschlüsse sind keine bestandenen Tests. Mobile/Desktop-Fixtures bei 360/390/1280px bestanden. Unabhängige finale Astra-Nachprüfung reproduzierte Erst-/Zweitprofil-Bootstrap aus neu kompilierten Backend-Nachweisen und prüfte Reset-, Schlüssel-, Stale-/Profil- und No-flash-Regressionen. Tatsächlicher ELF-Webinhalt und App/OTA-Identität geprüft. Kein Hardwarebeweis durch Fixtures und keine GitHub-CI-Ausführung behauptet.

Am Gerät wurde der Bluetooth-Deaktivierungsdialog geöffnet und abgebrochen: gespeicherte Einstellung bleibt aktiv. Der aktuelle ausgewählte Fahrzeugzustand zeigte den Suchablauf; der Zuordnungs-Löschpfad und die gekoppelte Sperre wurden nicht destruktiv am Gerät getestet. No-flash-Erfolgsablauf ist offline mit DOM-/Ereignistests geprüft, nicht durch einen zusätzlichen bestätigten Bluetooth-Neustart am Gerät. Die generische vorhandene Konfigurationsschreibfunktion garantiert keine nachgewiesene Stromausfall-Atomizität; API-Readback ist kein physischer Flash-Dauerhaftigkeitsbeweis.

## Noch nicht bestätigt

**Erfolgreiches Tesla-Pairing und realer SoC-Abruf sind weiterhin nicht hardwarebestätigt.** Gleiches gilt für Wiederverbindung/Mehrfahrzeugwechsel und Langzeit-/WLAN-/BLE-/Ladekoexistenz. Entkoppeln mit echten Fahrzeugschlüsseln wurde nicht destruktiv getestet; dazu dienen bisher isolierte Profil-/Backend- und UI-Tests. Der bestätigende Bluetooth-Aus-/An-Neustartablauf ist offline getestet, am Gerät wurde der Dialog bislang nur abgebrochen. Keine vollständige Fahrzeugabnahme.

## Installation

1. Nur WARP4; nicht WARP1/2/3 oder beliebige ESP32-Boards. Lokaler Zugriff und akzeptiertes Ausfallrisiko erforderlich.
2. Einstellungen vorab dokumentieren und vorhandene Sicherungsmöglichkeiten nutzen. Ein Originalslot ist kein Backup gemeinsam genutzter Konfigurationsdaten; vollständiger Restore/Fernrettung nicht garantiert.
3. `warp4-tesla-ble-2.13.5-6ad9e156-experimental_ota.bin` und SHA256SUMS herunterladen und Prüfsumme vergleichen.
4. Ausschließlich den normalen Web-Firmware-Updater verwenden. Keine esptool-/Fullflash-/Bootloader-/Partitionsinstallation. Keine Sicherheitsfunktionen deaktivieren. Warnung zur fehlenden Herstellersignatur bewusst nur für diesen Experimentalbuild bestätigen.
5. Nach Neustart automatische Boot-Validierung abwarten (im verwendeten Stand ungefähr sieben Minuten), danach Browser vollständig neu laden. Bootvalidierung ist kein Tesla-Funktionstest.
6. Bluetooth explizit unter Schnittstellen aktivieren und den Neustart bestätigen. Fahrzeugeinrichtung bleibt unter Fahrzeuge.

### Hinweis zum Upload

Bei der Installation dieses Builds brachen mehrere Uploads in der Originalfirmware 2.13.4 mit einem Webserver-/WebSocket-Watchdog ab. Nach einem ausdrücklich ausgelösten normalen Neustart der Originalfirmware ließ sich **dasselbe unveränderte Paket** erfolgreich installieren. Der genaue Auslöser ist nicht bewiesen. Bei Hängern nicht fortlaufend blind erneut hochladen; gültigen laufenden Slot prüfen und keine ungültige Teilinstallation starten. Ein normaler Neustart ist kein Werksreset. Kein pauschales Kompatibilitätsversprechen daraus ableiten.

## Quellen und Rückmeldungen

Der Release-Tag enthält den zugehörigen Anwendungsquellstand, Tests und vorhandene SDK-Overlays/Patches. Historische Source-only-Texte beschreiben frühere Übergaben oder gesperrte Review-Buildziele, nicht die Freigabe dieser experimentellen Anlage. Ein byteidentischer Ein-Kommando-Rebuild wird nicht zugesagt; private lokale Buildnachweise werden nicht veröffentlicht.

Fehler bitte mit Firmwareversion, Bedienfolge und sichtbarer Meldung berichten. Keine echten VINs, MAC-Adressen, Schlüssel, Zugangsdaten, vollständigen Debugreports oder Coredumps öffentlich posten. Testidentitäten im Quellcode sind synthetisch.
