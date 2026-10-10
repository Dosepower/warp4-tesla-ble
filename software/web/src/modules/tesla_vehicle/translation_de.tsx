/** @jsxImportSource preact */
import { h } from "preact";
let x = {
    "tesla_vehicle": {
        "navbar": {"tesla_vehicle": "Tesla-Bluetooth"},
        "content": {
            "title": "Tesla-Bluetooth",
            "enable": "Tesla-Bluetooth aktivieren",
            "enable_desc": "Ladestand per Bluetooth lesen; die WARP startet die normale AC-Ladung selbst",
            "vehicle_profile": "WARP-Fahrzeugprofil",
            "vehicle_profile_help": "Die von der WARP per SLAC gemessene MAC dieses Profils wird fest mit dem Tesla-Schlüssel verknüpft.",
            "select_vehicle": "Fahrzeug auswählen",
            "vin": "Tesla-VIN",
            "vin_help": "17-stellige Fahrzeug-Identifizierungsnummer.",
            "ble_address": "Tesla-BLE-MAC",
            "ble_address_help": <>Bluetooth-Adresse des Tesla-VCSEC. Mit einer Android-BLE-Scanner-App nach einem 18-stelligen Namen suchen, der mit <code>S</code> beginnt und mit <code>C</code> endet. iOS zeigt BLE-MAC-Adressen nicht an.</>,
            "poll_interval": "Abfrageintervall",
            "wake_once": "Tesla einmal wecken",
            "wake_once_desc": "Erste SoC-Abfrage je Steckvorgang darf das Fahrzeug wecken",
            "wake_once_help": "Ausgeschaltet lassen, wenn ein schlafender Tesla nicht allein für die regelmäßige SoC-Abfrage geweckt werden soll.",
            "pair_title": "Fahrzeugschlüssel koppeln",
            "pair_step_1": "Konfiguration speichern und die WARP neu starten, falls dies angezeigt wird.",
            "pair_step_2": "Den ausgewählten Tesla an genau diese WARP anschließen und warten, bis das zugehörige Fahrzeugprofil aktiv ist.",
            "pair_step_3": "Im Fahrzeug sitzen und die Schlüsselkarte bereithalten.",
            "pair_step_4": "„Tesla-Schlüssel koppeln“ drücken und anschließend die Schlüsselkarte auf die Mittelkonsole legen. Die Anfrage im Fahrzeug bestätigen.",
            "pair_step_5": "Danach „SoC jetzt lesen“ drücken. Erst eine authentifizierte Antwort setzt den Status auf „Gekoppelt“.",
            "pair_action": "Kopplung",
            "pair": "Tesla-Schlüssel koppeln",
            "pairing": "Kopplung läuft …",
            "test_actions": "Verbindung prüfen",
            "read_soc": "SoC jetzt lesen",

            "forget_key": "Tesla-Schlüssel löschen",
            "forget_confirm": "Tesla-Schlüssel, Sitzungsdaten und die Verknüpfung zur Fahrzeug-MAC wirklich löschen?",
            "cancel": "Abbrechen"
        },
        "status": {
            "pairing": "Schlüsselstatus",
            "paired": "Gekoppelt und authentifiziert",
            "pending": "Bestätigung im Tesla ausstehend",
            "key_only": "Schlüssel vorhanden, noch nicht authentifiziert",
            "not_paired": "Nicht gekoppelt",
            "ble": "Bluetooth",
            "connected": "Verbunden",
            "disconnected": "Getrennt",
            "soc": "Letzter Ladestand",

            "restart_required": "Die Konfiguration wurde geändert. WARP neu starten, bevor gekoppelt oder abgefragt wird."
        },
        "script": {
            "save_failed": "Speichern der Tesla-Bluetooth-Konfiguration fehlgeschlagen.",
            "command_failed": "Tesla-Bluetooth-Befehl fehlgeschlagen."
        }
    }
}
