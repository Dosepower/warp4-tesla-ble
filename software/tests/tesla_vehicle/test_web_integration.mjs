// Executes the real page's VDOM and async action handlers with in-memory API/OS boundaries.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const require = createRequire(new URL('../../web/package.json', import.meta.url));
const {build} = require('esbuild');
const root = new URL('../../', import.meta.url);
const source = path => readFileSync(new URL(path, root), 'utf8');
const fixture = {
    values: {'ev/config': {evs: [{name:'A',mac:'a'},{name:'B',mac:'b'},{name:'C',mac:'synthetic-nonzero'}].map(p=>({...p,capacity:60,charging_efficiency:0.92,user_id:0}))}}, listeners: {}, calls: [], modals: [],
    show: async () => true,
    call: async () => {},
    config: {entries:[{enable:true,ev_index:2,ev_mac:'synthetic-nonzero',vin:'5YJ3E1EA7KF000001',ble_address:'01:02:03:04:05:06',poll_interval:300,wake_once:false}]},
};
const mock = `
const f = globalThis.fixture;
export const get = topic => f.values[topic];
export const call = async (topic, payload) => { f.calls.push({topic, payload}); await f.call(); };
export const save = async (topic, payload) => call(topic + '_update', payload);
export const postReboot = () => {};
export const __ = key => key === 'ev.content.add_ev_message' ? (()=>key) : key;
export const toLocaleFixed = (n,d) => n.toFixed(d);
export const addApiEventListener = (topic, cb) => { f.listeners[topic] = cb; };
export const render_allowed = () => true;
export const is_central_management_enabled = () => false;
export const format_timespan_ms = ms => ms + ' ms';
export const async_modal_ref = {current: {show: async options => { f.modals.push(options); return f.show(); }}};
export class ConfigComponent {
    constructor(topic, error, unused, extra) { this.state = {...f.config, ...extra}; this.dirty = false; }
    setState(value) { Object.assign(this.state, value); }
    isDirty() { return this.dirty; }
    set() { return () => {}; } toggle() { return () => {}; }
}
export const Button = 'Button', Alert = 'Alert', Bluetooth = 'Bluetooth', BatteryCharging = 'BatteryCharging';
export const FormRow = 'FormRow', FormSeparator = 'FormSeparator', InputNumber = 'InputNumber';
export const InputSelect = 'InputSelect', InputText = 'InputText', NavbarItem = 'NavbarItem', Switch = 'Switch';
export const Table='Table', InputFloat='InputFloat', InputTextPatterned='InputTextPatterned';
export const StatusSection='StatusSection', DiscoveryResultItem='DiscoveryResultItem', DiscoveryResultGroup='DiscoveryResultGroup', Plus='Plus';
export const get_charge_manager_auth_info=async()=>[];
export const ConfigForm = 'ConfigForm', PageHeader = 'PageHeader';
export const SubPage = Object.assign(() => {}, {Status: 'Status', Config: 'Config'});
`;
const compiled = await build({
    stdin: {contents:'export * from "./web/src/modules/tesla_vehicle/main.tsx"; export {Ev} from "./web/src/modules/ev/main.tsx";',resolveDir:fileURLToPath(root),loader:'tsx'},
    bundle: true, write: false, platform: 'node', format: 'cjs',
    plugins: [{name: 'in-memory-boundaries', setup(b) {
        b.onResolve({filter: /^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/users\/main$)/}, args => ({path: args.path, namespace: 'boundary'}));
        b.onLoad({filter: /.*/, namespace: 'boundary'}, () => ({contents: mock, loader: 'js'}));
    }}],
});
const module = {exports: {}};
vm.runInNewContext(compiled.outputFiles[0].text, {module, exports: module.exports, fixture});
const {TeslaVehicle, TeslaVehicleNavbar} = module.exports;
const active = {profile_ev_index:2,running: true, paired: false, key_present: true, key_presence: 'present', last_soc: null};
fixture.values['tesla_vehicle/state'] = active;
fixture.values['tesla_vehicle/profiles'] = fixture.config;
fixture.values['bluetooth/state'] = {supported: true, phase: 'active', restart_required: false, persistence_error: false};
fixture.values['ev/state'] = {active_ev_index: 2, mac: 'synthetic-nonzero'};
const page = new TeslaVehicle();
page.state.edit_index=2;
const walk = node => !node || typeof node !== 'object' ? [] :
    [node, ...[node.props?.children].flat(Infinity).flatMap(walk)];
