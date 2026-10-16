R13 — Closure-Review A/B und TypeScript-Typabgleich — Source-only

Branch: feature/bluetooth-interface-ui-r13
HEAD unverändert: 768525a621b5d334dfc975a9704dabfc81cc6bdb
Kein Commit, Push, Flash, Hardwarezugriff, Modellwechsel oder Delegation.
Keine Änderung früherer R6–R12-Evidence/Seals. Dieser Bericht ersetzt nur den
vorigen R13-Bericht.

Letzter Nachtrag: ausschließlich unsupported_reason-Typabgleich

software/web/src/modules/tesla_vehicle/api.ts enthält jetzt den bereits vom
Backend publizierten Token storage_backend_unverified in der Union von
state___bluetooth.unsupported_reason. Keine Backend-, UI-Verhaltens- oder
sonstigen Funktionsänderungen. Der bestehende R13-Webtest prüft den Token
gezielt in genau dieser Property-Union; die Assertion läuft auch im normalen
VDOM-Test. Der Modus schema führt sie isoliert aus.

Striktes TDD in diesem Nachtrag, Worktree-Root als cwd:
  node software/tests/tesla_vehicle/test_r13_web.mjs schema
    RED Exit 1 nach Testergänzung und VOR Unionänderung:
    AssertionError: Bluetooth unsupported_reason must include the backend
    storage_backend_unverified token. Tatsächliche Union enthielt nur '',
    sdk_disabled, hardware_unsupported und bench_only.
  Minimale Unionänderung: zusätzlich 'storage_backend_unverified'.
  node software/tests/tesla_vehicle/test_r13_web.mjs schema
    GREEN Exit 0; nach Frontendgenerierung nochmals Exit 0.

Anschließend neu ausgeführt (nicht nur Ergebnisse der älteren Matrix):
  cwd software, PYTHONDONTWRITEBYTECODE=1:
    uv run pio run -d . -c "$TMPDIR/warp-r13/frontend-only.ini" -e warp4_tesla_ble -j 2
    Exit 0: echte TypeScript-/API-/Übersetzungs-/Frontendgenerierung mit
    tsc, esbuild, sass, postcss und HTML-Minifizierung. Vorhandene Hinweise
    zu fehlenden Silbentrennungen und übersprungenem Remote-Branch-Lookup
    bleiben; kein fehlerfreier Voll-Firmwarebuild wird daraus abgeleitet.
  cwd Worktree-Root, alle Exit 0:
    node software/tests/tesla_vehicle/test_r13_web.mjs schema
    node software/tests/tesla_vehicle/test_r13_web.mjs group
    node software/tests/tesla_vehicle/test_r13_web.mjs
    node software/tests/tesla_vehicle/test_web_integration.mjs
    node software/tests/tesla_vehicle/test_web_actions.mjs
    git diff --check
    python3 -B "$TMPDIR/r13_delta_audit.py"
  Delta-Audit: weiterhin 38 Dateien ohne Bericht, SCAN []. Gegen das vorherige
  Hashmanifest geprüft: ausschließlich api.ts und test_r13_web.mjs änderten
  sich; zusätzlich wurde dieser Bericht aktualisiert. Keine neue Quell-/
  Testdatei und keine Änderung anderer bisheriger Delta-Dateien.

Die nachfolgende vollständige Host-/Sanitizer-/Security-Matrix ist der zuvor
ausgeführte Closure-Stand VOR diesem reinen TypeScript-Typnachtrag. Sie wurde
hier nicht erneut ausgeführt. Die obigen Frontend-/Webprüfungen sind die
frischen Nachweise des aktuellen Baums. Alle bekannten Zielbuildgrenzen unten
bleiben unverändert offen; keine Veröffentlichung, kein Commit/Push/Flash.

A. Fehlerwahrheit, Backendbindung und Commitvertrag

Auf dem ESP-Pfad verwendet der Produktionsstore keine Arduino File-Objekte
mehr. LittleFS.open()/exists() liefern keinen ausreichend unterscheidbaren
Fehlerstatus; File.close() verwirft fclose/closedir-Ergebnisse. Diese APIs sind
weder Fehlend-Nachweis noch Commitbestätigung.

