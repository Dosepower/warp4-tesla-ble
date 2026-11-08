# WARP Fernbedienung – Entwicklungsstand (WIP)

Dieser Branch erweitert den aktuellen Stand des bestehenden `Dosepower/warp4-tesla-ble`-Forks. Er ersetzt weder dessen Historie noch den freigegebenen `main`-Stand. Änderungen an vorhandenen Dateien wurden vor dem Export gegen die aktuelle Fork-Basis abgeglichen.

## Ein Quellstand, getrennte Funktionen

Drei echte Compile-Time-Profile aus demselben aktuellen Quellstand sind lokal vollständig kompiliert und gelinkt:
- **Tesla solo**: `warp4_ble_tesla_only_review` — Tesla-BLE ohne Remote-Kryptografie, Enrollment, Session, Speicheradapter oder Remote-UI/API.
- **Fernbedienung solo**: `warp4_ble_remote_only_review` — Fernbedienung ohne `TeslaVehicle`, Tesla-Protokollimplementierung oder `TeslaBLEClean`-Bibliothek.
- **Kombiniert**: `warp4_ble_combined_review` — beide Funktionen mit genau einem gemeinsamen Bluetooth-Host und Lease-/Callback-Drain-Pfad.

Das vorhandene Ziel `warp4_tesla_ble_nimble_review` behält seinen kombinierten WIP-Kompatibilitätsstandard. Es wird nicht stillschweigend zum Solo-Ziel umgedeutet. Die drei neuen Ziele erben weiterhin WARP4-Produkt-, Partitions-, Updater-, Sicherheits- und SDK-Speichereinstellungen. Alle bisherigen kanonischen Backendmodule und normalen Fahrzeugprofile bleiben erhalten.

`BluetoothCore` besitzt die gemeinsame Bluetooth-Konfiguration, die native API und das sichere Neustartverhalten. `TeslaVehicle` erweitert diesen Eigentümer nur in Tesla-/kombinierten Builds; `WarpRemote` ist das eigenständige Remote-only-Backend. Die bestehenden Namen/Pfade des gemeinsamen NimBLE-Transports und der Frontend-/Übersetzungsnamespace `tesla_vehicle` bleiben aus Kompatibilitätsgründen erhalten. Das bedeutet nicht, dass Remote-only die Tesla-Implementierung mitführt: Quellen, EV-Erweiterung und Übersetzungen der ausgeschlossenen Funktion werden vor dem Bundling entfernt. Die Bluetooth-Navigation verwendet den tatsächlich registrierten Backend-Eigentümer.

Der Variantenwechsel löscht keine Daten der ausgeschlossenen Funktion. Remote-Schlüssel und Operationsjournal bleiben separat im Namespace `warp_remote`; Tesla-Zuordnungen und Schlüssel behalten ihre bisherigen Speicherpfade. Startup, Seitenanzeige und passives Speichern führen kein Pairing, keinen Tesla-Scan und keinen Ladebefehl aus. Das Laden vorhandener Remote-Daten schreibt weder Bindungen noch das Replay-/Operationsjournal neu. Die bestehenden expliziten Aktionen und das normale Betriebsverhalten der jeweils enthaltenen Funktion bleiben erhalten.

**Dies sind NONINSTALLABLE Review-ELFs, keine OTA-Dateien und keine Installationsfreigabe.** Ein Laufzeitschalter ist weiterhin kein Nachweis eines Solo-Builds.

Die Fernbedienung ist standardmäßig aus. Die bestehende Tesla-Funktion und deren Zuordnungen bleiben erhalten. Änderungen werden über diesen Draft-PR geprüft, nicht direkt in `main` veröffentlicht. Die Tag-Firmware wird separat im privaten Repository `Dosepower/nordic-tracker` gepflegt.

## Implementiert / noch offen

