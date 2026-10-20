import { h, Fragment } from "preact";
import { Bluetooth } from "react-feather";
import { Alert, Button, Table } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { PageHeader } from "../../ts/components/page_header";
import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { NavbarItem } from "../../ts/components/navbar_item";
import { SubPage } from "../../ts/components/sub_page";
import { Switch } from "../../ts/components/switch";
import { teslaActions, teslaKeyStatus, teslaVehicleReady, teslaStartAllowed, teslaBluetoothReady } from "./action_policy";
import { registerVehicleExtension } from "../ev/main";

export function BluetoothPageNavbar() {
    return <NavbarItem name="bluetooth" module="tesla_vehicle" title={__("tesla_vehicle.bluetooth.title")} symbol={<Bluetooth />} />;
}

export function BluetoothPage() {
    return <SubPage name="bluetooth" title={__("tesla_vehicle.bluetooth.title")}>
        <PageHeader title={__("tesla_vehicle.bluetooth.title")} />
        {util.render_allowed() ? <BluetoothSettings /> : undefined}
    </SubPage>;
}

interface BluetoothViewState {
    bluetooth_state: API.getType["bluetooth/state"];
    busy: boolean;
}

export class BluetoothSettings extends ConfigComponent<"bluetooth/config", {}, BluetoothViewState> {
    private restarting = false;
    constructor() {
        super("bluetooth/config", () => __("tesla_vehicle.bluetooth.save_failed"), undefined, {
            bluetooth_state: API.get("bluetooth/state"), busy: false,
        });
        util.addApiEventListener("bluetooth/state", () => this.setState({bluetooth_state: API.get("bluetooth/state")}));
    }
    override async sendSave(topic: "bluetooth/config", cfg: API.getType["bluetooth/config"]) {
        if (!this.state.bluetooth_state?.supported || this.restarting) throw new Error("Bluetooth unavailable");
        // No reboot argument: only a confirmed durable commit may show pending restart.
        await API.save(topic, cfg, () => __("tesla_vehicle.bluetooth.save_failed"));
    }
    private canRestart() {
        const s = this.state.bluetooth_state;
        return !!s?.supported && s.restart_required && !s.persistence_error && !this.isDirty();
    }
    private restart = async () => {
        if (this.restarting || !this.canRestart()) return;
        this.restarting = true; this.setState({busy: true});
        try {
            if (!await util.async_modal_ref.current.show({
                title: () => __("tesla_vehicle.bluetooth.restart"),
                body: () => __("tesla_vehicle.bluetooth.restart_confirm"),
                no_text: () => __("tesla_vehicle.content.cancel"),
                yes_text: () => __("tesla_vehicle.bluetooth.restart"),
                no_variant: "secondary", yes_variant: "danger",
            })) return;
            if (!this.canRestart()) return;
            await API.call("bluetooth/restart", {}, () => __("tesla_vehicle.bluetooth.save_failed"));
            util.postReboot(__("util.reboot_title"), __("util.reboot_text"));
        } finally {
            this.restarting = false; this.setState({busy: false});
        }
    };
    render(props: {}, state: API.getType["bluetooth/config"] & BluetoothViewState) {
        const s = state.bluetooth_state;
        const phases = {
            disabled: __("tesla_vehicle.bluetooth.disabled"), starting: __("tesla_vehicle.bluetooth.starting"),
            active: __("tesla_vehicle.bluetooth.active"), stopping: __("tesla_vehicle.bluetooth.stopping"),
            error: __("tesla_vehicle.bluetooth.error"),
        };
        return <Fragment>
            <Alert variant="info">{__("tesla_vehicle.bluetooth.help")}</Alert>
            {!s?.supported ? <Alert variant="warning">{__("tesla_vehicle.bluetooth.unsupported")} ({s?.unsupported_reason})</Alert> : undefined}
            <FormRow label={__("tesla_vehicle.bluetooth.runtime")}><InputText value={phases[s?.phase] ?? "–"} /></FormRow>
            {s?.persistence_error ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.save_failed")}</Alert> : undefined}
            <ConfigForm id="bluetooth_config_form" title={__("tesla_vehicle.bluetooth.enable")} isDirty={this.isDirty()} onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.bluetooth.enable")}>
                    <Switch checked={state.enable} desc={__("tesla_vehicle.bluetooth.desired")}
                            disabled={!s?.supported || state.busy} onClick={this.toggle("enable")} />
                </FormRow>
            </ConfigForm>
            {s?.supported && s.restart_required ? <Alert variant="warning">{__("tesla_vehicle.bluetooth.pending")}</Alert> : undefined}
            <Button variant="danger" disabled={!this.canRestart() || state.busy} onClick={this.restart}>
                {__("tesla_vehicle.bluetooth.restart")}
            </Button>
            {s?.phase === "active" ? <p>{__("tesla_vehicle.bluetooth.active_hint")}</p> : undefined}
        </Fragment>;
    }
}

type TeslaConfig = API.getType["tesla_vehicle/profiles"];
type TeslaProfile = TeslaConfig["entries"][number];
interface TeslaViewState {
    tesla_state: API.getType["tesla_vehicle/state"];
    ev_state: API.getType["ev/state"];
    command_busy: boolean;
    discovery: API.getType["tesla_vehicle/discovery"];
    search_error: boolean;
    edit_index: number;
}

// Inline vehicle-owned section, deliberately not a SubPage or a navbar item.
export class TeslaVehicle extends ConfigComponent<"tesla_vehicle/profiles", {}, TeslaViewState> {
    private command_in_progress = false;
    private saving = false;
    private vehicle_revision = 0;
    private vehicle_identity = "";
    private scan_waiting = false;
    private search_context = "";
    private searchContext() {
        const p = this.profile();
        return JSON.stringify([p.ev_index, p.ev_mac, p.vin]);
    }
    private matchingTargets() {
        const d = this.state.discovery;
        return this.search_context === this.searchContext() && d?.ev_index === this.state.edit_index
            ? d.targets.filter(t => t.matches_vin) : [];
    }