bluetooth_littlefs_backend.h ist die produktionsidentische Adapter-Seam:
- Missing nur nach erfolgreicher vollständiger Directory-Iteration bis EOF
  UND erfolgreich geprüftem raw closedir().
- Regular/Directory aus den Verzeichniseinträgen des gebundenen No-Link-
  Backends; unbekannte Typen und doppelte Namen ergeben Error.
- opendir-/readdir-/closedir-, Mount-, Ancestor-, Allokations- und FD-Fehler
  sind Error, niemals Missing. Fehlende Ancestors werden konservativ abgewiesen.
- .bak bleibt Bootautorität. Scheitert dessen Öffnen/Lesen, liefert load()
  false und enable=false. canonical ON wird NICHT ersatzweise autorisiert.
- Keine stat-follow-Bootautorität. fstat dient nach geprüfter Auswahl der
  Descriptor-/Größenprüfung. Exakte Größe, Short-read-Schleife, EOF und
  abschließendes fstat werden geprüft.
- Raw open/read/write/ftruncate/fsync/close/rename/unlink liefern prüfbare
  Resultate. Geprüftes raw close/closedir ersetzt das unprüfbare File.close.
  Closefehler verhindern Commit-ACK und Wiederholung desselben Handles.
  Der Store blockiert weitere normale IO; nur erneut geprüfter Metadata-
  Rollback bleibt möglich. Keine behauptete Quarantäne eines Arduino File.
- Erfolg bedeutet den Recoverable-I/O-Vertrag: vollständiger Write, fsync,
  checked descriptor close, Readback mit checked close und erfolgreiches
  Entfernen des Journals. Keine neue Stromausfall-/Flash-Durabilitätszusage.

Die Backendbindung ist nun im Produktionspfad erzwungen:
- Ohne passende Compilezeit-Attestation verweigert der Store load/save.
  Bei BLE-fähiger Kompilierung ist bluetooth_supported() false mit dem Grund
  storage_backend_unverified.
- r13_vfs_guard.py ist PlatformIO pre-script im BLE-Environment und im separat
  überschreibenden Bench-Environment. Die expandierte PlatformIO-Konfiguration
  wurde mit uv run pio project config --json-output geprüft: Exit 0; beide
  Scriptketten enthalten den Guard.
- Nur wenn ALLE SHA256-Pins in tools/r13_vfs_binding.py passen, setzt der Guard
  WARP_BLUETOOTH_LITTLEFS_BINDING=0x52313302: sechs Arduino FS/LittleFS-Dateien
  sowie esp_littlefs.h, libjoltwallet__littlefs.a, libvfs.a und die konkrete
  SDK-Konfiguration. ESP_PLATFORM, Versionsname oder Mountpoint reichen nicht.
  Der Compiler prüft zusätzlich den konkreten Typ des globalen LittleFS.
- Unbekannte/geänderte Pakete erhalten keine Attestation. Ein später neu
  gebautes BLE-SDK wird ausdrücklich NICHT automatisch freigegeben und braucht
  eigene Artefaktprüfung/Whitelist-Aktualisierung. Die aktuell attestierte
  Stock-SDK-Konfiguration hat BT OFF. Kein Ziel-BLE-Build wird behauptet.
- Die No-Link-Annahme gilt nur für den gebundenen LittleFS/VFS-Stack: LittleFS
  kennt nur REG/DIR und keine Symlink-/Hardlink-API. Host-POSIX behält volle
  lstat/O_NOFOLLOW/fstat/st_nlink/dev+ino-Prüfungen.
- Exklusiver Main-Task-/Pfadbesitz und statische Mountbelegung bleiben
  Voraussetzung. Kein Schutzversprechen gegen aktiv konkurrierende Renames,
  Mountwechsel oder absichtliche Manipulation der Toolchain.

Geprüfte API-/Returncode-Quellen:
  Arduino libraries/FS/src/vfs_api.cpp: VFSImpl::open/exists und
  VFSFileImpl::close. Keine File-Close-Bestätigung wird übernommen.
  joltwallet__littlefs 1.22.3 src/esp_littlefs.c:
    vfs_littlefs_close: lfs_file_close-Fehler -> -1, errno
    vfs_littlefs_opendir: Allokations-/lfs_dir_open-Fehler -> NULL, errno
    vfs_littlefs_readdir[_r]: lfs_dir_read-Fehler -> NULL, errno
    vfs_littlefs_closedir: lfs_dir_close-Fehler -> negativer Returncode
  ESP-IDF components/vfs/vfs.c: esp_vfs_close/esp_vfs_closedir propagieren
  den Backend-Returncode. lfs.h enthält nur REG/DIR, keine Linkoperation.
