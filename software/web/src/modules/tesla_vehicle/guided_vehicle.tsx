import { h, Fragment } from "preact";
import { Alert, Button } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";

import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";

import { teslaKeyStatus, teslaVehicleReady, teslaBluetoothReady } from "./action_policy";

type TeslaConfig = API.getType["tesla_vehicle/profiles"];
type Profile = TeslaConfig["entries"][number];
type Target = API.getType["tesla_vehicle/discovery"]["targets"][number];
interface View {
    tesla_state: API.getType["tesla_vehicle/state"];
    ev_state: API.getType["ev/state"];
    discovery: API.getType["tesla_vehicle/discovery"];
    guidance: API.getType["tesla_vehicle/guidance"];
    edit_index: number;
    command_busy: boolean;
    search_error: boolean | string;
    command_error: boolean | string;
    adopted: boolean;
    details_open: boolean;
    edit_binding: boolean;
}

// Inline extension of Fahrzeuge, not a second registry or a standalone page.
export class TeslaVehicle extends ConfigComponent<"tesla_vehicle/profiles", {}, View> {
    private command_in_progress = false;
    private saving = false;
    private disposed = false;
    private revision = 0;
    private selection_revision = 0;
    private backend_error = '';
    private backend_error_profile = -1;
    private backend_error_revision = 0;
    private listeners = new AbortController();
    private scan_waiting = false;
    private search_context = "";
    private search_saved = "";
    private search_generation = "";
    private discovery_received = NaN;
    private discovery_wall_received = NaN;
    private discovery_age = Infinity;
    private discovery_ttl = 0;
    private scan_revision = 0;
    private discovery_result_seen = false;
    private discovery_timer: ReturnType<typeof setTimeout>;
    private discovery_revision = -1;
    private identity = "";
    private ev_config = "";
    private saved_config = "";
    private pending_save = "";
    private timer: ReturnType<typeof setTimeout>;
    private cancelWait: () => void;
    private credential_evidence = "";
    private deletion_revision = 0;
    private guidance_revision: number;
    private guidance_generation: string;
    private owned_revision = -1;
    private bluetooth_context = "";
    private pending_command: {request: string; generation: string; revision: number; profile: Profile};

    private cancelCommand() {
        const payload = this.pending_command;
        this.pending_command = undefined;
        // Revocation only; never retries preparation, commit or any radio action.
        if (payload) void API.call("tesla_vehicle/guided", {...payload, phase: 'cancel'}).catch(() => {});
    }
    private commandFailure(error: unknown) {
        return error && typeof error === 'object' && 'message' in error ? String((error as {message: unknown}).message) :
            typeof error === 'string' ? error : __("tesla_vehicle.content.preparation_failed");
    }
    private bluetoothContext() {
        const b = API.get("bluetooth/state");
        return JSON.stringify([API.get("bluetooth/config")?.enable, b?.supported, b?.phase, b?.restart_required, b?.persistence_error]);
    }
    private requestDiscovery(p: Profile) {
        // Only explicit Search click or confirmed Pair/Poll may invoke this.
        return API.call("tesla_vehicle/scan", {vin: p.vin, ev_index: p.ev_index, ev_mac: p.ev_mac});
    }

    private credentialEvidence() {
        const s = API.get("tesla_vehicle/state"), g = API.get("tesla_vehicle/guidance");
        return JSON.stringify([s?.profile_ev_index, s?.key_presence, s?.key_present, s?.paired, s?.pairing_pending, g?.generation, g?.profiles]);
    }
    private updateCredentialEvidence() {
        const evidence = this.credentialEvidence();
        if (evidence !== this.credential_evidence) { ++this.deletion_revision; this.credential_evidence = evidence; }
    }