const nodes = () => walk(page.render({}, page.state));
const button = suffix => nodes().find(n => n.type === 'Button' && n.props.children === `tesla_vehicle.content.${suffix}`);
assert.equal(TeslaVehicleNavbar, undefined, 'No Tesla navbar export: Tesla belongs only in Vehicles');
assert(!nodes().some(n => n.type === 'SubPage' || n.props?.name === 'tesla_vehicle'), 'Tesla section is inline, not a standalone page');
assert(!nodes().some(n => n.type === 'InputNumber'), 'No interval setting');
assert.equal(nodes().filter(n => n.type === 'Switch').length, 1, 'Only Tesla permission, no wake toggle');
const advanced = nodes().find(n => n.type === 'details');
assert(advanced && walk(advanced).some(n => n.props?.children === 'tesla_vehicle.content.forget_key'));
assert(!walk(nodes().find(n => n.props?.id === 'tesla_vehicle_config_form')).some(n => n.props?.children === 'tesla_vehicle.content.forget_key'), 'Delete is outside configuration');
assert.equal(fixture.calls.length, 0, 'Construction/render never sends commands');
const evPage=new module.exports.Ev();
assert(!walk(evPage.render({},evPage.state)).some(n=>n.type===TeslaVehicle));
module.exports.pre_init();
const evNodes=walk(evPage.render({},evPage.state));
assert(evNodes.findIndex(n=>n.type===TeslaVehicle)>evNodes.findIndex(n=>n.props?.label==='ev.content.ev_profiles'),'Tesla section follows existing profiles');
assert(!evNodes.some(n=>n.type==='NavbarItem'));
assert.equal(fixture.calls.length,0,'Vehicle extension registration and rendering send nothing');
assert.equal(button('read_soc').props.disabled, false);
assert(nodes().some(n => n.props?.value === 'tesla_vehicle.status.key_only'));
assert(!nodes().some(n => String(n.props?.value).includes('null %'))); // No duplicated or null-formatted SoC.
assert(nodes().some(n => n.props?.children === 'tesla_vehicle.content.revocation_help'));
for (const [change, expected] of [
    [{pairing_pending: true, pair_request_sent: false}, 'sending'],
    [{pairing_pending: true, pair_request_sent: true}, 'pending'],
    [{paired: true}, 'paired'],
]) {
    page.state.tesla_state = {...active, ...change};
    assert(nodes().some(n => n.props?.value === `tesla_vehicle.status.${expected}`));
}
page.state.tesla_state = active;
// Cancellation sends nothing and restores the synchronous submission guard.
fixture.show = async () => false;
await button('pair').props.onClick();
assert.equal(fixture.calls.length, 0);
assert.equal(page.state.command_busy, false);
let confirm;
fixture.show = () => new Promise(resolve => { confirm = resolve; });
const first = button('pair').props.onClick();
assert.equal(page.state.command_busy, true);
await button('pair').props.onClick();
assert.equal(fixture.modals.length, 2); // cancelled modal + single in-flight modal
assert.equal(button('read_soc').props.disabled, true);
// A changed SLAC profile while the confirmation is open cancels submission.
fixture.values['ev/state'] = {active_ev_index: -1, mac: ''};
fixture.listeners['ev/state']();
confirm(true); await first;
assert.equal(fixture.calls.length, 0);
fixture.values['ev/state'] = {active_ev_index: 2, mac: 'synthetic-nonzero'};
fixture.listeners['ev/state']();
fixture.show = async () => true;
for (const [suffix, topic, confirmation] of [
    ['pair', 'tesla_vehicle/pair', 'pair_confirm'],
    ['read_soc', 'tesla_vehicle/poll', 'poll_confirm'],
    ['forget_key', 'tesla_vehicle/forget_key', 'forget_confirm'],
]) {
    await button(suffix).props.onClick();
    assert.equal(fixture.calls.at(-1).topic, topic);
    assert.equal(JSON.stringify(fixture.calls.at(-1).payload), '{"ev_index":2}');
    assert.equal(fixture.modals.at(-1).body(), `tesla_vehicle.content.${confirmation}`);
}
fixture.call = async () => { throw new Error('injected refusal'); };
await assert.rejects(button('read_soc').props.onClick(), /injected refusal/);
assert.equal(page.state.command_busy, false);
fixture.call = async () => {};
// Teardown/restart events update the real component, not only a policy helper.
for (const transition of [{stopping: true}, {restart_required: true}, {running: false}]) {
    fixture.values['tesla_vehicle/state'] = {...active, ...transition};
    fixture.listeners['tesla_vehicle/state']();
    assert.equal(button('pair').props.disabled, true);
    assert.equal(button('read_soc').props.disabled, true);
}
// Real VDOM and handlers must honor live Bluetooth, dirty and backend action gates.
fixture.values['tesla_vehicle/state']=active; fixture.listeners['tesla_vehicle/state']();
for (const change of [{phase:'disabled'},{phase:'starting'},{restart_required:true},{persistence_error:true}]) {
    const original=fixture.values['bluetooth/state'];
    fixture.values['bluetooth/state']={...original,...change}; fixture.listeners['bluetooth/state']();
    assert.equal(button('pair').props.disabled,true); assert.equal(button('read_soc').props.disabled,true);
    const count=fixture.calls.length;
    await button('read_soc').props.onClick(); assert.equal(fixture.calls.length,count);
    assert(nodes().some(n=>n.props?.children==='tesla_vehicle.content.bluetooth_blocked'));
    fixture.values['bluetooth/state']=original;
}
page.dirty=true;
assert.equal(button('pair').props.disabled,true); assert.equal(button('forget_key').props.disabled,true);
page.dirty=false;
fixture.values['tesla_vehicle/state']={...active,action_busy:true}; fixture.listeners['tesla_vehicle/state']();
assert.equal(button('pair').props.disabled,true); assert.equal(button('read_soc').props.disabled,true); assert.equal(button('forget_key').props.disabled,true);
fixture.values['tesla_vehicle/state']={...active,last_soc:63.5,last_update:123456}; fixture.listeners['tesla_vehicle/state']();
assert(!nodes().some(n=>n.props?.label==='tesla_vehicle.status.soc_time' || n.props?.label==='tesla_vehicle.status.soc'),'No second SoC display');
const beforeSave=fixture.calls.length;
await page.sendSave('tesla_vehicle/profiles',fixture.config);
assert.equal(fixture.calls.length,beforeSave+1); assert.equal(fixture.calls.at(-1).topic,'tesla_vehicle/profiles_update');
// A config update while a modal is open invalidates that consent even if the
// ConfigForm dirty event has not fired yet.
fixture.show=()=>new Promise(resolve=>{confirm=resolve;});
const pendingPoll=button('read_soc').props.onClick();
const beforeStaleConfig=fixture.calls.length;
page.state.edit_index=1;confirm(true);await pendingPoll;
assert.equal(fixture.calls.length,beforeStaleConfig);
page.state.edit_index=2;
fixture.show=()=>new Promise(resolve=>{confirm=resolve;});
const pendingConfig=button('read_soc').props.onClick();
fixture.values['tesla_vehicle/profiles']={entries:[{...fixture.config.entries[0],vin:'CHANGED_WHILE_MODAL'}]};
confirm(true);await pendingConfig;
assert.equal(fixture.calls.length,beforeStaleConfig,'Changed saved configuration cancels consent');
fixture.values['tesla_vehicle/profiles']=fixture.config;
fixture.show=async()=>true;
// Consent is tied to the observed connected identity, including unplug/replug
// of the same MAC while the modal is open (not merely the profile index).
for (const replacement of ['synthetic-replacement','synthetic-nonzero']) {
    fixture.show=()=>new Promise(resolve=>{confirm=resolve;});
    const pendingIdentity=button('pair').props.onClick();
    const count=fixture.calls.length;
    fixture.values['ev/state']={active_ev_index:-1,mac:''};fixture.listeners['ev/state']();
    fixture.values['ev/state']={active_ev_index:2,mac:replacement};fixture.listeners['ev/state']();
    confirm(true);await pendingIdentity;
    assert.equal(fixture.calls.length,count,'Vehicle identity transition invalidates modal consent');
    fixture.values['ev/state']={active_ev_index:2,mac:'synthetic-nonzero'};fixture.listeners['ev/state']();
}
fixture.show=async()=>true;
// Full state field names/types, command payloads and normal registration stay aligned.
const firmware = source('src/modules/tesla_vehicle/tesla_vehicle.cpp');
const schema = source('web/src/modules/tesla_vehicle/api.ts');
const stateBlock = firmware.split('\n    tesla->state = Config::Object({')[1].split('});')[0];
const stateFields = [...stateBlock.matchAll(/\{"(\w+)", Config::(\w+)/g)];
const tsFields = [...schema.split('export interface state {')[1].split('}')[0].matchAll(/(\w+): ([^;]+);/g)];
assert.deepEqual(stateFields.map(m => m[1]).sort(), tsFields.map(m => m[1]).sort());
for (const [, field, type] of stateFields) {
    const ts = tsFields.find(m => m[1] === field)[2];
    if (type === 'Bool') assert.equal(ts, 'boolean');
    if (type === 'Float' || type === 'Uptime') assert.equal(ts, 'number');
}
for (const command of ['start', 'pair', 'poll', 'forget_key']) {
    assert(schema.includes(`export type ${command} = {ev_index: number};`));
    assert(firmware.includes(`api.addCommand("tesla_vehicle/${command}"`));
}
assert.equal((source('warp4_tesla_ble.ini').match(/Tesla Vehicle/g) ?? []).length, 3);
assert(source('src/modules/tesla_vehicle/module.ini').includes('           Ev'));
assert(!source('web/src/modules/tesla_vehicle/main.tsx').includes('bench_'));
assert(!source('web/src/modules/tesla_vehicle/main.tsx').includes('__(`'));
const translationKeys = language => [...source(`web/src/modules/tesla_vehicle/translation_${language}.tsx`).matchAll(/"(\w+)":/g)].map(m => m[1]).sort();
assert.deepEqual(translationKeys('en'), translationKeys('de'));
// OFF must expose only general Bluetooth. Missing Tesla API is not a blank
// Tesla config and can never become an accidental overwrite of persisted data.
delete fixture.values['tesla_vehicle/profiles'];
assert.equal(button('start'), undefined);
assert.equal(button('pair'), undefined);
assert(nodes().some(n => n.props?.children === 'tesla_vehicle.content.off_help'));
fixture.values['tesla_vehicle/profiles'] = fixture.config;
fixture.values['tesla_vehicle/state'] = {...active, running: false};
fixture.listeners['tesla_vehicle/state']();
assert.equal(button('start').props.disabled, false);
const beforeStart = fixture.calls.length;
await button('start').props.onClick();
assert.equal(fixture.calls.length, beforeStart + 1);
assert.equal(fixture.calls.at(-1).topic, 'tesla_vehicle/start');
assert.equal(fixture.modals.at(-1).body(), 'tesla_vehicle.content.start_confirm');
fixture.show = () => new Promise(resolve => { confirm = resolve; });
const pendingStart = button('start').props.onClick();
assert.equal(button('start').props.disabled, true);
await button('start').props.onClick();
fixture.values['bluetooth/state'].restart_required = true;
fixture.listeners['bluetooth/state']();
confirm(true); await pendingStart;
assert.equal(fixture.calls.length, beforeStart + 1); // Restart became pending during modal.
console.log('Tesla BLE production page integration, modal safety, schema and translation parity passed.');
