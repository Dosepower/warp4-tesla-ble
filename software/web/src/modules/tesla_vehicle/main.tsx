import { h, Fragment } from "preact";
import { Bluetooth } from "react-feather";
import { Alert, Button } from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
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
        return <Fragment>
            <ConfigForm id="bluetooth_config_form" title={__("tesla_vehicle.bluetooth.title")} isDirty={this.isDirty()} onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("tesla_vehicle.bluetooth.enable")} help={__("tesla_vehicle.bluetooth.help")}>
                    <Switch checked={state.enable} desc={__("tesla_vehicle.bluetooth.desired")}
                            disabled={!s?.supported || state.busy} onClick={this.toggle("enable")} />
                </FormRow>
            </ConfigForm>
            {!s?.supported ? <Alert variant="warning">{__("tesla_vehicle.bluetooth.unsupported")} ({s?.unsupported_reason})</Alert> : undefined}
            {s?.persistence_error ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.save_failed")}</Alert> : undefined}
            {s?.supported && s.phase === "error" ? <Alert variant="danger">{__("tesla_vehicle.bluetooth.error")}</Alert> : undefined}
            {s?.supported && s.restart_required ? <Alert variant="warning">
                <p>{__("tesla_vehicle.bluetooth.pending")}</p>
                {this.canRestart() ? <Button variant="danger" disabled={state.busy} onClick={this.restart}>
                    {__("tesla_vehicle.bluetooth.restart")}
                </Button> : undefined}
            </Alert> : undefined}
        </Fragment>;
    }
}

export function pre_init() {
    registerVehicleExtension(TeslaVehicle);
}
export function init() {
}