    constructor() {
        super("tesla_vehicle/profiles", () => __("tesla_vehicle.script.save_failed"), undefined, {
            tesla_state: API.get("tesla_vehicle/state"), ev_state: API.get("ev/state"),
            discovery: API.get("tesla_vehicle/discovery"), guidance: API.get("tesla_vehicle/guidance"),
            edit_index: API.get("ev/config")?.evs.length ? 0 : -1,
            command_busy: false, search_error: false, command_error: false, adopted: false, details_open: false, edit_binding: false,
        });
        this.identity = this.vehicleIdentity();
        this.ev_config = JSON.stringify(API.get("ev/config"));
        this.saved_config = JSON.stringify(API.get("tesla_vehicle/profiles"));
        this.credential_evidence = this.credentialEvidence();
        this.guidance_revision = API.get("tesla_vehicle/guidance")?.revision;
        this.guidance_generation = API.get("tesla_vehicle/guidance")?.generation;
        this.bluetooth_context = this.bluetoothContext();
        this.backend_error = this.state.tesla_state?.last_error || '';
        this.backend_error_profile = this.state.tesla_state?.profile_ev_index ?? -1;
        const listen = (topic: keyof API.EventMap, callback: () => void) =>
            util.addApiEventListener(topic, () => { if (!this.disposed) callback(); }, {signal: this.listeners.signal});
        listen("tesla_vehicle/state", () => {
            this.updateCredentialEvidence();
            const next = API.get("tesla_vehicle/state");
            // A backend acknowledgement clears only the mirrored local failure,
            // never a different rejected command that still needs attention.
            const mirrored = this.backend_error && this.backend_error_profile === next?.profile_ev_index &&
                !next?.last_error && typeof this.state.command_error === 'string' &&
                this.errorText(this.backend_error) === this.errorText(this.state.command_error);
            // API.get is a mutable signal cache; retain scalar evidence, not a
            // reference whose old last_error has already been overwritten.
            if (this.backend_error !== (next?.last_error || '') || this.backend_error_profile !== (next?.profile_ev_index ?? -1)) ++this.backend_error_revision;
            this.backend_error = next?.last_error || '';
            this.backend_error_profile = next?.profile_ev_index ?? -1;
            this.setState({tesla_state: next, ...(mirrored ? {command_error: false} : {})});
        });
        listen("tesla_vehicle/guidance", () => {
            this.updateCredentialEvidence();
            const g = API.get("tesla_vehicle/guidance");
            if (g?.revision !== this.guidance_revision || g?.generation !== this.guidance_generation) {
                // Only our one exact transport-save transition may advance the
                // consent revision. Every external edit or identity ABA cancels.
                if (g?.revision !== this.owned_revision || g?.generation !== this.guidance_generation) ++this.revision;
                else this.owned_revision = -1;
                this.guidance_revision = g?.revision; this.guidance_generation = g?.generation;
            }
            if (this.search_context && API.get("tesla_vehicle/guidance")?.generation !== this.search_generation) this.search_context = "";
            this.setState({guidance: API.get("tesla_vehicle/guidance")});
        });
        listen("tesla_vehicle/discovery", () => {
            const discovery = API.get("tesla_vehicle/discovery");
            if (discovery && !discovery.busy && discovery.status !== 'searching') this.scan_waiting = false;
            this.sampleDiscovery(discovery);
            this.setState({discovery});
        });
        listen("ev/state", () => {
            const identity = this.vehicleIdentity();
            if (identity !== this.identity) { ++this.revision; this.identity = identity; this.search_context = ""; }
            this.setState({ev_state: API.get("ev/state")});
        });
        listen("ev/config", () => {
            const config = JSON.stringify(API.get("ev/config"));
            if (config !== this.ev_config) { ++this.revision; this.ev_config = config; this.search_context = ""; }
            this.setState({});
        });
        listen("tesla_vehicle/profiles", () => {
            const config = JSON.stringify(API.get("tesla_vehicle/profiles"));
            if (config !== this.saved_config) {
                if (config !== this.pending_save) { ++this.revision; this.search_context = ""; }
                this.saved_config = config;
            }
            this.setState({});
        });
        const bluetoothChanged = () => {
            const context = this.bluetoothContext();
            if (context !== this.bluetooth_context) { ++this.revision; this.bluetooth_context = context; }
            this.setState({});
        };
        listen("bluetooth/state", bluetoothChanged);
        // Visibility follows persisted intent, not the radio phase before reboot.
        listen("bluetooth/config", bluetoothChanged);
    }

    override componentWillUnmount() {
        this.disposed = true; ++this.revision;
        this.listeners.abort(); clearTimeout(this.timer); this.cancelWait?.();
        clearTimeout(this.discovery_timer);
        this.cancelCommand(); // Best-effort complete-schema local lease revocation.
    }

