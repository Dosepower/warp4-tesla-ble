import { __ } from "../../ts/translation";
import type { state } from "./api";

// Exact nonnegative WhitelistOperation_information_E order from Tesla's
// vcsec.proto, pinned fc5a1910ad45c42921d541089e3e366d0b897f52.
const reasons = () => [
    __("tesla_vehicle.pair_response.unknown"), __("tesla_vehicle.pair_response.undocumented"),
    __("tesla_vehicle.pair_response.remove_self"), __("tesla_vehicle.pair_response.fob_full"),
    __("tesla_vehicle.pair_response.whitelist_full"), __("tesla_vehicle.pair_response.add_permission"),
    __("tesla_vehicle.pair_response.invalid_key"), __("tesla_vehicle.pair_response.remove_permission"),
    __("tesla_vehicle.pair_response.change_permission"), __("tesla_vehicle.pair_response.elevate"),
    __("tesla_vehicle.pair_response.demote"), __("tesla_vehicle.pair_response.remove_own"),
    __("tesla_vehicle.pair_response.not_listed"), __("tesla_vehicle.pair_response.already_listed"),
    __("tesla_vehicle.pair_response.on_reader"), __("tesla_vehicle.pair_response.fm_mode"),
    __("tesla_vehicle.pair_response.fm_add"), __("tesla_vehicle.pair_response.fm_remove"),
    __("tesla_vehicle.pair_response.fs_full"), __("tesla_vehicle.pair_response.no_role"),
    __("tesla_vehicle.pair_response.service_role"), __("tesla_vehicle.pair_response.non_service"),
    __("tesla_vehicle.pair_response.service_mode"), __("tesla_vehicle.pair_response.local_auth"),
    __("tesla_vehicle.pair_response.ui_denied"), __("tesla_vehicle.pair_response.tap_timeout"),
    __("tesla_vehicle.pair_response.ui_timeout"), __("tesla_vehicle.pair_response.valet"),
    __("tesla_vehicle.pair_response.cancelled"),
];
export function pairingOutcomeReason(s: state): string {
    switch (s?.pair_outcome) {
        case 'vehicle_rejected':
        {
            const r = reasons();
            return __("tesla_vehicle.pair_response.prefix") + (Number.isInteger(s.pair_response_code) && s.pair_response_code >= 0 &&
                s.pair_response_code < r.length ? r[s.pair_response_code] : __("tesla_vehicle.pair_response.unknown"));
        }
        case 'local_expiry': return __("tesla_vehicle.status.approval_expired");
        case 'connection_failed': return __("tesla_vehicle.pair_response.connection_failed");
        case 'link_lost': return __("tesla_vehicle.status.link_lost");
        case 'internal_failure': return __("tesla_vehicle.pair_response.internal_failure");
        case 'storage_failure': return __("tesla_vehicle.status.key_unknown");
        case 'cancelled': return __("tesla_vehicle.pair_response.local_cancelled");
        default: return '';
    }
}
