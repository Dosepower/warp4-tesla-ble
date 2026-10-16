import { h, Fragment } from "preact";
import { BatteryCharging } from "react-feather";
import { Alert, Button } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { NavbarItem } from "../../ts/components/navbar_item";
import { SubPage } from "../../ts/components/sub_page";
import { Switch } from "../../ts/components/switch";
import { teslaActions, teslaKeyStatus, teslaVehicleReady } from "./action_policy";

export function TeslaVehicleNavbar() {
    return <NavbarItem name="tesla_vehicle" module="tesla_vehicle"
                       title={__("tesla_vehicle.navbar.tesla_vehicle")}
                       symbol={<BatteryCharging />} />;
}

type TeslaConfig = API.getType["tesla_vehicle/config"];
interface TeslaViewState {
    tesla_state: API.getType["tesla_vehicle/state"];
    ev_state: API.getType["ev/state"];
    command_busy: boolean;
}

export class TeslaVehicle extends ConfigComponent<"tesla_vehicle/config", {}, TeslaViewState> {
    private command_in_progress = false;

    constructor() {
        super("tesla_vehicle/config", () => __("tesla_vehicle.script.save_failed"), undefined, {
            tesla_state: API.get("tesla_vehicle/state"),
            ev_state: API.get("ev/state"),
            command_busy: false,
        });
        util.addApiEventListener("tesla_vehicle/state", () =>
            this.setState({tesla_state: API.get("tesla_vehicle/state")}));
        util.addApiEventListener("ev/state", () => this.setState({ev_state: API.get("ev/state")}));
        util.addApiEventListener("ev/config", () => this.setState({}));
    }

    private commandAllowed(topic: "tesla_vehicle/pair" | "tesla_vehicle/poll" | "tesla_vehicle/forget_key") {
        const state = this.state;
        const actions = teslaActions(state.enable, state.tesla_state, this.isDirty(),
                                     teslaVehicleReady(state.ev_index, state.ev_state));
        return !(topic === "tesla_vehicle/pair" ? actions.pairDisabled :
                 topic === "tesla_vehicle/poll" ? actions.pollDisabled : actions.deleteDisabled);
    }

    private async command(topic: "tesla_vehicle/pair" | "tesla_vehicle/poll" | "tesla_vehicle/forget_key") {
        // Synchronous guard also covers double clicks before Preact rerenders.
        if (this.command_in_progress || !this.commandAllowed(topic)) return;
        this.command_in_progress = true;
        this.setState({command_busy: true});
        try {
            const deletion = topic === "tesla_vehicle/forget_key";
            const pairing = topic === "tesla_vehicle/pair";
            if (!await util.async_modal_ref.current.show({
                title: () => deletion ? __("tesla_vehicle.content.forget_key") : pairing ? __("tesla_vehicle.content.pair") : __("tesla_vehicle.content.read_soc"),
                body: () => deletion ? __("tesla_vehicle.content.forget_confirm") : pairing ? __("tesla_vehicle.content.pair_confirm") : __("tesla_vehicle.content.poll_confirm"),
                no_text: () => __("tesla_vehicle.content.cancel"),
                yes_text: () => deletion ? __("tesla_vehicle.content.forget_key") : pairing ? __("tesla_vehicle.content.pair") : __("tesla_vehicle.content.read_soc"),
                no_variant: "secondary", yes_variant: deletion ? "danger" : "primary"
            })) return;
            // Live status/configuration may have changed while the modal was open.
            if (this.commandAllowed(topic))
                await API.call(topic, {}, () => __("tesla_vehicle.script.command_failed"));
        } finally {
            this.command_in_progress = false;
            this.setState({command_busy: false});
        }
    }

