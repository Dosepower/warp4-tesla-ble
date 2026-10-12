# Vorbereitende ESP32-IRAM-Diagnostik — Build-only

Arbeitsstand auf `diagnostics/esp32-ethernet-iram-prep`, Ausgangscommit
`0d8c0e4e639c9fe`. Kein Upload, Flash, Commit, Push oder Release.
Die unten genannten Binärdateien entstehen ausschließlich lokal durch die
vorhandenen Build-Hooks; sie sind keine veröffentlichten Release-Artefakte.

## Geltungsbereich und Hardwaregrenze

Das Firmwareziel bleibt `warp4_tesla_ble`. Die beiden Diagnoseumgebungen erben
WARP4-Module, `esp32_ethernet_brick_secure` und die WARP4-Partitionierung. Sie
sind **keine freigegebenen Firmwareabbilder für den normalen ESP32 Ethernet
Brick**. Für dessen späteren Prüfstand müssen Boardrevision, Flash/PSRAM,
Partitionierung, Boot-/Security-Konfiguration und fehlende WARP-Peripherie
zuerst separat abgeglichen werden. Ein Brick-Test validiert Controller-/Host-
Ressourcen unter seiner konkreten Last, nicht WARP4, EVSE, SLAC oder Laden.
Keine WARP3/WARP4 und kein anderer Controller wurde in diesem Schritt geflasht.

TeslaBLEClean, die kryptografische Logik, SLAC-/Fahrzeugbindung, Polling,
Schlaf-/Weckpolitik und Ladeverhalten wurden nicht geändert. Es gibt keinen
Bench-Autostart, keinen künstlichen SLAC-Zustand und keinen NimBLE-Pfad.
Bei `tesla_vehicle/config.enable=false` startet auch die Diagnosefirmware keine
Tesla-Runtime, keine Tesla-Queues, keinen Tesla-Worker und kein Bluetooth.

## Instrumentierung

Nur `warp4_tesla_ble_iram_diag` und `warp4_tesla_ble_iram_minimal` definieren
`TESLA_BLE_IRAM_DIAGNOSTICS=1`. Der normale Build hat Inline-No-ops und keine
zusätzlichen Diagnose-Linker-Wrapper. Im normalen ELF ist kein
`tesla_ble_iram_snapshot` enthalten; seine statischen Speicherwerte bleiben
gegenüber dem Ausgangsbuild identisch.

Die Instrumentierung beobachtet die vorhandenen Arduino-/IDF-Aufrufe über
`--wrap`, ohne deren Argumente, Rückgabecodes oder Besitzverhältnisse zu ändern:

| Stage | Messpunkte |
| --- | --- |
| `module.setup` | `boot`, nach Config-Restore, vor bedingtem Runtime-Start |
| `controller.init` | unmittelbar vor/nach `esp_bt_controller_init` |
| `controller.enable` | unmittelbar vor/nach `esp_bt_controller_enable` |
| `host.init` | unmittelbar vor/nach `esp_bluedroid_init` |
| `host.enable` | unmittelbar vor/nach `esp_bluedroid_enable` |
| `gatt.callback_register` | vor/nach `esp_ble_gattc_register_callback` |
| `gatt.app_register` | vor/nach Submission sowie `ESP_GATTC_REG_EVT` |
| `gatt.open` | vor/nach Submission sowie `ESP_GATTC_OPEN_EVT` |

Die Open-Messpunkte gelten für jeden Versuch, auch Fehlversuche und spätere
Verbindungen. Dadurch ist die erste erfolgreiche Verbindung ohne zusätzliche
Zustandsmaschine sichtbar. Callback-Messungen bleiben hinter den bestehenden
Owner-/Admission-/Stop-/App-ID-/Interface-/Peer-Filtern.

Jeder Messpunkt schreibt eine Zeile mit Präfix `TESLA_IRAM` per ROM-UART-Ausgabe:

- `stage`, `phase=boot|before|after|event`, `rc` als dezimale SDK-Rückgabe bzw.
  GATT-Eventstatus; `rc=na` vor dem Aufruf und beim Boot.
- `exec_free`, `exec_largest`, `exec_min` aus
  `heap_caps_get_info(..., MALLOC_CAP_EXEC)`.
- `internal_free`, `internal_largest`, `internal_min` aus
  `heap_caps_get_info(..., MALLOC_CAP_INTERNAL)`.
- Alle Speicherwerte sind Bytes. Keine VIN, Adresse oder Schlüssel im Log.

Wichtig für die Interpretation:

- Erfolgreiche GATT-Submission ist nicht erfolgreiche Registrierung/Verbindung;
  dafür ist zusätzlich der jeweilige `phase=event`-Status erforderlich.
