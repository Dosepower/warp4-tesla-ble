import {h, Fragment, Component} from "preact";
import {Alert, Button} from "react-bootstrap";
import * as API from "../../ts/api";
import * as util from "../../ts/util";
import {__} from "../../ts/translation";
import {FormRow} from "../../ts/components/form_row";
import {Switch} from "../../ts/components/switch";

export class RemoteSettings extends Component<{}, {busy: boolean; error: string}> {
    private disposed = false;
    private listeners = new AbortController();
    constructor() {
        super(); this.state = {busy: false, error: ""};
        for (const topic of ["bluetooth/config", "bluetooth/state", "bluetooth/remote_config", "bluetooth/remote_state", "bluetooth/remote_candidates", "bluetooth/remote_bindings"] as const)
            util.addApiEventListener(topic, () => {if (!this.disposed) this.setState({});}, {signal: this.listeners.signal});
    }
    override componentWillUnmount() {this.disposed = true; this.listeners.abort();}
    private ready() {
        return !this.disposed && API.get("bluetooth/config")?.enable === true &&
            API.get("bluetooth/state")?.phase === 'active' && typeof API.get("bluetooth/remote_config")?.enable === 'boolean';
    }
    private async run(action: () => Promise<void>) {
        if (this.state.busy || !this.ready()) return;
        this.setState({busy: true, error: ""});
        try {await action();} catch (e) {if (!this.disposed) this.setState({error: e instanceof Error ? e.message : String(e)});}
        finally {if (!this.disposed) this.setState({busy: false});}
    }
    private toggle = (event?: Event) => {
        event?.preventDefault();
        const enable = !API.get("bluetooth/remote_config")?.enable;
        return this.run(async () => {
            await API.save("bluetooth/remote_config", {enable}, () => __("tesla_vehicle.remote.failed"));
            const saved = JSON.parse(await (await util.download('/bluetooth/remote_config', true, 10000)).text());
            if (saved?.enable !== enable) throw new Error(__("tesla_vehicle.remote.failed"));
            // No reboot, discovery, pairing or charge action on save.
        });
    };
    private start = () => this.run(async () => {
        if (!API.get("bluetooth/remote_config")?.enable || API.get("bluetooth/remote_state")?.busy) return;
        await API.call("bluetooth/remote_start", {}, () => __("tesla_vehicle.remote.failed"));
    });
    private select = (index: number, generation: number) => this.run(async () => {
        const current = API.get("bluetooth/remote_state");
        if (!API.get("bluetooth/remote_config")?.enable || !current?.busy || current.generation !== generation) return;
        await API.call("bluetooth/remote_select", {index, generation}, () => __("tesla_vehicle.remote.failed"));
    });
    private recover = (id: string) => this.run(async () => {
        if (!API.get("bluetooth/remote_config")?.enable || API.get("bluetooth/remote_state")?.busy || util.async_modal_ref.current?.state?.show) return;
        if (!await util.async_modal_ref.current.show({title: () => __("tesla_vehicle.remote.recover"), body: () => __("tesla_vehicle.remote.recover_confirm"),
            no_text: () => __("tesla_vehicle.content.cancel"), yes_text: () => __("tesla_vehicle.remote.recover"), no_variant: "secondary", yes_variant: "primary"})) return;
        if (!this.ready() || !API.get("bluetooth/remote_config")?.enable || API.get("bluetooth/remote_state")?.busy || !API.get("bluetooth/remote_bindings")?.entries.some(b => b.id === id)) return;
        await API.call("bluetooth/remote_recover", {id, confirmed: true}, () => __("tesla_vehicle.remote.failed"));
    });
    private forget = (id: string, name: string) => this.run(async () => {
        if (API.get("bluetooth/remote_state")?.busy || util.async_modal_ref.current?.state?.show) return;
        if (!await util.async_modal_ref.current.show({title: () => __("tesla_vehicle.remote.forget"),
            body: () => `${__("tesla_vehicle.remote.forget_confirm")} ${name}`, no_text: () => __("tesla_vehicle.content.cancel"),
            yes_text: () => __("tesla_vehicle.remote.forget"), no_variant: "secondary", yes_variant: "danger"})) return;
        if (!this.ready() || API.get("bluetooth/remote_state")?.busy || !API.get("bluetooth/remote_bindings")?.entries.some(b => b.id === id)) return;
        await API.call("bluetooth/remote_forget", {id, confirmed: true}, () => __("tesla_vehicle.remote.failed"));
    });
    private cancel = () => this.run(async () => {await API.call("bluetooth/remote_cancel", {}, () => __("tesla_vehicle.remote.failed"));});
    render() {
        const config = API.get("bluetooth/remote_config"), state = API.get("bluetooth/remote_state");
        if (API.get("bluetooth/config")?.enable !== true || typeof config?.enable !== 'boolean') return null;
        return <Fragment>
            <FormRow label={__("tesla_vehicle.remote.enable")} help={__("tesla_vehicle.remote.help")}>
                <Switch checked={config.enable} disabled={!this.ready() || this.state.busy} onClick={this.toggle} />
            </FormRow>
            {API.get("bluetooth/remote_bindings")?.entries.map(binding => <FormRow key={binding.id} label={binding.name} help={binding.phase && binding.phase !== 'active' ? __("tesla_vehicle.remote.pending") : undefined}>
                <span><Button data-action="remote-recover" disabled={!this.ready() || !config.enable || this.state.busy || state?.busy} onClick={() => this.recover(binding.id)}>{__("tesla_vehicle.remote.recover")}</Button>
                <Button data-action="remote-forget" variant="danger" disabled={this.state.busy || state?.busy} onClick={() => this.forget(binding.id, binding.name)}>{__("tesla_vehicle.remote.forget")}</Button></span>
            </FormRow>)}
            {this.state.error || state?.persistence_error ? <Alert variant="danger">{this.state.error || __("tesla_vehicle.remote.failed")}</Alert> : null}
            {config.enable && state ? <Fragment>
                <FormRow label={__("tesla_vehicle.remote.pair")}>
                    <Button data-action="remote-start" disabled={!this.ready() || this.state.busy || state.busy} onClick={this.start}>{__("tesla_vehicle.remote.pair")}</Button>
                    {state.busy ? <Button data-action="remote-cancel" variant="secondary" disabled={this.state.busy} onClick={this.cancel}>{__("tesla_vehicle.content.cancel")}</Button> : null}
                </FormRow>
                {state.busy && !state.normal ? <FormRow label={__("tesla_vehicle.remote.window")}>
                    <span>{Math.ceil(state.remaining_ms / 1000)} s — {state.phase === 'confirm_on_tag' ? __("tesla_vehicle.remote.confirm") : __("tesla_vehicle.remote.triple")}</span>
                </FormRow> : null}
                {state.busy && !state.normal && state.phase !== 'confirm_on_tag' && state.phase !== 'draining' ? API.get("bluetooth/remote_candidates")?.entries.map(candidate => <FormRow key={candidate.index} label={`${__("tesla_vehicle.remote.candidate")} ${candidate.index + 1}`}>
                    <Button data-action="remote-select" disabled={this.state.busy} onClick={() => this.select(candidate.index, state.generation)}>{__("tesla_vehicle.remote.identify")}</Button>
                </FormRow>) : null}
                {state.phase === 'consent_untrusted' ? <Alert variant="warning">{__("tesla_vehicle.remote.untrusted")}</Alert> : null}
                {state.phase === 'failed' || state.phase === 'expired' ? <Alert variant="danger">{__("tesla_vehicle.remote.failed")}</Alert> : null}
                {state.phase === 'paired_tofu' && state.trusted ? <Alert variant="success">{__("tesla_vehicle.remote.paired")}</Alert> : null}
                <FormRow help={__("tesla_vehicle.remote.tofu")} />
                {!state.enrollment_ready ? <FormRow help={__("tesla_vehicle.remote.crypto_pending")} /> : null}
            </Fragment> : null}
        </Fragment>;
    }
}
