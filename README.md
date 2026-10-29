# WARP4 Tesla Bluetooth

Tesla-BLE-Integration für WARP4 auf Basis der Tinkerforge-Firmware.

## Aktueller Stand

**Installierter Teststand · Firmware `2.13.6+6ae39075`**

Basis: offizieller Quellstand WARP4 2.13.6 (`afa5c782`). Eigenes SDK auf ESP-IDF 5.5.6 / Arduino 3.3.12 neu gebaut und geprüft; keine Behauptung identischer offizieller Binärdateien. Installation und automatische Boot-Validierung am Testgerät bestätigt, bestehende Tesla-Kopplung erhalten.

Tesla-SoC nutzt jetzt begrenzte 25-Sekunden-Phasen, Session-Wiederholungen im 1-Sekunden-Takt mit 4-Sekunden-Challenge-Frische, exponentiellen Fehler-Backoff und eine absolute 90-Sekunden-Grenze. Bestehendes Kopplungs-Timing bleibt getrennt erhalten. Ob dies die zuvor beobachteten sporadischen Abruffehler am Fahrzeug behebt, muss der anschließende Live-Test zeigen.

Der Kandidat ist auf dem Testgerät gestartet. Host-/Frontend-Tests einschließlich Sanitizern, TypeScript-Prüfung, vollständiger Firmware-/Web-Build und OTA-Artefaktprüfungen wurden erfolgreich durchgeführt. Vollständige reale Schlaf-/Wake-/Ladezyklus-Abnahme bleibt offen; dies ist keine allgemeine Stable-Freigabe.

### Funktionen

- Geführte Tesla-Kopplung mit kryptographisch bestätigtem Erfolg und getrennten Fahrzeugschlüsseln.
- Automatischer SoC-Abruf beim Anstecken und Ladebeginn; periodische Aktualisierung nur während aktiver Ladung.
- Intervalle ab **5 Minuten**; bei Pause/Ende bleibt der letzte Wert mit echter Empfangsuhrzeit sichtbar.
- Manueller Abruf, bedarfsgerechtes begrenztes Wecken und einmaliger Folgeabruf ohne erneutes Wecken.
- Aufgeräumte Oberfläche mit Einstellungen und Diagnose; einheitliche Buttonbreiten.

## Orientierung

- Aktuelle Entwicklung: **`main`**.
- Quellcode und Tests: [`software/`](software/).
- Build-/Installationshinweise: [`software/WARP4_INSTALLER.md`](software/WARP4_INSTALLER.md).
- Einschränkungen: [`software/TESLA_BLE_EXPERIMENTAL.md`](software/TESLA_BLE_EXPERIMENTAL.md) sowie die historischen `CHECKPOINT_*.md`-Berichte.
- Frühere Entwicklungsstände bleiben unter **`archive/*`-Tags** erhalten. Alte Preview-Releases sind historische Artefakte, nicht der aktuelle Installationsstand.

## Sicherheit und Build

Das Repository enthält keine privaten Fahrzeugschlüssel, Gerätekonfigurationen oder privaten Fahrzeugprotokolle. Quellsicherung ersetzt keine Sicherung der separat gepinnten SDK-/Toolchain-Umgebung. Vor Installation die für das konkrete Gerät geltenden OTA-, Slot-, Verschlüsselungs- und Signaturprüfungen durchführen und eine gültige Original-Firmware erhalten. Keine privaten Provisionierungsdaten veröffentlichen.

## Herkunft

Basis: [Tinkerforge ESP32-Firmware](https://github.com/Tinkerforge/esp32-firmware). Die Wake-Implementierung wurde mit [yoziru/esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble) und dessen gepinnter TeslaBLE-Abhängigkeit verglichen; die bestehenden Authentifizierungs- und Sicherheitsgrenzen wurden nicht durch vereinfachte Referenzpfade ersetzt. Lizenzhinweise der jeweiligen Komponenten bleiben maßgeblich.
