// Retain real Fahrzeuge extension registration/render coverage after guided split.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
const require=createRequire(new URL('../../web/package.json',import.meta.url));
const {build}=require('esbuild');
const root=new URL('../../',import.meta.url);
const fixture={values:{'ev/config':{evs:[{name:'Synthetic A',mac:'11:22:33:44:55:00',capacity:60,charging_efficiency:0.9,read_soc:false,autocharge:false}]}},calls:[],listeners:{}};
const mock=`const f=globalThis.fixture;
export const get=t=>f.values[t];export const call=async(t,p)=>{f.calls.push({t,p});};export const save=call;export const postReboot=()=>{};export const download=async()=>{throw Error('Unexpected read');};
export const __=k=>k==='ev.content.add_ev_message'?(()=>k):k;export const toLocaleFixed=(n,d)=>n.toFixed(d);export const format_timespan_ms=ms=>String(ms);
export const addApiEventListener=(t,c)=>{f.listeners[t]=c;};export const render_allowed=()=>true;export const is_central_management_enabled=()=>false;
export const async_modal_ref={current:{show:async()=>true}};
export class ConfigComponent{constructor(t,e,u,x){this.state={...f.values[t],...x};this.dirty=false;}setState(v){Object.assign(this.state,v);}isDirty(){return this.dirty;}setDirty=()=>{};set(){return()=>{};}toggle(){return()=>{};}}
export const Button='Button',Alert='Alert',Bluetooth='Bluetooth',BatteryCharging='BatteryCharging',FormRow='FormRow',FormSeparator='FormSeparator',InputNumber='InputNumber',InputSelect='InputSelect',InputText='InputText',NavbarItem='NavbarItem',Switch='Switch',Table='Table',InputFloat='InputFloat',InputTextPatterned='InputTextPatterned',StatusSection='StatusSection',DiscoveryResultItem='DiscoveryResultItem',DiscoveryResultGroup='DiscoveryResultGroup',Plus='Plus',ConfigForm='ConfigForm',PageHeader='PageHeader';
export const SubPage=Object.assign(()=>{},{Status:'Status',Config:'Config'});export const get_charge_manager_auth_info=async()=>[];`;
const tree=process.env.TESLA_WEB_SOURCE_TREE??'src_tfpp';
assert(['src','src_tfpp'].includes(tree));
const compiled=await build({stdin:{contents:`export * from "./web/${tree}/modules/tesla_vehicle/main.tsx";export {Ev} from "./web/${tree}/modules/ev/main.tsx";`,resolveDir:fileURLToPath(root),loader:'tsx'},bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'api-boundary',setup(b){
 b.onResolve({filter:/^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/users\/main$)/},a=>({path:a.path,namespace:'boundary'}));
 b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));
}}]});
const module={exports:{}};vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture,AbortController,structuredClone,setTimeout,clearTimeout});
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
fixture.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};
const {TeslaVehicle,Ev}=module.exports;const ev=new Ev();
assert.equal(module.exports.TeslaVehicleNavbar,undefined);
assert(!walk(ev.render({},ev.state)).some(n=>n.type===TeslaVehicle));
module.exports.pre_init();const nodes=walk(ev.render({},ev.state));
assert.equal(nodes.filter(n=>n.type===TeslaVehicle).length,1,'Exactly one inline Tesla extension');
assert(nodes.findIndex(n=>n.type===TeslaVehicle)>nodes.findIndex(n=>n.props?.label==='ev.content.ev_profiles'));
assert(!nodes.some(n=>n.type==='NavbarItem'));assert.equal(fixture.calls.length,0);
// Resolve the registered extension from the actual Ev render; the surrounding
// Fahrzeuge tree must not acquire any title/separator/container of its own.
const ordinary=walk(ev.render({},ev.state)).filter(n=>n.type!==TeslaVehicle).map(n=>[n.type,n.props?.label]);
const extensionNode=nodes.find(n=>n.type===TeslaVehicle);
fixture.values['bluetooth/state']={supported:true,phase:'active',restart_required:false};
const extension=new extensionNode.type();
const persisted=JSON.stringify(fixture.values);
assert.equal(extension.render({},extension.state),null,'Unknown global config must hide the entire registered extension including title/separator');
fixture.values['bluetooth/config']={enable:false};
fixture.listeners['bluetooth/config']?.();
assert.equal(extension.render({},extension.state),null,'Persisted OFF hides even when runtime still active');
assert.equal(JSON.stringify({...fixture.values,'bluetooth/config':undefined}),JSON.stringify({...JSON.parse(persisted),'bluetooth/config':undefined}),'Visibility never mutates EV/Tesla data');
fixture.values['bluetooth/config']={enable:true};fixture.listeners['bluetooth/config']?.();
assert(walk(extension.render({},extension.state)).some(n=>n.type==='h2'),'Enabled shows title even with unknown Tesla metadata');
fixture.values['bluetooth/state']={supported:true,phase:'disabled',restart_required:true};fixture.listeners['bluetooth/state']?.();
assert(walk(extension.render({},extension.state)).some(n=>n.type==='h2'),'Persisted ON pending reboot shows extension, not radio readiness');
assert.equal(extension.searchAllowed(),false,'Pending reboot must retain existing action gate');
for(const value of [undefined,{}, {enable:false},{enable:'true'},{enable:1}]){
 fixture.values['bluetooth/config']=value;fixture.listeners['bluetooth/config']?.();
 assert.equal(extension.render({},extension.state),null,'Only authoritative boolean true exposes any extension DOM');
}
assert.deepEqual(walk(ev.render({},ev.state)).filter(n=>n.type!==TeslaVehicle).map(n=>[n.type,n.props?.label]),ordinary,'Ordinary WARP profiles and wrapper remain identical');
extension.componentWillUnmount();assert.equal(fixture.calls.length,0);
console.log('GUIDED_REAL_EV_MOUNT_PASS actual registration, whole title/separator/space hidden unknown/OFF; ON pending reboot retains gates; ordinary profiles/data unchanged; zero actions');
