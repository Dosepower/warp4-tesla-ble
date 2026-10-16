import type {state} from "./api";

type Status = Partial<state>;

export function teslaVehicleReady(index: number, vehicle: {active_ev_index: number; mac: string}) {
    // Display-only prerequisite; firmware rechecks SLAC/session/binding itself.
    return index >= 0 && vehicle?.active_ev_index === index && !!vehicle.mac && /[1-9a-f]/i.test(vehicle.mac);
}

export function teslaKeyStatus(status: Status) {
    if (status?.pairing_pending) return status.pair_request_sent ? 'pending' : 'sending';
    if (status?.paired) return 'paired';
    if (status?.key_presence === 'cleanup') return 'cleanup_required';
    if (!status || status.key_presence === 'unknown') return 'key_unknown';
    return status.key_present ? 'key_only' : 'not_paired';
}

export function teslaActions(enable: boolean, status: Status, dirty: boolean, vehicleReady = false, busy = false) {
    const stopping = !!status?.stopping;
    const unavailable = stopping || !enable || !status?.running || !!status?.restart_required || dirty || busy || !vehicleReady || !!status?.pairing_pending;
    return {
        transition: stopping,
        pairDisabled: unavailable,
        // Verification is precisely how a stored key becomes authenticated.
        pollDisabled: unavailable || !status?.key_present || status?.key_presence !== 'present',
        deleteDisabled: busy || stopping || !!status?.restart_required || !status || status.key_presence === 'absent'
    };
}
