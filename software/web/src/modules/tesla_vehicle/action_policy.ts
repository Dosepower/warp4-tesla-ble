import type {state, state___bluetooth} from "./api";

export function teslaBluetoothReady(bluetooth: Partial<state___bluetooth>) {
    return !!bluetooth?.supported && bluetooth.phase === 'active' && !bluetooth.restart_required && !bluetooth.persistence_error;
}

export function teslaStartAllowed(enable: boolean, status: Partial<state>, bluetooth: Partial<state___bluetooth>, dirty: boolean, busy: boolean) {
    return !!enable && !!status && teslaBluetoothReady(bluetooth) && !status.running &&
        !status.stopping && !status.restart_required && !status.action_busy && !dirty && !busy;
}

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

export function teslaActions(enable: boolean, status: Status, dirty: boolean, vehicleReady = false, busy = false, bluetooth?: Partial<state___bluetooth>) {
    const stopping = !!status?.stopping;
    const actionBusy = busy || !!status?.action_busy || !!status?.pairing_pending;
    const unavailable = !teslaBluetoothReady(bluetooth) || stopping || !enable || !status?.running || !!status?.restart_required || dirty || actionBusy || !vehicleReady;
    return {
        transition: stopping,
        pairDisabled: unavailable || !!status?.paired,
        // Verification is precisely how a stored key becomes authenticated.
        pollDisabled: unavailable || !status?.key_present || status?.key_presence !== 'present',
        deleteDisabled: actionBusy || dirty || stopping || !!status?.restart_required || !status || status.key_presence === 'absent'
    };
}
