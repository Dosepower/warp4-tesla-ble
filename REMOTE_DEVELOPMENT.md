# WARP Fernbedienung – Entwicklungsstand (WIP)

Dieser Branch erweitert den aktuellen Stand des bestehenden `Dosepower/warp4-tesla-ble`-Forks. Er ersetzt weder dessen Historie noch den freigegebenen `main`-Stand. Änderungen an vorhandenen Dateien wurden vor dem Export gegen die aktuelle Fork-Basis abgeglichen.

## Ein Quellstand, getrennte Funktionen

Ziel sind drei echte Buildprofile aus einem gemeinsamen Quellstand:
- **Tesla solo**: nur Tesla-BLE.
- **Fernbedienung solo**: nur WARP-Fernbedienung.
- **Kombiniert**: beide Funktionen mit gemeinsamem Bluetooth-Host.

**Aktuell ist nur der kombinierte Entwicklungsweg vorhanden.** Tesla und Fernbedienung sind noch nicht vollständig in unabhängig auswählbare Buildmodule getrennt. Es gibt daher noch keine geprüften Solo-Images und keine Drei-Varianten-Firmware-CI. Ein deaktivierter Laufzeitschalter ist kein Nachweis eines Solo-Builds.

Die Fernbedienung ist standardmäßig aus. Die bestehende Tesla-Funktion und deren Zuordnungen bleiben erhalten. Änderungen werden über diesen Draft-PR geprüft, nicht direkt in `main` veröffentlicht. Die Tag-Firmware wird separat im privaten Repository `Dosepower/nordic-tracker` gepflegt.

## Implementiert / noch offen

Vorhanden sind native Bluetooth-Einstellungen, Blink/Klick-Kopplung, verschlüsselte Enrollment-/Statuspfade, getrennte Schlüsselspeicherung, Wiederaufnahme und Replay-/Wiederholungsschutz. Die Tag-Bedienung trennt kurze Status-/Auswahlklicks, Bestätigung beim Loslassen nach 2 bis unter 5 Sekunden und ausschließliches Tracking-Umschalten ab 5 Sekunden. Schütteln verändert keinen Lademodus.

**Nicht fertig:** evcc-Ladebefehlsadapter, abschließende Protokollprüfung, vollständige Maximalleistungs-/Telemetriequalifikation, Solo-Buildprofile, unabhängige Abschlussprüfung und physische Geräteabnahme. Die neue destruktive lokale Löschgeste ist nicht freigegeben und nicht implementiert. Kein Release und keine Installationsfreigabe durch diesen Quellcheckpoint.

## Prüfungen und Reproduzierbarkeit

Lokal wurden beide Firmwareziele, native Tests, generierte/montierte UI und eingebettete Webdaten geprüft. Diese Aussage beschreibt lokale Kandidaten, **nicht einen auf GitHub erfolgreich reproduzierten Komplettbuild**. GitHub Actions führt zunächst nur ausgewählte portable Hosttests aus; es erzeugt oder veröffentlicht keine Firmware.

Der Export lässt private Geräte-/Kontodaten, generierte Schlüsselheader, lokale SDK-/Buildpfade, Builds, Dumps, Installationsbelege und lokale Installer-Trust-Anker weg. Quellcode allein ist kein vollständiges SDK-Backup. Vor einem eigenen Firmwarebuild sind die bestehenden Fork-SDK-/Verifier-Voraussetzungen zu erfüllen. Keine Schutzprüfung durch erfundene Locks umgehen.

Für die optionalen Kryptografie-/Zwei-Peer-Hosttests werden lokale geprüfte Abhängigkeiten explizit angegeben:
- `REMOTE_TEST_MBEDTLS_SOURCE`: mbedTLS-Quellen.
- `REMOTE_TEST_MBEDTLS_BUILD`: dazu passender Build mit den benötigten statischen Bibliotheken.
- `REMOTE_TEST_NCS_ROOT`: Nordic-SDK-Installationswurzel für den Oberon-Crosscheck.

Die Pfadnormalisierung betrifft nur exportierte Tests, nicht die Produktquellen. Nicht alle Testwerkzeuge sind bereits für fremde Rechner vollständig paketiert.

## Veröffentlichung

- Quell-WIP und Draft-PR zuerst; kein automatischer Merge.
- Bestehende Releases und Rückfallfirmware unverändert erhalten.
- Später geprüfte OTA-Dateien je Variante plus SHA256SUMS; keine Fullflash-/Gerätedumps.
- Eine gemeinsame Remote-Protokolländerung muss in WARP und Tag abgestimmt werden. Der aktuelle WIP ist noch keine stabil zugesicherte Protokollversion.
