// Production VDOM and event handlers, synthetic-only API boundary.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
const require=createRequire(new URL('../../web/package.json',import.meta.url));
const {build}=require('esbuild');
const f={values:{},listeners:{},calls:[],modals:[],show:async()=>true,call:async()=>{}};
const mock=`const f=globalThis.fixture;
export const get=t=>f.values[t]; export const call=async(t,p)=>{f.calls.push({topic:t,payload:structuredClone(p)});await f.call(t,p);};
export const save=(t,p)=>call(t+'_update',p); export const __=k=>k;
export const addApiEventListener=(t,c)=>{(f.listeners[t]??=[]).push(c);};export const render_allowed=()=>true;
export const format_timespan_ms=ms=>String(ms);export const toLocaleFixed=(n,d)=>n.toFixed(d);export const postReboot=()=>{};export const download=async url=>({text:async()=>JSON.stringify(f.values[url.slice(1)])});
export const async_modal_ref={current:{show:async o=>{f.modals.push(o);return f.show();}}};
export class ConfigComponent{constructor(t,e,u,x){this.state={...structuredClone(f.values[t]),...x};this.dirty=false;}setState(v){Object.assign(this.state,v);}isDirty(){return this.dirty;}setDirty=(v)=>{this.dirty=v;};set(){return()=>{};}toggle(){return()=>{};}}
export const Button='Button',Alert='Alert',Table='Table',Bluetooth='Bluetooth',FormRow='FormRow',FormSeparator='FormSeparator',InputSelect='InputSelect',InputText='InputText',NavbarItem='NavbarItem',Switch='Switch',ConfigForm='ConfigForm',PageHeader='PageHeader',SubPage='SubPage';export const registerVehicleExtension=()=>{};`;
const tree=process.env.TESLA_WEB_SOURCE_TREE??'src_tfpp';
assert(['src','src_tfpp'].includes(tree));
const compiled=await build({entryPoints:[fileURLToPath(new URL(`../../web/${tree}/modules/tesla_vehicle/main.tsx`,import.meta.url))],metafile:true,bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundary',setup(b){b.onResolve({filter:/^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/ev\/main$)/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));}}]});
// This is the real import-resolved implementation, never a detached authored fixture.
assert(Object.keys(compiled.metafile.inputs).some(p=>p.endsWith(`/web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)||p.endsWith(`web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)), 'Production entrypoint must resolve the final guided component');
const module={exports:{}};
vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture:f,structuredClone,Date:{now:()=>Date.now()+(f.offset??0)},performance:{now:()=>f.now??1000},setTimeout:(fn,ms)=>{f.tick=fn;return 1;},clearTimeout:()=>{f.tick=undefined;},AbortController,crypto:globalThis.crypto});
f.values['ev/config']={evs:[0,1].map(i=>({name:`Synthetic ${i}`,mac:`11:22:33:44:55:0${i}`}))};
f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};
f.values['bluetooth/config']={enable:true};
f.values['bluetooth/state']={supported:true,phase:'active'};
f.values['tesla_vehicle/profiles']={entries:[0,1].map(i=>({ev_index:i,ev_mac:f.values['ev/config'].evs[i].mac,enable:false,vin:i?'5YJ3E1EA7KF000002':'5YJ3E1EA7KF000001',ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}))};
f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};
f.values['tesla_vehicle/guidance']={generation:'7',request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};
f.values['tesla_vehicle/discovery']={status:'idle',busy:false,targets:[]};