- EXEC und INTERNAL sind überlappende Capability-Sichten, keine addierbaren Pools.
- Die SDK-Minima sind Low-water-Werte der registrierten Heaps, nicht pro Stage
  zurückgesetzt und nicht zwingend ein zeitgleich aufgetretenes Gesamtminimum.
- Die beiden Capability-Abfragen sind keine atomare Gesamtspeichermessung.
- UART-Ausgabe beeinflusst Scheduling und Laufzeit. Andere Logs/Callbacks können
  sich zeitlich dazwischenlegen; ein Event kann vor der `after`-Zeile eintreffen.
- Eine bereits initialisierte Arduino-BLE-Instanz kann Init-Aufrufe überspringen;
  ein solcher Lauf ersetzt keine Messung nach sauberem Neustart.
- Kein Diagnosepunkt startet selbst Bluetooth. Controller/Host benötigen den
  normalen ausdrücklich aktivierten Anwendungspfad. Registrierung/Open bleiben
  an dessen bestehende Identitäts-/Fahrzeugbedingungen gebunden. Ein Brick ohne
  SLAC liefert deshalb nicht automatisch Verbindungsnachweise.

## Getrennte konservative Bluedroid-Variante

`software/lib-builder/defconfig.esp32ethernetbrick.ble-minimal` ist ein Overlay
auf das **vorhandene Bluetooth-fähige sdkconfig**, nicht auf den Bluetooth-
deaktivierten Stock-WARP4-SDK. `warp4_tesla_ble_iram_minimal` verwendet ausschließlich
das getrennte Paket aus `TESLA_BLE_MINIMAL_FRAMEWORK_LIBS`.
Anwendungs-`-D`-Flags allein können vorgebaute IDF-Archive nicht minimieren.

| Einstellung | Ausgangs-SDK | Minimal-SDK | Begründung |
| --- | --- | --- | --- |
| Controller-Modus | BLE-only | BLE-only | bereits aktiv, keine neue Einsparung |
| Host ACL-Verbindungen | 4 | 1 | eine Runtime, ein Peer, serieller Open-Pfad |
| Controller BLE-Verbindungen | 3 | 1 | gleicher Anwendungspfad |
| Multi-Connection | an | aus | keine parallelen Peer-Verbindungen |
| GATT Client | an | an | für Tesla erforderlich |
| GATT Server | an | aus | keine Server-API in der Anwendung; Arduino-Registrierung ist SDK-guarded |
| BLUFI | an | aus | kein BLE-Provisioning; hängt am GATT Server |
| BLE Mesh | an | aus | kein Mesh-Aufruf im Anwendungspfad |
| BLE SMP / SMP | an | aus | Descriptor- und Characteristic-Writes verwenden `ESP_GATT_AUTH_REQ_NONE` |

Der gepinnte Arduino-Fork guardet GATTS-Registrierung mit `CONFIG_GATTS_ENABLE`
und SMP-Security-Setup mit `CONFIG_BLE_SMP_ENABLE`. Der vollständige Minimal-
SDK- und Firmwarebuild kompiliert/linkt die tatsächliche Arduino-BLE-Bibliothek
mit beiden Rollen deaktiviert. Im Minimal-ELF fehlen die C-API-Symbole
`esp_ble_gatts_*`, `esp_ble_set_encryption` und
`esp_ble_gap_set_security_param`; alle sieben Diagnose-Wrapper sind vorhanden.
Ein eventuell verbleibender Arduino-Handlername ist kein aktivierter SDK-Server.

Task-Stacks (BTC/BTU jeweils 8192), Client-Cache (40), Notification-Limit (5),
MTU-/Fragmentierung, Controller-Features und interne/externe Allocation-Policy
bleiben unverändert. Der normalisierte sdkconfig-Vergleich ergab 54 geänderte
aktive Einträge, ausschließlich `CONFIG_BT*` und `CONFIG_BLE_MESH*`, einschließlich
abhängiger/abgeleiteter Optionen. Keine Nicht-Bluetooth-Konfigurationsänderung.
Das vorhandene Ausgangspaket wurde nicht durch die Minimalvariante ersetzt.

SMP aus bedeutet **nicht**, dass Tesla-Interoperabilität bewiesen ist.
Anwendungsseitige Tesla-Authentifizierung bleibt unverändert; ob der reale Peer
unverschlüsselte ATT-Zugriffe in allen relevanten Zuständen akzeptiert, muss ein
späterer Hardwaretest bestätigen. Bis dahin ist die Variante nur ein Kandidat,
nicht der neue Standard.