Vorhanden sind native Bluetooth-Einstellungen, Blink/Klick-Kopplung, verschlüsselte Enrollment-/Statuspfade, getrennte Schlüsselspeicherung, Wiederaufnahme und Replay-/Wiederholungsschutz. Die Tag-Bedienung trennt kurze Status-/Auswahlklicks, Bestätigung beim Loslassen nach 2 bis unter 5 Sekunden und ausschließliches Tracking-Umschalten ab 5 Sekunden. Schütteln verändert keinen Lademodus.

**Weiterhin offen:** evcc-Ladebefehlsadapter, abschließende Protokollprüfung, vollständige Maximalleistungs-/Telemetriequalifikation, unabhängige Abschlussprüfung und physische Geräteabnahme. Diese Variantenarbeit löst die evcc-Autoritätsfrage nicht und aktiviert keinen neuen Ladebefehl. Die neue destruktive lokale Löschgeste ist nicht freigegeben und nicht implementiert. Tag-Firmware und gemeinsame Protokollheader wurden für die Varianten nicht geändert. Kein Release und keine Installationsfreigabe durch diesen Quellcheckpoint.

## Prüfungen und Reproduzierbarkeit

Die drei vollständigen Review-Links wurden offline mit den vorhandenen geprüften SDK-Libraries und dem Arduino-Core schreibgeschützt ausgeführt. Pro Variante wurden ELF/MAP, Konfiguration, Metadaten, Aufruf, Log und Hashes separat erhalten. Die Prüfung verknüpft die tatsächlichen Modulregistrierungen mit den vom Linker behaltenen Objekten/Symbolen und den exakt im ELF gefundenen, dekomprimierten Webdaten. Sie prüft auch das Fehlen der Gegenfunktion, deutsche/englische UI, gemeinsame Bluetooth-Seite und die unveränderte Menge kanonischer Backendmodule. Statische Größen sind dokumentiert; daraus wird keine gemessene Laufzeit-Speicherreserve abgeleitet.

Native Tesla-Sanitizerregressionen laufen sowohl kombiniert als auch ohne Remote-Quellen. Die Remote-Tests umfassen produktive Zwei-Peer-GATT-Pfade, Kryptografie-Crosscheck, Namespace-/Journalerhalt, passives Startup/Speichern und die Radio-Lease samt SDK-Callback-Drain auch ohne Tesla. Alle drei tatsächlich generierten Frontends wurden typgeprüft und ihre Varianten-/Komponentenpfade ausgeführt. Lokale Tests sind **kein Nachweis physischer Funk-/Geräteabnahme oder eines auf GitHub reproduzierten Firmwarebuilds**.

Die ursprünglich versiegelten Toolpaket-Verweise waren nicht mehr vollständig auflösbar. Für diese neuen Builds wurde ein ausdrücklich freigegebener, bereits lokal vorhandener alternativer Toolpaketsatz anhand tatsächlicher Dateihashes, Paketidentität, Compiler-Version/-Ausführung und erfolgreicher Links neu qualifiziert. Eine historische Byteidentität mit der alten Toolchain wurde nicht nachgewiesen und wird nicht behauptet. Originale SDKs, Manifeste, Locks und frühere Binärbelege bleiben unverändert; keine synthetischen Trust-Metadaten ersetzen eine Schutzprüfung.

Beide Workflowdateien sind jetzt ausdrücklich auf portable Hostprüfungen mit `contents: read` begrenzt. Der geerbte nächtliche Komplettbuild und dessen Cache-Schreibzugriffe wurden im Feature-Branch durch Varianten-/Radio-Hosttests ersetzt. Es gibt keine Firmware-/Release-/Upload-Aktion und keine behauptete Drei-Varianten-Firmware-CI. Die Workflow-Kommandos wurden lokal ausgeführt; ein GitHub-Lauf dieses Quellstands steht noch aus.

