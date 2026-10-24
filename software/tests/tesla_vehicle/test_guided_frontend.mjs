// Production VDOM and event handlers, synthetic-only API boundary.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
import {validateGuided} from './guided_schema.mjs';
const require=createRequire(new URL('../../web/package.json',import.meta.url));
const {build}=require('esbuild');
const f={values:{},listeners:{},calls:[],modals:[],show:async()=>true,call:async()=>{},validateGuided};
const mock=`const f=globalThis.fixture;
export const get=t=>f.values[t]; export const call=async(t,p)=>{f.calls.push({topic:t,payload:structuredClone(p)});if(t==='tesla_vehicle/guided'){try{f.validateGuided(p);}catch(e){console.error(e.message);throw e;}}await f.call(t,p);};
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
vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,console,fixture:f,structuredClone,performance,Date:{now:()=>Date.now()+(f.offset??0)},setTimeout:(fn,ms)=>setTimeout(()=>{f.offset=(f.offset??0)+(f.advance??0);fn();},f.advance?0:ms),clearTimeout,AbortController,crypto:globalThis.crypto});
f.values['ev/config']={evs:[0,1].map(i=>({name:`Synthetic ${i}`,mac:`11:22:33:44:55:0${i}`}))};
f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};
f.values['bluetooth/config']={enable:true};
f.values['bluetooth/state']={supported:true,phase:'active'};
f.values['tesla_vehicle/profiles']={entries:[0,1].map(i=>({ev_index:i,ev_mac:f.values['ev/config'].evs[i].mac,enable:false,vin:i?'5YJ3E1EA7KF000002':'5YJ3E1EA7KF000001',ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}))};
f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};
f.values['tesla_vehicle/guidance']={generation:'7',revision:12,request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};
f.values['tesla_vehicle/discovery']={status:'idle',busy:false,targets:[]};
const page=new module.exports.TeslaVehicle();
const event=t=>{for(const cb of f.listeners[t]??[])cb();};
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const nodes=()=>walk(page.render({},page.state));
const findButton=k=>nodes().find(n=>n.type==='Button'&&(n.props.children===`tesla_vehicle.content.${k}` || k==='search'&&n.props.children==='tesla_vehicle.content.searching'));
// Use the actual visible details/manage controls to reach a deliberate refresh.
const button=k=>{if(k==='forget_key'&&!findButton(k)&&findButton('show_details')){f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false};event('tesla_vehicle/state');findButton('show_details').props.onClick();}if(k==='search'&&!findButton(k)&&findButton('show_details'))findButton('show_details').props.onClick();if(k==='search'&&!findButton(k))page.state.edit_binding=true;return findButton(k);};
// Existing Details control alone gets the approved destructive appearance.
page.state.entries[0].ble_address='01:02:03:04:05:06';page.state.entries[0].discovery_revision=2;
page.state.details_open=true;assert.equal(findButton('edit_binding').props.variant,'danger','Existing VIN/reset control must be solid red');
page.state.entries[0].ble_address='';page.state.entries[0].discovery_revision=0;page.state.details_open=false;
assert.equal(nodes().filter(n=>n.type==='Switch').length,0,'No per-vehicle permission switch');
assert(!nodes().some(n=>n.type==='ConfigForm'),'No separate permission Save block');
assert(!nodes().some(n=>n.type==='summary'||n.type==='details'),'Details must use standard WARP heading and fullwidth button');
assert(nodes().some(n=>n.type==='h3'&&n.props.children==='tesla_vehicle.content.details'));
assert.equal(findButton('show_details').props.className,'w-100');
assert.equal(findButton('show_details').props.variant,'primary');
assert(nodes().some(n=>n.type==='FormRow'&&walk(n).some(c=>c.type==='Button'&&c.props.children==='tesla_vehicle.content.show_details')&&n.props.contentColClasses==='col-12'&&n.props.labelColClasses==='d-none'),'Details toggle spans the whole page, not the 8-column input area');
assert(!nodes().some(n=>n.props.label==='tesla_vehicle.content.ble_address'),'Details default collapsed');
findButton('show_details').props.onClick();
assert(findButton('hide_details'));
for(const label of ['vin','ble_address','loaded_profile','active_profile'])assert(nodes().some(n=>n.props.label===`tesla_vehicle.content.${label}`));
assert(!nodes().some(n=>n.props.label==='tesla_vehicle.content.address_type'));
assert(nodes().filter(n=>n.type==='InputText'&&n.props.onValue===undefined).every(n=>n.props.readonly===true),'Diagnostics explicitly readonly');
findButton('hide_details').props.onClick();
assert.equal(f.calls.length,0);
assert(!findButton('pair'),'Before adopting a match show VIN/search, not a redundant disabled pairing step');
await button('search').props.onClick();
const target={name:'S3acc31774a738ea0C',ble_address:'01:02:03:04:05:06',ble_address_type:1,matches_vin:true,rssi:-50};
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:8,targets:[target]};event('tesla_vehicle/discovery');
await button('select_target').props.onClick();
assert.equal(f.calls.at(-1).topic,'tesla_vehicle/profiles_update','Adopt itself saves the existing profile transaction; no second Save');
assert.equal(f.calls.at(-1).payload.entries[0].ble_address_type,1);
assert.equal(f.calls.at(-1).payload.entries[0].discovery_revision,8);
assert.equal(f.calls.at(-1).payload.entries[0].enable,true,'Explicit exact-match adoption persists permission for this profile without a hidden checkbox');
assert.equal(f.calls.at(-1).payload.entries[1].enable,false,'Adoption never enables another stored profile');
assert.deepEqual(f.calls.map(c=>c.topic),['tesla_vehicle/scan','tesla_vehicle/profiles_update'],'Search/adopt never prepare, connect, pair, poll, wake or charge');
assert.equal(f.calls.at(-1).payload.entries[1].ble_address,'');
assert(nodes().some(n=>n.props.children==='tesla_vehicle.content.assigned'));
assert(!findButton('search'),'Adopted default is compact; VIN/search can be reopened explicitly');
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
assert.equal(button('pair').props.disabled,false,'Saved mapping opens explicit consent even after TTL/session expiry');
f.call=async(t,p)=>{
 if(t==='tesla_vehicle/scan'){
  f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:page.profile().vin,ev_mac:page.profile().ev_mac,generation:'7',revision:10,targets:[{...target,ble_address:'02:03:04:05:06:07',ble_address_type:0}]};event('tesla_vehicle/discovery');
 }
 if(t==='tesla_vehicle/profiles_update'){
  f.values['tesla_vehicle/profiles']=structuredClone(p);event('tesla_vehicle/profiles');
  f.values['tesla_vehicle/guidance'].revision++;f.values['tesla_vehicle/guidance'].profiles[0].binding_fresh=true;event('tesla_vehicle/guidance');
 }
 if(p.phase==='prepare_pair'){f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}
};
const refreshStart=f.calls.length;
await button('pair').props.onClick();
assert.deepEqual(f.calls.slice(refreshStart).map(c=>c.payload.phase??c.topic),['tesla_vehicle/scan','tesla_vehicle/profiles_update','prepare_pair','commit']);
assert.equal(f.calls.at(-1).payload.revision,13);
assert.equal(f.calls.at(-1).payload.profile.ble_address,'02:03:04:05:06:07');
assert.equal(f.calls.at(-1).payload.profile.ble_address_type,0);
console.log('GUIDED_CONSENT_BOUND_REFRESH_PASS');
const good=structuredClone(f.values['tesla_vehicle/profiles']);
const normal=()=>{
 f.values['tesla_vehicle/profiles']=structuredClone(good);page.state.entries=structuredClone(good.entries);page.dirty=false;
 f.values['tesla_vehicle/guidance']={generation:'7',revision:12,request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};event('tesla_vehicle/guidance');
 f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};event('tesla_vehicle/state');
 f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};event('ev/state');
 f.values['bluetooth/config']={enable:true};
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
f.call=async()=>{throw Error('injected prepare refusal');};count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count+2);assert.equal(f.calls.at(-1).payload.phase,'cancel');assert.equal(page.state.command_error,'injected prepare refusal');assert.equal(page.state.command_busy,false);normal();
// Correct request alone is insufficient: a generation change before ACK cancels commit.
f.call=async(t,p)=>{if(p.phase==='prepare_pair'){
 f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],generation:'8',ready:true,request:p.request};event('tesla_vehicle/guidance');
}};count=f.calls.length;await button('pair').props.onClick();assert.equal(f.calls.length,count+2);assert.equal(f.calls.at(-1).payload.phase,'cancel');normal();
// Existing paired profile is compact and polls via preparation, never re-pairs.
f.values['tesla_vehicle/guidance'].profiles[0]={ev_index:0,paired:true,key_presence:'present',binding_fresh:true};event('tesla_vehicle/guidance');
assert(!findButton('pair'),'Paired view never invites re-pairing');
assert.equal(button('read_soc').props.disabled,false);
assert(findButton('forget_key'),'Unpair remains visible with Details collapsed');
assert.equal(findButton('forget_key').props.variant,'danger');
assert.equal(findButton('forget_key').props.className,'w-100');
f.call=async(t,p)=>{if(p.phase==='prepare_poll'){f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}};
await button('read_soc').props.onClick();assert.deepEqual(f.calls.slice(-2).map(c=>c.payload.phase),['prepare_poll','commit']);
assert(!nodes().some(n=>String(n.props.value).includes('null %')));
f.values['ev/state'].soc=63.5;f.values['tesla_vehicle/state']={profile_ev_index:0,last_soc:63.5,last_update:12345,key_presence:'present',key_present:true,paired:true};event('ev/state');event('tesla_vehicle/state');
assert(nodes().some(n=>n.props.value==='63.5 %'));assert(nodes().some(n=>n.props.label==='tesla_vehicle.status.soc_time'));
f.values['ev/state'].soc=null;event('ev/state');assert(!nodes().some(n=>n.props.value==='63.5 %'));normal();
// Unique-only, TTL and unsaved/config scope protections for discovery adoption.
await button('search').props.onClick();
for(const targets of [[{...target,matches_vin:false}],[target,{...target,ble_address:'02:02:03:04:05:06'}],[]]){
 f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:20,targets};event('tesla_vehicle/discovery');
 assert(!button('select_target'));count=f.calls.length;await page.selectTarget(target);assert.equal(f.calls.length,count);
}
for(const vin of ['', '5YJ3E1EA7KF00000I', '5yj3E1EA7KF000001','5YJ3E1EA7KF0000010']){page.setProfile({vin});assert.equal(button('search').props.disabled,true);}
normal();
// An in-flight A adoption response never marks B (or A-B-A) as saved.
await button('search').props.onClick();f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:21,targets:[target]};event('tesla_vehicle/discovery');
let savedResponse;f.call=()=>new Promise(r=>{savedResponse=r;});const pendingSave=button('select_target').props.onClick();page.selectProfile('1');page.selectProfile('0');savedResponse();await pendingSave;assert.equal(page.state.adopted,false);normal();
// OFF exposes no Tesla actions and cannot synthesize a writable config.
const savedApi=f.values['tesla_vehicle/profiles'];delete f.values['tesla_vehicle/profiles'];
assert(!button('pair')&&!button('search')&&!button('read_soc'));count=f.calls.length;nodes();assert.equal(f.calls.length,count);f.values['tesla_vehicle/profiles']=savedApi;
// Unmount while the explicitly requested local preparation is in flight cannot commit later.
f.advance=13000;count=f.calls.length;await button('pair').props.onClick();assert.deepEqual(f.calls.slice(count).map(c=>c.payload.phase),['prepare_pair','cancel'],'No commit on missing ACK/timeout; explicit local cancellation');assert(page.state.command_error);f.advance=0;f.offset=0;normal();
await button('search').props.onClick();f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:25,targets:[target]};event('tesla_vehicle/discovery');
const staleAdopt=button('select_target').props.onClick;
f.offset=120001;assert(!button('select_target'),'Client expiry hides even a cached matching result');
count=f.calls.length;await staleAdopt();assert.equal(f.calls.length,count,'Stale rendered adoption handler cannot save or crash');f.offset=0;
const discoveryBefore=structuredClone(f.values['tesla_vehicle/discovery']);
f.values['tesla_vehicle/discovery']={...discoveryBefore,revision:26,targets:[{...target,ble_address:'02:03:04:05:06:07'}]};event('tesla_vehicle/discovery');
await staleAdopt();assert.equal(f.calls.length,count,'An old result click cannot adopt a newly replaced peer');
f.values['tesla_vehicle/discovery']={...discoveryBefore,revision:27,targets:[target]};event('tesla_vehicle/discovery');
f.call=async(t,p)=>{if(t==='tesla_vehicle/profiles_update'){f.values['tesla_vehicle/profiles']=structuredClone(p);event('tesla_vehicle/profiles');}};
await button('select_target').props.onClick();assert(page.state.adopted,'Own config ACK before HTTP response is not mistaken for a foreign update');normal();
// New approved primary paired action and failed-pair recovery.
normal();f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');
assert(!findButton('pair'));assert.equal(findButton('forget_key').props.variant,'danger','Paired primary is solid red');
assert.equal(findButton('forget_key').props.className,'w-100');
assert(nodes().some(n=>n.type==='FormRow'&&walk(n).some(c=>c.type==='Button'&&c.props.children==='tesla_vehicle.content.forget_key')),'Same primary FormRow position/width as pair');
findButton('show_details').props.onClick();assert.equal(nodes().filter(n=>n.type==='Button'&&n.props.children==='tesla_vehicle.content.forget_key').length,1,'No duplicate unpair below Details');
normal();assert(!findButton('forget_key'),'No unpair primary without authenticated pairing');
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false};event('tesla_vehicle/state');
assert(findButton('pair'));assert(!findButton('forget_key'));findButton('show_details').props.onClick();assert(findButton('forget_key'),'Orphan credential has explicit recovery in Details');
normal();
const configBeforeOff=f.values['tesla_vehicle/profiles'];delete f.values['tesla_vehicle/profiles'];assert(!nodes().some(n=>n.type==='Alert'&&n.props.variant==='info'),'Remove only permanent disabled blue alert');f.values['tesla_vehicle/profiles']=configBeforeOff;
// Delete consent expires when selected credential evidence changes, even A->B->A.
for(const mutate of [()=>{page.selectProfile('1');page.selectProfile('0');},()=>{const old=f.values['tesla_vehicle/state'];f.values['tesla_vehicle/state']={...old,key_presence:'absent',key_present:false,paired:false};event('tesla_vehicle/state');f.values['tesla_vehicle/state']=old;event('tesla_vehicle/state');},()=>{const old=f.values['tesla_vehicle/profiles'];f.values['tesla_vehicle/profiles']={entries:[]};event('tesla_vehicle/profiles');f.values['tesla_vehicle/profiles']=old;event('tesla_vehicle/profiles');}]){
 normal();f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');
 let yes;f.show=()=>new Promise(r=>yes=r);const n=f.calls.length;const pending=button('forget_key').props.onClick();mutate();yes(true);await pending;assert.equal(f.calls.length,n,'Changed key/profile/config expires old deletion confirmation');
}
normal();f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');
f.show=async()=>false;await button('forget_key').props.onClick();assert.equal(f.modals.at(-1).title(),'tesla_vehicle.content.forget_title');assert.equal(f.modals.at(-1).yes_text(),'tesla_vehicle.content.forget_key');
const deletionText=walk(f.modals.at(-1).body()).flatMap(n=>[n.props?.children].flat(Infinity)).filter(s=>typeof s==='string').join(' ');assert(deletionText.includes('Synthetic 0'),'Dialog identifies selected profile');
// Retain prior search/delete confirmation, submission refusal, and key-status coverage.
normal();f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');
assert.equal(nodes().filter(n=>n.type==='Switch').length,0);
assert(!nodes().some(n=>n.type==='InputNumber'));
assert(findButton('forget_key'));
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
assert(walk(f.modals.at(-1).body()).some(n=>n.type==='p'));normal();
page.dirty=true;assert(button('forget_key').props.disabled);page.dirty=false;
f.call=async()=>{throw Error('injected search refusal');};await button('search').props.onClick();assert(page.state.search_error);assert(!page.state.command_busy);normal();
f.values['tesla_vehicle/discovery']={status:'error',busy:false,targets:[],error:'timeout'};event('tesla_vehicle/discovery');
assert(nodes().some(n=>n.props.children==='tesla_vehicle.content.search_failed'));normal();
await button('search').props.onClick();assert(page.searchBusy());count=f.calls.length;await button('search').props.onClick();assert.equal(f.calls.length,count);
await assert.rejects(page.sendSave('tesla_vehicle/profiles',{entries:page.state.entries}));
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:30,targets:[target],truncated:true};event('tesla_vehicle/discovery');assert(!button('select_target'));
normal();
// A disabled persisted profile remains disabled after a failed adoption save.
f.values['tesla_vehicle/profiles'].entries[0].enable=false;
f.values['tesla_vehicle/profiles'].entries[0].ble_address='';
f.values['tesla_vehicle/profiles'].entries[0].discovery_revision=0;
page.state.entries=structuredClone(f.values['tesla_vehicle/profiles'].entries);event('tesla_vehicle/profiles');
await button('search').props.onClick();
f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:31,targets:[target]};event('tesla_vehicle/discovery');
f.call=async()=>{throw Error('injected persistence refusal');};count=f.calls.length;
await button('select_target').props.onClick();
assert.equal(f.calls.length,count+1);assert(!page.state.adopted && !page.state.entries[0].enable && !f.values['tesla_vehicle/profiles'].entries[0].enable);
assert(page.state.command_error && !page.state.command_busy);normal();
// Double adoption is one save; its late ACK cannot cross A-B-A.
await button('search').props.onClick();f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:'5YJ3E1EA7KF000001',ev_mac:'11:22:33:44:55:00',generation:'7',revision:32,targets:[target]};event('tesla_vehicle/discovery');
let adoptionAck;f.call=()=>new Promise(r=>{adoptionAck=r;});count=f.calls.length;
const adoptHandler=button('select_target').props.onClick;const adopting=adoptHandler();await adoptHandler();assert.equal(f.calls.length,count+1);
page.selectProfile('1');page.selectProfile('0');adoptionAck();await adopting;assert(!page.state.adopted);normal();
// Changing away and back during preparation cancels a later matching ACK.
f.call=async(t,p)=>{if(p.phase==='prepare_pair'){page.selectProfile('1');page.selectProfile('0');f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}};
count=f.calls.length;await button('pair').props.onClick();assert.deepEqual(f.calls.slice(count).map(c=>c.payload.phase),['prepare_pair','cancel']);normal();
// Actual reset control confirmation + persistence/readback, never a reopen-only action.
normal();page.state.edit_binding=false;page.state.details_open=true;
f.values['tesla_vehicle/guidance'].revision=12;
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'absent',key_present:false,paired:false};event('tesla_vehicle/state');
const resetHandler=findButton('edit_binding').props.onClick;
f.show=async()=>false;count=f.calls.length;await resetHandler();assert.equal(f.calls.length,count,'Reset cancel is inert');
f.show=async()=>true;
f.call=async(t,p)=>{if(p.phase==='reset'){
 assert.equal(p.revision,12);assert.equal(p.profile.ev_index,0);
 const cfg=structuredClone(f.values['tesla_vehicle/profiles']);Object.assign(cfg.entries[0],{enable:false,ble_address:'',ble_address_type:0,discovery_revision:0});
 f.values['tesla_vehicle/profiles']=cfg;event('tesla_vehicle/profiles');
 f.values['tesla_vehicle/guidance'].revision++;event('tesla_vehicle/guidance');
}};
const B=structuredClone(f.values['tesla_vehicle/profiles'].entries[1]);const initialVIN=page.state.entries[0].vin;
await resetHandler();assert.equal(f.calls.at(-1).payload.phase,'reset');
assert.equal(page.state.entries[0].ble_address,'','Confirmed reset must apply exact readback to UI');
assert.equal(page.state.entries[0].vin,initialVIN);assert(page.state.edit_binding);assert(!page.state.details_open);
assert.equal(JSON.stringify(page.state.entries[1]),JSON.stringify(B));assert(findButton('search'));assert(!page.dirty);
// Paired and uncertain states block reset, including programmatically stale click.
for(const presence of ['present','unknown','cleanup']){
 normal();page.state.details_open=true;f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:presence,key_present:presence==='present',paired:presence==='present'};event('tesla_vehicle/state');
 assert(findButton('edit_binding').props.disabled);count=f.calls.length;await findButton('edit_binding').props.onClick();assert.equal(f.calls.length,count);
 assert(nodes().some(n=>n.props.children===`tesla_vehicle.content.${presence==='present'?'unpair_first':'reset_key_unknown'}`));
 const before=page.profile().vin;page.setProfile({vin:'5YJ3E1EA7KF000009'});assert.equal(page.profile().vin,before);
}
for(const mutate of [()=>{page.selectProfile('1');page.selectProfile('0');},()=>{f.values['tesla_vehicle/guidance'].revision++;event('tesla_vehicle/guidance');},()=>{f.values['tesla_vehicle/state'].paired=true;event('tesla_vehicle/state');}]){
 normal();page.state.details_open=true;let yes;f.show=()=>new Promise(r=>yes=r);count=f.calls.length;
 const pending=page.resetBinding();await page.resetBinding();mutate();yes(true);await pending;assert.equal(f.calls.length,count,'Reset stale/ABA/duplicate consent rejected');
}
normal();page.state.details_open=true;f.call=async()=>{throw Error('reset refused');};count=f.calls.length;await page.resetBinding();assert.equal(f.calls.length,count+1);assert.equal(page.state.command_error,'reset refused');assert(page.profile().ble_address);
// Every malformed/ambiguous/expired refresh fails closed with a useful error.
let negativeRevision=100;
for(const kind of ['none','ambiguous','truncated','expired','generation','scope','wrong-vin','bad-type','bad-address','error','timeout','foreign-save','dirty-save']){
 normal();f.values['tesla_vehicle/guidance'].profiles[0].binding_fresh=false;event('tesla_vehicle/guidance');
 const startRevision=negativeRevision;negativeRevision+=2;
 f.values['tesla_vehicle/discovery']={status:'idle',busy:false,revision:startRevision,targets:[]};event('tesla_vehicle/discovery');
 const before=f.calls.length,oldB=JSON.stringify(f.values['tesla_vehicle/profiles'].entries[1]);
 f.call=async(t,p)=>{
  if(t==='tesla_vehicle/scan'){
   let d={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:page.profile().vin,ev_mac:page.profile().ev_mac,generation:'7',revision:startRevision+1,targets:[target]};
   if(kind==='none')d.targets=[];
   if(kind==='ambiguous')d.targets.push({...target,ble_address:'02:03:04:05:06:07'});
   if(kind==='truncated')d.truncated=true;
   if(kind==='expired')d.result_age_ms=120000;
   if(kind==='generation')d.generation='8';
   if(kind==='scope')d.ev_index=1;
   if(kind==='wrong-vin')d.searched_vin='5YJ3E1EA7KF000002';
   if(kind==='bad-type')d.targets=[{...target,ble_address_type:3}];
   if(kind==='bad-address')d.targets=[{...target,ble_address:'00:00:00:00:00:00'}];
   if(kind==='error'){d.status='error';d.error='injected radio failure';}
   if(kind==='timeout')d.status='scanning';
   f.values['tesla_vehicle/discovery']=d;event('tesla_vehicle/discovery');
  }
  if(t==='tesla_vehicle/profiles_update'){
   f.values['tesla_vehicle/profiles']=structuredClone(p);event('tesla_vehicle/profiles');
   f.values['tesla_vehicle/guidance'].revision+=kind==='foreign-save'?2:1;event('tesla_vehicle/guidance');
   if(kind==='dirty-save')page.setDirty(true);
  }
  if(p.phase==='prepare_pair'){f.values['tesla_vehicle/guidance'].request=p.request;f.values['tesla_vehicle/guidance'].ready=true;event('tesla_vehicle/guidance');}
 };
 if(kind==='timeout')f.advance=23000;
 await button('pair').props.onClick();f.advance=0;f.offset=0;
 assert.equal(f.calls[before]?.topic,'tesla_vehicle/scan',kind+' exercises actual confirmed refresh');
 assert(!f.calls.slice(before).some(c=>c.payload.phase?.startsWith('prepare')||c.payload.phase==='commit'),kind+' cannot prepare/commit');
 assert(page.state.command_error,kind+' retains error');assert.equal(JSON.stringify(f.values['tesla_vehicle/profiles'].entries[1]),oldB,kind+' preserves B');
 const expected=kind==='none'||kind.startsWith('bad-')?'refresh_empty':['ambiguous','truncated'].includes(kind)?'refresh_ambiguous':['expired','generation','scope','wrong-vin'].includes(kind)?'result_expired':kind==='error'?'refresh_failed':kind==='timeout'?'refresh_timeout':'refresh_changed';
 assert(String(page.state.command_error).includes('tesla_vehicle.content.'+expected),kind+' exact useful failure path, not a stale fixture');
}
// Cancel, local/profile/SLAC/config/guidance/Bluetooth ABA during the scan.
for(const mutate of [()=>page.selectProfile('1'),()=>{page.selectProfile('1');page.selectProfile('0');},()=>page.setDirty(true),()=>{f.values['ev/state'].active_ev_index=1;event('ev/state');f.values['ev/state'].active_ev_index=0;event('ev/state');},()=>{f.values['tesla_vehicle/guidance'].revision++;event('tesla_vehicle/guidance');f.values['tesla_vehicle/guidance'].revision--;event('tesla_vehicle/guidance');},()=>{f.values['bluetooth/state'].phase='disabled';event('bluetooth/state');f.values['bluetooth/state'].phase='active';event('bluetooth/state');}]){
 normal();f.values['tesla_vehicle/guidance'].profiles[0].binding_fresh=false;event('tesla_vehicle/guidance');f.values['tesla_vehicle/discovery']={status:'idle',busy:false,revision:50,targets:[]};event('tesla_vehicle/discovery');
 let release;f.call=async(t)=>{if(t==='tesla_vehicle/scan')await new Promise(r=>release=r);};count=f.calls.length;
 const first=button('pair').props.onClick();await new Promise(setImmediate);await page.command('pair');mutate();release();await first;
 assert.deepEqual(f.calls.slice(count).map(c=>c.topic),['tesla_vehicle/scan'],'No duplicate request, save, prepare or commit after invalidation');
}
console.log('GUIDED_REFRESH_NEGATIVE_RACES_PASS');
normal();
let prepareResponse;f.call=async(t,p)=>{if(p.phase==='prepare_pair')await new Promise(r=>{prepareResponse=r;});};const pendingUnmount=button('pair').props.onClick();await new Promise(setImmediate);
count=f.calls.length;page.componentWillUnmount();prepareResponse();await pendingUnmount;assert.equal(f.calls.length,count+1);assert.equal(f.calls.at(-1).payload.phase,'cancel');
assert(f.calls.every(c=>['tesla_vehicle/scan','tesla_vehicle/profiles_update','tesla_vehicle/guided','tesla_vehicle/forget_key'].includes(c.topic)));
console.log('GUIDED_BEHAVIOR_PASS no-auto/toggle-save/disabled-reasons/modal-cancel/double-click/A-B-A/config/SLAC/generation/rejection/paired-poll/common-SoC/ambiguous/invalid-VIN/stale-save/unmount');