Die tatsächlichen Paketbytes sind im Produktionsguard gepinnt, nicht nur hier.

B. Vollständiges Host-Confinement

Der Materializer akzeptiert nur vier EXAKTE Source/Destination-Paare:
  SOURCE/library/version_features.c -> LIBS/library/version_features.c
  SOURCE/library/error.c -> LIBS/library/error.c
  SOURCE/library/psa_crypto_driver_wrappers_no_static.c
    -> LIBS/library/psa_crypto_driver_wrappers_no_static.c
  SOURCE/library/ssl_debug_helpers_generated.c
    -> LIBS/library/ssl_debug_helpers_generated.c
SOURCE ist TESLA_BLE_HOST_MBEDTLS_SOURCE oder der bestehende Default. LIBS muss
exakt TESLA_CLEAN_MBEDTLS_BUILD bzw. TESLA_CLEAN_TEST_OUT/mbedtls entsprechen.
Lexical '..', Source-Linkchains, Hardlinks und nichtkanonische Quellen werden
abgewiesen. Passender Name bei fremder Quelle/falscher Destination genügt nicht.

Vor JEGLICHER Materialisierung: EIN vollständiger bench_confinement-Plan über
TESLA_CLEAN_TEST_OUT UND LIBS, alle Geschwister, Source-Leaves, erlaubte Links
und Ancestors. Auch ohne erlaubte Links erfolgt der Preflight. Erst danach
Bytes lesen, Plan erneut prüfen, exakt erlaubte Links unlinken und O_EXCL-
Dateien erstellen. Keine Opferverfolgung und kein später Geschwister-Check.

run_host_tests.sh und cleanroom/run_test.sh weisen das Dependency-Ergebnis
separat zu, brechen bei nonzero oder leerem Ergebnis VOR Mutation ab und
exportieren erst danach. r13_host_dependency.py prüft den ganzen Quellbaum und
verlangt src/ArduinoJson.h. Keine export-Statusmaskierung mehr.

TDD: tatsächlich ausgeführte RED/GREEN-Befehle

  python3 -B software/tests/tesla_vehicle/test_r13_target_vfs.py
    RED exit 1 vor Produktionsänderung: .bak-open autorisierte canonical ON;
    canonical-open/Mount/Ancestor/I/O-/Closefehler wurden teils als fehlend
    behandelt; ESP ohne Backend-Attestation wurde akzeptiert.
    GREEN exit 0 nach Änderung; zusätzlich sanitize exit 0.
    Test führt denselben BluetoothConfigStore und dieselbe Adapter-Seam aus,
    ersetzt nur Mountmethoden und POSIX-Calls deterministisch. Keine Kopie
    eines Zustandsmodells, kein MODULE_HOST_TEST-Fake.
    Anschließend echter Xtensa-ESP_PLATFORM-Compile des ausgeübten Codes gegen
    die attestierten realen Header, ohne BLE-Includepfad, mit
    -Wall -Wextra -Werror. Kein Firmwarelink-/Hardwarebeweis.
    Fälle: .bak OFF/canonical ON + .bak EIO/ENOMEM/EMFILE/ENOENT, Missing,
    canonical-open, Mount/Ancestor fehlend/fehlerhaft, falscher Leaf-Typ,
    read/size/EOF/Short-read, Write/Sync/Rename/Unlink, Rollbackfehler,
    alle gezählten späteren raw close/closedir-Positionen und Retry mit frischer
    Storeinstanz. Positionskontrolle: 54 Fehlerpläne, alle bestanden.
    Der echte Buildguard läuft auf kopierten exakten Artefakten: bekannte Bytes
    setzen das Flag; geändertes Backend setzt keines. Ohne Attestation
    verweigert die ausgeführte Storeinstanz load/save.

  python3 -B software/tests/tesla_vehicle/test_r13_helper_security.py
    RED exit 1 vor Helferänderung: foreign-source, wrong-destination, unsafe
    sibling symlink/hardlink/alias, falscher Buildroot, leere Dependency und
    nonzero/empty Dependencycheck in BEIDEN Runnern erlaubten Teilmutation.
    GREEN exit 0: Negativfälle vor Mutation abgewiesen, vollständige Opfer-/
    Sentinel-/Pfadsnapshots unverändert. Positivkontrolle materialisiert die
    vier erlaubten Paare. Zusätzlich beide echten Shellrunner mit erlaubtem
    Alias plus vergiftetem Geschwisterpfad geprüft.