Der Export lässt private Geräte-/Kontodaten, generierte Schlüsselheader, lokale SDK-/Buildpfade, Builds, Dumps, Installationsbelege und lokale Installer-Trust-Anker weg. Quellcode allein ist kein vollständiges SDK-Backup. Vor einem eigenen Firmwarebuild sind die bestehenden Fork-SDK-/Verifier-Voraussetzungen zu erfüllen. Keine Schutzprüfung durch erfundene Locks umgehen.

### Buildvoraussetzungen

Benötigt werden die zu den vorhandenen Verifier-Manifests passenden SDK-/Core-Eingaben, eine qualifizierte PlatformIO-Toolchain, Python-Abhängigkeiten und die zum Lockfile passenden Webabhängigkeiten. SDK-Auswahl erfolgt über `TESLA_BLE_FRAMEWORK_CORE`, `TESLA_BLE_FRAMEWORK_LIBS`, `TESLA_BLE_SDK_SOURCE_ROOT` und die vorhandene NimBLE-Policy `TESLA_BLE_SDK_POLICY`. Originale Trust-/Verifier-Eingaben werden ausschließlich in privaten Buildkopien bereitgestellt. Maschinenpfade, Locks oder generierte Header gehören nicht in den öffentlichen Quellbaum.

Nach Bereitstellung dieser Voraussetzungen wird im Verzeichnis `software` je Variante `python -m platformio run -e <Umgebung> -t tesla-review` ausgeführt. Das Ziel erlaubt nur den ausdrücklich nicht installierbaren Compile-/Link-Review, kein Upload, Signieren oder Provisionieren. Ausgaben jeder Variante separat aufbewahren; der historische ELF-Basisname enthält weiterhin `tesla-source-review-NONINSTALLABLE`.

Für die optionalen Kryptografie-/Zwei-Peer-Hosttests werden lokale geprüfte Abhängigkeiten explizit angegeben:
- `REMOTE_TEST_MBEDTLS_SOURCE`: mbedTLS-Quellen.
- `REMOTE_TEST_MBEDTLS_BUILD`: dazu passender Build mit den benötigten statischen Bibliotheken.
- `REMOTE_TEST_NCS_ROOT`: Nordic-SDK-Installationswurzel für den Oberon-Crosscheck.
- `REMOTE_TEST_TAG_ROOT`: passender Tag-Checkout für echte Zwei-Peer- und Headervergleichstests; keine Tag-Änderung erforderlich.
- `REMOTE_TEST_GENERATED_ROOT`: privater generierter `software`-Buildbaum für den tatsächlichen Charge-Mode-Enum-Header.

`test_build_variants.py` und `test_remote_scan.py` benötigen nur Python/pytest und den nativen C++-Compiler, keine Firmware-SDKs. Der bestehende Tesla-Sanitizerrunner erhält seine mbedTLS-Eingaben über `TESLA_CLEAN_TEST_OUT` und `TESLA_BLE_HOST_MBEDTLS_SOURCE`; mit `TESLA_NIMBLE_REMOTE_FEATURE=0` schließt er Remote-Quellen tatsächlich aus. `test_variant_frontend.mjs` verwendet über `WARP_VARIANT_SOFTWARE` den echten generierten Buildbaum und prüft die mit `WARP_TEST_TESLA`/`WARP_TEST_REMOTE` bezeichnete Auswahl. Test-/Buildausgaben bleiben außerhalb des öffentlichen Quellbaums.

Die Pfadnormalisierung betrifft nur exportierte Tests, nicht die Produktquellen. Nicht alle Testwerkzeuge sind bereits für fremde Rechner vollständig paketiert.

## Veröffentlichung

- Quell-WIP und Draft-PR zuerst; kein automatischer Merge.
- Bestehende Releases und Rückfallfirmware unverändert erhalten.
- Später geprüfte OTA-Dateien je Variante plus SHA256SUMS; keine Fullflash-/Gerätedumps.
- Eine gemeinsame Remote-Protokolländerung muss in WARP und Tag abgestimmt werden. Der aktuelle WIP ist noch keine stabil zugesicherte Protokollversion.
