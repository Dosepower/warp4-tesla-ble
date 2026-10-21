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
export const format_timespan_ms=ms=>String(ms);export const toLocaleFixed=(n,d)=>n.toFixed(d);export const postReboot=()=>{};
export const async_modal_ref={current:{show:async o=>{f.modals.push(o);return f.show();}}};
export class ConfigComponent{constructor(t,e,u,x){this.state={...structuredClone(f.values[t]),...x};this.dirty=false;}setState(v){Object.assign(this.state,v);}isDirty(){return this.dirty;}setDirty=(v)=>{this.dirty=v;};set(){return()=>{};}toggle(){return()=>{};}}
export const Button='Button',Alert='Alert',Table='Table',Bluetooth='Bluetooth',FormRow='FormRow',FormSeparator='FormSeparator',InputSelect='InputSelect',InputText='InputText',NavbarItem='NavbarItem',Switch='Switch',ConfigForm='ConfigForm',PageHeader='PageHeader',SubPage='SubPage';export const registerVehicleExtension=()=>{};`;
const tree=process.env.TESLA_WEB_SOURCE_TREE??'src_tfpp';
assert(['src','src_tfpp'].includes(tree));
const compiled=await build({entryPoints:[fileURLToPath(new URL(`../../web/${tree}/modules/tesla_vehicle/main.tsx`,import.meta.url))],metafile:true,bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundary',setup(b){b.onResolve({filter:/^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/ev\/main$)/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));}}]});
// This is the real import-resolved implementation, never a detached authored fixture.
assert(Object.keys(compiled.metafile.inputs).some(p=>p.endsWith(`/web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)||p.endsWith(`web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)), 'Production entrypoint must resolve the final guided component');
const module={exports:{}};
vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture:f,structuredClone,Date:{now:()=>Date.now()+(f.offset??0)},setTimeout:(fn,ms)=>setTimeout(()=>{f.offset=(f.offset??0)+(f.advance??0);fn();},f.advance?0:ms),clearTimeout,AbortController,crypto:globalThis.crypto});
f.values['ev/config']={evs:[0,1].map(i=>({name:`Synthetic ${i}`,mac:`11:22:33:44:55:0${i}`}))};
f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};
f.values['bluetooth/state']={supported:true,phase:'active'};
f.values['tesla_vehicle/profiles']={entries:[0,1].map(i=>({ev_index:i,ev_mac:f.values['ev/config'].evs[i].mac,enable:true,vin:i?'5YJ3E1EA7KF000002':'5YJ3E1EA7KF000001',ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}))};
f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};
f.values['tesla_vehicle/guidance']={generation:'7',request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};
f.values['tesla_vehicle/discovery']={status:'idle',busy:false,targets:[]};
const page=new module.exports.TeslaVehicle();
const event=t=>{for(const cb of f.listeners[t]??[])cb();};
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const nodes=()=>walk(page.render({},page.state));
const button=k=>nodes().find(n=>n.type==='Button'&&(n.props.children===`tesla_vehicle.content.${k}` || k==='search'&&n.props.children==='tesla_vehicle.content.searching'));
assert.equal(f.calls.length,0);
await button('search').props.onClick();
const target={name:'S3acc31774a738ea0C',ble_address:'01:02:03:04:05:06',ble_address_type:1,matches_vin:true,rssi:-50};
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,revision:8,targets:[target]};event('tesla_vehicle/discovery');
await button('select_target').props.onClick();
assert.equal(f.calls.at(-1).topic,'tesla_vehicle/profiles_update','Adopt itself saves the existing profile transaction; no second Save');
assert.equal(f.calls.at(-1).payload.entries[0].ble_address_type,1);
assert.equal(f.calls.at(-1).payload.entries[0].discovery_revision,8);
assert.equal(f.calls.at(-1).payload.entries[1].ble_address,'');
assert(nodes().some(n=>n.props.children==='tesla_vehicle.content.adopted'));
assert(!nodes().some(n=>n.props.children==='tesla_vehicle.content.search_empty'),'Successful adoption must not contradict itself with an empty-result warning');
// Permission persistence is local and remains available with global Bluetooth off.
const beforePermission=structuredClone(page.state.entries);const beforeBluetooth=f.values['bluetooth/state'];
f.values['bluetooth/state']={supported:true,phase:'off'};
await page.sendSave('tesla_vehicle/profiles',{entries:beforePermission});
assert.equal(f.calls.at(-1).topic,'tesla_vehicle/profiles_update');
f.values['bluetooth/state']=beforeBluetooth;
console.log('GUIDED_ADOPTION_PASS');
f.values['tesla_vehicle/profiles']=structuredClone(f.calls.at(-1).payload);
assert.equal(button('pair').props.disabled,false,'Explicit pairing can prepare the correct local profile internally');
f.call=async(t,p)=>{
 if(t==='tesla_vehicle/guided' && p.phase==='prepare_pair') {
  assert.equal(f.calls.filter(c=>c.payload.phase==='commit').length,0);
  f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],request:p.request,ready:true};event('tesla_vehicle/guidance');
 }
};
await button('pair').props.onClick();
assert.deepEqual(f.calls.slice(-2).map(c=>[c.topic,c.payload.phase]),[['tesla_vehicle/guided','prepare_pair'],['tesla_vehicle/guided','commit']]);
assert.equal(f.calls.at(-2).payload.generation,'7');
assert.match(f.calls.at(-2).payload.request,/^[0-9a-f]{32}$/,'ES6-compatible formatting retains the exact 128-bit consent ID');
assert.equal(f.calls.at(-1).payload.request,f.calls.at(-2).payload.request);
assert(!button('start'),'No separate local-load action');
assert.equal(page.state.command_busy,false);
console.log('GUIDED_EXPLICIT_SEQUENCE_PASS');
f.values['tesla_vehicle/guidance'].profiles[0].binding_fresh=false;event('tesla_vehicle/guidance');
assert.equal(button('pair').props.disabled,true,'Expired discovery explains and disables pairing');
console.log('GUIDED_FRESHNESS_GATE_PASS');
const good=structuredClone(f.values['tesla_vehicle/profiles']);
const normal=()=>{
 f.values['tesla_vehicle/profiles']=structuredClone(good);page.state.entries=structuredClone(good.entries);page.dirty=false;
 f.values['tesla_vehicle/guidance']={generation:'7',request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};event('tesla_vehicle/guidance');
 f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};event('tesla_vehicle/state');
 f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};event('ev/state');
 f.values['bluetooth/state']={supported:true,phase:'active'};event('bluetooth/state');
 page.selectProfile('0');f.show=async()=>true;f.call=async()=>{};
};
normal();
let count=f.calls.length;
for(const topic of Object.keys(f.listeners))event(topic);
nodes();assert.equal(f.calls.length,count,'Render and status/config events never send radio commands');
page.setProfile({enable:false});assert.equal(f.calls.length,count,'Toggle alone sends nothing');
await page.sendSave('tesla_vehicle/profiles',{entries:page.state.entries});
assert.equal(f.calls.length,count+1);assert.equal(f.calls.at(-1).topic,'tesla_vehicle/profiles_update');normal();
for(const change of [{supported:false},{phase:'disabled'},{phase:'starting'},{restart_required:true},{persistence_error:true}]){
 f.values['bluetooth/state']={supported:true,phase:'active',...change};event('bluetooth/state');
 assert.equal(button('pair').props.disabled,true);assert.equal(button('read_soc')?.props.disabled??true,true);
 count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count);
 assert(nodes().some(n=>n.props.children==='tesla_vehicle.content.bluetooth_blocked'));
}normal();
for(const change of [{stopping:true},{restart_required:true},{action_busy:true},{pairing_pending:true},{legacy_blocked:true}]){
 f.values['tesla_vehicle/state']={profile_ev_index:-1,...change};event('tesla_vehicle/state');
 assert.equal(button('pair').props.disabled,true);count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count);
}normal();
f.show=async()=>false;count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count);normal();
for(const mutate of [()=>page.selectProfile('1'),()=>{page.selectProfile('1');page.selectProfile('0');},()=>{
 f.values['ev/state']={active_ev_index:-1,mac:''};event('ev/state');f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00'};event('ev/state');
},()=>{f.values['tesla_vehicle/profiles']={entries:[]};event('tesla_vehicle/profiles');},()=>{f.values['bluetooth/state'].restart_required=true;event('bluetooth/state');}]){
 let confirm;f.show=()=>new Promise(r=>{confirm=r;});count=f.calls.length;
 const pending=button('pair').props.onClick();await button('pair').props.onClick();mutate();confirm(true);await pending;
 assert.equal(f.calls.length,count,'Changed selection/config/identity cancels modal consent (including A-B-A)');normal();
}
f.call=async()=>{throw Error('injected prepare refusal');};count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count+1);assert.equal(page.state.command_busy,false);normal();
// Correct request alone is insufficient: a generation change before ACK cancels commit.
f.call=async(t,p)=>{if(p.phase==='prepare_pair'){
 f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],generation:'8',ready:true,request:p.request};event('tesla_vehicle/guidance');
}};count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count+1);normal();
// Existing paired profile is compact and polls via preparation, never re-pairs.
f.values['tesla_vehicle/guidance'].profiles[0]={ev_index:0,paired:true,key_presence:'present',binding_fresh:true};event('tesla_vehicle/guidance');
assert(nodes().some(n=>n.type==='summary'&&n.props.children==='tesla_vehicle.content.manage'));
assert.equal(button('pair').props.disabled,true);assert.equal(button('read_soc').props.disabled,false);
f.call=async(t,p)=>{if(p.phase==='prepare_poll'){f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}};
await button('read_soc').props.onClick();assert.deepEqual(f.calls.slice(-2).map(c=>c.payload.phase),['prepare_poll','commit']);
assert(!nodes().some(n=>String(n.props.value).includes('null %')));
f.values['ev/state'].soc=63.5;f.values['tesla_vehicle/state']={profile_ev_index:0,last_soc:63.5,last_update:12345,key_presence:'present',key_present:true,paired:true};event('ev/state');event('tesla_vehicle/state');
assert(nodes().some(n=>n.props.value==='63.5 %'));assert(nodes().some(n=>n.props.label==='tesla_vehicle.status.soc_time'));
f.values['ev/state'].soc=null;event('ev/state');assert(!nodes().some(n=>n.props.value==='63.5 %'));normal();
// Unique-only, TTL and unsaved/config scope protections for discovery adoption.
await button('search').props.onClick();
for(const targets of [[{...target,matches_vin:false}],[target,{...target,ble_address:'02:02:03:04:05:06'}],[]]){
 f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,revision:20,targets};event('tesla_vehicle/discovery');
 assert(!button('select_target'));count=f.calls.length;await page.selectTarget(target);assert.equal(f.calls.length,count);
}
for(const vin of ['', '5YJ3E1EA7KF00000I', '5yj3E1EA7KF000001','5YJ3E1EA7KF0000010']){page.setProfile({vin});assert.equal(button('search').props.disabled,true);}
normal();
// An in-flight A adoption response never marks B (or A-B-A) as saved.
await button('search').props.onClick();f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,revision:21,targets:[target]};event('tesla_vehicle/discovery');
let savedResponse;f.call=()=>new Promise(r=>{savedResponse=r;});const pendingSave=button('select_target').props.onClick();page.selectProfile('1');page.selectProfile('0');savedResponse();await pendingSave;assert.equal(page.state.adopted,false);normal();
// OFF exposes no Tesla actions and cannot synthesize a writable config.
const savedApi=f.values['tesla_vehicle/profiles'];delete f.values['tesla_vehicle/profiles'];
assert(!button('pair')&&!button('search')&&!button('read_soc'));count=f.calls.length;nodes();assert.equal(f.calls.length,count);f.values['tesla_vehicle/profiles']=savedApi;
// Unmount while the explicitly requested local preparation is in flight cannot commit later.
f.advance=13000;count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count+1,'No commit on missing ACK/timeout');assert(page.state.command_error);f.advance=0;f.offset=0;normal();
await button('search').props.onClick();f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,revision:25,targets:[target]};event('tesla_vehicle/discovery');
f.offset=110001;assert(!button('select_target'),'Client expiry hides even a cached matching result');f.offset=0;
f.call=async(t,p)=>{if(t==='tesla_vehicle/profiles_update'){f.values['tesla_vehicle/profiles']=structuredClone(p);event('tesla_vehicle/profiles');}};
await button('select_target').props.onClick();assert(page.state.adopted,'Own config ACK before HTTP response is not mistaken for a foreign update');normal();
// Retain prior search/delete confirmation, submission refusal, and key-status coverage.
normal();
assert.equal(nodes().filter(n=>n.type==='Switch').length,1);
assert(!nodes().some(n=>n.type==='InputNumber'));
const advanced=nodes().find(n=>n.type==='details');assert(walk(advanced).some(n=>n.props.children==='tesla_vehicle.content.forget_key'));
assert(!walk(nodes().find(n=>n.props.id==='tesla_vehicle_config_form')).some(n=>n.props.children==='tesla_vehicle.content.forget_key'));
for(const [change,key]of [[{pairing_pending:true,pair_request_sent:false},'sending'],[{pairing_pending:true,pair_request_sent:true},'pending'],[{paired:true},'paired']]){
 f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,...change};event('tesla_vehicle/state');
 assert(nodes().some(n=>n.props.children===`tesla_vehicle.status.${key}`));
}normal();
for(const action of ['search','forget_key']){
 f.show=async()=>false;count=f.calls.length;await button(action).props.onClick();assert.equal(f.calls.length,count);
 let yes;f.show=()=>new Promise(r=>{yes=r;});const pending=button(action).props.onClick();await button(action).props.onClick();
 page.selectProfile('1');page.selectProfile('0');yes(true);await pending;assert.equal(f.calls.length,count);normal();
}
await button('forget_key').props.onClick();assert.equal(f.calls.at(-1).topic,'tesla_vehicle/forget_key');assert.equal(f.calls.at(-1).payload.ev_index,0);
assert.equal(f.modals.at(-1).body(),'tesla_vehicle.content.forget_confirm');normal();
page.dirty=true;assert(button('forget_key').props.disabled);page.dirty=false;
f.call=async()=>{throw Error('injected search refusal');};await button('search').props.onClick();assert(page.state.search_error);assert(!page.state.command_busy);normal();
f.values['tesla_vehicle/discovery']={status:'error',busy:false,targets:[],error:'timeout'};event('tesla_vehicle/discovery');
assert(nodes().some(n=>n.props.children==='tesla_vehicle.content.search_failed'));normal();
await button('search').props.onClick();assert(page.searchBusy());count=f.calls.length;await button('search').props.onClick();assert.equal(f.calls.length,count);
await assert.rejects(page.sendSave('tesla_vehicle/profiles',{entries:page.state.entries}));
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,revision:30,targets:[target],truncated:true};event('tesla_vehicle/discovery');assert(!button('select_target'));
normal();
let prepareResponse;f.call=()=>new Promise(r=>{prepareResponse=r;});const pendingUnmount=button('pair').props.onClick();await new Promise(setImmediate);
count=f.calls.length;page.componentWillUnmount();prepareResponse();await pendingUnmount;assert.equal(f.calls.length,count);
assert(f.calls.every(c=>['tesla_vehicle/scan','tesla_vehicle/profiles_update','tesla_vehicle/guided','tesla_vehicle/forget_key'].includes(c.topic)));
console.log('GUIDED_BEHAVIOR_PASS no-auto/toggle-save/disabled-reasons/modal-cancel/double-click/A-B-A/config/SLAC/generation/rejection/paired-poll/common-SoC/ambiguous/invalid-VIN/stale-save/unmount');
