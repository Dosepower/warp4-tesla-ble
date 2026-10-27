interface TeslaProfile {
    enable: boolean;
    vin: string;
    ble_address: string;
    ble_address_type: number;
    discovery_revision: number;
    ev_index: number;
    ev_mac: string;
    poll_interval: number;
    wake_once: boolean;
}
export interface profiles { entries: TeslaProfile[]; }
export interface state {
    profile_ev_index: number;
    active_ev_index: number;
    legacy_blocked: boolean;
    running: boolean;
    action_busy: boolean;
    stopping: boolean;
    connected: boolean;
    key_present: boolean;
    key_presence: 'unknown' | 'present' | 'absent' | 'cleanup';
    paired: boolean;
    pairing_pending: boolean;
    pair_request_sent: boolean;
    // Read-only sampled transport evidence, never proof of vehicle approval.
    pair_tx_total_bytes: number;
    pair_tx_attempted_bytes: number;
    pair_tx_acked_bytes: number;
    pair_tx_complete: boolean;
    pair_result: number; // -1: no completion; otherwise VehicleResultCode
    pair_outcome?: 'none' | 'pending' | 'waiting' | 'verifying' | 'approved' | 'vehicle_rejected' | 'local_expiry' | 'connection_failed' | 'link_lost' | 'internal_failure' | 'cancelled' | 'storage_failure';
    pair_response_seen?: boolean;
    pair_response_whitelist?: boolean;
    pair_response_code?: number;
    pair_response_outer?: number;
    pair_response_operation?: number;
    // Nonsecret first-cause pairing trace (numbers/fixed labels only; Details/diagnostics, never authority).
    pair_stage?: 'none' | 'claimed' | 'connected' | 'sent' | 'verifying' | 'response' | 'approved';
    pair_loss_cause?: 'none' | 'remote_disconnect' | 'security' | 'rx_overflow' | 'frame_refused' | 'send_completion' | 'verify_session' | 'identity_change' | 'profile_reload' | 'stop' | 'operation_timeout' | 'operation_cancelled' | 'unavailable';
    pair_loss_stage?: string;
    pair_loss_detail?: number; // HCI/ATT/SM status or fixed negative code
    pair_loss_elapsed_ms?: number; // first loss relative to PRESENT_KEY completion
    pair_disconnect_reason?: number; // first HCI disconnect reason of the attempt, -1 none
    pair_verify_polls?: number;
    pair_verify_denied?: number;
    pair_rx_notifications?: number;
    pair_rx_frames?: number;
    pair_rx_pair_replies?: number;
    pair_rx_session_replies?: number;
    timeout_cause: string; // first ownership-supervision failure; no identity data
    timeout_operation: number;
    timeout_elapsed_ms: number;
    // Identity-free SDK observations, frozen at first ownership fault.
    connect_attempt?: number;
    connect_flags?: number; // submit enter/return=1/2, cancel enter/return=4/8, CONNECT/DISCONNECT=16/32, unannounced link found/terminated/drained=64/128/256
    connect_submit_rc?: number;
    connect_cancel_rc?: number;
    connect_status?: number;
    restart_required: boolean;
    last_soc: number;
    last_update: number;
    last_error: string;
}
export interface guidance {
    generation: string;
    revision: number;
    request: string;
    ready: boolean;
    profiles: {ev_index: number; ev_mac: string; namespace_clean: boolean; paired: boolean; key_presence: state['key_presence']; binding_fresh: boolean}[];
}
export type guided = {phase: 'prepare_pair' | 'prepare_poll' | 'commit' | 'cancel' | 'reset' | 'unpair' | 'cleanup'; request: string; generation: string; revision: number; profile: TeslaProfile};
export type start = {ev_index: number};
export type pair = {ev_index: number};
export type poll = {ev_index: number};
export type forget_key = {ev_index: number};
export type scan = {vin: string; ev_index: number; ev_mac: string};
export interface discovery {
    // Completion-relative monotonic age sampled by the backend; no wallclock.
    result_age_ms: number;
    result_ttl_ms: number;
    searched_vin: string;
    ev_mac: string;
    generation: string;
    status: 'idle' | 'searching' | 'complete' | 'error';
    busy: boolean;
    error: string;
    truncated: boolean;
    revision: number;
    ev_index: number;
    targets: {name: string; ble_address: string; ble_address_type: number; matches_vin: boolean; rssi: number}[];
}

//APIPath:bluetooth/
export interface config___bluetooth { enable: boolean; }
export interface state___bluetooth {
    supported: boolean;
    unsupported_reason: '' | 'sdk_disabled' | 'hardware_unsupported' | 'bench_only' | 'storage_backend_unverified';
    phase: 'disabled' | 'starting' | 'active' | 'stopping' | 'error';
    restart_required: boolean;
    restart_reason: '' | 'configuration_changed' | 'lifecycle_latched';
    persistence_error: boolean;
    legacy_tesla_enabled: boolean;
}
export type restart = {};