let page=new module.exports.TeslaVehicle();
const event=t=>{for(const cb of f.listeners[t]??[])cb();};
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const nodes=()=>walk(page.render({},page.state));
const text=n=>typeof n==='string'||typeof n==='number'?String(n):n&&typeof n==='object'?[n.props?.children].flat(Infinity).map(text).join(''):'';
const rendered=()=>text(page.render({},page.state));
const button=k=>nodes().find(n=>n.type==='Button'&&n.props.children===`tesla_vehicle.content.${k}`);
const target={name:'S3acc31774a738ea0C',ble_address:'02:AB:CD:EF:12:34',ble_address_type:1,matches_vin:true,rssi:-50};
await button('search').props.onClick();
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,ev_mac:'11:22:33:44:55:00',searched_vin:'5YJ3E1EA7KF000001',generation:'7',revision:8,result_age_ms:3000,result_ttl_ms:120000,targets:[target]};event('tesla_vehicle/discovery');
assert(rendered().includes('tesla_vehicle.content.match_found'),'Approved unique-match heading must be rendered');
assert(rendered().includes('…000001'),'Mask suffix from actual searched VIN, never sample VIN');
assert(rendered().includes(target.ble_address),'Display exact found Bluetooth address');
assert(rendered().includes('tesla_vehicle.content.found_recently'));
assert(rendered().includes('tesla_vehicle.content.adopt_help'));
assert(rendered().includes('tesla_vehicle.content.result_lifetime'));
assert(button('select_target'));
assert(!rendered().includes(target.name),'No model/name inferred from advertisement');
assert.deepEqual(f.calls.map(c=>c.topic),['tesla_vehicle/scan']);
f.now=18000;f.tick?.();
assert(rendered().includes('tesla_vehicle.content.found_seconds'),'Age must advance from authoritative 3000ms, not page load');
assert(!rendered().includes('tesla_vehicle.content.found_recently'));
f.now=118000;f.tick?.(); // 3000 + 117000 = exact TTL
assert(!button('select_target'),'Exact TTL boundary must remove usable adoption without an API event');
assert(rendered().includes('tesla_vehicle.content.result_expired'));
page.componentWillUnmount();
console.log('ADOPT_INFO_UNIQUE_CARD_PASS');
const sample=structuredClone(f.values['tesla_vehicle/discovery']);
const fresh=async(patch={})=>{
 f.now=1000;f.offset=0;f.calls=[];f.listeners={};
 f.values['tesla_vehicle/discovery']={status:'idle',revision:0,busy:false,targets:[]};
 f.values['tesla_vehicle/guidance'].generation='7';
 page=new module.exports.TeslaVehicle();await button('search').props.onClick();
 f.values['tesla_vehicle/discovery']={...structuredClone(sample),...patch};event('tesla_vehicle/discovery');
};
for(const patch of [
 {targets:[{...target,ble_address:''}]}, {targets:[{...target,ble_address:'not-a-MAC'}]},
 {targets:[{...target,ble_address_type:2}]}, {searched_vin:'5YJ3E1EA7KF000002'},
 {ev_mac:'11:22:33:44:55:01'}, {generation:'6'},
]) {
 await fresh(patch);assert(!button('select_target'),'Missing/mismatched exact result identity must not be offered');
 assert(!nodes().some(n=>n.type==='Alert'&&n.props.variant==='success'));page.componentWillUnmount();
}
for(const patch of [
 {targets:[target,{...target,ble_address:'02:AB:CD:EF:12:35'}]}, {targets:[]},
 {truncated:true}, {targets:[{...target,matches_vin:false}]}, {ev_index:1},
 {result_age_ms:undefined},{result_ttl_ms:undefined},{result_age_ms:-1},{result_age_ms:NaN},
 {result_age_ms:120000},{result_age_ms:120001},{result_ttl_ms:0},{result_ttl_ms:120001},
]) {
 await fresh(patch);assert(!button('select_target'));assert(!nodes().some(n=>n.type==='Alert'&&n.props.variant==='success'));
 assert.equal(f.calls.length,1);page.componentWillUnmount();
}
await fresh();const stale=button('select_target').props.onClick;
f.now=117999;f.tick?.();assert(button('select_target'),'One ms before TTL still valid');
f.now=118000;f.tick?.();assert(!button('select_target'));await stale();assert.equal(f.calls.length,1);
// Repeated/out-of-order samples of the same result cannot refresh its age.
f.values['tesla_vehicle/discovery'].result_age_ms=0;event('tesla_vehicle/discovery');assert(!button('select_target'));page.componentWillUnmount();
await fresh();f.values['tesla_vehicle/discovery'].revision=7;event('tesla_vehicle/discovery');assert(!button('select_target'),'Older result revisions never revive');page.componentWillUnmount();
await fresh();f.values['tesla_vehicle/discovery']={status:'idle',revision:9,busy:false,targets:[]};event('tesla_vehicle/discovery');
assert(rendered().includes('tesla_vehicle.content.result_expired'));assert(!button('select_target'));page.componentWillUnmount();
// Reload/navigation must never turn cached data into a locally authorized result.
await fresh();page.componentWillUnmount();f.calls=[];page=new module.exports.TeslaVehicle();
for(const t of Object.keys(f.listeners))event(t);assert(!button('select_target'));assert.equal(f.calls.length,0);page.componentWillUnmount();
for(const mutate of [
 ()=>{page.selectProfile('1');page.selectProfile('0');},
 ()=>{f.values['tesla_vehicle/guidance'].generation='8';event('tesla_vehicle/guidance');f.values['tesla_vehicle/guidance'].generation='7';event('tesla_vehicle/guidance');},
 ()=>{const old=structuredClone(f.values['ev/state']);f.values['ev/state'].active_ev_index=1;event('ev/state');f.values['ev/state']=old;event('ev/state');},
 ()=>{const old=structuredClone(f.values['tesla_vehicle/profiles']);f.values['tesla_vehicle/profiles']={entries:[]};event('tesla_vehicle/profiles');f.values['tesla_vehicle/profiles']=old;event('tesla_vehicle/profiles');},
 ()=>{f.offset=-100000;f.tick?.();assert(!button('select_target'));f.offset=0;},
 ()=>{f.now=0;f.tick?.();assert(!button('select_target'));f.now=1000;},
 ()=>{f.offset=200000;},
]) {
 await fresh();const old=button('select_target').props.onClick;mutate();assert(!button('select_target'));await old();assert.equal(f.calls.length,1);page.componentWillUnmount();
}
// Exact actual second VIN/MAC and saved Details distinction, no invented value.
await fresh();page.selectProfile('1');await button('search').props.onClick();
f.values['tesla_vehicle/discovery']={...sample,revision:9,ev_index:1,ev_mac:'11:22:33:44:55:01',searched_vin:'5YJ3E1EA7KF000002',targets:[{...target,ble_address:'02:FE:DC:BA:98:76'}]};event('tesla_vehicle/discovery');
assert(rendered().includes('…000002'));assert(rendered().includes('02:FE:DC:BA:98:76'));
button('show_details').props.onClick();const diagnostic=nodes().find(n=>n.type==='FormRow'&&n.props.label==='tesla_vehicle.content.ble_address');
assert.equal(walk(diagnostic).find(n=>n.type==='InputText').props.value,'–','Found address is not yet the saved address');
await button('select_target').props.onClick();assert.equal(f.calls.at(-1).payload.entries[1].ble_address,'02:FE:DC:BA:98:76');
assert(f.calls.every(c=>['tesla_vehicle/scan','tesla_vehicle/profiles_update'].includes(c.topic)));page.componentWillUnmount();
console.log('ADOPT_INFO_FRESHNESS_IDENTITY_MATRIX_PASS');
