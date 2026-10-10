/** @jsxImportSource preact */
import { h } from "preact";
let x = {
    "tesla_vehicle": {
        "navbar": {"tesla_vehicle": "Tesla Bluetooth"},
        "content": {
            "title": "Tesla Bluetooth",
            "enable": "Enable Tesla Bluetooth",
            "enable_desc": "Read state of charge over Bluetooth; WARP starts normal AC charging itself",
            "vehicle_profile": "WARP vehicle profile",
            "vehicle_profile_help": "The profile MAC measured by WARP through SLAC is permanently bound to the Tesla key.",
            "select_vehicle": "Select vehicle",
            "vin": "Tesla VIN",
            "vin_help": "17-character vehicle identification number.",
            "ble_address": "Tesla BLE MAC",
            "ble_address_help": <>Bluetooth address of Tesla VCSEC. On Android, scan for an 18-character BLE name beginning with <code>S</code> and ending with <code>C</code>. iOS does not expose BLE MAC addresses.</>,
            "poll_interval": "Polling interval",
            "wake_once": "Wake Tesla once",
            "wake_once_desc": "The first SoC request per plug session may wake the vehicle",
            "wake_once_help": "Leave disabled if periodic SoC polling must not wake a sleeping Tesla.",
            "pair_title": "Pair vehicle key",
            "pair_step_1": "Save the configuration and reboot WARP if requested.",
            "pair_step_2": "Plug the selected Tesla into this WARP and wait until its vehicle profile is active.",
            "pair_step_3": "Sit in the vehicle and have the key card ready.",
            "pair_step_4": "Press Pair Tesla key, then place the key card on the center console and approve the request in the vehicle.",
            "pair_step_5": "Press Read SoC now afterwards. Only an authenticated response changes the status to Paired.",
            "pair_action": "Pairing",
            "pair": "Pair Tesla key",
            "pairing": "Pairing …",
            "test_actions": "Test connection",
            "read_soc": "Read SoC now",

            "forget_key": "Delete Tesla key",
            "forget_confirm": "Really delete the Tesla key, sessions and vehicle-MAC binding?",
            "cancel": "Cancel"
        },
        "status": {
            "pairing": "Key status",
            "paired": "Paired and authenticated",
            "pending": "Waiting for confirmation in Tesla",
            "key_only": "Key present, not yet authenticated",
            "not_paired": "Not paired",
            "ble": "Bluetooth",
            "connected": "Connected",
            "disconnected": "Disconnected",
            "soc": "Last state of charge",

            "restart_required": "Configuration changed. Reboot WARP before pairing or polling."
        },
        "script": {
            "save_failed": "Failed to save Tesla Bluetooth configuration.",
            "command_failed": "Tesla Bluetooth command failed."
        }
    }
}