    private profile(): TeslaProfile {
        const index = this.state.edit_index;
        return this.state.entries?.find(p => p.ev_index === index) ?? {
            ev_index: index, ev_mac: API.get("ev/config")?.evs[index]?.mac ?? "",
            enable: false, vin: "", ble_address: "", ble_address_type: 0, discovery_revision: 0, poll_interval: 300, wake_once: false,
        };
    }
    private setProfile = (patch: Partial<TeslaProfile>) => {
        const profile = this.profile();
        if (profile.ev_index < 0 || !profile.ev_mac || this.command_in_progress || this.searchBusy()) return;
        if (patch.vin !== undefined && patch.vin !== profile.vin) {
            patch = {...patch, ble_address: "", ble_address_type: 0, discovery_revision: 0};
            this.search_context = "";
        }
        const entries = [...(this.state.entries ?? [])];
        const at = entries.findIndex(p => p.ev_index === profile.ev_index);
        if (at < 0) entries.push({...profile, ...patch}); else entries[at] = {...profile, ...patch};
        this.setState({entries}); this.setDirty(true);
    };
    private profileStatus() {
        const status = this.state.tesla_state;
        return status?.profile_ev_index === this.state.edit_index ? status : {
            ...status, running: false, connected: false, paired: false, key_present: false,
            key_presence: 'unknown' as const, pairing_pending: false, pair_request_sent: false,
        };
    }