## Reproduzierbare statische Map-Analyse

`software/tools/iram_map_report.py` liest den GNU-Linker-Map:
`iram0_0_seg`, `.iram0.vectors`, `.iram0.text`, `.iram0.data`, `_iram_end`.
Es prüft Grenzen, überlappende Sections und beim Vergleich identische Layouts.
Fehlende/ungültige Eingaben liefern Exit 2 ohne Teil-JSON oder Traceback.
Das JSON kennzeichnet ausdrücklich
`measurement=static_linker_layout_not_runtime_heap`.

`static_used = _iram_end - segment_origin` enthält Alignment-Lücken.
`static_tail = segment_origin + segment_size - _iram_end` ist ausschließlich
statischer Linker-Tail. Die Build-Size-Tabelle summiert Sections und zeigt daher
hier ein Byte weniger Belegung/ein Byte mehr Rest; für diesen Bericht gelten die
Endsymbol-Werte, nicht die reine Section-Summe.

Gemessene Werte (Bytes; Segment in allen Fällen 131072):

| Build | Vectors | IRAM text | IRAM data | Static used | Static tail | PIO RAM used | PIO Flash used |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Ausgangsbuild | 1028 | 100511 | 480 | 102020 | 29052 | 92244 | 4773029 |
| Normal, nach Änderung | 1028 | 100511 | 480 | 102020 | 29052 | 92244 | 4773029 |
| Diagnose, Ausgangs-SDK | 1028 | 100511 | 480 | 102020 | 29052 | 92244 | 4774313 |
| Diagnose, Minimal-SDK | 1028 | 100475 | 480 | 101984 | 29088 | 84048 | 4588305 |

- Diagnose vs. normal: IRAM/DRAM unverändert, Flash +1284.
- Minimal vs. Diagnose: IRAM static used/text -36, static tail +36;
  Vectors/Data unverändert; statisches PIO-RAM -8196, Flash -186008.
- Minimal vs. normal: Flash -184724.

**Weder 29052 noch 29088 Byte sind gemessener freier Runtime-IRAM.**
Auch die statische DRAM-Einsparung belegt keinen späteren größten Heap-Block.
Controller-Runtime-Allokationen, Fragmentierung, PSRAM-Erkennung, WiFi/Ethernet-
Last und die tatsächliche Heapregistrierung bleiben hardwareabhängig.

## TDD und ausgeführte Verifikation

Vorhandene Map-Implementierung/-Tests erhalten und geprüft: zunächst 3/3 GREEN.
Neue Map-Tests zuerst ausgeführt: 8 Tests, erwartetes RED mit 2 Failures und
1 Error (Section-Widersprüche, fehlendes Static-only-Label, CLI-Fehlerverhalten).
Danach minimal ergänzt: zunächst 8/8 GREEN. Die unabhängige Prüfung fand danach
mehrdeutige doppelte Pflichtdatensätze; zwei zusätzliche Parser-/CLI-Tests liefen
erwartungsgemäß RED (6 Einzelfehler) und nach der Eindeutigkeitsprüfung 10/10 GREEN.

Neue Diagnose-Tests vor Produktionsdateien/-Bindings/-Varianten ausgeführt:
4 Tests, erwartetes RED mit 4 Failures. Danach 4/4 GREEN; zusammen mit Map 14/14.
Der native Wrappertest führt die echte Diagnose-Implementierung aus und prüft
für sieben SDK-Aufrufe jeweils 0, -17 und 0x101, unveränderte Argumente/Returns,
EXEC/INTERNAL-Caps, Vorher/Nachher-Reihenfolge, sechs Messwerte und Eventstatus.
Die dabei benutzten Heapwerte sind klar markierte Host-Testwerte, **keine
Brick-Messungen**. Der deaktivierte Diagnosebuild linkt ohne SDK-/Heap-/UART-
Symbole. Ein anfänglicher GREEN-Versuch entdeckte zwei UART-Schreibvorgänge pro
Messpunkt; die Implementierung wurde auf einen vollständigen Aufruf korrigiert.

`test_warp_iram_diagnostics.cpp` führt zusätzlich das echte Modul/den echten
Callbackpfad mit aktivierter Diagnose aus: deaktivierter Boot/Loop ohne Runtime,
Queues, Tasks oder BLE-Init sowie akzeptierte/abgelehnte GATT-Eventstatus und
bestehende Filter. Er ist normal und unter ASan/UBSan in die Clean-Suite eingebunden.

Erfolgreiche Kommandos, aus `/home/hermes/warp-tesla-ble`:

