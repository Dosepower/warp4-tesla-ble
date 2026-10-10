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

export function TeslaVehicleNavbar() {
    return <NavbarItem name="tesla_vehicle" module="tesla_vehicle"
                       title={__("tesla_vehicle.navbar.tesla_vehicle")}
                       symbol={<BatteryCharging />} />;
}

type TeslaConfig = API.getType["tesla_vehicle/config"];
interface TeslaViewState {
    tesla_state: API.getType["tesla_vehicle/state"];
}

export class TeslaVehicle extends ConfigComponent<"tesla_vehicle/config", {}, TeslaViewState> {
    constructor() {
        super("tesla_vehicle/config", () => __("tesla_vehicle.script.save_failed"), undefined, {
            tesla_state: API.get("tesla_vehicle/state"),
        });
        util.addApiEventListener("tesla_vehicle/state", () =>
            this.setState({tesla_state: API.get("tesla_vehicle/state")}));
    }

    private async command(topic: "tesla_vehicle/pair" | "tesla_vehicle/poll") {
        await API.call(topic, {}, () => __("tesla_vehicle.script.command_failed"));
    }

    render(props: {}, state: TeslaConfig & TeslaViewState) {
        if (!util.render_allowed())
            return <SubPage name="tesla_vehicle" />;

        const status = state.tesla_state;
        const evs = API.get("ev/config")?.evs ?? [];
        const ev_items: [string, string][] = [["-1", __("tesla_vehicle.content.select_vehicle")],
            ...evs.map((ev, index): [string, string] => [String(index), `${ev.name} (${ev.mac})`])];

        return <SubPage name="tesla_vehicle" title={__("tesla_vehicle.content.title")}>
            <SubPage.Status>
                <FormRow label={__("tesla_vehicle.status.pairing")}>
                    <InputText value={status?.paired ? __("tesla_vehicle.status.paired") :
                                      status?.pairing_pending ? __("tesla_vehicle.status.pending") :
                                      status?.key_present ? __("tesla_vehicle.status.key_only") : __("tesla_vehicle.status.not_paired")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.status.ble")}>
                    <InputText value={status?.connected ? __("tesla_vehicle.status.connected") : __("tesla_vehicle.status.disconnected")} />
                </FormRow>
                <FormRow label={__("tesla_vehicle.status.soc")}>
                    <InputText value={Number.isFinite(status?.last_soc) ? `${status.last_soc.toFixed(1)} %` : "–"} />
                </FormRow>

                {status?.last_error ? <Alert variant="danger">{status.last_error}</Alert> : undefined}
                {status?.restart_required ? <Alert variant="warning">{__("tesla_vehicle.status.restart_required")}</Alert> : undefined}
            </SubPage.Status>

            <SubPage.Config id="tesla_vehicle_config_form" isDirty={this.isDirty()}
                            onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.content.enable")}>
                    <Switch desc={__("tesla_vehicle.content.enable_desc")} checked={state.enable} onClick={this.toggle("enable")} />
                </FormRow>
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
                    <Button className="w-100" variant="primary"
                            disabled={!state.enable || !status?.running || status?.pairing_pending || status?.restart_required || this.isDirty()}
                            onClick={() => this.command("tesla_vehicle/pair")}>
                        {status?.pairing_pending ? __("tesla_vehicle.content.pairing") : __("tesla_vehicle.content.pair")}
                    </Button>
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.test_actions")}>
                    <Button className="w-100" variant="secondary"
                            disabled={!status?.paired || !status?.running || status?.restart_required || this.isDirty()}
                            onClick={() => this.command("tesla_vehicle/poll")}>
                        {__("tesla_vehicle.content.read_soc")}
                    </Button>
                </FormRow>
                <FormRow label={__("tesla_vehicle.content.forget_key")}>
                    <Button className="w-100" variant="danger" disabled={!status?.key_present}
                            onClick={async () => {
                                const modal = util.async_modal_ref.current;
                                if (!await modal.show({
                                    title: () => __("tesla_vehicle.content.forget_key"),
                                    body: () => __("tesla_vehicle.content.forget_confirm"),
                                    no_text: () => __("tesla_vehicle.content.cancel"),
                                    yes_text: () => __("tesla_vehicle.content.forget_key"),
                                    no_variant: "secondary", yes_variant: "danger"
                                })) return;
                                await API.call("tesla_vehicle/forget_key", {}, () => __("tesla_vehicle.script.command_failed"));
                            }}>{__("tesla_vehicle.content.forget_key")}</Button>
                </FormRow>
            </SubPage.Config>
        </SubPage>;
    }
}

export function pre_init() {
}

export function init() {
}