    private searchBusy() { return this.scan_waiting || !!this.state.discovery?.busy; }
    private searchAllowed() {
        const s = this.state.tesla_state;
        return this.state.edit_index >= 0 && /^[A-HJ-NPR-Z0-9]{17}$/.test(this.profile().vin) && !s?.legacy_blocked && !!API.get("tesla_vehicle/profiles") && teslaBluetoothReady(API.get("bluetooth/state")) &&
            !!s && !s.action_busy && !s.pairing_pending && !s.stopping && !s.restart_required &&
            !this.saving && !this.searchBusy();
    }
    private search = async () => {
        if (this.command_in_progress || !this.searchAllowed()) return;
        const context = this.searchContext();
        this.command_in_progress = true; this.setState({command_busy: true, search_error: false});
        try {
            if (!await util.async_modal_ref.current.show({
                title: () => __("tesla_vehicle.content.search"),
                body: () => __("tesla_vehicle.content.search_confirm"),
                no_text: () => __("tesla_vehicle.content.cancel"), yes_text: () => __("tesla_vehicle.content.search"),
                no_variant: "secondary", yes_variant: "primary",
            })) return;
            if (!this.searchAllowed() || context !== this.searchContext()) return;
            this.scan_waiting = true;
            this.search_context = context;
            const p = this.profile();
            await API.call("tesla_vehicle/scan", {vin: p.vin, ev_index: p.ev_index, ev_mac: p.ev_mac}, () => __("tesla_vehicle.content.search_failed"));
            // Keep synchronous admission closed until the shared discovery state
            // reports terminal completion; HTTP acceptance is not scan completion.
        } catch (e) {
            this.scan_waiting = false; this.search_context = ""; this.setState({search_error: true});
        } finally {
            this.command_in_progress = false; this.setState({command_busy: false});
        }
    };
    private selectTarget(target: API.getType["tesla_vehicle/discovery"]["targets"][number]) {
        if (this.searchBusy() || this.command_in_progress || this.saving || !this.searchAllowed() ||
            this.state.discovery?.status !== 'complete' || !this.matchingTargets().some(current => current === target)) return;
        this.setProfile({ble_address: target.ble_address, ble_address_type: target.ble_address_type, discovery_revision: this.state.discovery.revision});
        this.setDirty(true); // Only the normal form save makes this persistent.
    }

    private vehicleIdentity() {
        const vehicle = API.get("ev/state");
        return `${vehicle?.active_ev_index}:${vehicle?.mac}`;
    }

    private configurationDirty() {
        const saved = API.get("tesla_vehicle/profiles");
        return this.isDirty() || !saved || JSON.stringify(saved.entries) !== JSON.stringify(this.state.entries);
    }

    override async sendSave(topic: "tesla_vehicle/profiles", cfg: TeslaConfig) {
        if (!API.get(topic) || this.saving || this.command_in_progress || this.searchBusy() || this.state.tesla_state?.legacy_blocked || this.state.tesla_state?.action_busy || this.state.tesla_state?.pairing_pending)
            throw new Error("Tesla action in progress or configuration unavailable");
        this.saving = true; this.setState({command_busy: true});
        try { await API.save(topic, cfg, () => __("tesla_vehicle.script.save_failed")); }
        finally { this.saving = false; this.setState({command_busy: false}); }
    }

    constructor() {
        super("tesla_vehicle/profiles", () => __("tesla_vehicle.script.save_failed"), undefined, {
            tesla_state: API.get("tesla_vehicle/state"), ev_state: API.get("ev/state"), command_busy: false,
            discovery: API.get("tesla_vehicle/discovery"), search_error: false, edit_index: API.get("ev/config")?.evs.length ? 0 : -1,
        });
        util.addApiEventListener("tesla_vehicle/state", () => this.setState({tesla_state: API.get("tesla_vehicle/state")}));
        util.addApiEventListener("tesla_vehicle/discovery", () => {
            const discovery = API.get("tesla_vehicle/discovery");
            if (discovery && !discovery.busy && discovery.status !== 'searching') this.scan_waiting = false;
            this.setState({discovery});
        });
        this.vehicle_identity = this.vehicleIdentity();
        util.addApiEventListener("ev/state", () => {
            const identity = this.vehicleIdentity();
            if (identity !== this.vehicle_identity) { ++this.vehicle_revision; this.vehicle_identity = identity; }
            this.setState({ev_state: API.get("ev/state")});
        });
        util.addApiEventListener("ev/config", () => this.setState({}));
        util.addApiEventListener("bluetooth/state", () => this.setState({}));
    }