```sh
python3 -m unittest discover -s software/tests/tesla_vehicle -p 'test_iram_*.py' -v
# Exit 0, 14 Tests

export TESLA_CLEAN_TEST_OUT=/home/hermes/warp-tesla-ble/software/.pio/iram-evidence/host
bash software/tests/tesla_vehicle/run_host_tests.sh
bash software/tests/tesla_vehicle/run_host_tests.sh sanitize
# beide Exit 0: je 18 Modulhelfer, 44 Clean-Einträge, 18 zusätzliche WARP-Modi,
# Web-Aktionstest, Source-/Safety-/Binding-Verträge und Faultmatrizen

bash software/tests/tesla_vehicle/cleanroom/run_test.sh test_warp_iram_diagnostics
bash software/tests/tesla_vehicle/cleanroom/run_test.sh test_warp_iram_diagnostics sanitize
# beide Exit 0

export TESLA_BLE_FRAMEWORK_LIBS=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs
uv run --offline --directory software pio run -e warp4_tesla_ble -t clean
uv run --offline --directory software pio run -e warp4_tesla_ble -j 4
uv run --offline --directory software pio run -e warp4_tesla_ble_iram_diag -j 4
export TESLA_BLE_MINIMAL_FRAMEWORK_LIBS=/home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder/out/tools/esp32-arduino-libs
uv run --offline --directory software pio run -e warp4_tesla_ble_iram_minimal -j 4
# alle finalen Builds Exit 0; vollständiger Compile/Link/Image- und Web-Build
# --offline betrifft uv, nicht automatisch PlatformIO/npm.

python3 software/tools/iram_map_report.py software/.pio/iram-evidence/diag.map --baseline software/.pio/iram-evidence/baseline.map
python3 software/tools/iram_map_report.py software/.pio/iram-evidence/minimal.map --baseline software/.pio/iram-evidence/baseline.map
python3 software/tools/iram_map_report.py software/.pio/iram-evidence/minimal.map --baseline software/.pio/iram-evidence/diag.map

git diff --check
```

Die drei Builds werden **nacheinander** ausgeführt. Beim Umgebungswechsel können
die bisherigen `.pio/build/<env>`-Dateien verschwinden. Deshalb Map/ELF jeweils
unmittelbar nach Erfolg in `software/.pio/iram-evidence` sichern; keine parallelen
Firmwarebuilds mit gemeinsam generierten Quellen/Web-Dateien starten.
Die Map-Ausgabe ist reproduzierbar für dieselben Inputs; vollständige Firmware-
Binärhashes sind wegen Buildzeit/Metadaten nicht als reproduzierbar behauptet.

## Getrennten SDK-Build reproduzieren

Voraussetzung: vorhandener, bereits gepatchter lokaler Builder samt IDF/Tools.
Nur das gemeinsame IDF wird als Quellinput verwendet; Builder/SDK-Ausgaben liegen
im Repository. Für eine **frische**, noch nicht existierende Zielkopie:

```sh
python3 -c 'from pathlib import Path; import shutil; s=Path("/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder"); d=Path("software/.pio/iram-minimal-builder"); d.mkdir(); [shutil.copytree(s/n,d/n,symlinks=True) for n in ["main","components","managed_components","tools","patches","configs"]]; [shutil.copy2(s/n,d/n) for n in ["CMakeLists.txt","partitions.csv","dependencies.lock"]]; (d/"esp-idf").symlink_to(s/"esp-idf",target_is_directory=True)'
python3 software/lib-builder/merge_defconfig.py /home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs/esp32/sdkconfig software/lib-builder/defconfig.esp32ethernetbrick.ble-minimal software/.pio/iram-minimal-builder/sdkconfig
export IDF_COMPONENT_MANAGER=0
export IDF_PATH=/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/esp-idf
. "$IDF_PATH/export.sh"
idf.py -C /home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder -B /home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder/build -D 'EXTRA_COMPONENT_DIRS=/home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder/managed_components/espressif__mdns;/home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder/managed_components/joltwallet__littlefs;/home/hermes/warp-tesla-ble/software/.pio/iram-minimal-builder/managed_components/espressif__libsodium' reconfigure
cmake --build software/.pio/iram-minimal-builder/build --target idf-libs copy-bootloader -- -j4
python3 -c 'from pathlib import Path; import shutil; r=Path("software/.pio/iram-minimal-builder"); s=Path("/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder/out/tools/esp32-arduino-libs"); d=r/"out/tools/esp32-arduino-libs"; [shutil.copy2(s/n,d/n) for n in ["package.json","tools.json","versions.txt"]]; [shutil.copy2(r/"build"/p,d/"esp32/lib"/Path(p).name) for p in ["esp-idf/mbedtls/mbedtls/library/libmbedtls_2.a","esp-idf/esp_driver_sdm/libesp_driver_sdm_2.a"]]'
/home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-strip -g software/.pio/iram-minimal-builder/out/tools/esp32-arduino-libs/esp32/lib/libmbedtls_2.a software/.pio/iram-minimal-builder/out/tools/esp32-arduino-libs/esp32/lib/libesp_driver_sdm_2.a
```

