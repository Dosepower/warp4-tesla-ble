# WARP4 Tesla BLE · experimentelle Firmware

**Inoffizielle Erweiterung für WARP4. Keine Herstellerfreigabe, keine stabile Fahrzeugfreigabe.**

## Firmware herunterladen

- **[Aktuell veröffentlichte Preview 4 – OTA, Anleitung und SHA256SUMS](https://github.com/Dosepower/warp4-tesla-ble/releases/tag/v0.2.3-warp4-experimental-preview4)** – Firmware `2.13.5+6ad9fd3c`.
- [Alle Releases und ältere Versionen](https://github.com/Dosepower/warp4-tesla-ble/releases)
- [Aktueller Entwicklungs-/Review-PR #2](https://github.com/Dosepower/warp4-tesla-ble/pull/2)

Preview 4 ergänzt Fundinformationen mit Ablaufprüfung und blendet die Tesla-Erweiterung bei Bluetooth aus vollständig aus. Die Ausblendung wurde auf der eigenen WARP4 im Browser geprüft; neue Suche/Übernahme wurde für diesen Build nicht am Fahrzeug ausgeführt. Für die Installation gilt immer die Anleitung des konkret gewählten Releases; ausschließlich dessen normales WARP4-Web-OTA verwenden. Keine Fullflash-/Bootloader-/Partitionsinstallation und keine Sicherheitsfunktionen deaktivieren.

## Was die Erweiterung bietet

- NimBLE-basierte Tesla-Bluetooth-Anbindung in der vorhandenen Fahrzeugverwaltung.
- Mehrere getrennte Fahrzeugprofile; explizite VIN-Suche, eindeutigen Treffer übernehmen, danach ausdrücklich koppeln.
- Keine automatische Suche, Kopplung oder SoC-Abfrage durch Seitenaufruf oder Update.
- Schlüsselzentrierte Oberfläche, Details und getrennte Zuordnungs-/Entkopplungsaktionen mit Sicherheitsprüfungen.
- Normales AC-Laden bleibt vom Erfolg einer BLE-Abfrage unabhängig.

## Getestet / noch offen

**Bestätigt:** Preview 4 auf eigener WARP4 installiert und bootvalidiert; Originalfirmware im zweiten gültigen Slot erhalten. Sicherheitsfunktionen aktiv, kein EVSE-Fehler bei Prüfung. Umfangreiche lokale Backend-/Frontend-/Sanitizer-Tests und unabhängige Abschlussprüfung. VIN-Suche und Trefferübernahme wurden in einem früheren Build am Gerät geprüft.

**Noch nicht fahrzeugbestätigt:** erfolgreiches Tesla-Pairing, authentifizierter realer SoC-Abruf, Mehrfahrzeug-Wiederverbindung und Langzeit-/Ladekoexistenz. Destruktive Zuordnungs-/Schlüsseltests sind keine behaupteten Live-Tests. Details und Grenzen stehen in jedem Release.

Ein gültiger Originalslot ist kein Backup der gemeinsam genutzten Konfiguration. Keine garantierte Fernrettung oder vollständige Stromausfall-/Restoregarantie.

## Orientierung im Repository

- Diese Startseite und die **Release-Anleitung** sind der Einstieg.
- `EXPERIMENTAL_PREVIEW_*.md`: festgehaltene Hinweise zum jeweiligen veröffentlichten Stand, keine automatische Aussage über spätere Firmware.
- [Historische Unterlagen und Archiv](HISTORY.md): frühere Testboard-, Phase- und Reviewstände. Nicht als aktuelle Installationsanleitung verwenden.
- [Ursprüngliche Tinkerforge-Projektübersicht](README_UPSTREAM.rst): Herkunft, übrige Produkte und Upstream-Verweise.
- `software/`: Firmwarequellen und Tests. Release-Tags binden die jeweils veröffentlichten Quellstände; die zusätzliche Projektstartseite verändert keine Firmwarebytes.

Fehler zur Erweiterung bitte [hier melden](https://github.com/Dosepower/warp4-tesla-ble/issues). Keine echten VINs, MAC-Adressen, Schlüssel, Passwörter, vollständigen Debugreports oder Coredumps veröffentlichen.