    private commandAllowed(topic: "tesla_vehicle/start" | "tesla_vehicle/pair" | "tesla_vehicle/poll" | "tesla_vehicle/forget_key") {
        if (this.saving || this.searchBusy() || this.state.tesla_state?.legacy_blocked || !API.get("tesla_vehicle/profiles")) return false;
        const state = this.state;
        const profile = this.profile(), status = this.profileStatus();
        const ready = teslaVehicleReady(profile.ev_index, state.ev_state) && profile.ev_mac === state.ev_state?.mac;
        if (topic === "tesla_vehicle/start") return ready && teslaStartAllowed(profile.enable, status, API.get("bluetooth/state"), this.configurationDirty(), false);
        if (topic === "tesla_vehicle/forget_key") return profile.ev_index >= 0 && !this.configurationDirty() && !state.tesla_state?.action_busy &&
            !state.tesla_state?.pairing_pending && !state.tesla_state?.stopping && teslaBluetoothReady(API.get("bluetooth/state"));
        const actions = teslaActions(profile.enable, status, this.configurationDirty(), ready, false, API.get("bluetooth/state"));
        return !(topic === "tesla_vehicle/pair" ? actions.pairDisabled : topic === "tesla_vehicle/poll" ? actions.pollDisabled : actions.deleteDisabled);
    }

    private async command(topic: "tesla_vehicle/start" | "tesla_vehicle/pair" | "tesla_vehicle/poll" | "tesla_vehicle/forget_key") {
        // Synchronous guard covers double clicks before Preact rerenders.
        if (this.command_in_progress || !this.commandAllowed(topic)) return;
        const confirmedConfiguration = JSON.stringify(API.get("tesla_vehicle/profiles"));
        const confirmedIndex = this.state.edit_index;
        const confirmedProfiles = JSON.stringify(API.get("ev/config"));
        const confirmedIdentity = this.vehicleIdentity();
        const confirmedRevision = this.vehicle_revision;
        this.command_in_progress = true; this.setState({command_busy: true});
        try {
            const deletion = topic === "tesla_vehicle/forget_key";
            const pairing = topic === "tesla_vehicle/pair";
            const starting = topic === "tesla_vehicle/start";
            if (!await util.async_modal_ref.current.show({
                title: () => starting ? __("tesla_vehicle.content.start") : deletion ? __("tesla_vehicle.content.forget_key") : pairing ? __("tesla_vehicle.content.pair") : __("tesla_vehicle.content.read_soc"),
                body: () => starting ? __("tesla_vehicle.content.start_confirm") : deletion ? __("tesla_vehicle.content.forget_confirm") : pairing ? __("tesla_vehicle.content.pair_confirm") : __("tesla_vehicle.content.poll_confirm"),
                no_text: () => __("tesla_vehicle.content.cancel"),
                yes_text: () => starting ? __("tesla_vehicle.content.start") : deletion ? __("tesla_vehicle.content.forget_key") : pairing ? __("tesla_vehicle.content.pair") : __("tesla_vehicle.content.read_soc"),
                no_variant: "secondary", yes_variant: deletion ? "danger" : "primary"
            })) return;
            // Recheck live prerequisites after confirmation, never chain start→pair/poll.
            if (confirmedIndex === this.state.edit_index && confirmedConfiguration === JSON.stringify(API.get("tesla_vehicle/profiles")) &&
                confirmedProfiles === JSON.stringify(API.get("ev/config")) && confirmedIdentity === this.vehicleIdentity() &&
                confirmedRevision === this.vehicle_revision && this.commandAllowed(topic))
                await API.call(topic, {ev_index: confirmedIndex}, () => __("tesla_vehicle.script.command_failed"));
        } finally {
            this.command_in_progress = false; this.setState({command_busy: false});
        }
    }