Ursprüngliche RED-Ausgaben stehen in den Toolausgaben dieser Sitzung; keine
nachträglich erfundenen RED-Logs. Die GREEN-Logs unten stammen vom damaligen
Closure-Quellbaum vor dem oben dokumentierten TypeScript-Typnachtrag.

Vorherige Closure-Abschlussmatrix — echte Befehle, Reihenfolge und Logs

Worktree-Root: /home/hermes/warp-bluetooth-ui-r13
Logverzeichnis: /home/hermes/.hermes/cache/scratch/r13-closure-final
Manifest: /home/hermes/.hermes/cache/scratch/r13-closure-final/results.json
Driver: /home/hermes/.hermes/cache/scratch/r13_closure_matrix.py
Umgebung des Drivers:
  PYTHONDONTWRITEBYTECODE=1
  TESLA_BLE_HOST_MBEDTLS_SOURCE=$TMPDIR/warp-r13/mbedtls-source
  TESLA_BLE_HOST_FRAMEWORK_SOURCE=$TMPDIR/warp-r13/host-framework
  TESLA_CLEAN_TEST_OUT=$TMPDIR/r13/native
  TESLA_CLEAN_MBEDTLS_BUILD nicht gesetzt (Default unter obigem Outputbaum)

Alle folgenden 21 damaligen Schritte haben Exit 0; Log = <Schrittname>.log:
  helper-security:
    python3 -B software/tests/tesla_vehicle/test_r13_helper_security.py
  target-vfs-focused:
    python3 -B software/tests/tesla_vehicle/test_r13_target_vfs.py
  target-vfs-sanitize:
    python3 -B software/tests/tesla_vehicle/test_r13_target_vfs.py sanitize
  host-before:
    bash software/tests/tesla_vehicle/run_host_tests.sh
  host-sanitize-before:
    bash software/tests/tesla_vehicle/run_host_tests.sh sanitize
  frontend (cwd software):
    uv run pio run -d . -c /home/hermes/.hermes/cache/scratch/warp-r13/frontend-only.ini -e warp4_tesla_ble -j 2
  host-after:
    bash software/tests/tesla_vehicle/run_host_tests.sh
  host-sanitize-after:
    bash software/tests/tesla_vehicle/run_host_tests.sh sanitize
  store-security:
    python3 -B software/tests/tesla_vehicle/test_r13_store_security.py
  store-sanitize:
    python3 -B software/tests/tesla_vehicle/test_r13_store_security.py sanitize
  runner-security:
    python3 -B software/tests/tesla_vehicle/test_r13_runner_security.py
  target-vfs:
    python3 -B software/tests/tesla_vehicle/test_r13_target_vfs.py
  sdk-off:
    python3 -B software/tests/tesla_vehicle/test_r13_sdk_off.py
  r13-module-persistence:
    bash software/tests/tesla_vehicle/run_r13_tests.sh
  r13-unsupported:
    bash software/tests/tesla_vehicle/run_r13_tests.sh unsupported
  r13-sanitize:
    bash software/tests/tesla_vehicle/run_r13_tests.sh sanitize
  web-group:
    node software/tests/tesla_vehicle/test_r13_web.mjs group
  web-r13:
    node software/tests/tesla_vehicle/test_r13_web.mjs
  web-integration:
    node software/tests/tesla_vehicle/test_web_integration.mjs
  diff-check:
    git diff --check
  delta-audit:
    python3 -B /home/hermes/.hermes/cache/scratch/r13_delta_audit.py