    private profile(): Profile {
        const index = this.state.edit_index;
        return this.state.entries?.find(p => p.ev_index === index) ?? {
            ev_index: index, ev_mac: API.get("ev/config")?.evs[index]?.mac ?? "", enable: false,
            vin: "", ble_address: "", ble_address_type: 0, discovery_revision: 0, poll_interval: 300, wake_once: false,
        };
    }
    private vehicleIdentity() { const v = API.get("ev/state"); return JSON.stringify([v?.active_ev_index, v?.mac]); }
    private searchContext() { const p = this.profile(); return JSON.stringify([p.ev_index, p.ev_mac, p.vin]); }
    private configurationDirty() {
        const saved = API.get("tesla_vehicle/profiles");
        return this.isDirty() || !saved || JSON.stringify(saved.entries) !== JSON.stringify(this.state.entries);
    }
    private selectProfile = (value: string) => {
        ++this.revision; ++this.selection_revision; this.search_context = ""; this.cancelWait?.();
        this.setState({edit_index: parseInt(value), adopted: false, command_error: false, search_error: false, details_open: false, edit_binding: false});
    };
    private setProfile = (patch: Partial<Profile>) => {
        const profile = this.profile();
        if (this.disposed || profile.ev_index < 0 || !profile.ev_mac || this.command_in_progress || this.saving || this.searchBusy()) return;
        if (patch.vin !== undefined && patch.vin !== profile.vin) {
            if (!this.vinEditable()) return;
            patch = {...patch, ble_address: "", ble_address_type: 0, discovery_revision: 0}; this.search_context = "";
        }
        ++this.revision;
        const entries = (this.state.entries ?? []).map(p => ({...p}));
        const at = entries.findIndex(p => p.ev_index === profile.ev_index);
        if (at < 0) entries.push({...profile, ...patch}); else entries[at] = {...profile, ...patch};
        this.setState({entries, adopted: false}); this.setDirty(true);
    };
    private profileStatus() {
        const status = this.state.tesla_state;
        if (status?.profile_ev_index === this.state.edit_index) return status;
        const saved = this.state.guidance?.profiles.find(p => p.ev_index === this.state.edit_index);
        return {...status, last_error: '', running: false, connected: false, paired: saved?.paired ?? false,
            key_present: saved?.key_presence === 'present', key_presence: saved?.key_presence ?? 'unknown' as const,
            pairing_pending: false, pair_request_sent: false};
    }
    private vinEditable() {
        const p = this.profile(), status = this.profileStatus();
        if (status.paired || this.state.tesla_state?.legacy_blocked) return false;
        const saved = API.get("tesla_vehicle/profiles")?.entries.find(e => e.ev_index === p.ev_index);
        const g = this.state.guidance;
        const row = g?.profiles.find(e => e.ev_index === p.ev_index && e.ev_mac === p.ev_mac);
        // A pre-mapping bootstrap row is stale once a mapping is published.
        if (saved) return !row?.namespace_clean && status.key_presence === 'absent';
        if (status.key_present || status.key_presence === 'present' || status.key_presence === 'cleanup') return false;
        // No mapping must never promote unknown runtime state to absence.
        // Only backend inventory of this exact namespace permits bootstrap.
        return !!g?.generation && !!row?.namespace_clean && row.key_presence === 'absent' && !row.paired &&
            API.get("ev/config")?.evs[p.ev_index]?.mac === p.ev_mac;
    }
    private searchBusy() { return this.scan_waiting || !!this.state.discovery?.busy; }
    private idle() {
        const s = this.state.tesla_state;
        return !this.disposed && !!API.get("tesla_vehicle/profiles") && teslaBluetoothReady(API.get("bluetooth/state")) &&
            !!s && !s.legacy_blocked && !s.action_busy && !s.pairing_pending && !s.stopping && !s.restart_required && !this.saving && !this.searchBusy();
    }
    private searchAllowed() { return this.idle() && this.state.edit_index >= 0 && /^[A-HJ-NPR-Z0-9]{17}$/.test(this.profile().vin); }
    private discoveryAge() {
        // Age starts at the backend's completion-relative sample, not navigation
        // or event receipt. Wallclock is only an additional conservative expiry
        // fence (sleep/clock skew), never a manufactured discovery timestamp.
        const monotonic = typeof performance === 'undefined' ? NaN : performance.now();
        const elapsed = monotonic - this.discovery_received, wall = Date.now() - this.discovery_wall_received;
        if (!Number.isFinite(elapsed) || !Number.isFinite(wall) || elapsed < 0 || wall < 0) { this.discovery_age = Infinity; return Infinity; }
        return this.discovery_age + Math.max(elapsed, wall);
    }
    private sampleDiscovery(d: View['discovery']) {
        const same = d?.revision === this.discovery_revision;
        const previous = same ? this.discoveryAge() : 0;
        const valid = d?.status === 'complete' && Number.isInteger(d.revision) && d.revision >= this.discovery_revision && Number.isInteger(d.result_age_ms) && d.result_age_ms >= 0 &&
            Number.isInteger(d.result_ttl_ms) && d.result_ttl_ms > 0 && d.result_ttl_ms <= 120000 &&
            (!same || this.discovery_ttl === d.result_ttl_ms);
        this.discovery_age = valid ? Math.max(previous, d.result_age_ms) : Infinity;
        this.discovery_ttl = valid ? d.result_ttl_ms : 0;
        this.discovery_revision = Math.max(this.discovery_revision, d?.revision ?? -1);
        this.discovery_received = typeof performance === 'undefined' ? NaN : performance.now();
        this.discovery_wall_received = Date.now();
        if (d?.status === 'complete' && d.ev_index === this.state.edit_index && d.revision > this.scan_revision)
            this.discovery_result_seen = true;
        this.scheduleDiscoveryTick();
    }
    private scheduleDiscoveryTick() {
        clearTimeout(this.discovery_timer);
        const remaining = this.discovery_ttl - this.discoveryAge();
        if (this.disposed || !this.search_context || !(remaining > 0)) return;
        this.discovery_timer = setTimeout(() => {
            if (!this.disposed) { this.setState({}); this.scheduleDiscoveryTick(); }
        }, Math.min(1000, remaining));
    }
    private discoveryFresh() { return this.discoveryAge() < this.discovery_ttl; }
    private discoveryAgeText() {
        const age = this.discoveryAge();
        return age < 10000 ? __("tesla_vehicle.content.found_recently") :
            __("tesla_vehicle.content.found_seconds").replace('{seconds}', String(Math.floor(age / 1000)));
    }
    private matchingTargets() {
        const d = this.state.discovery;
        const p = this.profile();
        if (this.search_context !== this.searchContext() || this.search_saved !== JSON.stringify(API.get("tesla_vehicle/profiles")) ||
            !this.search_generation || d?.generation !== this.search_generation || d.generation !== this.state.guidance?.generation ||
            d.searched_vin !== p.vin || d.ev_mac !== p.ev_mac ||
            d?.ev_index !== this.state.edit_index || d.status !== 'complete' || d.busy || d.truncated ||
            !Number.isInteger(d.revision) || d.revision <= this.scan_revision || !this.discoveryFresh()) return [];
        const matches = d.targets.filter(t => t.matches_vin);
        const t = matches[0];
        return matches.length === 1 && /^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test(t.ble_address) &&
            !/^(00:){5}00$|^(FF:){5}FF$/i.test(t.ble_address) && (t.ble_address_type === 0 || t.ble_address_type === 1) ? matches : [];
    }
    private search = async () => {
        if (this.command_in_progress || !this.searchAllowed()) return;
        const context = this.searchContext(), revision = this.revision;
        const selection_revision = this.selection_revision;
        this.command_in_progress = true; this.setState({command_busy: true, command_error: false, search_error: false, adopted: false});
        try {
            if (!await this.confirm('search')) return;
            if (!this.searchAllowed() || context !== this.searchContext() || revision !== this.revision) return;
            this.scan_waiting = true; this.search_context = context;
            this.scan_revision = API.get("tesla_vehicle/discovery")?.revision ?? 0;
            this.search_generation = API.get("tesla_vehicle/guidance")?.generation ?? "";
            this.discovery_result_seen = false;
            this.discovery_age = Infinity; this.discovery_ttl = 0;
            clearTimeout(this.discovery_timer);
            this.search_saved = JSON.stringify(API.get("tesla_vehicle/profiles"));
            const p = this.profile();
            await this.requestDiscovery(p);
        } catch (error) {
            this.scan_waiting = false; this.search_context = "";
            if (!this.disposed && selection_revision === this.selection_revision && context === this.searchContext()) this.setState({search_error: this.commandFailure(error)});
        } finally {
            this.command_in_progress = false; if (!this.disposed) this.setState({command_busy: false});
        }
    };
    private async selectTarget(target: Target) {
        if (!target || this.command_in_progress || !this.searchAllowed() || this.matchingTargets()[0] !== target) return;
        const p = this.profile(), context = this.searchContext(), revision = this.revision;

        const entries = (this.state.entries ?? []).map(e => ({...e}));
        const at = entries.findIndex(e => e.ev_index === p.ev_index);
        // Only this explicit unique-match adoption grants local profile permission.
        // Saving metadata never starts a worker or grants pairing/radio consent.
        const adopted = {...p, enable: true, ble_address: target.ble_address, ble_address_type: target.ble_address_type, discovery_revision: this.state.discovery.revision};
        if (at < 0) entries.push(adopted); else entries[at] = adopted;
        try {
            await this.sendSave("tesla_vehicle/profiles", {entries});
            // Never apply an asynchronous A response into a newly selected B.
            if (!this.disposed && context === this.searchContext() && revision === this.revision) {
                this.setState({entries, adopted: true, edit_binding: false}); this.setDirty(false);
            }
        } catch (error) { if (!this.disposed && context === this.searchContext() && revision === this.revision) this.setState({command_error: this.commandFailure(error)}); }
    }
    override async sendSave(topic: "tesla_vehicle/profiles", cfg: TeslaConfig) {
        // Saving permission/configuration is local, including while global BT
        // is off. Keep busy/lifetime guards without requiring the radio gate.
        const s = this.state.tesla_state;
        if (this.disposed || !API.get("tesla_vehicle/profiles") || this.saving || this.command_in_progress ||
            this.searchBusy() || s?.legacy_blocked || s?.action_busy || s?.pairing_pending || s?.stopping)
            throw new Error("Tesla configuration unavailable");
        this.saving = true; this.setState({command_busy: true});
        this.pending_save = JSON.stringify(cfg);
        // This component owns the canonical error region. Supplying API's
        // error callback would also create a duplicate global add_alert banner.
        try { await API.save(topic, cfg); }
        finally { this.saving = false; this.pending_save = ""; if (!this.disposed) this.setState({command_busy: false}); }
    }