    render(props: {}, state: TeslaConfig & TeslaViewState) {
        if (!util.render_allowed())
            return <SubPage name="tesla_vehicle" />;

        const status = state.tesla_state;
        const vehicleReady = teslaVehicleReady(state.ev_index, state.ev_state);
        const actions = teslaActions(state.enable, status, this.isDirty(), vehicleReady, state.command_busy);
        // Keep literal translation IDs for WARP's generation/placeholder audit.
        const keyStatusText = {
            paired: __("tesla_vehicle.status.paired"),
            pending: __("tesla_vehicle.status.pending"),
            sending: __("tesla_vehicle.status.sending"),
            key_unknown: __("tesla_vehicle.status.key_unknown"),
            key_only: __("tesla_vehicle.status.key_only"),
            cleanup_required: __("tesla_vehicle.status.cleanup_required"),
            not_paired: __("tesla_vehicle.status.not_paired")
        }[teslaKeyStatus(status)];
        const evs = API.get("ev/config")?.evs ?? [];
        const ev_items: [string, string][] = [["-1", __("tesla_vehicle.content.select_vehicle")],
            ...evs.map((ev, index): [string, string] => [String(index), `${ev.name} (${ev.mac})`])];

        return <SubPage name="tesla_vehicle" title={__("tesla_vehicle.content.title")}>
            <SubPage.Status>
                <FormRow label={__("tesla_vehicle.status.pairing")}>
                    <InputText value={keyStatusText} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.status.local_key")}>
                    <InputText value={status?.key_presence === 'present' ? __("tesla_vehicle.status.key_present") :
                                      status?.key_presence === 'cleanup' ? __("tesla_vehicle.status.cleanup_required") :
                                      status?.key_presence === 'absent' ? __("tesla_vehicle.status.key_absent") : __("tesla_vehicle.status.key_unknown")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.status.ble")}>
                    <InputText value={status?.connected ? __("tesla_vehicle.status.connected") : __("tesla_vehicle.status.disconnected")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.status.soc")}>
                    <InputText value={typeof status?.last_soc === 'number' && Number.isFinite(status.last_soc) ? `${status.last_soc.toFixed(1)} %` : "–"} />
                </FormRow>

                {status?.last_error ? <Alert variant="danger">{status.last_error}</Alert> : undefined}
                {status?.restart_required ? <Alert variant="warning">{__("tesla_vehicle.status.restart_required")}</Alert> : undefined}
                {actions.transition ? <Alert variant="info">{__("tesla_vehicle.status.stopping")}</Alert> : undefined}
            </SubPage.Status>

            <SubPage.Config id="tesla_vehicle_config_form" isDirty={this.isDirty()}
                            onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.content.enable")}>
                    <Switch desc={__("tesla_vehicle.content.enable_desc")} checked={state.enable} onClick={this.toggle("enable")} />
                </FormRow>
                <Alert variant={this.isDirty() && status?.running ? "warning" : "info"}>
                    {__("tesla_vehicle.content.lifecycle_help")}
                </Alert>
                <FormRow label={__("tesla_vehicle.content.vehicle_profile")} help={__("tesla_vehicle.content.vehicle_profile_help")}>
                    <InputSelect items={ev_items} value={String(state.ev_index)}
                                 onValue={(value) => this.setState({ev_index: parseInt(value)})} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.vin")} help={__("tesla_vehicle.content.vin_help")}>
                    <InputText value={state.vin} maxLength={17} onValue={this.set("vin")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.ble_address")} help={__("tesla_vehicle.content.ble_address_help")}>
                    <InputText value={state.ble_address} maxLength={17} onValue={this.set("ble_address")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.poll_interval")}>
                    <InputNumber value={state.poll_interval} min={30} max={3600} unit="s" onValue={this.set("poll_interval")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.wake_once")} help={__("tesla_vehicle.content.wake_once_help")}>
                    <Switch desc={__("tesla_vehicle.content.wake_once_desc")} checked={state.wake_once} onClick={this.toggle("wake_once")} />
                </FormRow>

                <FormSeparator />
                <h3>{__("tesla_vehicle.content.pair_title")}</h3>
                <Alert variant="info">
                    <ol class="mb-0">
                        <li>{__("tesla_vehicle.content.pair_step_1")}</li>
                        <li>{__("tesla_vehicle.content.pair_step_2")}</li>
                        <li>{__("tesla_vehicle.content.pair_step_3")}</li>
                        <li>{__("tesla_vehicle.content.pair_step_4")}</li>
                        <li>{__("tesla_vehicle.content.pair_step_5")}</li>
                    </ol>
                </Alert>
                <FormRow label={__("tesla_vehicle.content.pair_action")}>
                    <Alert variant={vehicleReady ? "info" : "warning"}>
                        {vehicleReady ? __("tesla_vehicle.content.vehicle_ready") : __("tesla_vehicle.content.vehicle_not_ready")}
                        <ul class="mb-0">
                            {this.isDirty() ? <li>{__("tesla_vehicle.content.save_first")}</li> : undefined}
                            {!state.enable ? <li>{__("tesla_vehicle.content.enable_first")}</li> : undefined}
                            {!status?.running ? <li>{__("tesla_vehicle.content.runtime_inactive")}</li> : undefined}
                            {status?.pairing_pending ? <li>{keyStatusText}</li> : undefined}
                            {status?.key_presence !== 'present' ? <li>{__("tesla_vehicle.content.verify_requires_key")}</li> : undefined}
                        </ul>
                    </Alert>
                    <Button className="w-100" variant="primary"
                            disabled={actions.pairDisabled}
                            onClick={() => this.command("tesla_vehicle/pair")}>
                        {status?.pairing_pending ? __("tesla_vehicle.content.pairing") : __("tesla_vehicle.content.pair")}
                    </Button>
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.test_actions")} help={__("tesla_vehicle.content.manual_wake_help")}>
                    <Button className="w-100" variant="secondary"
                            disabled={actions.pollDisabled}
                            onClick={() => this.command("tesla_vehicle/poll")}>
                        {__("tesla_vehicle.content.read_soc")}
                    </Button>
                </FormRow>
                <Alert variant="info">{__("tesla_vehicle.content.revocation_help")}</Alert>
                <FormRow label={__("tesla_vehicle.content.forget_key")}>
                    <Button className="w-100" variant="danger" disabled={actions.deleteDisabled}
                            onClick={() => this.command("tesla_vehicle/forget_key")}>{__("tesla_vehicle.content.forget_key")}</Button>
                </FormRow>
            </SubPage.Config>
        </SubPage>;
    }
}

export function pre_init() {
}

export function init() {
}
