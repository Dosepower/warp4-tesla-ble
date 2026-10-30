# WARP4 Tesla BLE — Experimental Preview 1

**Experimentelle, inoffizielle Firmware ausschließlich für WARP4. Keine offizielle Tinkerforge-Firmware und keine stabile Release.**

Firmware: `2.13.5+6ad91246`. Der OTA-Download entspricht bytegenau dem auf meiner WARP4 installierten Teststand. Der Build basiert auf dem veröffentlichten WARP4-Port und verwendet ein separat aus Quellen gebautes Secure/BLE-SDK mit NimBLE und dokumentierten Patches. Er ist nicht mit einem unveränderten Hersteller-SDK gleichzusetzen.

## Am eigenen Gerät bestätigt

- Update über den Web-Firmware-Updater, erfolgreicher Start und automatische Boot-Validierung.
- Bluetooth aktiviert und im Leerlauf betrieben.
- VIN-basierte Tesla-Suche und Speichern des eindeutigen Treffers im Fahrzeugprofil.
- Geführte Tesla-Einrichtung unter Fahrzeuge; reduzierte Bluetooth-Seite unter Schnittstellen.
- Die neue Oberfläche wurde direkt im finalen ELF/App/OTA nachgewiesen, nicht nur im Quellcode geprüft.
- Ein früherer loopTask-Stacküberlauf beim Speichern wurde gezielt behoben. Der wiederholte Such-/Speichervorgang funktionierte danach ohne erneuten Stacküberlauf.

## Implementiert, aber am echten Fahrzeug noch nicht bestätigt

- Schlüsselkopplung mit Freigabe am Tesla.
- Authentifizierte Ladezustandsabfrage.
- Wiederverbindung, Schlüsselwiederverwendung und Wechsel zwischen mehreren Tesla-Profilen.
- Langzeitstabilität, Schlaf-/Aufweckverhalten und gleichzeitiger WLAN-/BLE-/Ladebetrieb.

**Erfolgreiches Pairing und ein realer SoC-Abruf sind bisher nicht bestätigt.** Mehrere getrennte Fahrzeugprofile sind implementiert; es gibt nur eine gleichzeitige BLE-Verbindung. Die noch offenen Fahrzeugtests werde ich am Samstag nach meinem Urlaub selbst fortsetzen.

Bluetooth-Aktivierung, Seitenaufruf, Speichern und Übernehmen eines Suchtreffers starten keine automatische Kopplung oder SoC-Abfrage. Fahrzeugaktionen bleiben explizit. BLE ist keine Ladefreigabe und ersetzt nicht die normale IEC-61851-Ladesteuerung.

## Softwareprüfung

56/56 lokale Prüfskripte bestanden; dokumentierte Legacy-Ausschlüsse bleiben bestehen. Hosttests normal und mit ASan/UBSan, zusätzliche Sanitizer-Prüfungen, Produktions-Frontend-Tests und Firmware-/OTA-Prüfungen bestanden. Eine unabhängige abschließende Softwareprüfung des korrigierten eingebetteten Webinhalts bestand ebenfalls. Diese Prüfungen ersetzen keine vollständigen Fahrzeug- oder Dauerbetriebstests. Es wird kein GitHub-CI-Ergebnis behauptet.

## Installation / Download

1. Nur auf einer **WARP4** verwenden. Nicht für WARP1/2/3 oder beliebige ESP32-Boards.
2. Nur testen, wenn lokaler Zugriff und ein geeigneter Wiederherstellungsweg vorhanden sind. Kein Update einer ausschließlich aus der Ferne erreichbaren produktiven Ladestation empfehlen.
3. Vorher vorhandene Einstellungen dokumentieren bzw. die verfügbaren Sicherungsmöglichkeiten nutzen. Eine vollständige unterstützte Konfigurations-Restore-Lösung wird hier nicht zugesagt.
4. Die Datei `warp4-tesla-ble-2.13.5-6ad91246-experimental_ota.bin` herunterladen und die SHA-256-Prüfsumme mit `SHA256SUMS` vergleichen.
5. Über den normalen **Web-Firmware-Updater** hochladen. Das Paket hat keine Tinkerforge-Herstellersignatur; die Warnung nur bewusst für diesen Experimentalbuild bestätigen. Keine Sicherheitsfunktionen deaktivieren.
6. Nach Neustart die automatische Boot-Validierung abwarten (im verwendeten Stand ungefähr sieben Minuten). Ein erfolgreicher Boot ist kein Tesla-Funktionstest.
7. Weboberfläche vollständig neu laden. Bluetooth bei Bedarf unter Schnittstellen aktivieren; Änderungen können einen bestätigten Neustart benötigen. Danach unter Fahrzeuge das passende Profil verwenden und die Einrichtung explizit durchführen.

**Kein vollständiges Flash-Abbild:** Nicht mit esptool als Bootloader-/Partitions-/Full-Flash-Image schreiben. Die Pre-release enthält nur das OTA-Paket für den Web-Updater.

Ein normaler OTA-Wechsel soll vorhandene Konfiguration erhalten; eine Garantie für jeden Fehlerfall ist damit nicht verbunden. Ein Originalslot ist **kein Backup** der gemeinsam genutzten Konfigurationsdaten. Bei weiteren Updates muss die Slotbelegung geprüft werden, damit eine gewünschte Originalfirmware nicht überschrieben wird. Ein sicherer Fern-Rückfall ist nicht garantiert.

## Quellen und Rebuild-Grenzen

Der Release-Tag enthält den zugehörigen Anwendungsquellstand, Tests, SDK-Konfigurations-Overlays und SDK-Patches. Historische Source-only-/DO-NOT-INSTALL-Texte beschreiben ältere Übergaben bzw. die absichtlich gesperrten lokalen Review-Buildziele; dieses Dokument beschreibt ausschließlich das hier angehängte Experimental-OTA. Nicht alle privaten Buildpfad-/Umgebungsnachweise werden veröffentlicht. Ein sofort portabler oder byteidentischer Ein-Kommando-Rebuild wird nicht zugesagt. Die SDK-Pins und Änderungen sind im Quellbaum dokumentiert; keine Hersteller-Freigabe wird behauptet.

Testidentitäten in den Quellen sind synthetisch. Bitte keine echten VINs, MAC-Adressen, Zugangsdaten, Schlüssel, vollständigen Debugreports oder Coredumps öffentlich melden. Für Fehlerberichte genügen zunächst Firmwareversion, konkrete Bedienfolge und die sichtbare Fehlermeldung.
