# WARP4 Tesla BLE – Experimental Preview 2

**Inoffizielle experimentelle Firmware ausschließlich für WARP4. Keine stabile Release und keine Tinkerforge-Freigabe.**

Firmware `2.13.5+6ad9ac5e`. Angefügtes OTA ist exakt der auf meiner WARP4 installierte und bootvalidierte Build. Separat aus Quellen gebautes, angepasstes NimBLE/Secure-BLE-SDK; kein unverändertes Hersteller-SDK.

## Änderungen seit Preview 1

- Schlüsselzentrierte Fahrzeugeinrichtung ohne zusätzlichen „Tesla-Bluetooth verwenden“-Schalter oder separaten Freigabe-Speichern-Schritt.
- Expliziter Ablauf VIN → Fahrzeug suchen → eindeutigen Treffer übernehmen → Mit Tesla koppeln. Übernehmen speichert die Zuordnung, startet aber keine Kopplung, Verbindung oder Abfrage.
- Kompakte gekoppelte Ansicht mit Ladezustand und manueller Abfrage; keine erfundenen Produktionswerte.
- Eigener Bereich Details mit seitenbreitem Anzeigen-/Verstecken-Button. VIN, BLE-Adresse, vorbereitetes und kabelseitig erkanntes Profil sowie Verbindung sind Diagnoseangaben. Der technisch weiterhin gespeicherte Adresstyp wird nicht angezeigt.
- Nach bestätigter Kopplung ersetzt ein vollflächig roter Entkoppeln-Button den Koppeln-Button an derselben Position. Bestätigung erklärt lokale Schlüsselentfernung und die davon unabhängige Tesla-Schlüsselverwaltung. Andere Fahrzeugprofile bleiben unberührt.
- Bluetooth-Schalter öffnet direkt Aktivieren-/Deaktivieren-und-Neustarten-Dialog; separater Speichern-Button und gelbe Neustartbox entfallen. Abbrechen/Schließen/Escape ändern nichts. Neustart erst nach erfolgreichem Speichern und Zustandsprüfung; Fehler und veraltete Bestätigungen lösen keinen Neustart aus. Der Neustart bleibt technisch erforderlich, kein Live-Umschalten des Bluetooth-Stacks.
- Permanenter blauer Tesla-Hinweis bei deaktiviertem Bluetooth entfernt. Echte Fehler und notwendige Aktionsvoraussetzungen bleiben sichtbar.
- Bestehende Profil-/Identitäts-/Frische-/Single-use-Consent-Prüfungen sowie die Trennung von BLE und Ladefreigabe erhalten.

## Tatsächlich getestet

Auf eigener WARP4: Installation, Start und automatische Boot-Validierung dieses exakten Builds. Originalfirmware bleibt auf dem anderen gültigen Slot erhalten. Aktuelle Oberfläche live geprüft: Bluetooth-Aktivierungsdialog öffnen und abbrechen lässt den Schalter und die gespeicherte Einstellung aus; kein separater Speichern-Button. Fahrzeugseite bei Bluetooth aus ohne permanente blaue Info. Sicherheit aktiv, kein EVSE-Fehler.

Suche und Speichern eines eindeutigen VIN-Treffers waren im Vorgängerbuild am Gerät erfolgreich; dies ist kein behaupteter neuer Fahrzeugtest dieses Builds. Der frühere Stackoverflow beim Speichern wurde zuvor gezielt behoben.

Offline: 56/56 Prüfskripte, Hosttests normal und ASan/UBSan, betroffene zusätzliche Sanitizer-Tests, Produktionsfrontend und Browserfixtures bei 360/390/1280 px bestanden. Die unabhängige Endprüfung einschließlich 60 zusätzlicher UI-Race-/Abbruchprüfungen bestand. Eingebetteter Webinhalt wurde aus dem ELF gegen tatsächlichen Webbuild und App/OTA geprüft. Keine GitHub-CI-Ausführung behauptet.

## Noch nicht bestätigt

**Erfolgreiches Tesla-Pairing und realer SoC-Abruf sind weiterhin nicht hardwarebestätigt.** Gleiches gilt für Wiederverbindung/Mehrfahrzeugwechsel und Langzeit-/WLAN-/BLE-/Ladekoexistenz. Entkoppeln mit echten Fahrzeugschlüsseln wurde nicht destruktiv getestet; dazu dienen bisher isolierte Profil-/Backend- und UI-Tests. Der bestätigende Bluetooth-Aus-/An-Neustartablauf ist offline getestet, am Gerät wurde der Dialog bislang nur abgebrochen. Keine vollständige Fahrzeugabnahme.

## Installation

1. Nur WARP4; nicht WARP1/2/3 oder beliebige ESP32-Boards. Lokaler Zugriff und akzeptiertes Ausfallrisiko erforderlich.
2. Einstellungen vorab dokumentieren und vorhandene Sicherungsmöglichkeiten nutzen. Ein Originalslot ist kein Backup gemeinsam genutzter Konfigurationsdaten; vollständiger Restore/Fernrettung nicht garantiert.
3. `warp4-tesla-ble-2.13.5-6ad9ac5e-experimental_ota.bin` und SHA256SUMS herunterladen und Prüfsumme vergleichen.
4. Ausschließlich den normalen Web-Firmware-Updater verwenden. Keine esptool-/Fullflash-/Bootloader-/Partitionsinstallation. Keine Sicherheitsfunktionen deaktivieren. Warnung zur fehlenden Herstellersignatur bewusst nur für diesen Experimentalbuild bestätigen.
5. Nach Neustart automatische Boot-Validierung abwarten (im verwendeten Stand ungefähr sieben Minuten), danach Browser vollständig neu laden. Bootvalidierung ist kein Tesla-Funktionstest.
6. Bluetooth explizit unter Schnittstellen aktivieren und den Neustart bestätigen. Fahrzeugeinrichtung bleibt unter Fahrzeuge.

### Hinweis zum Upload

Bei der Installation dieses Builds brachen mehrere Uploads in der Originalfirmware 2.13.4 mit einem Webserver-/WebSocket-Watchdog ab. Nach einem ausdrücklich ausgelösten normalen Neustart der Originalfirmware ließ sich **dasselbe unveränderte Paket** erfolgreich installieren. Der genaue Auslöser ist nicht bewiesen. Bei Hängern nicht fortlaufend blind erneut hochladen; gültigen laufenden Slot prüfen und keine ungültige Teilinstallation starten. Ein normaler Neustart ist kein Werksreset. Kein pauschales Kompatibilitätsversprechen daraus ableiten.

## Quellen und Rückmeldungen

Der Release-Tag enthält den zugehörigen Anwendungsquellstand, Tests und vorhandene SDK-Overlays/Patches. Historische Source-only-Texte beschreiben frühere Übergaben oder gesperrte Review-Buildziele, nicht die Freigabe dieser experimentellen Anlage. Ein byteidentischer Ein-Kommando-Rebuild wird nicht zugesagt; private lokale Buildnachweise werden nicht veröffentlicht.

Fehler bitte mit Firmwareversion, Bedienfolge und sichtbarer Meldung berichten. Keine echten VINs, MAC-Adressen, Schlüssel, Zugangsdaten, vollständigen Debugreports oder Coredumps öffentlich posten. Testidentitäten im Quellcode sind synthetisch.
