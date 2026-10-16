/** @jsxImportSource preact */
import { h } from "preact";
let x = {
    "tesla_vehicle": {
        "navbar": {"tesla_vehicle": "Bluetooth"},
        "bluetooth": {
            "title": "Bluetooth",
            "tesla_title": "Tesla state of charge",
            "enable": "Enable Bluetooth",
            "desired": "Desired general permission (saved, not runtime status)",
            "help": "Bluetooth can be enabled to read the Tesla state of charge (SoC). The vehicle must be configured separately and paired using its key card. This is not a general Bluetooth device manager. Changes to general permission take effect only after saving and an explicitly confirmed restart. No different firmware is needed. Disabling deletes neither the local key nor authorization in the Tesla.",
            "runtime": "Actual Bluetooth status",
            "disabled": "Disabled", "starting": "Starting", "active": "Active", "stopping": "Stopping", "error": "Error / restart locked",
            "unsupported": "Bluetooth is unavailable with this SDK/hardware configuration. Activation and Bluetooth restart are disabled. Reason",
            "legacy": "An existing Tesla service configuration is retained. It does not automatically enable Bluetooth. Save the general permission separately and confirm restart.",
            "save_failed": "Bluetooth configuration commit was not confirmed. No restart through this action. The last confirmed configuration is retained; check the error and save again.",
            "pending": "Restart pending: saved permission and runtime may differ, or the safe lifecycle latch requires a restart. The saved permission is applied only afterwards.",
            "restart": "Restart now",
            "restart_confirm": "Restart WARP now and apply the confirmed saved Bluetooth permission? Charging control will be interrupted."
        },
        "content": {
            "title": "Tesla Bluetooth",
            "enable": "Enable Tesla SoC service",
            "enable_desc": "Read state of charge over Bluetooth; WARP starts normal AC charging itself",
            "vehicle_profile": "WARP vehicle profile",
            "vehicle_profile_help": "The profile MAC measured by WARP through SLAC is permanently bound to the Tesla key.",
            "select_vehicle": "Select vehicle",
            "vin": "Tesla VIN",
            "vin_help": "17-character vehicle identification number.",
            "ble_address": "Tesla BLE MAC",
            "ble_address_help": <>Transport address only; the connected vehicle is identified through WARP SLAC, never this address. On Android, scan for an 18-character BLE name beginning with <code>S</code> and ending with <code>C</code>. iOS does not expose BLE MAC addresses.</>,
            "poll_interval": "Polling interval",
            "wake_once": "Wake Tesla once",
            "wake_once_desc": "The first SoC request per plug session may wake the vehicle",
            "wake_once_help": "Leave disabled if periodic SoC polling must not wake a sleeping Tesla.",
            "pair_title": "Pair vehicle key",
            "pair_step_1": "Save the configuration and reboot WARP if requested.",
            "pair_step_2": "Plug the selected Tesla into this WARP and wait until its vehicle profile is active.",
            "pair_step_3": "Sit in the vehicle and have the key card ready.",
            "pair_step_4": "Press Pair Tesla key. Once the request has been sent, follow the vehicle's key-card approval instructions at its card reader.",
            "pair_step_5": "WARP checks approval within a bounded verification window. A sent request or a local key is not proof of approval. If verification times out, check the vehicle and retry deliberately. Read SoC now can also verify an existing local key.",
            "pair_confirm": "Send a key-enrollment request to the connected Tesla? Have the key card ready and approve it in the vehicle. This does not start charging.",
            "poll_confirm": "Verify the local key and read SoC? This manual request may wake the vehicle even when automatic waking is disabled. It does not start charging.",
            "lifecycle_help": "The Tesla service requires the general Bluetooth permission applied at boot above. Changing Tesla configuration while running safely stops the runtime; after controller use, restart stays locked until reboot. Disabling preserves the local key and Tesla authorization. No automatic reboot is performed.",
            "vehicle_ready": "The selected WARP vehicle profile is active. Firmware rechecks the connected SLAC identity and key binding before every operation.",
            "vehicle_not_ready": "Pairing and reading require the selected Tesla to be plugged into this WARP with its vehicle profile active. BLE connectivity alone is not sufficient.",
            "save_first": "Save configuration changes before pairing or reading.",
            "enable_first": "Enable and save Tesla Bluetooth first.",
            "runtime_inactive": "The Bluetooth runtime is not running. Check errors and restart_required; after a startup failure, correct the configuration before retrying.",
            "verify_requires_key": "Manual verification needs a known local key. A new key is created only by pairing; an unknown storage state is not proof that no key exists.",
            "manual_wake_help": "Manual verification may wake the vehicle, independently of the automatic wake setting; the existing one-wake-per-plug-session limit still applies.",
            "revocation_help": "Vehicle-side revocation and local deletion are different. To revoke WARP access, remove its key in the Tesla's key management, then delete the local key here. WARP cannot reliably distinguish revocation from communication failures; an earlier authenticated response is not proof of continuing authorization. Local deletion sends no revocation command to Tesla.",
            "pair_action": "Pairing",
            "pair": "Pair Tesla key",
            "pairing": "Pairing …",
            "test_actions": "Test connection",
            "read_soc": "Read SoC now",

            "forget_key": "Delete local key",
            "forget_confirm": "Delete WARP's local Tesla key, sessions and SLAC vehicle binding? This does not revoke the key in the Tesla. Remove the WARP key in the vehicle separately. Pairing again will require key-card approval.",
            "cancel": "Cancel"
        },
        "status": {
            "pairing": "Key status",
            "paired": "Authenticated response verified (not a live revocation check)",
            "sending": "Pairing requested; transmission not yet confirmed",
            "pending": "Request sent; authenticated vehicle approval still pending",
            "local_key": "Local key storage",
            "key_present": "Local key present (not proof of Tesla authorization)",
            "key_absent": "Local key absent",
            "cleanup_required": "Uncommitted local key material remains; delete the local key before retrying",
            "key_only": "Key present, not yet authenticated",
            "key_unknown": "Key status unknown (storage not inspected)",
            "not_paired": "Not paired",
            "ble": "Bluetooth",
            "connected": "Connected",
            "disconnected": "Disconnected",
            "soc": "Last state of charge",

            "stopping": "Tesla Bluetooth is stopping. Actions remain unavailable until shutdown completes. If shutdown cannot complete, reboot WARP.",
            "restart_required": "Bluetooth has stopped or failed. Reboot WARP before enabling, pairing or polling again."
        },
        "script": {
            "save_failed": "Failed to save Tesla Bluetooth configuration.",
            "command_failed": "Tesla Bluetooth command failed."
        }
    }
}