    render(props: {}, state: TeslaConfig & TeslaViewState) {
        if (!util.render_allowed()) return null;
        // OFF publishes no Tesla configuration. Never synthesize writable defaults.
        if (!API.get("tesla_vehicle/profiles")) return <Fragment>
            <FormSeparator />
            <h2>{__("tesla_vehicle.bluetooth.tesla_title")}</h2>
            <Alert variant="info">{__("tesla_vehicle.content.off_help")}</Alert>
        </Fragment>;

        const profile = this.profile();
        const status = this.profileStatus();
        const vehicleReady = teslaVehicleReady(profile.ev_index, state.ev_state) && profile.ev_mac === state.ev_state?.mac;
        const bluetooth = API.get("bluetooth/state");
        const dirty = this.configurationDirty();
        const actions = teslaActions(profile.enable, status, dirty, vehicleReady, state.command_busy || this.searchBusy(), bluetooth);
        const keyStatusText = {
            paired: __("tesla_vehicle.status.paired"), pending: __("tesla_vehicle.status.pending"),
            sending: __("tesla_vehicle.status.sending"), key_unknown: __("tesla_vehicle.status.key_unknown"),
            key_only: __("tesla_vehicle.status.key_only"), cleanup_required: __("tesla_vehicle.status.cleanup_required"),
            not_paired: __("tesla_vehicle.status.not_paired")
        }[teslaKeyStatus(status)];
        const evs = API.get("ev/config")?.evs ?? [];
        const ev_items: [string, string][] = [["-1", __("tesla_vehicle.content.select_vehicle")],
            ...evs.map((ev, index): [string, string] => [String(index), `${ev.name} (${ev.mac})`])];
        return <Fragment>
            <FormSeparator />
            <ConfigForm id="tesla_vehicle_config_form" title={__("tesla_vehicle.bluetooth.tesla_title")} isDirty={this.isDirty()}
                        onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.content.vehicle_profile")} help={__("tesla_vehicle.content.vehicle_profile_help")}>
                    <InputSelect items={ev_items} value={String(state.edit_index)} onValue={(value) => this.setState({edit_index: parseInt(value)})} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.vin")} help={__("tesla_vehicle.content.vin_help")}>
                    <InputText value={profile.vin} maxLength={17} onValue={vin => this.setProfile({vin})} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.ble_address")} help={__("tesla_vehicle.content.ble_address_help")}>
                    <InputText value={profile.ble_address} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.search")} help={__("tesla_vehicle.content.search_help")}>
                    <Button type="button" className="w-100" variant="secondary" disabled={state.command_busy || !this.searchAllowed()} onClick={this.search}>
                        {this.searchBusy() ? __("tesla_vehicle.content.searching") : __("tesla_vehicle.content.search")}
                    </Button>
                </FormRow>
                {this.searchBusy() ? <Alert variant="info" role="status">{__("tesla_vehicle.content.searching")}</Alert> : undefined}
                {state.search_error || state.discovery?.status === 'error' ? <Alert variant="danger" role="alert">{__("tesla_vehicle.content.search_failed")}</Alert> : undefined}
                {state.discovery?.status === 'complete' && !this.searchBusy() ? <Fragment>
                    {this.matchingTargets().length === 0 ? <Alert variant="info" role="status">{__("tesla_vehicle.content.search_empty")}</Alert> :
                        <Table size="sm" responsive className="mb-3">
                            <thead><tr><th>{__("tesla_vehicle.content.search_identifier")}</th><th>{__("tesla_vehicle.content.ble_address")}</th><th>{__("tesla_vehicle.content.search_signal")}</th><th>{__("tesla_vehicle.content.search_selection")}</th></tr></thead>
                            <tbody>{this.matchingTargets().map(target => <tr key={target.ble_address}>
                                <td><span>{target.name}</span><div class="text-success">{__("tesla_vehicle.content.matching_vin")}</div></td><td>{target.ble_address}</td><td>{`${target.rssi} dBm`}</td>
                                <td><Button type="button" size="sm" variant={profile.ble_address === target.ble_address ? "primary" : "secondary"}
                                    aria-pressed={profile.ble_address === target.ble_address} disabled={state.command_busy || !this.searchAllowed()}
                                    onClick={() => this.selectTarget(target)}>{__("tesla_vehicle.content.select_target")}</Button></td>
                            </tr>)}</tbody>
                        </Table>}
                    {state.discovery.truncated ? <p class="text-muted">{__("tesla_vehicle.content.search_limited")}</p> : undefined}
                </Fragment> : undefined}
                <FormRow label={__("tesla_vehicle.content.enable")}>
                    <Switch desc={__("tesla_vehicle.content.enable_desc")} checked={profile.enable} onClick={() => this.setProfile({enable: !profile.enable})} />
                </FormRow>
                <p class="text-muted">{__("tesla_vehicle.content.lifecycle_help")}</p>
            </ConfigForm>
            <FormRow label={__("tesla_vehicle.content.active_profile")}><InputText value={evs[state.ev_state?.active_ev_index]?.name ?? "–"} /></FormRow>
            <FormRow label={__("tesla_vehicle.content.loaded_profile")}><InputText value={evs[state.tesla_state?.profile_ev_index]?.name ?? "–"} /></FormRow>
            {state.tesla_state?.legacy_blocked ? <Alert variant="danger">{__("tesla_vehicle.content.legacy_blocked")}</Alert> : undefined}
            <FormRow label={__("tesla_vehicle.status.pairing")}><InputText value={keyStatusText} /></FormRow>
            <FormRow label={__("tesla_vehicle.status.ble")}>
                <InputText value={status?.connected ? __("tesla_vehicle.status.connected") : __("tesla_vehicle.status.disconnected")} />
            </FormRow>
            <p class="text-muted">{__("tesla_vehicle.content.shared_soc")}</p>
            <FormRow label={__("tesla_vehicle.bluetooth.runtime")}>
                <InputText value={teslaBluetoothReady(bluetooth) ? __("tesla_vehicle.bluetooth.active") : __("tesla_vehicle.content.bluetooth_blocked")} />
            </FormRow>
            {!teslaBluetoothReady(bluetooth) ? <Alert variant="warning">{__("tesla_vehicle.content.bluetooth_blocked")}</Alert> : undefined}
            {status?.action_busy ? <Alert variant="info">{__("tesla_vehicle.content.action_busy")}</Alert> : undefined}
            {status?.last_error ? <Alert variant="danger">{status.last_error}</Alert> : undefined}
            {status?.restart_required ? <Alert variant="warning">{__("tesla_vehicle.status.restart_required")}</Alert> : undefined}
            {actions.transition ? <Alert variant="info">{__("tesla_vehicle.status.stopping")}</Alert> : undefined}
            <p>{__("tesla_vehicle.content.start_help")}</p>
            <Button variant="secondary" disabled={state.command_busy || !this.commandAllowed("tesla_vehicle/start")}
                    onClick={() => this.command("tesla_vehicle/start")}>{__("tesla_vehicle.content.start")}</Button>
            <Alert className="mt-3" variant={vehicleReady ? "info" : "warning"}>
                {vehicleReady ? __("tesla_vehicle.content.vehicle_ready") : __("tesla_vehicle.content.vehicle_not_ready")}
                <ul class="mb-0">
                    {dirty ? <li>{__("tesla_vehicle.content.save_first")}</li> : undefined}
                    {!profile.enable ? <li>{__("tesla_vehicle.content.enable_first")}</li> : undefined}
                    {!status?.running ? <li>{__("tesla_vehicle.content.runtime_inactive")}</li> : undefined}
                    {status?.pairing_pending ? <li>{keyStatusText}</li> : undefined}
                    {status?.key_presence !== 'present' ? <li>{__("tesla_vehicle.content.verify_requires_key")}</li> : undefined}
                </ul>
            </Alert>
            <FormRow label={__("tesla_vehicle.content.pair_action")} help={__("tesla_vehicle.content.pair_confirm")}>
                <Button className="w-100" variant="primary" disabled={actions.pairDisabled} onClick={() => this.command("tesla_vehicle/pair")}>
                    {status?.pairing_pending ? __("tesla_vehicle.content.pairing") : __("tesla_vehicle.content.pair")}
                </Button>
            </FormRow>
            <FormRow label={__("tesla_vehicle.content.test_actions")} help={__("tesla_vehicle.content.manual_wake_help")}>
                <Button className="w-100" variant="secondary" disabled={actions.pollDisabled} onClick={() => this.command("tesla_vehicle/poll")}>
                    {__("tesla_vehicle.content.read_soc")}
                </Button>
            </FormRow>
            <details class="mt-3">
                <summary>{__("tesla_vehicle.content.advanced")}</summary>
                <Alert variant="info">{__("tesla_vehicle.content.revocation_help")}</Alert>
                <FormRow label={__("tesla_vehicle.content.forget_key")}>
                    <Button className="w-100" variant="danger" disabled={state.command_busy || !this.commandAllowed("tesla_vehicle/forget_key")} onClick={() => this.command("tesla_vehicle/forget_key")}>
                        {__("tesla_vehicle.content.forget_key")}
                    </Button>
                </FormRow>
            </details>
        </Fragment>;
    }
}

export function pre_init() {
    registerVehicleExtension(TeslaVehicle);
}
export function init() {
}