Ausführungshinweis: Der erste Terminalaufruf des Drivers wurde von der
420-Sekunden-Transportgrenze nach den sechs bestätigten Schritten bis Frontend
unterbrochen. Es gab keine Quelländerung dazwischen. Der Driver prüfte beim
Resume den ausschließlich grünen Manifestpräfix und führte host-after erneut
vollständig sowie ALLE Folgeschritte aus. Kein unvollständiger Hostlauf wurde
als Erfolg gezählt. Beide Host-/Sanitizerpaare liefen komplett; das Manifest
wurde abschließend auf exakt 21 Schritte mit ausschließlich Exit 0 geprüft.

Grenzen unverändert

Frontend-only erzeugt echte TypeScript/API/Übersetzungen und Webbundle, ist
aber kein erfolgreicher Voll-Firmwarebuild. TESLA_BLE_FRAMEWORK_LIBS fehlt
weiterhin; 0x8000 vs. 0xe000 bleibt ungelöst. Keine neue Ziel-ELF-, Ressourcen-,
Produktionsimage- oder Hardwarefreigabe. Native Fehlerbackends sind keine
Hardwarefehlerinjektion; die reale Backendgrenze wird zusätzlich kompiliert.

Aktuelles vollständiges R13-Hashmanifest gegenüber HEAD

38 Quell-/Test-/Konfigurationsdateien ohne diesen Bericht. Der zuvor vertauschte
Einzelhash (build_module_validation_harness.py) ist korrigiert; das gesamte
Manifest wird programmgesteuert gegen Dateibytes geprüft.

