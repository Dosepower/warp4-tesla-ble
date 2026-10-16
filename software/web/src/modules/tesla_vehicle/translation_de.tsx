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
            "ble_address_help": <>Nur Transportadresse; die WARP identifiziert das angeschlossene Fahrzeug per SLAC, niemals anhand dieser Adresse. Mit einem Android-BLE-Scanner nach einem 18-stelligen Namen suchen, der mit <code>S</code> beginnt und mit <code>C</code> endet. iOS zeigt BLE-MAC-Adressen nicht an.</>,
            "poll_interval": "Abfrageintervall",
            "wake_once": "Tesla einmal wecken",
            "wake_once_desc": "Erste SoC-Abfrage je Steckvorgang darf das Fahrzeug wecken",
            "wake_once_help": "Ausgeschaltet lassen, wenn ein schlafender Tesla nicht allein für die regelmäßige SoC-Abfrage geweckt werden soll.",
            "pair_title": "Fahrzeugschlüssel koppeln",
            "pair_step_1": "Konfiguration speichern und die WARP neu starten, falls dies angezeigt wird.",
            "pair_step_2": "Den ausgewählten Tesla an genau diese WARP anschließen und warten, bis das zugehörige Fahrzeugprofil aktiv ist.",
            "pair_step_3": "Im Fahrzeug sitzen und die Schlüsselkarte bereithalten.",
            "pair_step_4": "„Tesla-Schlüssel koppeln“ drücken. Sobald die Anfrage gesendet wurde, den Anweisungen im Fahrzeug zur Freigabe mit der Schlüsselkarte am Kartenleser folgen.",
            "pair_step_5": "Die WARP prüft die Freigabe innerhalb eines begrenzten Zeitfensters. Eine gesendete Anfrage oder ein lokaler Schlüssel beweist keine Freigabe. Bei Zeitüberschreitung das Fahrzeug prüfen und bewusst erneut versuchen. „SoC jetzt lesen“ kann auch einen vorhandenen lokalen Schlüssel prüfen.",
            "pair_confirm": "Eine Anfrage zum Anlernen eines Schlüssels an den angeschlossenen Tesla senden? Schlüsselkarte bereithalten und im Fahrzeug freigeben. Dadurch wird keine Ladung gestartet.",
            "poll_confirm": "Lokalen Schlüssel prüfen und SoC lesen? Diese manuelle Abfrage darf das Fahrzeug auch bei ausgeschaltetem automatischem Wecken wecken. Dadurch wird keine Ladung gestartet.",
            "lifecycle_help": "Gespeicherte Konfiguration und aktive Bluetooth-Laufzeit sind getrennt. Erstmaliges Aktivieren nach deaktiviertem Boot startet ohne Neustart. Jede gespeicherte Änderung im Betrieb beendet die Laufzeit; nach Nutzung des Bluetooth-Controllers verlangt restart_required vor erneuter Nutzung einen WARP-Neustart. Der lokale Schlüssel bleibt beim Beenden erhalten. Kein automatischer Neustart.",
            "vehicle_ready": "Das gewählte WARP-Fahrzeugprofil ist aktiv. Die Firmware prüft vor jeder Aktion erneut die angeschlossene SLAC-Identität und die Schlüsselbindung.",
            "vehicle_not_ready": "Zum Koppeln und Lesen muss der gewählte Tesla an dieser WARP angeschlossen und sein Fahrzeugprofil aktiv sein. Eine BLE-Verbindung allein genügt nicht.",
            "save_first": "Konfigurationsänderungen vor dem Koppeln oder Lesen speichern.",
            "enable_first": "Tesla-Bluetooth zuerst aktivieren und speichern.",
            "runtime_inactive": "Die Bluetooth-Laufzeit läuft nicht. Fehler und restart_required prüfen; nach fehlgeschlagenem Start die Konfiguration vor einem erneuten Versuch korrigieren.",
            "verify_requires_key": "Die manuelle Prüfung benötigt einen bekannten lokalen Schlüssel. Ein neuer Schlüssel wird nur beim Koppeln erzeugt; unbekannter Speicherstatus bedeutet nicht, dass kein Schlüssel vorhanden ist.",
            "manual_wake_help": "Die manuelle Prüfung darf unabhängig vom automatischen Wecken das Fahrzeug wecken; die bestehende Grenze von einmaligem Wecken je Steckvorgang gilt weiterhin.",
            "revocation_help": "Fahrzeugseitiger Widerruf und lokales Löschen sind verschieden. Für den Widerruf des WARP-Zugriffs den Schlüssel in der Tesla-Schlüsselverwaltung entfernen und danach hier den lokalen Schlüssel löschen. Die WARP kann Widerruf und Kommunikationsfehler nicht zuverlässig unterscheiden; eine frühere authentifizierte Antwort beweist keine fortbestehende Berechtigung. Lokales Löschen sendet keinen Widerrufsbefehl an Tesla.",
            "pair_action": "Kopplung",
            "pair": "Tesla-Schlüssel koppeln",
            "pairing": "Kopplung läuft …",
            "test_actions": "Verbindung prüfen",
            "read_soc": "SoC jetzt lesen",

            "forget_key": "Lokalen Schlüssel löschen",
            "forget_confirm": "Lokalen Tesla-Schlüssel der WARP, Sitzungen und SLAC-Fahrzeugbindung löschen? Dies widerruft den Schlüssel im Tesla nicht. Den WARP-Schlüssel separat im Fahrzeug entfernen. Erneutes Koppeln erfordert die Freigabe mit der Schlüsselkarte.",
            "cancel": "Abbrechen"
        },
        "status": {
            "pairing": "Schlüsselstatus",
            "paired": "Authentifizierte Antwort geprüft (keine laufende Widerrufsprüfung)",
            "sending": "Kopplung angefordert; Übertragung noch nicht bestätigt",
            "pending": "Anfrage gesendet; authentifizierte Fahrzeugfreigabe ausstehend",
            "local_key": "Lokaler Schlüsselspeicher",
            "key_present": "Lokaler Schlüssel vorhanden (kein Beweis der Tesla-Berechtigung)",
            "key_absent": "Lokaler Schlüssel nicht vorhanden",
            "cleanup_required": "Nicht übernommene lokale Schlüsselreste vorhanden; vor erneutem Versuch lokal löschen",
            "key_only": "Schlüssel vorhanden, noch nicht authentifiziert",
            "key_unknown": "Schlüsselstatus unbekannt (Speicher nicht geprüft)",
            "not_paired": "Nicht gekoppelt",
            "ble": "Bluetooth",
            "connected": "Verbunden",
            "disconnected": "Getrennt",
            "soc": "Letzter Ladestand",

            "stopping": "Tesla-Bluetooth wird beendet. Aktionen sind bis zum Abschluss gesperrt. Falls das Beenden nicht abgeschlossen werden kann, WARP neu starten.",
            "restart_required": "Bluetooth wurde beendet oder ist fehlgeschlagen. WARP vor erneutem Aktivieren, Koppeln oder Abfragen neu starten."
        },
        "script": {
            "save_failed": "Speichern der Tesla-Bluetooth-Konfiguration fehlgeschlagen.",
            "command_failed": "Tesla-Bluetooth-Befehl fehlgeschlagen."
        }
    }
}
