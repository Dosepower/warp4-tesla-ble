# WIP: autonome Tesla-SoC-Abfragen

**SOURCE REVIEW ONLY — DO NOT INSTALL.** Kein Firmware-Download und keine Installationsfreigabe.

Dieser Quellcheckpoint baut auf `checkpoint/paired-soc96-timing-20261028` (Commit `03fea5a5b78ab073b184fea42038559e516e4157`) auf.

Enthalten: persistentes, je Fahrzeugprofil einstellbares Abfrageintervall; autonomer Firmware-Scheduler ohne Browser; wiederholtes bedarfsgerechtes Wecken mit mindestens 120 Sekunden Abstand zwischen exponierten Weckvorgaengen; weiterhin manuelle Sofortabfrage; Empfangsdatum/-zeit bzw. ehrlicher Zustand bei nicht synchronisierter Uhr. Haeufiges Wecken kann den Standbyverbrauch erhoehen. Keine Ladefreigabe durch BLE.

Nach unabhaengiger Pruefung korrigiert: Intervallverkuerzung/-verlaengerung ohne Festhalten am alten Termin, Empfangszeit im Worker statt bei verzoegerter Main-Veroeffentlichung, Namenskollision mit dem WARP-defer-Makro. Opus 5.5 schrieb die Produktionsaenderungen; Sol fuehrte Qualifikation und Build aus.

Lokal ausgefuehrt: produktionsnahe RED/GREEN-Faelle, normale und ASan/UBSan-Regressionslaeufe, authored und produktionsgenerierte Frontendtests, TypeScript und kompletter Firmware-/Webbuild, ELF-/MAP-/App-/OTA-Pruefungen mit negativ manipulierten Eingaben und bytegenauem Abgleich des eingebetteten Webpayloads. Quelltests: `software/tests/tesla_vehicle/test_nimble_module.py` (Modi `soc-auto-interval`, `soc-auto-recvtime`, `soc-auto`, `soc-auto-stop`), `test_tools_macro_collision.py`, `test_soc_time_frontend.mjs` sowie `cleanroom/run_test.sh test_wake_exposure sanitize`. Die Hosttests benoetigen die dokumentierten SDK-/mbedTLS-Abhaengigkeiten und TMPDIR.

Nur die 23 neuen/geaenderten Feature- und Testdateien sind gegenueber dem Elterncheckpoint uebernommen. Bereits bewusst lokal gehaltene Installer-Werkzeuge, Locks, SDKs, Buildumgebungen, Berichte, private Geraetedaten und Binaerdateien bleiben ausgeschlossen. Keine Produktionsdatei wurde fuer die Veroeffentlichung umgeschrieben. Dies ist kein vollstaendiges SDK-Backup oder reproduzierbarer Ein-Kommando-Installer.

Keine neue Installation oder Fahrzeugpruefung: der zuvor bestaetigte Geraetestand bleibt getrennt. Reale Wake-/SoC-Zyklen, Uhrzeitanzeige, Schlaf-/Standbyverhalten und Langzeitstabilitaet dieses Kandidaten sind noch nicht am Fahrzeug bestaetigt. Installation erfordert eine separate Freigabe des konkreten Kandidaten.