Reconfigure und Build erfolgreich. Die manuelle Aufnahme der beiden **frisch
gebauten** `_2`-Archive ist nötig, weil der bestehende Export sie nicht mitnimmt,
seine Linkerflags sie aber anfordern. Es wurden keine Dummy-/leeren Archive oder
ersatzweise unverifizierten Stock-Bibliotheken erzeugt.

Aufgelöste Build-Probleme:

- Direktes `~/.platformio/penv/bin/pio`: `ModuleNotFoundError: No module named 'tree_sitter'`.
  Richtiger Projektinterpreter über `uv run --offline --directory software`.
- Fehlende Umgebungsvariable: `PackageException: Can not create a symbolic link for '', not a directory`.
  Frameworkpfad explizit im jeweiligen Shell-Aufruf exportieren.
- `idf.py -B build` war relativ zum Aufrufverzeichnis; absolute Buildpfade benutzen.
  Nur das selbst erzeugte falsche Root-Buildverzeichnis wurde wieder entfernt.
- Ein früher Exportversuch ohne `configs`/explizite Managed-Komponenten war
  unvollständig. Diese Kopie wurde korrigiert, von ursprünglichem sdkconfig neu
  konfiguriert und vollständig gebaut. Nicht-Bluetooth-Konfigurationsgleichheit
  und mdns/LittleFS/libsodium-Archive wurden anschließend geprüft.
- `mem-variant` nicht zusätzlich parallel zu `idf-libs` aufrufen: beide verschieben
  `build/config/sdkconfig.h`; `idf-libs` exportiert die benötigte Variante bereits.
- Erster Minimal-Firmwarelink: `cannot find -lmbedtls_2` und
  `cannot find -lesp_driver_sdm_2`. Exportkorrektur wie oben; erneuter Link Exit 0.

Nicht fatale vorhandene Warnungen: fehlender Main-Branch-Fetch, Hyphenations,
gzip statt 7z, Nanopb versucht pip-Aufrufe (keine Generierung erforderlich),
Builder-`git symbolic-ref` bei detached IDF und `tr`-Portabilität.

## Lokale Evidenz und nächste Hardware-Gates

Logs, JSON-Vergleiche und Map-/ELF-Sicherungen:
`/home/hermes/warp-tesla-ble/software/.pio/iram-evidence/`.
Insbesondere `map-red.log`, `map-green.log`, `diagnostics-red.log`,
`new-tests-green.log`, `host-normal-final.log`, `host-sanitize.log`,
`baseline-build.log`, `default-final-build.log`, `diag-final-build.log`,
`minimal-libs-configure.log`, `minimal-libs-build.log`, `minimal-build.log` und
`minimal-build-packaging-failure.log`. Diese generierten Dateien sind ignorierte
lokale Evidenz, keine zu veröffentlichende Firmware.

Später zu beweisen, nach separater Hardware-/Recovery-Freigabe:

1. Boot auf dem normalen Brick, PSRAM/Revision/Partitionen/Peripherie passend;
   deaktivierter Tesla-Pfad ohne zusätzliche Runtime-Ressourcen.
2. Sauberer Neustart und normal aktivierter Startpfad: Vor/Nach-Messungen aller
   tatsächlich ausgeführten Controller-/Host-Schritte mit echten SDK-Returns.
3. Ausgangs- vs. Minimal-SDK unter gleicher WiFi-/Ethernet-Last: echter EXEC-
   Gesamtfreiraum, größter Block, SDK-Minima; keine Extrapolation aus Linker-Tail.
4. Bei erfüllten bestehenden Identitätsbedingungen Registrierung und erste
   Verbindung: Submission- und Eventstatus getrennt, danach ATT/SoC und SMP-aus-
   Interoperabilität. Fehlende Identitätsbedingungen nicht künstlich umgehen.
5. Wiederverbindung, Langzeitlast, Funk-Koexistenz und Fehler-/Abbruchpfade.
6. Separat echte WARP4 mit Tesla: SLAC-Bindung, Schlüsselkoppelung, SoC,
   Schlaf-/Weckverhalten, EVSE und beaufsichtigter AC-Ladevorgang.