    // First failing gate is always displayed beside the disabled action.
    private actionReason(poll: boolean) {
        const p = this.profile(), s = this.profileStatus(), global = this.state.tesla_state;
        if (!teslaBluetoothReady(API.get("bluetooth/state"))) return __("tesla_vehicle.content.bluetooth_blocked");
        if (global?.legacy_blocked) return __("tesla_vehicle.content.legacy_blocked");
        if (this.saving || this.command_in_progress || this.searchBusy() || global?.action_busy || global?.pairing_pending) return __("tesla_vehicle.content.action_busy");
        if (global?.stopping || global?.restart_required) return __("tesla_vehicle.status.restart_required");
        if (p.ev_index < 0) return __("tesla_vehicle.content.select_vehicle");
        if (this.configurationDirty()) return __("tesla_vehicle.content.save_first");
        if (!p.enable) return __("tesla_vehicle.content.enable_first");
        if (!teslaVehicleReady(p.ev_index, this.state.ev_state) || p.ev_mac !== this.state.ev_state?.mac || API.get("ev/config")?.evs[p.ev_index]?.mac !== p.ev_mac)
            return __("tesla_vehicle.content.vehicle_not_ready");
        if (!p.ble_address || !p.discovery_revision) return __("tesla_vehicle.content.find_first");
        if (!this.state.guidance?.generation || !Number.isInteger(this.state.guidance.revision)) return __("tesla_vehicle.content.preparation_unavailable");
        if (s.key_presence === 'cleanup') return __("tesla_vehicle.status.cleanup_required");
        if (s.key_presence !== 'present' && s.key_presence !== 'absent') return __("tesla_vehicle.status.key_unknown");
        if (poll && !s.key_present) return __("tesla_vehicle.content.verify_requires_key");
        if (!poll && s.paired) return __("tesla_vehicle.content.already_paired");
        return "";
    }
    private confirm(action: 'search' | 'pair' | 'poll' | 'unpair' | 'cleanup') {
        const name = API.get("ev/config")?.evs[this.state.edit_index]?.name ?? '';
        const deleting = action === 'unpair' || action === 'cleanup';
        const title = action === 'search' ? () => __("tesla_vehicle.content.search") : action === 'pair' ? () => __("tesla_vehicle.content.pair") : action === 'unpair' ? () => __("tesla_vehicle.content.forget_title") : action === 'cleanup' ? () => __("tesla_vehicle.content.cleanup_title") : () => __("tesla_vehicle.content.read_soc");
        const body = action === 'search' ? () => __("tesla_vehicle.content.search_confirm") : action === 'pair' ? () => __("tesla_vehicle.content.pair_confirm") : deleting ? () => <Fragment>
            {action === 'cleanup' ? <p>{__("tesla_vehicle.content.cleanup_explanation")}</p> : undefined}
            <p>{__("tesla_vehicle.content.forget_local")} „{name}“ {__("tesla_vehicle.content.forget_local_end")}</p>
            <p>{__("tesla_vehicle.content.forget_vehicle")}</p>
            <p>{action === 'cleanup' ? __("tesla_vehicle.content.cleanup_again") : __("tesla_vehicle.content.forget_again")}</p>
        </Fragment> : () => __("tesla_vehicle.content.poll_confirm");
        return util.async_modal_ref.current.show({title, body, no_text: () => __("tesla_vehicle.content.cancel"), yes_text: action === 'unpair' ? () => __("tesla_vehicle.content.forget_key") : action === 'cleanup' ? () => __("tesla_vehicle.content.cleanup_key") : title, no_variant: 'secondary', yes_variant: deleting ? 'danger' : 'primary'});
    }
    private async command(action: 'pair' | 'poll' | 'unpair' | 'cleanup') {
        const deleting = action === 'unpair' || action === 'cleanup';
        if (this.command_in_progress || (deleting ? !this.deleteAllowed(action) : !!this.actionReason(action === 'poll'))) return;
        const revision = this.revision, index = this.state.edit_index;
        const selection_revision = this.selection_revision;
        const deletion_revision = this.deletion_revision;
        let saved = JSON.stringify(API.get("tesla_vehicle/profiles"));
        const evs = JSON.stringify(API.get("ev/config")), identity = this.vehicleIdentity();
        const generation = API.get("tesla_vehicle/guidance")?.generation;
        let backend_revision = API.get("tesla_vehicle/guidance")?.revision;
        const baseCurrent = () => !this.disposed && !this.isDirty() && revision === this.revision && index === this.state.edit_index &&
            (!deleting || deletion_revision === this.deletion_revision) &&
            evs === JSON.stringify(API.get("ev/config")) && identity === this.vehicleIdentity() &&
            generation === API.get("tesla_vehicle/guidance")?.generation && teslaBluetoothReady(API.get("bluetooth/state")) &&
            !this.state.tesla_state?.restart_required && !this.state.tesla_state?.stopping;
        const current = () => baseCurrent() && saved === JSON.stringify(API.get("tesla_vehicle/profiles")) &&
            backend_revision === API.get("tesla_vehicle/guidance")?.revision && Number.isInteger(backend_revision) && !this.configurationDirty();
        const pause = async () => {
            await new Promise<void>(resolve => { this.cancelWait = resolve; this.timer = setTimeout(resolve, 50); });
            this.cancelWait = undefined;
        };
        this.command_in_progress = true; this.setState({command_busy: true, command_error: false, search_error: false});
        try {
            if (!await this.confirm(action) || !current() || !this.idle()) return;
            if (deleting) {
                if (!this.deleteAllowed(action)) return;
                const request = Array.from(crypto.getRandomValues(new Uint8Array(16)), b => ('0' + b.toString(16)).slice(-2)).join('');
                await API.call("tesla_vehicle/guided", {phase: action, request, generation, revision: backend_revision, profile: {...this.profile()}});
                return; // Accepted local cleanup is not a Tesla-side revocation.
            }
            // Radio discovery belongs ONLY to this explicitly confirmed action.
            // A saved address is permission metadata, never fresh transport proof.
            if (!API.get("tesla_vehicle/guidance")?.profiles.find(p => p.ev_index === index)?.binding_fresh) {
                const p = {...this.profile()}, scan_revision = API.get("tesla_vehicle/discovery")?.revision ?? 0;
                const deadline = Date.now() + 22000;
                await this.requestDiscovery(p);
                let target: Target;
                while (current() && Date.now() < deadline) {
                    const d = API.get("tesla_vehicle/discovery");
                    if (d?.status === 'error') throw new Error(__("tesla_vehicle.content.refresh_failed") + (d.error ? ': ' + d.error : ''));
                    if (d?.status === 'complete' && !d.busy && d.revision > scan_revision) {
                        if (d.generation !== generation || d.ev_index !== index || d.ev_mac !== p.ev_mac || d.searched_vin !== p.vin ||
                            !this.discoveryFresh()) throw new Error(__("tesla_vehicle.content.result_expired"));
                        const matches = d.targets.filter(t => t.matches_vin);
                        if (d.truncated || matches.length > 1) throw new Error(__("tesla_vehicle.content.refresh_ambiguous"));
                        target = matches[0];
                        if (!target || !/^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test(target.ble_address) ||
                            /^(00:){5}00$|^(FF:){5}FF$/i.test(target.ble_address) || (target.ble_address_type !== 0 && target.ble_address_type !== 1))
                            throw new Error(__("tesla_vehicle.content.refresh_empty"));
                        const cfg = JSON.parse(saved) as TeslaConfig;
                        const at = cfg.entries.findIndex(e => e.ev_index === index);
                        cfg.entries[at] = {...p, ble_address: target.ble_address, ble_address_type: target.ble_address_type, discovery_revision: d.revision};
                        if (!current() || backend_revision >= 0xffffffff) return;
                        const expected = JSON.stringify(cfg), next_revision = backend_revision + 1;
                        this.pending_save = expected; this.owned_revision = next_revision;
                        await API.save("tesla_vehicle/profiles", cfg);
                        // Accept precisely our one config ACK in either topic order.
                        // Every other revision/config transition cancels consent.
                        const ack_deadline = Date.now() + 3000;
                        while (baseCurrent() && Date.now() < ack_deadline) {
                            const bytes = JSON.stringify(API.get("tesla_vehicle/profiles")), rev = API.get("tesla_vehicle/guidance")?.revision;
                            if ((bytes !== saved && bytes !== expected) || (rev !== backend_revision && rev !== next_revision)) break;
                            if (bytes === expected && rev === next_revision) {
                                saved = expected; backend_revision = next_revision;
                                this.setState({entries: cfg.entries}); this.setDirty(false); break;
                            }
                            await pause();
                        }
                        this.pending_save = ""; this.owned_revision = -1;
                        if (!current() || backend_revision !== next_revision) throw new Error(__("tesla_vehicle.content.refresh_changed"));
                        break;
                    }
                    await pause();
                }
                if (!current()) return;
                if (!target) throw new Error(__("tesla_vehicle.content.refresh_timeout"));
            }
            if (!current() || !this.idle()) return;
            const bytes = crypto.getRandomValues(new Uint8Array(16));
            const request = Array.from(bytes, b => ('0' + b.toString(16)).slice(-2)).join('');
            const payload = {request, generation, revision: backend_revision, profile: {...this.profile()}};
            const error_revision = this.backend_error_revision;
            this.pending_command = payload;
            await API.call("tesla_vehicle/guided", {...payload, phase: action === 'pair' ? 'prepare_pair' : 'prepare_poll'});
            const deadline = Date.now() + 12000;
            while (current() && Date.now() < deadline) {
                const g = API.get("tesla_vehicle/guidance"), s = API.get("tesla_vehicle/state");
                // A cached failure from the previous attempt (or another
                // profile) must not cancel a newly explicit retained-key retry.
                // A clear->same failure transition is new evidence, not dedup.
                const failure = s?.profile_ev_index === index && this.backend_error_revision !== error_revision ? s.last_error : '';
                if (s?.restart_required || s?.stopping || failure) throw new Error(failure || __("tesla_vehicle.content.preparation_failed"));
                if (g?.request === request && g.ready) {
                    if (!current()) return;
                    // A lost response is not permission to replay the commit.
                    await API.call("tesla_vehicle/guided", {...payload, phase: 'commit'});
                    this.pending_command = undefined;
                    return; // Accepted command is NOT displayed as authenticated pairing.
                }
                await pause();
            }
            if (current()) throw new Error(__("tesla_vehicle.content.preparation_timeout"));
        } catch (error) { if (!this.disposed && selection_revision === this.selection_revision && index === this.state.edit_index) this.setState({command_error: this.commandFailure(error)}); }
        finally {
            this.cancelCommand(); this.pending_save = ""; this.owned_revision = -1;
            clearTimeout(this.timer); this.cancelWait = undefined; this.command_in_progress = false;
            if (!this.disposed) this.setState({command_busy: false});
        }
    }
    private resetReason() {
        const s = this.profileStatus();
        const row = API.get("tesla_vehicle/guidance")?.profiles.find(p => p.ev_index === this.state.edit_index);
        if (s.paired || row?.paired) return __("tesla_vehicle.content.unpair_first");
        if (s.key_presence !== 'absent' && s.key_presence !== 'present') return __("tesla_vehicle.content.reset_key_unknown");
        return "";
    }
    private resetBinding = async () => {
        if (this.command_in_progress || !this.idle() || this.configurationDirty() || this.resetReason()) return;
        const revision=this.revision, credential=this.deletion_revision, index=this.state.edit_index;
        const selection_revision=this.selection_revision;
        const profile={...this.profile()}, saved=JSON.stringify(API.get("tesla_vehicle/profiles")), evs=JSON.stringify(API.get("ev/config"));
        const guidance=API.get("tesla_vehicle/guidance"), generation=guidance?.generation, backend_revision=guidance?.revision;
        const current=()=>!this.disposed && revision===this.revision && credential===this.deletion_revision && index===this.state.edit_index &&
            saved===JSON.stringify(API.get("tesla_vehicle/profiles")) && evs===JSON.stringify(API.get("ev/config")) &&
            generation===API.get("tesla_vehicle/guidance")?.generation && backend_revision===API.get("tesla_vehicle/guidance")?.revision && !this.resetReason();
        this.command_in_progress=true;this.setState({command_busy:true,command_error:false});
        try {
            if (!await util.async_modal_ref.current.show({title:()=>__("tesla_vehicle.content.edit_binding"), body:()=>__("tesla_vehicle.content.reset_confirm"),
                no_text:()=>__("tesla_vehicle.content.cancel"),yes_text:()=>__("tesla_vehicle.content.edit_binding"),no_variant:'secondary',yes_variant:'danger'}) || !current() || !this.idle()) return;
            const request=Array.from(crypto.getRandomValues(new Uint8Array(16)),b=>('0'+b.toString(16)).slice(-2)).join('');
            this.pending_save=JSON.stringify({entries:this.state.entries.map(p=>p.ev_index===index ? {...p,enable:false,ble_address:'',ble_address_type:0,discovery_revision:0} : p)});
            this.owned_revision=backend_revision+1;
            await API.call("tesla_vehicle/guided",{phase:'reset',request,generation,revision:backend_revision,profile});
            // Exact persisted API readback before returning to the existing edit/search workflow.
            const config=JSON.parse(await (await util.download('/tesla_vehicle/profiles',true,10000)).text()) as TeslaConfig;
            const p=config.entries.find(p=>p.ev_index===index);
            if (!p || p.ev_mac!==profile.ev_mac || p.vin!==profile.vin || p.enable || p.ble_address || p.ble_address_type || p.discovery_revision) throw new Error("Assignment reset readback failed");
            if (!this.disposed && revision===this.revision && index===this.state.edit_index) {
                this.search_context="";this.search_saved="";this.cancelWait?.();
                this.setState({entries:config.entries,adopted:false,edit_binding:true,details_open:false});this.setDirty(false);
            }
        } catch (error) {if(!this.disposed && selection_revision===this.selection_revision && index===this.state.edit_index)this.setState({command_error:this.commandFailure(error)});}
        finally {this.pending_save="";this.owned_revision=-1;this.command_in_progress=false;if(!this.disposed)this.setState({command_busy:false});}
    };
    private deleteAllowed(action: 'unpair' | 'cleanup') {
        const s = this.profileStatus(), g = this.state.guidance;
        return this.idle() && !this.configurationDirty() && this.profile().ev_index >= 0 &&
            !!g?.generation && Number.isInteger(g.revision) &&
            (s.key_presence === 'present' || s.key_presence === 'cleanup') &&
            (action === 'unpair' ? s.paired === true && s.key_presence === 'present' : s.paired === false);
    }
    private errorText(text: string) { return text.trim().replace(/\s+/g, ' '); }
    private errors() {
        const local = this.state.command_error, search = this.state.search_error, d = this.state.discovery, p = this.profile();
        const discovery = d?.status === 'error' && d.ev_index === p.ev_index && d.ev_mac === p.ev_mac && d.searched_vin === p.vin ? d.error || __("tesla_vehicle.content.search_failed") : '';
        const messages = [typeof local === 'string' ? local : local ? __("tesla_vehicle.content.preparation_failed") : '',
            typeof search === 'string' ? search : search ? __("tesla_vehicle.content.search_failed") : '', discovery, this.profileStatus()?.last_error || ''];
        return Array.from(new Set(messages.map(text => this.errorText(text)).filter(Boolean)));
    }

