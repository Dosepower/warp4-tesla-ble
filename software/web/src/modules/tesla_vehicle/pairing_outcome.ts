import { __ } from "../../ts/translation";
import type { state } from "./api";

// Exact nonnegative WhitelistOperation_information_E order from Tesla's
// vcsec.proto, pinned fc5a1910ad45c42921d541089e3e366d0b897f52.
const reasons = [
    "tesla_vehicle.pair_response.unknown", "tesla_vehicle.pair_response.undocumented",
    "tesla_vehicle.pair_response.remove_self", "tesla_vehicle.pair_response.fob_full",
    "tesla_vehicle.pair_response.whitelist_full", "tesla_vehicle.pair_response.add_permission",
    "tesla_vehicle.pair_response.invalid_key", "tesla_vehicle.pair_response.remove_permission",
    "tesla_vehicle.pair_response.change_permission", "tesla_vehicle.pair_response.elevate",
    "tesla_vehicle.pair_response.demote", "tesla_vehicle.pair_response.remove_own",
    "tesla_vehicle.pair_response.not_listed", "tesla_vehicle.pair_response.already_listed",
    "tesla_vehicle.pair_response.on_reader", "tesla_vehicle.pair_response.fm_mode",
    "tesla_vehicle.pair_response.fm_add", "tesla_vehicle.pair_response.fm_remove",
    "tesla_vehicle.pair_response.fs_full", "tesla_vehicle.pair_response.no_role",
    "tesla_vehicle.pair_response.service_role", "tesla_vehicle.pair_response.non_service",
    "tesla_vehicle.pair_response.service_mode", "tesla_vehicle.pair_response.local_auth",
    "tesla_vehicle.pair_response.ui_denied", "tesla_vehicle.pair_response.tap_timeout",
    "tesla_vehicle.pair_response.ui_timeout", "tesla_vehicle.pair_response.valet",
    "tesla_vehicle.pair_response.cancelled",
] as const;
export function pairingOutcomeReason(s: state): string {
    switch (s?.pair_outcome) {
        case 'vehicle_rejected':
            return __("tesla_vehicle.pair_response.prefix") + __(Number.isInteger(s.pair_response_code) && s.pair_response_code >= 0 &&
                s.pair_response_code < reasons.length ? reasons[s.pair_response_code] : "tesla_vehicle.pair_response.unknown");
        case 'local_expiry': return __("tesla_vehicle.status.approval_expired");
        case 'connection_failed': return __("tesla_vehicle.pair_response.connection_failed");
        case 'link_lost': return __("tesla_vehicle.status.link_lost");
        case 'internal_failure': return __("tesla_vehicle.pair_response.internal_failure");
        case 'storage_failure': return __("tesla_vehicle.status.key_unknown");
        case 'cancelled': return __("tesla_vehicle.pair_response.local_cancelled");
        default: return '';
    }
}
