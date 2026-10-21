// Execute the real Bluetooth-only VDOM and browser confirmation handlers.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
const require = createRequire(new URL('../../web/package.json', import.meta.url));
const {build} = require('esbuild');
const root = new URL('../../', import.meta.url);
const fixture = {values: {}, listeners: {}, calls: [], modals: [], show: async () => true, fail: false};
const mock = `
const f = globalThis.fixture;
export const get = topic => f.values[topic];
export const call = async (topic, payload) => { f.calls.push({topic,payload}); if(f.fail) throw new Error('commit failure'); };
export const save = async (topic,payload) => call(topic + '_update',payload);
export const postReboot = () => {};
export const __ = key => key;
export const addApiEventListener = (topic,cb) => { f.listeners[topic] = cb; };
export const render_allowed = () => true;
export const format_timespan_ms = ms => ms + ' ms';
export const toLocaleFixed = (n,d) => n.toFixed(d);
export const async_modal_ref = {current:{show:async options => { f.modals.push(options); return f.show(); }}};
export class ConfigComponent {
 constructor(topic,error,unused,extra) { this.state = {...f.values[topic],...extra}; this.dirty=false; }
 setState(v) { Object.assign(this.state,v); } isDirty() { return this.dirty; }
 set() { return () => {}; } toggle() { return () => {}; }
}
export const Button='Button', Alert='Alert', Table='Table', Bluetooth='Bluetooth';
export const FormRow='FormRow', FormSeparator='FormSeparator', InputNumber='InputNumber';
export const InputSelect='InputSelect', InputText='InputText', NavbarItem='NavbarItem', Switch='Switch';
export const registerVehicleExtension=()=>{};
export const ConfigForm='ConfigForm', PageHeader='PageHeader', SubPage='SubPage';
`;
const compiled = await build({entryPoints:[fileURLToPath(new URL('web/src/modules/tesla_vehicle/main.tsx',root))],bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundaries',setup(b) {
 b.onResolve({filter:/^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/ev\/main$)/},args=>({path:args.path,namespace:'boundary'}));
 b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));
}}]});
const module={exports:{}};
vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture});
const {BluetoothPage,BluetoothPageNavbar,BluetoothSettings}=module.exports;
assert.equal(module.exports.TeslaVehicleNavbar, undefined);
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
for (const phase of ['disabled','starting','active','stopping','error']) {
 fixture.values['bluetooth/config']={enable:phase!=='disabled'};
 fixture.values['bluetooth/state']={supported:true,phase,restart_required:true,persistence_error:false};
 const page=BluetoothPage();
 const nodes=walk(page);
 assert.equal(BluetoothPageNavbar().props.name, 'bluetooth');
 assert.equal(page.props.name, 'bluetooth');
 assert(nodes.some(n=>n.type===BluetoothSettings));
 const settings = new BluetoothSettings();
 const settingsNodes = walk(settings.render({},settings.state));
 assert(!settingsNodes.some(n=>n.props?.children==='tesla_vehicle.bluetooth.active_hint'),'Approved page has no permanent Tesla paragraph, even when active');
 assert(!settingsNodes.some(n=>n.props?.label==='tesla_vehicle.bluetooth.runtime'));
 assert(!settingsNodes.some(n=>n.type==='a' || n.props?.href || n.type==='NavbarItem'));
 assert.equal(settingsNodes.filter(n=>n.type==='Button').length,1,'Only restart button');
 assert(!nodes.some(n=>n.props?.id==='tesla_vehicle_config_form'));
 assert(!nodes.some(n=>n.type==='Button')); // page contains Bluetooth child only
}
const bt=new BluetoothSettings();
const nodes=()=>walk(bt.render({},bt.state));
const restart=()=>nodes().find(n=>n.type==='Button');
assert(nodes().some(n=>n.props?.id==='bluetooth_config_form'));
assert(nodes().some(n=>n.type==='Switch'));
fixture.calls=[];
await bt.sendSave('bluetooth/config',{enable:true});
assert.equal(fixture.calls.length,1); assert.equal(fixture.calls[0].topic,'bluetooth/config_update');
fixture.calls=[];
fixture.show=async()=>false;
await restart().props.onClick(); assert.equal(fixture.calls.length,0);
fixture.show=async()=>true;
const retainedRestart=restart();
bt.dirty=true; assert(!restart(),'No non-actionable restart button');
await retainedRestart.props.onClick(); assert.equal(fixture.calls.length,0);
bt.dirty=false;
bt.state.bluetooth_state.persistence_error=true;
assert(!restart());
await retainedRestart.props.onClick(); assert.equal(fixture.calls.length,0);
bt.state.bluetooth_state.persistence_error=false;
let confirm;
fixture.show=()=>new Promise(resolve=>{confirm=resolve;});
const pending=restart().props.onClick();
await restart().props.onClick(); // no double-submit
bt.state.bluetooth_state.restart_required=false;
confirm(true); await pending; assert.equal(fixture.calls.length,0);
bt.state.bluetooth_state.restart_required=true;
fixture.show=async()=>true;
await restart().props.onClick();
assert.equal(fixture.calls.length,1); assert.equal(fixture.calls[0].topic,'bluetooth/restart');
assert.equal(fixture.modals.at(-1).body(),'tesla_vehicle.bluetooth.restart_confirm');
fixture.fail=true;
await assert.rejects(bt.sendSave('bluetooth/config',{enable:false}),/commit failure/);
assert(fixture.calls.every(c=>c.topic.startsWith('bluetooth/')));
console.log('MANUAL_WEB_FRONTEND_PASS Bluetooth-only branch in all 5 phases; save, cancel, confirm, dirty, persistence error, stale modal and double-submit gates');