    render(props: {}, state: TeslaConfig & View) {
        // Own the complete extension boundary here: separator, heading and body.
        // Unknown config fails closed; no wrapper is emitted by Fahrzeuge.
        if (!util.render_allowed() || API.get("bluetooth/config")?.enable !== true) return null;
        if (!API.get("tesla_vehicle/profiles")) return <Fragment><FormSeparator /><h2>{__("tesla_vehicle.bluetooth.tesla_title")}</h2></Fragment>;
        const p = this.profile(), status = this.profileStatus(), evs = API.get("ev/config")?.evs ?? [];
        const items: [string, string][] = [["-1", __("tesla_vehicle.content.select_vehicle")], ...evs.map((ev, index): [string, string] => [String(index), ev.name])];
        const pairReason = this.actionReason(false), pollReason = this.actionReason(true);
        const keyText = {
            paired: __("tesla_vehicle.status.paired"), pending: __("tesla_vehicle.status.pending"), sending: __("tesla_vehicle.status.sending"),
            key_unknown: __("tesla_vehicle.status.key_unknown"), key_only: __("tesla_vehicle.status.key_only"),
            cleanup_required: __("tesla_vehicle.status.cleanup_required"), not_paired: __("tesla_vehicle.status.not_paired"),
        }[teslaKeyStatus(status)];
        const assigned = !!p.ble_address && !!p.discovery_revision;
        const prerequisiteReasons = Array.from(new Set([
            (!assigned || state.edit_binding) && !teslaBluetoothReady(API.get("bluetooth/state")) ? __("tesla_vehicle.content.bluetooth_blocked") : '',
            assigned && !status.paired ? pairReason : '',
            status.key_present || status.paired ? pollReason : '',
        ].map(reason => this.errorText(reason)).filter(Boolean)));
        const target = this.matchingTargets()[0];
        const setup = !assigned || state.edit_binding ? <Fragment>
            <FormRow label={__("tesla_vehicle.content.vin")} help={__("tesla_vehicle.content.vin_help")}>
                <InputText value={p.vin} maxLength={17} disabled={state.command_busy || this.searchBusy() || !this.vinEditable()} onValue={vin => this.setProfile({vin})} />
            </FormRow>
            {!this.vinEditable() ? <p role="status" class="text-muted">{status.paired ? __("tesla_vehicle.content.unpair_first") : status.key_presence === 'present' ? __("tesla_vehicle.content.retained_key_binding") : __("tesla_vehicle.content.reset_key_unknown")}</p> : undefined}
            <FormRow help={__("tesla_vehicle.content.search_help")}>
                <Button type="button" className="w-100" variant="outline-primary" disabled={state.command_busy || !this.searchAllowed()} onClick={this.search}>
                    {this.searchBusy() ? __("tesla_vehicle.content.searching") : __("tesla_vehicle.content.search")}
                </Button>
            </FormRow>

            {!/^[A-HJ-NPR-Z0-9]{17}$/.test(p.vin) ? <p class="text-muted">{__("tesla_vehicle.content.vin_help")}</p> : undefined}

            {(state.discovery?.status === 'complete' || this.discovery_result_seen) && !this.searchBusy() && !state.adopted && this.search_context === this.searchContext() ? target ?
                <Alert variant="success">
                    <h4 class="h5">{__("tesla_vehicle.content.match_found")}</h4>
                    <dl class="mb-3">
                        <div class="d-flex flex-wrap justify-content-between gap-2 my-2"><dt class="fw-normal">{__("tesla_vehicle.content.match_vin")}</dt><dd class="mb-0 text-end text-break">{'…' + p.vin.slice(-6)}</dd></div>
                        <div class="d-flex flex-wrap justify-content-between gap-2 my-2"><dt class="fw-normal">{__("tesla_vehicle.content.match_address")}</dt><dd class="mb-0 text-end text-break">{target.ble_address}</dd></div>
                        <div class="d-flex flex-wrap justify-content-between gap-2 my-2"><dt class="fw-normal">{__("tesla_vehicle.content.found")}</dt><dd class="mb-0 text-end">{this.discoveryAgeText()}</dd></div>
                    </dl>
                    <Button type="button" className="w-100" disabled={state.command_busy || !this.searchAllowed()} onClick={() => this.selectTarget(target)}>{__("tesla_vehicle.content.select_target")}</Button>
                    <p class="mt-3 mb-0">{__("tesla_vehicle.content.adopt_help")}</p>
                    {state.discovery.result_ttl_ms === 120000 ? <p class="small mt-2 mb-0">{__("tesla_vehicle.content.result_lifetime")}</p> : undefined}
                </Alert>
                : <Alert variant="info">{!this.discoveryFresh() || state.discovery?.status !== 'complete' ? __("tesla_vehicle.content.result_expired") : __("tesla_vehicle.content.search_empty")}</Alert> : undefined}
        </Fragment> : undefined;
        const commonSoc = state.ev_state?.soc;
        const currentSoc = state.ev_state?.active_ev_index === p.ev_index && state.ev_state?.mac === p.ev_mac &&
            state.tesla_state?.profile_ev_index === p.ev_index && Number.isFinite(commonSoc) && Number.isFinite(state.tesla_state?.last_soc) && state.tesla_state.last_update > 0;
        return <Fragment>
            <FormSeparator /><h2>{__("tesla_vehicle.bluetooth.tesla_title")}</h2>
            <FormRow label={__("tesla_vehicle.content.vehicle_profile")} help={__("tesla_vehicle.content.vehicle_profile_help")}>
                <InputSelect items={items} value={String(state.edit_index)} onValue={this.selectProfile} />
            </FormRow>
            <FormRow label={__("tesla_vehicle.status.pairing")}><div class="form-control-plaintext" role="status">{keyText}</div></FormRow>
            {setup}

            {assigned && !status.paired ? <FormRow label={__("tesla_vehicle.content.vehicle")}><div class="form-control-plaintext text-success" role="status">{__("tesla_vehicle.content.assigned")}</div></FormRow> : undefined}
            {assigned && !status.paired ? <Fragment>
                <FormRow><Button type="button" className="w-100" disabled={!!pairReason} onClick={() => this.command('pair')}>{__("tesla_vehicle.content.pair")}</Button></FormRow>
                <p class="text-muted">{__("tesla_vehicle.content.pair_help")}</p>

            </Fragment> : undefined}
            {status.paired ? <FormRow><Button type="button" variant="danger" className="w-100" disabled={state.command_busy || !this.deleteAllowed('unpair')} onClick={() => this.command('unpair')}>{__("tesla_vehicle.content.forget_key")}</Button></FormRow> : undefined}
            {status.key_present || status.paired ? <Fragment>

            <FormRow help={__("tesla_vehicle.content.manual_wake_help")}>
                <Button type="button" className="w-100" variant="primary" disabled={!!pollReason} onClick={() => this.command('poll')}>{__("tesla_vehicle.content.read_soc")}</Button>
            </FormRow>

            {currentSoc ? <FormRow label={__("tesla_vehicle.status.soc")}><InputText readonly value={util.toLocaleFixed(commonSoc, 1) + ' %'} /></FormRow> : <p role="status" class="text-muted">{__("tesla_vehicle.content.soc_unavailable")}</p>}
            {currentSoc ? <FormRow label={__("tesla_vehicle.status.soc_time")}><InputText readonly value={util.format_timespan_ms(state.tesla_state.last_update)} /></FormRow> : undefined}
            </Fragment> : undefined}
            {prerequisiteReasons.map(reason => <p key={reason} role="status" class="text-muted">{reason}</p>)}
            {this.errors().length ? <Alert variant="danger">{this.errors().map(message => <div key={message}>{message}</div>)}</Alert> : undefined}
            <h3 class="mt-3">{__("tesla_vehicle.content.details")}</h3>
            <FormRow labelColClasses="d-none" contentColClasses="col-12"><Button type="button" variant="primary" className="w-100" aria-expanded={state.details_open} aria-controls="tesla_vehicle_details" onClick={() => this.setState({details_open: !state.details_open})}>{state.details_open ? __("tesla_vehicle.content.hide_details") : __("tesla_vehicle.content.show_details")}</Button></FormRow>
            {state.details_open ? <div id="tesla_vehicle_details">
                <FormRow label={__("tesla_vehicle.content.vin")}><InputText readonly value={p.vin || '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.ble_address")}><InputText readonly value={p.ble_address || '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.loaded_profile")}><InputText readonly value={state.tesla_state?.running ? evs[state.tesla_state.profile_ev_index]?.name ?? '–' : '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.active_profile")}><InputText readonly value={evs[state.ev_state?.active_ev_index]?.name ?? '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.status.ble")}><InputText readonly value={status.connected ? __("tesla_vehicle.status.connected") : __("tesla_vehicle.status.disconnected")} /></FormRow>
                <p class="text-muted">{__("tesla_vehicle.content.key_connection_help")}</p>
                <p class="text-muted">{__("tesla_vehicle.content.shared_soc")}</p>
                {assigned ? <FormRow><Button type="button" variant="danger" className="w-100" disabled={state.command_busy || !this.idle() || this.configurationDirty() || !!this.resetReason()} onClick={this.resetBinding}>{__("tesla_vehicle.content.edit_binding")}</Button></FormRow> : undefined}
                {assigned && this.resetReason() ? <p role="status" class="text-muted">{this.resetReason()}</p> : undefined}
                {!status.paired && (status.key_presence === 'present' || status.key_presence === 'cleanup') ? <FormRow><Button type="button" variant="outline-danger" className="w-100" disabled={state.command_busy || !this.deleteAllowed('cleanup')} onClick={() => this.command('cleanup')}>{__("tesla_vehicle.content.cleanup_key")}</Button></FormRow> : undefined}
            </div> : undefined}
        </Fragment>;
    }
}