fd9f9558f567fbbab25aab9414532e2ec30e51843d44809e02832b99b8d0d605  software/r13_vfs_guard.py
3e4091026772b68147f1ff30f353852005ce50dcb880ee8d1c1a7eb70a5b25d8  software/src/modules/tesla_vehicle/bluetooth_config_store.h
70662bc326a1e4a0b2c6953453e779da268f5effc98e3a648feecdb47f77e1b5  software/src/modules/tesla_vehicle/bluetooth_littlefs_backend.h
b8a5421b1baeb547ab50b8b0d921fa8433a8015f3496204ce9200843ed658c9f  software/src/modules/tesla_vehicle/bluetooth_littlefs_binding.h
fdfbfb23cd097c758525d728096e8f0ab12806be734c3549445c4d8f028cb3b7  software/src/modules/tesla_vehicle/bluetooth_support.h
2effb959ecc5f438cfa695dcd0b46704c73b18fb4ab33d054e831ae86d84f5d1  software/src/modules/tesla_vehicle/tesla_ble_gatt.cpp
cc4df450dcf1d616b200da929e2d0b8dd5064756ba58d4824a7dc0450b2b7898  software/src/modules/tesla_vehicle/tesla_vehicle.cpp
697aca8745171f2159a8401f17aae9a516147e4717ac008096022f7564462549  software/src/modules/tesla_vehicle/tesla_vehicle.h
4a230fcf367111f6b75c96c81db5498cb89e06c566f581dfb129fb92d75b258c  software/tests/tesla_vehicle/cleanroom/run_test.sh
a58996518cfcb01a039d98836bcf22d938aff063c700b10f1850ec29eff6176a  software/tests/tesla_vehicle/cleanroom/test_warp_iram_diagnostics.cpp
a3083347234f0819c4de8d01491ed94723370d42af1b21b9a90101592d09c33f  software/tests/tesla_vehicle/cleanroom/test_warp_module.cpp
e7fb6baefbd6fbed51b2ed75d8c5b6e11ee17931cb08c1975c32d044afd79cd2  software/tests/tesla_vehicle/cleanroom/warp_module_contracts.inc
6ceb399873a839cb715aba3faeff48c77546e6db4f4ce6dbcbaa846e8db52af2  software/tests/tesla_vehicle/host/audit_external_publication.py
3437c87fffe14d3b6acb614e05bb7cc5d968fe7cdc9c62c704828fc279670ea2  software/tests/tesla_vehicle/host/build_module_validation_harness.py
3204d5553dc8eea601b1e8cecc655e95244012d59146cb8542a121356dbde8e0  software/tests/tesla_vehicle/host/build_target_oom_harness.py
7f6d0eca81719fd5f5dbd1001bc47af916c9c49152c3b9b827bbed7ba5452c81  software/tests/tesla_vehicle/host/module_platform.h
c10e1b9a0f36cac391a02494258888332355b16265cd3513452bdaded698d3ff  software/tests/tesla_vehicle/materialize_cmake_sources.py
f4a98c4efc1737e5353ec0f02a660381b42593b322cde361862b17f05a07cbe6  software/tests/tesla_vehicle/r13_host_dependency.py
ac14b4e6a5848a643f629f7e504c1a087dc41027ec82067c952cf32e2e09d06e  software/tests/tesla_vehicle/r13_output_preflight.py
156e23dffd2562883f63186491a23bea9549f77f260f8bab0ab49bf490f452d1  software/tests/tesla_vehicle/run_host_tests.sh
89c102c253f83fc9c48b9c9bc0318070bf6697f23feb42fba89f67dea32a587a  software/tests/tesla_vehicle/run_r13_tests.sh
e45764518bfcab865cb0143205cb627e24b623c750bb9770387ca14db63cea86  software/tests/tesla_vehicle/test_r13_helper_security.py
224150ce8538f823f0c453ef1c0cb0b78561471b334582ce5be768d09b210c73  software/tests/tesla_vehicle/test_r13_module.cpp
afaac0b788e8f4379cbb878e05ee4593f18d7d18b42a24fd89d0a1566c2760fa  software/tests/tesla_vehicle/test_r13_persistence.cpp
9251358bca622e6ac477cbd7aef1638ac44604a989c0e00686cd4aaeafa54846  software/tests/tesla_vehicle/test_r13_runner_security.py
2cc6da01cd81d6b4ba440aaafbcf928d892c3fa992a11309435b776ce476807d  software/tests/tesla_vehicle/test_r13_sdk_off.py
631b11efff062a53b695bcde5b7447ec3b8a2bd05529428feb3a0c50e804125a  software/tests/tesla_vehicle/test_r13_store_security.cpp
355b8b5d634a80cbef566de9d5017feb9aa3548c22f30bf8d4455fe1e6d64b1b  software/tests/tesla_vehicle/test_r13_store_security.py
a185dff6d9ed5ee63c5931bcbebbae449a606e5130f091086bd4014147bc4138  software/tests/tesla_vehicle/test_r13_target_vfs.cpp
2687ffbac7388610c47e119c49210480e6cdbf22d4f9c44d5ce12e2fd8f00e37  software/tests/tesla_vehicle/test_r13_target_vfs.py
ac04070d672bf281d684977acee47a4c87c9b7953f5ed146b0a5bfad93d88383  software/tests/tesla_vehicle/test_r13_web.mjs
1ada1823d2156c0305504ba166f67a5f6b18f0b2258aa97cb3753d8fd1d7fb83  software/tests/tesla_vehicle/test_web_integration.mjs
0e9f5ac2f620b016f9587613f7eec00afa8ded7cf54ed49231b2ab6b371718c3  software/tools/r13_vfs_binding.py
f19ff59bd098ae07b2ee6f8f3744d5cccf8e907a52354abed1e8070623f68a49  software/warp4_tesla_ble.ini
6fd8d5f399308282c745b2a8bfd3280f2f4c763812f51d123c89bed32bdc04ec  software/web/src/modules/tesla_vehicle/api.ts
0e251c5980f5576473b4895a11bb8fe72562f407c22064df65df82c3b507661f  software/web/src/modules/tesla_vehicle/main.tsx
0285a90226e04a0cee1456aa10837658d88dacf19c9d2e2f6947b884e960fb59  software/web/src/modules/tesla_vehicle/translation_de.tsx
af439858176c9996a30e4d884d4796e81be0fa5eb04ae57737f4229d1a039dff  software/web/src/modules/tesla_vehicle/translation_en.tsx

Delta-SHA256 ohne Bericht:
f6c75359b240541e287bc5a0e78a65f21aa70c1a8c6ce089a1475d65283af049
Berechnung: SHA256(concat(path UTF-8 + NUL + SHA256(bytes) hex ASCII + LF)),
lexikographisch sortiert, inklusive untracked Dateien, ohne Selbstreferenz.
Delta-Audit Exit 0, SCAN []. Keine Veröffentlichung.
