import { h, Fragment, Component } from "preact";
import { Bluetooth } from "react-feather";
import { Alert, Button } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { PageHeader } from "../../ts/components/page_header";
import { FormRow } from "../../ts/components/form_row";
import { NavbarItem } from "../../ts/components/navbar_item";
import { SubPage } from "../../ts/components/sub_page";
import { Switch } from "../../ts/components/switch";
import { registerVehicleExtension } from "../ev/main";
import { TeslaVehicle } from "./guided_vehicle";
export { TeslaVehicle };

export function BluetoothPageNavbar() {
    return <NavbarItem name="bluetooth" module="tesla_vehicle" title={__("tesla_vehicle.bluetooth.title")} symbol={<Bluetooth />} />;
}
export function BluetoothPage() {
    return <SubPage name="bluetooth" title={__("tesla_vehicle.bluetooth.title")}>
        {util.render_allowed() ? <BluetoothSettings /> : undefined}
    </SubPage>;
}
type BluetoothState = API.getType["bluetooth/state"];
interface BluetoothViewState { enable: boolean; bluetooth_state: BluetoothState; busy: boolean; error: string; }
export class BluetoothSettings extends Component<{}, BluetoothViewState> {
    private disposed = false;
    private in_progress = false;
    private revision = 0;
    private listeners = new AbortController();
    private config = JSON.stringify(API.get("bluetooth/config"));
    private runtime = JSON.stringify(API.get("bluetooth/state"));
    private intended: boolean;
    private saving = false;
    private own_config_seen = false;
    private own_runtime_seen = false;
    constructor() {
        super();
        this.state = {enable: API.get("bluetooth/config")?.enable, bluetooth_state: API.get("bluetooth/state"), busy: false, error: ""};
        util.addApiEventListener("bluetooth/config", () => {
            if (this.disposed) return;
            const config = JSON.stringify(API.get("bluetooth/config"));
            if (config !== this.config) {
                // The existing API has no transaction ID. Accept only the one
                // exact expected own ACK transition; every other change expires consent.
                if (this.saving && !this.own_config_seen && config === JSON.stringify({enable: this.intended})) this.own_config_seen = true;
                else ++this.revision;
                this.config = config;
            }
            this.setState({enable: API.get("bluetooth/config")?.enable});
        }, {signal: this.listeners.signal});
        util.addApiEventListener("bluetooth/state", () => {
            if (this.disposed) return;
            const s = API.get("bluetooth/state"), value = JSON.stringify(s);
            if (value !== this.runtime) {
                const before: BluetoothState = JSON.parse(this.runtime ?? 'null');
                const expected = {...before, restart_required: true, restart_reason: 'configuration_changed'};
                if (this.saving && !this.own_runtime_seen && value === JSON.stringify(expected)) this.own_runtime_seen = true;
                else ++this.revision;
                this.runtime = value;
            }
            this.setState({bluetooth_state: s});
        }, {signal: this.listeners.signal});
        for (const topic of ["tesla_vehicle/state", "tesla_vehicle/discovery"] as const) {
            util.addApiEventListener(topic, () => {
                if (this.disposed) return;
                if (this.radioBusy()) ++this.revision;
                this.setState({});
            }, {signal: this.listeners.signal});
        }
    }
    override componentWillUnmount() { this.disposed = true; ++this.revision; this.listeners.abort(); }
    private radioBusy() {
        const s = API.get("tesla_vehicle/state");
        return !!(s?.action_busy || s?.pairing_pending || s?.stopping || API.get("tesla_vehicle/discovery")?.busy);
    }
    private available(s: BluetoothState = this.state.bluetooth_state) {
        return !this.disposed && !!s?.supported && typeof this.state.enable === 'boolean' &&
            s.phase !== 'starting' && s.phase !== 'stopping' && !this.radioBusy();
    }
    private toggleBluetooth = (event?: Event) => { event?.preventDefault(); return this.change(!this.state.enable); };
    private retryRestart = () => this.change(this.state.enable);
    private async change(intended: boolean) {
        if (this.in_progress) return;
        if (!this.available() || util.async_modal_ref.current?.state?.show) {
            if (!this.disposed) this.setState({error: __("tesla_vehicle.bluetooth.unavailable")});
            return;
        }
        // Capture the intended value, not a later inverse of a mutable switch.
        const revision = this.revision, initial = {...this.state.bluetooth_state};
        const current = () => !this.disposed && revision === this.revision && this.available();
        this.in_progress = true; this.intended = intended;
        this.own_config_seen = this.own_runtime_seen = false;
        this.setState({busy: true, error: ""});
        try {
            if (!await util.async_modal_ref.current.show({
                title: () => intended ? __("tesla_vehicle.bluetooth.activate_title") : __("tesla_vehicle.bluetooth.deactivate_title"),
                body: () => __("tesla_vehicle.bluetooth.change_confirm"),
                no_text: () => __("tesla_vehicle.content.cancel"),
                yes_text: () => intended ? __("tesla_vehicle.bluetooth.activate_restart") : __("tesla_vehicle.bluetooth.deactivate_restart"),
                no_variant: "secondary", yes_variant: "danger",
            })) return;
            if (!current()) throw new Error(__("tesla_vehicle.bluetooth.changed"));
            this.saving = true;
            // Existing durable backend transaction; never pass an automatic reboot callback.
            await API.save("bluetooth/config", {enable: intended}, () => __("tesla_vehicle.bluetooth.save_failed"));
            if (!current()) throw new Error(__("tesla_vehicle.bluetooth.changed"));
            // HTTP success alone (or a stale cached state) is not persistence evidence.
            const config = JSON.parse(await (await util.download('/bluetooth/config', true, 10000)).text());
            const runtime: BluetoothState = JSON.parse(await (await util.download('/bluetooth/state', true, 10000)).text());
            if (!current() || config?.enable !== intended || Object.keys(config).length !== 1 ||
                !this.available(runtime) || runtime.persistence_error || runtime.phase !== initial.phase ||
                runtime.legacy_tesla_enabled !== initial.legacy_tesla_enabled ||
                (runtime.restart_required && ['configuration_changed', 'lifecycle_latched'].indexOf(runtime.restart_reason) < 0))
                throw new Error(__("tesla_vehicle.bluetooth.changed"));
            this.setState({enable: intended, bluetooth_state: runtime});
            if (!runtime.restart_required) return; // Desired state already applied: no unnecessary reboot.
            if (!current()) throw new Error(__("tesla_vehicle.bluetooth.changed"));
            // Standard backend handler retains its persistence/lifecycle/charging restart gates.
            await API.call("bluetooth/restart", {}, () => __("tesla_vehicle.bluetooth.save_failed"));
            if (current()) util.postReboot(__("util.reboot_title"), __("util.reboot_text"));
        } catch (e) {
            if (!this.disposed) this.setState({error: e instanceof Error ? e.message : String(e)});
        } finally {
            this.saving = false; this.in_progress = false;
            if (!this.disposed) this.setState({busy: false});
        }
    }
    render(props: {}, state: BluetoothViewState) {
        const s = state.bluetooth_state;
        return <Fragment>
            <PageHeader title={__("tesla_vehicle.bluetooth.title")} />
            <FormRow label={__("tesla_vehicle.bluetooth.enable")} help={__("tesla_vehicle.bluetooth.help")}>
                <Switch checked={state.enable} desc={__("tesla_vehicle.bluetooth.desired")}
                        disabled={!this.available() || state.busy} onClick={this.toggleBluetooth} />
            </FormRow>
            {!s?.supported ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.unsupported")} ({s?.unsupported_reason})</Alert> : undefined}
            {s?.persistence_error ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.save_failed")}</Alert> : undefined}
            {s?.supported && s.phase === 'error' ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.error")}</Alert> : undefined}
            {state.error ? <Alert variant="danger">{state.error}</Alert> : undefined}
            {s?.restart_required && !s.persistence_error && this.available() ? <FormRow>
                <Button type="button" variant="danger" className="w-100" disabled={state.busy} onClick={this.retryRestart}>
                    {state.enable ? __("tesla_vehicle.bluetooth.activate_restart") : __("tesla_vehicle.bluetooth.deactivate_restart")}
                </Button>
            </FormRow> : undefined}
        </Fragment>;
    }
}
export function pre_init() { registerVehicleExtension(TeslaVehicle); }
export function init() {}
