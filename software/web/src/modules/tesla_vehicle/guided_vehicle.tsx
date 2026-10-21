import { h, Fragment } from "preact";
import { Alert, Button, Table } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { Switch } from "../../ts/components/switch";
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
    search_error: boolean;
    command_error: boolean;
    adopted: boolean;
}

// Inline extension of Fahrzeuge, not a second registry or a standalone page.
export class TeslaVehicle extends ConfigComponent<"tesla_vehicle/profiles", {}, View> {
    private command_in_progress = false;
    private saving = false;
    private disposed = false;
    private revision = 0;
    private listeners = new AbortController();
    private scan_waiting = false;
    private search_context = "";
    private search_saved = "";
    private discovery_received = 0;
    private discovery_revision = -1;
    private identity = "";
    private ev_config = "";
    private saved_config = "";
    private pending_save = "";
    private timer: ReturnType<typeof setTimeout>;
    private cancelWait: () => void;

    constructor() {
        super("tesla_vehicle/profiles", () => __("tesla_vehicle.script.save_failed"), undefined, {
            tesla_state: API.get("tesla_vehicle/state"), ev_state: API.get("ev/state"),
            discovery: API.get("tesla_vehicle/discovery"), guidance: API.get("tesla_vehicle/guidance"),
            edit_index: API.get("ev/config")?.evs.length ? 0 : -1,
            command_busy: false, search_error: false, command_error: false, adopted: false,
        });
        this.identity = this.vehicleIdentity();
        this.ev_config = JSON.stringify(API.get("ev/config"));
        this.saved_config = JSON.stringify(API.get("tesla_vehicle/profiles"));
        const listen = (topic: keyof API.EventMap, callback: () => void) =>
            util.addApiEventListener(topic, () => { if (!this.disposed) callback(); }, {signal: this.listeners.signal});
        listen("tesla_vehicle/state", () => this.setState({tesla_state: API.get("tesla_vehicle/state")}));
        listen("tesla_vehicle/guidance", () => this.setState({guidance: API.get("tesla_vehicle/guidance")}));
        listen("tesla_vehicle/discovery", () => {
            const discovery = API.get("tesla_vehicle/discovery");
            if (discovery && !discovery.busy && discovery.status !== 'searching') this.scan_waiting = false;
            if (discovery?.revision !== this.discovery_revision) {
                this.discovery_revision = discovery?.revision;
                this.discovery_received = Date.now();
            }
            this.setState({discovery});
        });
        listen("ev/state", () => {
            const identity = this.vehicleIdentity();
            if (identity !== this.identity) { ++this.revision; this.identity = identity; }
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
                if (config !== this.pending_save) ++this.revision;
                this.saved_config = config;
            }
            this.setState({});
        });
        listen("bluetooth/state", () => this.setState({}));
    }

    override componentWillUnmount() {
        this.disposed = true; ++this.revision;
        this.listeners.abort(); clearTimeout(this.timer); this.cancelWait?.();
        // No network/action at unmount. An uncommitted backend lease expires.
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
        ++this.revision; this.search_context = ""; this.cancelWait?.();
        this.setState({edit_index: parseInt(value), adopted: false, command_error: false, search_error: false});
    };
    private setProfile = (patch: Partial<Profile>) => {
        const profile = this.profile();
        if (this.disposed || profile.ev_index < 0 || !profile.ev_mac || this.command_in_progress || this.saving || this.searchBusy()) return;
        if (patch.vin !== undefined && patch.vin !== profile.vin) {
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
        return {...status, running: false, connected: false, paired: saved?.paired ?? false,
            key_present: saved?.key_presence === 'present', key_presence: saved?.key_presence ?? 'unknown' as const,
            pairing_pending: false, pair_request_sent: false};
    }
    private searchBusy() { return this.scan_waiting || !!this.state.discovery?.busy; }
    private idle() {
        const s = this.state.tesla_state;
        return !this.disposed && !!API.get("tesla_vehicle/profiles") && teslaBluetoothReady(API.get("bluetooth/state")) &&
            !!s && !s.legacy_blocked && !s.action_busy && !s.pairing_pending && !s.stopping && !s.restart_required && !this.saving && !this.searchBusy();
    }
    private searchAllowed() { return this.idle() && this.state.edit_index >= 0 && /^[A-HJ-NPR-Z0-9]{17}$/.test(this.profile().vin); }
    private matchingTargets() {
        const d = this.state.discovery;
        if (this.search_context !== this.searchContext() || this.search_saved !== JSON.stringify(API.get("tesla_vehicle/profiles")) ||
            d?.ev_index !== this.state.edit_index || d.status !== 'complete' || d.busy || d.truncated || Date.now() - this.discovery_received >= 110000) return [];
        const matches = d.targets.filter(t => t.matches_vin);
        return matches.length === 1 ? matches : [];
    }
    private search = async () => {
        if (this.command_in_progress || !this.searchAllowed()) return;
        const context = this.searchContext(), revision = this.revision;
        this.command_in_progress = true; this.setState({command_busy: true, search_error: false, adopted: false});
        try {
            if (!await this.confirm('search')) return;
            if (!this.searchAllowed() || context !== this.searchContext() || revision !== this.revision) return;
            this.scan_waiting = true; this.search_context = context;
            this.search_saved = JSON.stringify(API.get("tesla_vehicle/profiles"));
            const p = this.profile();
            await API.call("tesla_vehicle/scan", {vin: p.vin, ev_index: p.ev_index, ev_mac: p.ev_mac}, () => __("tesla_vehicle.content.search_failed"));
        } catch {
            this.scan_waiting = false; this.search_context = "";
            if (!this.disposed) this.setState({search_error: true});
        } finally {
            this.command_in_progress = false; if (!this.disposed) this.setState({command_busy: false});
        }
    };
    private async selectTarget(target: Target) {
        if (this.command_in_progress || !this.searchAllowed() || this.matchingTargets()[0] !== target) return;
        const p = this.profile(), context = this.searchContext(), revision = this.revision;

        const entries = (this.state.entries ?? []).map(e => ({...e}));
        const at = entries.findIndex(e => e.ev_index === p.ev_index);
        const adopted = {...p, ble_address: target.ble_address, ble_address_type: target.ble_address_type, discovery_revision: this.state.discovery.revision};
        if (at < 0) entries.push(adopted); else entries[at] = adopted;
        try {
            await this.sendSave("tesla_vehicle/profiles", {entries});
            // Never apply an asynchronous A response into a newly selected B.
            if (!this.disposed && context === this.searchContext() && revision === this.revision) {
                this.setState({entries, adopted: true}); this.setDirty(false);
            }
        } catch { if (!this.disposed) this.setState({command_error: true}); }
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
        try { await API.save(topic, cfg, () => __("tesla_vehicle.script.save_failed")); }
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
        if (!this.state.guidance?.profiles.find(e => e.ev_index === p.ev_index)?.binding_fresh) return __("tesla_vehicle.content.binding_saved");
        if (!this.state.guidance?.generation) return __("tesla_vehicle.content.preparation_unavailable");
        if (s.key_presence === 'cleanup') return __("tesla_vehicle.status.cleanup_required");
        if (s.key_presence === 'unknown') return __("tesla_vehicle.status.key_unknown");
        if (poll && !s.key_present) return __("tesla_vehicle.content.verify_requires_key");
        if (!poll && s.paired) return __("tesla_vehicle.content.already_paired");
        return "";
    }
    private confirm(action: 'search' | 'pair' | 'poll' | 'delete') {
        const title = action === 'search' ? () => __("tesla_vehicle.content.search") : action === 'pair' ? () => __("tesla_vehicle.content.pair") : action === 'delete' ? () => __("tesla_vehicle.content.forget_key") : () => __("tesla_vehicle.content.read_soc");
        const body = action === 'search' ? () => __("tesla_vehicle.content.search_confirm") : action === 'pair' ? () => __("tesla_vehicle.content.pair_confirm") : action === 'delete' ? () => __("tesla_vehicle.content.forget_confirm") : () => __("tesla_vehicle.content.poll_confirm");
        return util.async_modal_ref.current.show({title, body, no_text: () => __("tesla_vehicle.content.cancel"), yes_text: title, no_variant: 'secondary', yes_variant: action === 'delete' ? 'danger' : 'primary'});
    }
    private async command(action: 'pair' | 'poll' | 'delete') {
        if (this.command_in_progress || (action === 'delete' ? !this.deleteAllowed() : !!this.actionReason(action === 'poll'))) return;
        const revision = this.revision, index = this.state.edit_index;
        const saved = JSON.stringify(API.get("tesla_vehicle/profiles")), evs = JSON.stringify(API.get("ev/config"));
        const generation = API.get("tesla_vehicle/guidance")?.generation;
        const current = () => !this.disposed && revision === this.revision && index === this.state.edit_index &&
            saved === JSON.stringify(API.get("tesla_vehicle/profiles")) && evs === JSON.stringify(API.get("ev/config")) &&
            generation === API.get("tesla_vehicle/guidance")?.generation && !this.configurationDirty() && teslaBluetoothReady(API.get("bluetooth/state"));
        this.command_in_progress = true; this.setState({command_busy: true, command_error: false});
        try {
            if (!await this.confirm(action) || !current() || !this.idle()) return;
            if (action === 'delete') {
                await API.call("tesla_vehicle/forget_key", {ev_index: index}, () => __("tesla_vehicle.script.command_failed")); return;
            }
            const bytes = crypto.getRandomValues(new Uint8Array(16));
            const request = Array.from(bytes, b => ('0' + b.toString(16)).slice(-2)).join('');
            const payload = {request, generation, profile: {...this.profile()}};
            await API.call("tesla_vehicle/guided", {...payload, phase: action === 'pair' ? 'prepare_pair' : 'prepare_poll'}, () => __("tesla_vehicle.script.command_failed"));
            const deadline = Date.now() + 12000;
            // Wait for the exact backend worker ACK using cached API events.
            // No network polling, implicit scan, connect or pairing in this loop.
            while (current() && Date.now() < deadline) {
                const g = API.get("tesla_vehicle/guidance"), s = API.get("tesla_vehicle/state");
                if (s?.restart_required || s?.stopping || s?.last_error) throw new Error("Preparation refused");
                if (g?.request === request && g.ready) {
                    if (!current()) return;
                    await API.call("tesla_vehicle/guided", {...payload, phase: 'commit'}, () => __("tesla_vehicle.script.command_failed"));
                    return;
                }
                await new Promise<void>(resolve => { this.cancelWait = resolve; this.timer = setTimeout(resolve, 50); });
                this.cancelWait = undefined;
            }
            if (!this.disposed) this.setState({command_error: true});
        } catch { if (!this.disposed) this.setState({command_error: true}); }
        finally {
            clearTimeout(this.timer); this.cancelWait = undefined; this.command_in_progress = false;
            if (!this.disposed) this.setState({command_busy: false});
        }
    }
    private deleteAllowed() { return this.idle() && !this.configurationDirty() && this.profile().ev_index >= 0; }

    render(props: {}, state: TeslaConfig & View) {
        if (!util.render_allowed()) return null;
        if (!API.get("tesla_vehicle/profiles")) return <Fragment><FormSeparator /><h2>{__("tesla_vehicle.bluetooth.tesla_title")}</h2><Alert variant="info">{__("tesla_vehicle.content.off_help")}</Alert></Fragment>;
        const p = this.profile(), status = this.profileStatus(), evs = API.get("ev/config")?.evs ?? [];
        const items: [string, string][] = [["-1", __("tesla_vehicle.content.select_vehicle")], ...evs.map((ev, index): [string, string] => [String(index), ev.name])];
        const pairReason = this.actionReason(false), pollReason = this.actionReason(true);
        const keyText = {
            paired: __("tesla_vehicle.status.paired"), pending: __("tesla_vehicle.status.pending"), sending: __("tesla_vehicle.status.sending"),
            key_unknown: __("tesla_vehicle.status.key_unknown"), key_only: __("tesla_vehicle.status.key_only"),
            cleanup_required: __("tesla_vehicle.status.cleanup_required"), not_paired: __("tesla_vehicle.status.not_paired"),
        }[teslaKeyStatus(status)];
        const setup = <Fragment>
            <ConfigForm small id="tesla_vehicle_config_form" title={__("tesla_vehicle.content.step_permission")} isDirty={this.isDirty()} onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.content.enable")} help={__("tesla_vehicle.content.enable_desc")}>
                    <Switch checked={p.enable} disabled={state.command_busy || this.searchBusy()} onClick={() => this.setProfile({enable: !p.enable})} />
                </FormRow>
            </ConfigForm>
            <h3 class="h5 mt-3">{__("tesla_vehicle.content.step_search")}</h3>
            <FormRow label={__("tesla_vehicle.content.vin")} help={__("tesla_vehicle.content.vin_help")}>
                <InputText value={p.vin} maxLength={17} onValue={vin => this.setProfile({vin})} />
            </FormRow>
            <FormRow help={__("tesla_vehicle.content.search_help")}>
                <Button type="button" className="w-100" variant="secondary" disabled={state.command_busy || !this.searchAllowed()} onClick={this.search}>
                    {this.searchBusy() ? __("tesla_vehicle.content.searching") : __("tesla_vehicle.content.search")}
                </Button>
            </FormRow>
            {!teslaBluetoothReady(API.get("bluetooth/state")) ? <p class="text-warning">{__("tesla_vehicle.content.bluetooth_blocked")}</p> : undefined}
            {!/^[A-HJ-NPR-Z0-9]{17}$/.test(p.vin) ? <p class="text-muted">{__("tesla_vehicle.content.vin_help")}</p> : undefined}
            {state.search_error || state.discovery?.status === 'error' ? <Alert variant="danger">{__("tesla_vehicle.content.search_failed")}</Alert> : undefined}
            {state.discovery?.status === 'complete' && !this.searchBusy() && !state.adopted && this.search_context === this.searchContext() ? this.matchingTargets().length === 1 ?
                <Table responsive size="sm"><tbody>{this.matchingTargets().map(t => <tr key={t.ble_address}><td>{t.name}<div class="text-success">{__("tesla_vehicle.content.matching_vin")}</div><small>{t.ble_address} · {t.rssi} dBm</small></td>
                    <td><Button type="button" disabled={state.command_busy || !this.searchAllowed()} onClick={() => this.selectTarget(t)}>{__("tesla_vehicle.content.select_target")}</Button></td></tr>)}</tbody></Table>
                : <Alert variant="info">{__("tesla_vehicle.content.search_empty")}</Alert> : undefined}
            {state.adopted ? <Alert variant="success" role="status">{__("tesla_vehicle.content.adopted")}</Alert> : p.ble_address ? <p class="text-muted">{__("tesla_vehicle.content.binding_saved")}</p> : undefined}
            <h3 class="h5 mt-3">{__("tesla_vehicle.content.step_pair")}</h3>
            <p class="text-muted">{__("tesla_vehicle.content.pair_help")}</p>
            <FormRow>
                <Button type="button" className="w-100" disabled={!!pairReason} onClick={() => this.command('pair')}>{__("tesla_vehicle.content.pair")}</Button>
            </FormRow>
            {pairReason ? <p role="status" class="text-muted">{pairReason}</p> : undefined}
        </Fragment>;
        const commonSoc = state.ev_state?.soc;
        const currentSoc = state.ev_state?.active_ev_index === p.ev_index && state.ev_state?.mac === p.ev_mac &&
            state.tesla_state?.profile_ev_index === p.ev_index && Number.isFinite(commonSoc) && Number.isFinite(state.tesla_state?.last_soc) && state.tesla_state.last_update > 0;
        return <Fragment>
            <FormSeparator /><h2>{__("tesla_vehicle.bluetooth.tesla_title")}</h2>
            <FormRow label={__("tesla_vehicle.content.vehicle_profile")} help={__("tesla_vehicle.content.vehicle_profile_help")}>
                <InputSelect items={items} value={String(state.edit_index)} onValue={this.selectProfile} />
            </FormRow>
            <FormRow label={__("tesla_vehicle.status.pairing")}><div class="form-control-plaintext" role="status">{keyText}</div></FormRow>
            {status.paired ? <details class="mb-3"><summary>{__("tesla_vehicle.content.manage")}</summary>{setup}</details> : setup}
            {status.key_present || status.paired ? <Fragment>
            <h3 class="h5 mt-3">{__("tesla_vehicle.content.step_soc")}</h3>
            <FormRow help={__("tesla_vehicle.content.manual_wake_help")}>
                <Button type="button" className="w-100" variant="secondary" disabled={!!pollReason} onClick={() => this.command('poll')}>{__("tesla_vehicle.content.read_soc")}</Button>
            </FormRow>
            {pollReason ? <p role="status" class="text-muted">{pollReason}</p> : undefined}
            {currentSoc ? <FormRow label={__("tesla_vehicle.status.soc")}><InputText value={util.toLocaleFixed(commonSoc, 1) + ' %'} /></FormRow> : <p role="status" class="text-muted">{__("tesla_vehicle.content.soc_unavailable")}</p>}
            {currentSoc ? <FormRow label={__("tesla_vehicle.status.soc_time")}><InputText value={util.format_timespan_ms(state.tesla_state.last_update)} /></FormRow> : undefined}
            <p class="text-muted">{__("tesla_vehicle.content.shared_soc")}</p>
            </Fragment> : undefined}
            {state.command_error ? <Alert variant="danger">{__("tesla_vehicle.content.preparation_failed")}</Alert> : undefined}
            {state.tesla_state?.last_error ? <Alert variant="danger">{state.tesla_state.last_error}</Alert> : undefined}
            <details class="mt-3"><summary>{__("tesla_vehicle.content.advanced")}</summary>
                <FormRow label={__("tesla_vehicle.content.ble_address")}><InputText value={p.ble_address || '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.address_type")}><InputText value={p.ble_address_type === 1 ? __("tesla_vehicle.content.address_random") : __("tesla_vehicle.content.address_public")} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.loaded_profile")}><InputText value={evs[state.tesla_state?.profile_ev_index]?.name ?? '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.content.active_profile")}><InputText value={evs[state.ev_state?.active_ev_index]?.name ?? '–'} /></FormRow>
                <FormRow label={__("tesla_vehicle.status.ble")}><InputText value={status.connected ? __("tesla_vehicle.status.connected") : __("tesla_vehicle.status.disconnected")} /></FormRow>
                <Alert variant="info">{__("tesla_vehicle.content.revocation_help")}</Alert>
                <Button type="button" variant="danger" className="w-100" disabled={state.command_busy || !this.deleteAllowed()} onClick={() => this.command('delete')}>{__("tesla_vehicle.content.forget_key")}</Button>
            </details>
        </Fragment>;
    }
}
