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
    values: {'ev/config': {evs: []}}, listeners: {}, calls: [], modals: [],
    show: async () => true,
    call: async () => {},
    config: {enable: true, ev_index: 2},
};
const mock = `
const f = globalThis.fixture;
export const get = topic => f.values[topic];
export const call = async (topic, payload) => { f.calls.push({topic, payload}); await f.call(); };
export const save = async (topic, payload) => call(topic + '_update', payload);
export const postReboot = () => {};
export const __ = key => key;
export const addApiEventListener = (topic, cb) => { f.listeners[topic] = cb; };
export const render_allowed = () => true;
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
export const ConfigForm = 'ConfigForm', PageHeader = 'PageHeader';
export const SubPage = Object.assign(() => {}, {Status: 'Status', Config: 'Config'});
`;
const compiled = await build({
    entryPoints: [fileURLToPath(new URL('web/src/modules/tesla_vehicle/main.tsx', root))],
    bundle: true, write: false, platform: 'node', format: 'cjs',
    plugins: [{name: 'in-memory-boundaries', setup(b) {
        b.onResolve({filter: /^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$)/}, args => ({path: args.path, namespace: 'boundary'}));
        b.onLoad({filter: /.*/, namespace: 'boundary'}, () => ({contents: mock, loader: 'js'}));
    }}],
});
const module = {exports: {}};
vm.runInNewContext(compiled.outputFiles[0].text, {module, exports: module.exports, fixture});
const {TeslaVehicle, TeslaVehicleNavbar} = module.exports;
const active = {running: true, paired: false, key_present: true, key_presence: 'present', last_soc: null};
fixture.values['tesla_vehicle/state'] = active;
fixture.values['ev/state'] = {active_ev_index: 2, mac: 'synthetic-nonzero'};
const page = new TeslaVehicle();
const walk = node => !node || typeof node !== 'object' ? [] :
    [node, ...[node.props?.children].flat(Infinity).flatMap(walk)];
const nodes = () => walk(page.render({}, page.state));
const button = suffix => nodes().find(n => n.type === 'Button' && n.props.children === `tesla_vehicle.content.${suffix}`);
assert.equal(TeslaVehicleNavbar().props.module, 'tesla_vehicle');
assert.equal(button('read_soc').props.disabled, false);
assert(nodes().some(n => n.props?.value === 'tesla_vehicle.status.key_only'));
assert(nodes().some(n => n.props?.value === '–')); // JSON null SoC is never formatted.
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
    assert.equal(JSON.stringify(fixture.calls.at(-1).payload), '{}');
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
// Full state field names/types, command payloads and normal registration stay aligned.
const firmware = source('src/modules/tesla_vehicle/tesla_vehicle.cpp');
const schema = source('web/src/modules/tesla_vehicle/api.ts');
const stateBlock = firmware.split('\n    state = Config::Object({')[1].split('});')[0];
const stateFields = [...stateBlock.matchAll(/\{"(\w+)", Config::(\w+)/g)];
const tsFields = [...schema.split('export interface state {')[1].split('}')[0].matchAll(/(\w+): ([^;]+);/g)];
assert.deepEqual(stateFields.map(m => m[1]).sort(), tsFields.map(m => m[1]).sort());
for (const [, field, type] of stateFields) {
    const ts = tsFields.find(m => m[1] === field)[2];
    if (type === 'Bool') assert.equal(ts, 'boolean');
    if (type === 'Float' || type === 'Uptime') assert.equal(ts, 'number');
}
for (const command of ['pair', 'poll', 'forget_key']) {
    assert(schema.includes(`export type ${command} = {};`));
    assert(firmware.includes(`api.addCommand("tesla_vehicle/${command}", &empty_command`));
}
assert.equal((source('warp4_tesla_ble.ini').match(/Tesla Vehicle/g) ?? []).length, 3);
assert(source('src/modules/tesla_vehicle/module.ini').includes('           Ev'));
assert(!source('web/src/modules/tesla_vehicle/main.tsx').includes('bench_'));
assert(!source('web/src/modules/tesla_vehicle/main.tsx').includes('__(`'));
const translationKeys = language => [...source(`web/src/modules/tesla_vehicle/translation_${language}.tsx`).matchAll(/"(\w+)":/g)].map(m => m[1]).sort();
assert.deepEqual(translationKeys('en'), translationKeys('de'));
console.log('Tesla BLE production page integration, modal safety, schema and translation parity passed.');
