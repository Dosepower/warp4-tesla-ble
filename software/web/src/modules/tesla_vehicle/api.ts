export interface config {
    enable: boolean;
    vin: string;
    ble_address: string;
    ev_index: number;
    poll_interval: number;
    wake_once: boolean;
}

export interface state {
    running: boolean;
    connected: boolean;
    key_present: boolean;
    paired: boolean;
    pairing_pending: boolean;
    restart_required: boolean;
    last_soc: number;

    last_update: number;
    last_error: string;
}

export type pair = {};
export type poll = {};
export type forget_key = {};
