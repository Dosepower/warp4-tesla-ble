import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const root = new URL('../../', import.meta.url);
const source = path => readFileSync(new URL(path, root), 'utf8');
if (process.argv[2] === 'group') {
    const ini = source('warp4_tesla_ble.ini').split('custom_frontend_components =')[1].split('\n\n')[0];
    assert(ini.indexOf('Interfaces Group $ Open') >= 0);
    assert(ini.indexOf('Tesla Vehicle') > ini.indexOf('Interfaces Group $ Open'));
    assert(ini.indexOf('Tesla Vehicle') < ini.indexOf('Interfaces Group $ Close'));
    assert.equal((ini.match(/Tesla Vehicle/g) || []).length, 1);
    console.log('R13 Bluetooth inside Interfaces passed');
    process.exit(0);
}
const require = createRequire(new URL('../../web/package.json', import.meta.url));
const bluetoothSchema = source('web/src/modules/tesla_vehicle/api.ts').split('export interface state___bluetooth {')[1];
assert.match(bluetoothSchema.match(/unsupported_reason:\s*([^;]+);/)[1], /'storage_backend_unverified'/,
    'Bluetooth unsupported_reason must include the backend storage_backend_unverified token');
if (process.argv[2] === 'schema') {
    console.log('R13 Bluetooth storage backend rejection schema passed');
    process.exit(0);
}
const {build} = require('esbuild');
const fixture = {values: {}, listeners: {}, calls: [], modals: [], config: {enable: false}, show: async () => true, fail: false};
const mock = `const f = globalThis.fixture;
export const get = t => f.values[t];
export const call = async (t,p) => { f.calls.push(t); if(f.fail) throw Error('commit refused'); };
export const save = async (t,p) => { f.calls.push(t+'_update'); if(f.fail) throw Error('commit refused'); };
export const __ = k => k;
export const addApiEventListener = (t,cb) => {f.listeners[t]=cb;};
export const postReboot = () => {};
export const render_allowed = () => true;
export const toLocaleFixed = (n,d) => n.toFixed(d);
export const format_timespan_ms = ms => String(ms);
export const async_modal_ref = {current: {show: async opts => {f.modals.push(opts); return f.show();}}};
export class ConfigComponent {
 constructor(t,e,r,s) {this.state={...f.config,...s};this.dirty=false;this.topic=t;}
 setState(s) {Object.assign(this.state,s);} isDirty(){return this.dirty;}
 setDirty(d){this.dirty=d;} set(){return ()=>{};} toggle(k){return ()=>{this.state[k]=!this.state[k];};}
 save = async () => this.sendSave(this.topic,{enable:this.state.enable});
}
export const Table='Table',Button='Button',Alert='Alert',Bluetooth='Bluetooth',BatteryCharging='BatteryCharging';
export const FormRow='FormRow',FormSeparator='FormSeparator',InputText='InputText',InputSelect='InputSelect',InputNumber='InputNumber',NavbarItem='NavbarItem',Switch='Switch',ConfigForm='ConfigForm',PageHeader='PageHeader';
export const registerVehicleExtension=()=>{};
export const SubPage=Object.assign(()=>{}, {Status:'Status',Config:'Config'});`;
const result = await build({entryPoints:[fileURLToPath(new URL('web/src/modules/tesla_vehicle/main.tsx',root))],bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundaries',setup(b){
 b.onResolve({filter:/^(\.\.\/ev\/main$|\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$)/},a=>({path:a.path,namespace:'boundary'}));
 b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));
}}]});
const module={exports:{}};
vm.runInNewContext(result.outputFiles[0].text,{module,exports:module.exports,fixture});
const {BluetoothSettings,BluetoothPageNavbar}=module.exports;
assert.equal(typeof BluetoothSettings,'function');
assert.equal(BluetoothPageNavbar().props.title,'tesla_vehicle.bluetooth.title');
fixture.values['bluetooth/state']={supported:true,phase:'disabled',restart_required:false,persistence_error:false};
const page=new BluetoothSettings();
const walk=n=>!n || typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const nodes=()=>walk(page.render({},page.state));
const restart=()=>nodes().find(n=>n.type==='Button');
// SubPage.Config is a null-returning marker, not a usable nested form.
assert(nodes().some(n=>n.type==='ConfigForm'));
assert(!restart(),'Approved page has no permanent disabled restart button');
const sw=nodes().find(n=>n.type==='Switch');
sw.props.onClick(); assert.equal(fixture.calls.length,0); assert.equal(fixture.modals.length,0);
fixture.fail=true; await assert.rejects(page.save(),/commit refused/);
assert(!fixture.calls.includes('bluetooth/restart'));
fixture.fail=false; await page.save();
assert.equal(fixture.calls.at(-1),'bluetooth/config_update');
assert(!fixture.calls.includes('bluetooth/restart'));
fixture.values['bluetooth/state']={supported:true,phase:'disabled',restart_required:true,persistence_error:false};
fixture.listeners['bluetooth/state']();
assert.equal(restart().props.disabled,false);
fixture.show=async()=>false; await restart().props.onClick();
assert(!fixture.calls.includes('bluetooth/restart'));
fixture.show=async()=>true; await restart().props.onClick();
assert.equal(fixture.calls.at(-1),'bluetooth/restart');
let confirm; fixture.show=()=>new Promise(r=>{confirm=r;});
const pending=restart().props.onClick();
fixture.values['bluetooth/state']={supported:false,unsupported_reason:'sdk_disabled',restart_required:true};
fixture.listeners['bluetooth/state'](); confirm(true); await pending;
assert.equal(fixture.calls.filter(t=>t==='bluetooth/restart').length,1);
assert(nodes().find(n=>n.type==='Switch').props.disabled);
assert(!restart(),'Unsupported Bluetooth cannot expose a restart action');
await assert.rejects(page.save());
for(const lang of ['de','en']) {
 const tr=source(`web/src/modules/tesla_vehicle/translation_${lang}.tsx`);
 assert(tr.includes('"tesla_title"')); assert(tr.includes('"unsupported"'));
}
console.log('R13 real Bluetooth VDOM, commit failure, no toggle reboot, confirmation and unsupported guards passed');
