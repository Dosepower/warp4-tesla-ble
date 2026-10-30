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
export const get=t=>f.values[t]; export const call=async(t,p,error)=>{if(error)throw Error('Duplicate global error surface requested');f.calls.push({topic:t,payload:structuredClone(p)});if(t==='tesla_vehicle/guided'){try{f.validateGuided(p);}catch(e){console.error(e.message);throw e;}}await f.call(t,p);};
export const save=(t,p,error)=>call(t+'_update',p,error); export const __=k=>k;
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
// Busy buttons render the spinner component with the step text as their label.
const label=n=>typeof n.props.children==='string'?n.props.children:n.props.children?.props?.text;
const findButton=k=>nodes().find(n=>n.type==='Button'&&(label(n)===`tesla_vehicle.content.${k}` || k==='search'&&label(n)==='tesla_vehicle.content.searching'));
// Rare/destructive actions live in the collapsed Einstellungen section (clean main view).
const openSettings=()=>{if(findButton('show_settings'))findButton('show_settings').props.onClick();};
// Use the actual visible details/manage controls to reach a deliberate refresh.
const button=k=>{if(['forget_key','cleanup_key','edit_binding'].includes(k)&&!findButton(k))openSettings();if(k==='forget_key'&&!findButton(k)){f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');}if(k==='search'&&!findButton(k)&&findButton('show_details'))findButton('show_details').props.onClick();if(k==='search'&&!findButton(k))page.state.edit_binding=true;return findButton(k);};
// Existing Details control alone gets the approved destructive appearance.
page.state.entries[0].ble_address='01:02:03:04:05:06';page.state.entries[0].discovery_revision=2;
assert(!findButton('edit_binding'),'Rare reset action collapsed under Einstellungen by default');
page.state.settings_open=true;assert(findButton('edit_binding'),'VIN/reset control available under expanded Einstellungen');assert.equal(findButton('edit_binding').props.variant,'danger','Existing VIN/reset control must be solid red');
page.state.entries[0].ble_address='';page.state.entries[0].discovery_revision=0;page.state.settings_open=false;
assert.equal(nodes().filter(n=>n.type==='Switch').length,0,'No per-vehicle permission switch');
assert(!nodes().some(n=>n.type==='ConfigForm'),'No separate permission Save block');
assert(!nodes().some(n=>n.type==='summary'||n.type==='details'),'Details must use standard WARP heading and fullwidth button');
assert(nodes().some(n=>n.type==='h3'&&n.props.children==='tesla_vehicle.content.details'));
assert.equal(findButton('show_details').props.className,'w-100');
assert.equal(findButton('show_details').props.variant,'primary');
assert(nodes().some(n=>n.type==='FormRow'&&walk(n).some(c=>c.type==='Button'&&c.props.children==='tesla_vehicle.content.show_details')&&n.props.contentColClasses===undefined&&n.props.labelColClasses===undefined),'Details toggle sits in the right form column (like the red actions), not full-width over the label column');
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
  f.values['tesla_vehicle/discovery']={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:page.profile().vin,ev_mac:page.profile().ev_mac,generation:'7',revision:(f.values['tesla_vehicle/discovery'].revision??8)+2,targets:[{...target,ble_address:'02:03:04:05:06:07',ble_address_type:0}]};event('tesla_vehicle/discovery');
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
// Still-fresh evidence must be refreshed when it cannot span preparation and
// commit latency. This is inside the SAME explicit confirmation, never retry.
f.values['tesla_vehicle/discovery'].result_age_ms=119000;event('tesla_vehicle/discovery');
const agedStart=f.calls.length;
await button('pair').props.onClick();
assert.deepEqual(f.calls.slice(agedStart).map(c=>c.payload.phase??c.topic),['tesla_vehicle/scan','tesla_vehicle/profiles_update','prepare_pair','commit']);
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
assert(!findButton('forget_key'),'Unpair is a rare action: collapsed under Einstellungen in the clean main view');
openSettings();assert(findButton('forget_key'),'Unpair available under Einstellungen');
assert.equal(findButton('forget_key').props.variant,'danger');
assert.equal(findButton('forget_key').props.className,'w-100');
findButton('hide_settings').props.onClick();
f.call=async(t,p)=>{if(p.phase==='prepare_poll'){f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}};
await button('read_soc').props.onClick();assert.deepEqual(f.calls.slice(-2).map(c=>c.payload.phase),['prepare_poll','commit']);
{ // Persisted trusted binding: a paired profile refreshes WITHOUT a frontend search, even with an
  // expired/absent discovery result; the backend connects the stored address. Controls below.
 const g=f.values['tesla_vehicle/guidance'],saved=structuredClone(g.profiles[0]),savedCall=f.call;
 const steps=[];
 f.call=async(t,p)=>{steps.push(page.state.busy_step);if(p.phase==='prepare_poll'){f.values['tesla_vehicle/guidance']={...f.values['tesla_vehicle/guidance'],ready:true,request:p.request};event('tesla_vehicle/guidance');}};
 g.profiles[0]={ev_index:0,paired:true,key_presence:'present',binding_fresh:false,binding_persisted:true};event('tesla_vehicle/guidance');
 f.values['tesla_vehicle/discovery']={status:'complete',busy:false,revision:3,result_age_ms:999999,result_ttl_ms:120000,targets:[]};event('tesla_vehicle/discovery');
 let n=f.calls.length;await button('read_soc').props.onClick();
 assert.deepEqual(f.calls.slice(n).map(c=>c.topic+':'+(c.payload.phase??'')),['tesla_vehicle/guided:prepare_poll','tesla_vehicle/guided:commit'],
  'Persisted paired refresh: no scan, no profile save, just prepare+commit');
 assert(!steps.includes('searching'),'Persisted refresh never shows the vehicle search step');
 assert(!page.state.command_error,'Persisted refresh succeeds without error');
 // Control 1: not persisted (no saved binding) -> the confirmed fresh search is still required first.
 g.profiles[0]={ev_index:0,paired:true,key_presence:'present',binding_fresh:false,binding_persisted:false};event('tesla_vehicle/guidance');
 let release;f.call=async(t)=>{if(t==='tesla_vehicle/scan')await new Promise(r=>release=r);};n=f.calls.length;
 let pending=button('read_soc').props.onClick();await new Promise(setImmediate);
 assert.equal(f.calls[n]?.topic,'tesla_vehicle/scan','Without a persisted binding the refresh starts with the explicit search');
 assert.equal(page.state.busy_step,'searching','Search step shown only during the actual search');
 page.selectProfile('1');page.selectProfile('0');release();await pending; // cancel the control search
 assert(!f.calls.slice(n).some(c=>c.payload?.phase?.startsWith('prepare')),'Control search never prepared');
 // Control 2: pairing never uses the persisted path, even if a binding is reported persisted.
 g.profiles[0]={ev_index:0,paired:false,key_presence:'present',binding_fresh:false,binding_persisted:true};event('tesla_vehicle/guidance');
 f.call=async(t)=>{if(t==='tesla_vehicle/scan')await new Promise(r=>release=r);};n=f.calls.length;
 pending=button('pair').props.onClick();await new Promise(setImmediate);
 assert.equal(f.calls[n]?.topic,'tesla_vehicle/scan','Pairing keeps the fresh exact-VIN search');
 page.selectProfile('1');page.selectProfile('0');release();await pending;
 assert(!f.calls.slice(n).some(c=>c.payload?.phase?.startsWith('prepare')),'Cancelled pairing search never prepared');
 g.profiles[0]=saved;event('tesla_vehicle/guidance');f.call=savedCall;normal();
}
console.log('GUIDED_PERSISTED_BINDING_PASS');
assert(!nodes().some(n=>String(n.props.value).includes('null %')));
f.values['ev/state'].soc=63.5;f.values['tesla_vehicle/state']={profile_ev_index:0,last_soc:63.5,last_update:12345,key_presence:'present',key_present:true,paired:true};event('ev/state');event('tesla_vehicle/state');
assert(nodes().some(n=>n.props.value==='63.5 %'));assert(nodes().some(n=>n.props.label==='tesla_vehicle.status.soc_time'));
{ // Receive time is wall-clock from the WARP, never uptime; unsynchronized clock is stated, not invented.
 const timeValue=()=>walk(nodes().find(n=>n.type==='FormRow'&&n.props.label==='tesla_vehicle.status.soc_time')).find(n=>n.type==='InputText').props.value;
 assert.equal(timeValue(),'tesla_vehicle.status.soc_time_unsynced');assert(!String(timeValue()).includes('12345'));
 f.values['ntp/config']={timezone:'Europe/Berlin'};
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],last_update_unix:1790000000,poll_outcome:'asleep_wake_disabled',poll_outcome_automatic:true,auto_poll:'waiting'};event('tesla_vehicle/state');
 assert.match(timeValue(),/\d{2}:\d{2}:\d{2}$/);assert(!timeValue().includes('tesla_vehicle.status.soc_time_unsynced'));
 assert(timeValue().startsWith('21.09.2026')||timeValue().includes('2026'),'Older value shows an unambiguous date');
 // Clean main view: no grey permanent status lines; last outcome/background state under Diagnose.
 const grey=()=>nodes().filter(n=>(n.type==='p'||n.type==='div')&&/text-muted|form-text/.test(n.props.class??'')&&String(n.props.children).startsWith('tesla_vehicle.status.'));
 const diag=(label,value)=>{findButton('show_details').props.onClick();const ok=nodes().some(n=>n.type==='FormRow'&&n.props.label===label&&walk(n).some(c=>c.props?.value===value));findButton('hide_details').props.onClick();return ok;};
 assert.equal(grey().length,0,'No grey routine status text');
 assert(diag('tesla_vehicle.status.last_outcome','tesla_vehicle.status.outcome_asleep_wake_disabled'),'Asleep outcome under Diagnose');
 assert(diag('tesla_vehicle.status.auto_poll','tesla_vehicle.status.auto_poll_waiting'),'Background state under Diagnose');
 let shownTime=timeValue();
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],auto_poll:'not_charging'};event('tesla_vehicle/state');
 assert.equal(timeValue(),shownTime,'Paused: last receive time unchanged');
 assert(diag('tesla_vehicle.status.auto_poll','tesla_vehicle.status.auto_poll_not_charging')&&grey().length===0,'Paused state under Diagnose, not as grey main text');
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],auto_poll:'waiting'};event('tesla_vehicle/state');
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],poll_outcome:'ok',last_error:'Tesla asleep; wake skipped, last wake less than 2 min ago; SoC not updated'};event('tesla_vehicle/state');
 assert(!nodes().some(n=>n.type==='p'&&String(n.props.children).startsWith('tesla_vehicle.status.outcome_')),'ok outcome adds no status line');
 assert(walk(nodes().find(n=>n.type==='Alert'&&n.props.variant==='danger')).some(n=>n.props?.children==='tesla_vehicle.status.outcome_asleep_wake_limited'),'Specific asleep message instead of generic failure');
 // Status priority: an older automatic not_found never overlays a newer success; error->success resets.
 const danger=()=>walk(nodes().find(n=>n.type==='Alert'&&n.props.variant==='danger')).filter(n=>typeof n.props?.children==='string').map(n=>n.props.children);
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],poll_outcome:'failed',auto_poll:'not_found',last_error:'Tesla authenticated SoC request failed'};event('tesla_vehicle/state');
 assert.deepEqual(danger(),['tesla_vehicle.status.outcome_failed','tesla_vehicle.status.auto_poll_not_found'],'Current failures once each, red only');
 assert.equal(timeValue(),shownTime,'Failure keeps the last value with its real receive time');assert(nodes().some(n=>n.props.value==='63.5 %'));
 // Real firmware transitions (tesla_auto_poll.inc): a fresh SoC publishes poll_outcome ok, a new
 // receive time and, while Idle, withdraws an older automatic not_found/failed (-> waiting).
 // A later automatic scan publishes searching -> not_found/failed while poll_outcome stays the historical ok.
 const busyText=()=>nodes().filter(n=>n.type==='Button'&&typeof n.props.children==='object'&&n.props.children?.props?.text).map(n=>n.props.children.props.text);
 const socValue=()=>walk(nodes().find(n=>n.type==='FormRow'&&n.props.label==='tesla_vehicle.status.soc')).find(n=>n.type==='InputText').props.value;
 const freshSuccess=(soc,unix)=>{f.values['ev/state'].soc=soc;f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],last_soc:soc,last_update:unix-1789990000,last_update_unix:unix,poll_outcome:'ok',poll_outcome_automatic:false,auto_poll:'waiting',last_error:''};event('ev/state');event('tesla_vehicle/state');};
 freshSuccess(64,1790000600);
 assert.deepEqual(danger(),[],'Old scan failure -> fresh success: no old automatic not_found and no old error');
 assert.equal(socValue(),'64.0 %');assert.notEqual(timeValue(),shownTime,'Fresh success carries its own new receive time');shownTime=timeValue();
 assert.equal(button('read_soc').props.disabled,false,'Button clickable again after error->success');
 let unix=1790000600;
 for(const [end,text] of [['not_found','tesla_vehicle.status.auto_poll_not_found'],['failed','tesla_vehicle.status.auto_poll_failed']]){
  f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],auto_poll:'searching'};event('tesla_vehicle/state');
  assert.deepEqual(busyText(),['tesla_vehicle.content.searching'],'Current automatic scan: spinner at the button only');
  assert.deepEqual(danger(),[],'No failure shown while the new scan is still running');
  f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],auto_poll:end};event('tesla_vehicle/state');
  assert.equal(f.values['tesla_vehicle/state'].poll_outcome,'ok','Fixture: last request outcome is still the historical success');
  assert.deepEqual(danger(),[text],'Success -> newer automatic '+end+': the new failure is visible, once');
  assert.deepEqual(busyText(),[],'Spinner ends with the scan');assert.equal(button('read_soc').props.disabled,false,'Refresh available after the scan failure');
  assert.equal(socValue(),'64.0 %','Last valid SoC kept, not cleared');assert.equal(timeValue(),shownTime,'No fabricated fresh receive time after a scan failure');
  assert(diag('tesla_vehicle.status.auto_poll',text),'Diagnose: current automatic scan state is the new failure');
  assert(diag('tesla_vehicle.status.last_outcome','tesla_vehicle.status.outcome_ok'),'Diagnose: last request separately attributed as the earlier success');
  unix+=600;freshSuccess(64,unix);
  assert.deepEqual(danger(),[],'The next fresh success withdraws the newer '+end+' again');assert.notEqual(timeValue(),shownTime);shownTime=timeValue();
 }
 // Wake honesty and pending post-wake follow-up: info line, never a red failure, no grey duplicate.
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],poll_outcome:'wake_sent_no_soc',auto_poll:'after_wake',last_error:'Tesla wake sent but no SoC received in time; SoC not updated'};event('tesla_vehicle/state');
 assert.deepEqual(danger(),[],'Pending follow-up: not presented as a final failure');
 assert(nodes().some(n=>n.type==='div'&&n.props.children==='tesla_vehicle.status.after_wake_sent'),'Wake sent (no awake proof) -> "Weckversuch gesendet"');
 assert(!nodes().some(n=>n.props?.children==='tesla_vehicle.status.after_wake_woken'||n.props?.children==='tesla_vehicle.status.outcome_woken_no_soc'),'Never claims woken without evidence');
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],poll_outcome:'woken_no_soc',last_error:'Tesla woke up but no SoC received in time; SoC not updated'};event('tesla_vehicle/state');
 assert(nodes().some(n=>n.type==='div'&&n.props.children==='tesla_vehicle.status.after_wake_woken'),'Woken only with backend awake evidence');
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],auto_poll:'not_charging'};event('tesla_vehicle/state');
 assert.deepEqual(danger(),['tesla_vehicle.status.outcome_woken_no_soc'],'Without a follow-up the woken failure is shown once');
 f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],poll_outcome:'wake_sent_no_soc',last_error:'Tesla wake sent but no SoC received in time; SoC not updated'};event('tesla_vehicle/state');
 assert.deepEqual(danger(),['tesla_vehicle.status.outcome_wake_sent_no_soc'],'Unconfirmed wake reported as wake sent');
 assert.equal(timeValue(),shownTime,'Receive time never re-dated by failures');
 f.values['tesla_vehicle/state']={profile_ev_index:0,last_soc:63.5,last_update:12345,key_presence:'present',key_present:true,paired:true};delete f.values['ntp/config'];event('tesla_vehicle/state');
 console.log('GUIDED_SOC_TIME_OUTCOME_PASS');
}
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
assert(!findButton('pair'));assert(!findButton('forget_key'),'Clean main view: unpair collapsed under Einstellungen');openSettings();
assert.equal(findButton('forget_key').props.variant,'danger','Unpair is solid red');
assert.equal(findButton('forget_key').props.className,'w-100');
assert(nodes().some(n=>n.type==='FormRow'&&walk(n).some(c=>c.type==='Button'&&c.props.children==='tesla_vehicle.content.forget_key')),'Full-width FormRow');
findButton('show_details').props.onClick();assert.equal(nodes().filter(n=>n.type==='Button'&&n.props.children==='tesla_vehicle.content.forget_key').length,1,'No duplicate unpair below Diagnose');
normal();openSettings();assert(!findButton('forget_key'),'No unpair without authenticated pairing');
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false};event('tesla_vehicle/state');
assert(findButton('pair'));assert(!findButton('forget_key'));openSettings();assert((openSettings(),findButton('cleanup_key')),'Orphan credential has distinctly local explicit recovery under Einstellungen');assert(!findButton('forget_key'));
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
// Paired profile: persisted per-profile background interval + explicit wake permission (user requirement).
{
 assert.equal(nodes().filter(n=>n.type==='Switch').length,0,'No wake switch: automatic wake is coupled to the firmware request contexts');
 const interval=()=>nodes().find(n=>n.type==='InputSelect'&&n.props.items?.some(i=>i[0]==='3600'));
 assert(interval(),'Interval selector visible');assert.equal(interval().props.value,'300');
 assert.equal(JSON.stringify(interval().props.items.map(i=>i[0])),JSON.stringify(['300','600','900','1800','3600']),'5 min is the lowest selectable interval; no 30 s/1 min/2 min');
 assert.equal(JSON.stringify(interval().props.items.map(i=>i[1])),JSON.stringify(['5 min','10 min','15 min','30 min','60 min']),'Interval shown with unit');
 assert(!nodes().some(n=>n.type==='InputNumber'));
 findButton('show_details').props.onClick();assert(nodes().some(n=>n.props.label==='tesla_vehicle.status.auto_poll'&&walk(n).some(c=>c.props?.value==='tesla_vehicle.status.auto_poll_off')),'Background status under Diagnose');findButton('hide_details').props.onClick();
 const before=f.calls.length;
  interval().props.onValue('600');
 assert.equal(f.calls.length,before,'Changing settings alone sends nothing');
 assert(page.dirty,'Settings mark the form dirty until saved');
 await page.sendSave('tesla_vehicle/profiles',{entries:page.state.entries});
 const saved=f.calls.at(-1);assert.equal(saved.topic,'tesla_vehicle/profiles_update');
 const entry=saved.payload.entries.find(e=>e.ev_index===0);assert.equal(entry.poll_interval,600);assert.equal(entry.wake_once,false,'Legacy wake_once carried unchanged, never rewritten by the UI');
 assert.equal(f.calls.length,before+1,'Exactly one persistent profile save, no radio action');
 page.state.entries=structuredClone(f.values['tesla_vehicle/profiles'].entries);page.setDirty(false);
 // Unlisted but firmware-valid value stays representable.
 page.state.entries[0].poll_interval=301;assert(interval().props.items.some(i=>i[0]==='301'&&i[1]==='301 s'));
 // Legacy positive values below 300 s are never offered again (firmware normalizes them to 300 s on restore).
 for(const legacy of [30,60,120,299]){page.state.entries[0].poll_interval=legacy;
  assert(!interval().props.items.some(i=>Number(i[0])<300),'legacy '+legacy+' s not selectable again');
  assert.equal(interval().props.value,'300','legacy '+legacy+' s shown as the effective 5 min minimum');}
 page.state.entries=structuredClone(f.values['tesla_vehicle/profiles'].entries);
 console.log('GUIDED_POLL_SETTINGS_PASS');
}
// Approved layout (tesla-ui-buttonbreite-v2): blue Einstellungen/Diagnose toggles share the right form
// column with the red Entkoppeln and VIN actions; colors stay primary/danger; no permanent gray unpair text.
{
 const rowOf=k=>nodes().find(n=>n.type==='FormRow'&&walk(n).some(c=>c.type==='Button'&&label(c)===`tesla_vehicle.content.${k}`));
 page.state.entries[0].ble_address='01:02:03:04:05:06';page.state.entries[0].discovery_revision=2;
 f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true};event('tesla_vehicle/state');
 page.state.settings_open=true;page.state.details_open=true;
 for(const k of ['hide_settings','hide_details','forget_key','edit_binding']){
  const row=rowOf(k);assert(row,k+' row rendered');
  assert.equal(row.props.labelColClasses,undefined,k+': label column kept, no full-width over the label column');
  assert.equal(row.props.contentColClasses,undefined,k+': same right form column as the red actions');
  const b=walk(row).find(c=>c.type==='Button');assert.equal(b.props.className,'w-100',k+' fills exactly its form column');
  assert(!String(b.props.variant).startsWith('outline'),k+': no outline variant');
 }
 assert.equal(findButton('hide_settings').props.variant,'primary');assert.equal(findButton('hide_details').props.variant,'primary');
 assert.equal(findButton('forget_key').props.variant,'danger');assert.equal(findButton('edit_binding').props.variant,'danger');
 assert(findButton('edit_binding').props.disabled,'paired: VIN reset stays disabled (greyed-out danger)');
 assert(!nodes().some(n=>n.props?.children==='tesla_vehicle.content.unpair_first'),'no permanent gray unpair_first text under the VIN action');
 const count=f.calls.length;await findButton('edit_binding').props.onClick();assert.equal(f.calls.length,count,'paired reset gate kept');
 page.state.settings_open=false;page.state.details_open=false;
 page.state.entries=structuredClone(f.values['tesla_vehicle/profiles'].entries);
 console.log('GUIDED_BUTTON_COLUMN_PASS');
}
assert(button('forget_key'));
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
await button('forget_key').props.onClick();assert.equal(f.calls.at(-1).topic,'tesla_vehicle/guided');assert.equal(f.calls.at(-1).payload.phase,'unpair');assert.equal(f.calls.at(-1).payload.profile.ev_index,0);
assert(walk(f.modals.at(-1).body()).some(n=>n.type==='p'));normal();
page.dirty=true;assert(button('forget_key').props.disabled);page.dirty=false;
f.call=async()=>{throw Error('injected search refusal');};await button('search').props.onClick();assert(page.state.search_error);assert(!page.state.command_busy);normal();
f.values['tesla_vehicle/discovery']={status:'error',busy:false,targets:[],ev_index:0,ev_mac:page.profile().ev_mac,searched_vin:page.profile().vin,error:'timeout'};event('tesla_vehicle/discovery');
assert(nodes().some(n=>n.props.children==='tesla_vehicle.status.action_failed'));page.state.details_open=true;assert(nodes().some(n=>n.props.children==='timeout'));page.selectProfile('1');assert(!nodes().some(n=>n.props.children==='timeout'));normal();
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
const resetHandler=(openSettings(),findButton('edit_binding')).props.onClick;
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
 assert((openSettings(),findButton('edit_binding')).props.disabled);count=f.calls.length;await (openSettings(),findButton('edit_binding')).props.onClick();assert.equal(f.calls.length,count);
 if(presence==='present')assert(!nodes().some(n=>n.props.children==='tesla_vehicle.content.unpair_first'),'paired: gate kept, permanent gray unpair_first text removed');
 else assert(nodes().some(n=>n.props.children===(presence==='cleanup'?'tesla_vehicle.status.cleanup_required':'tesla_vehicle.status.key_unknown')));
 const before=page.profile().vin;page.setProfile({vin:'5YJ3E1EA7KF000009'});assert.equal(page.profile().vin,before);
}
for(const mutate of [()=>{page.selectProfile('1');page.selectProfile('0');},()=>{f.values['tesla_vehicle/guidance'].revision++;event('tesla_vehicle/guidance');},()=>{f.values['tesla_vehicle/state'].paired=true;event('tesla_vehicle/state');}]){
 normal();page.state.details_open=true;let yes;f.show=()=>new Promise(r=>yes=r);count=f.calls.length;
 const pending=page.resetBinding();await page.resetBinding();mutate();yes(true);await pending;assert.equal(f.calls.length,count,'Reset stale/ABA/duplicate consent rejected');
}
normal();page.state.details_open=true;f.call=async()=>{throw Error('reset refused');};count=f.calls.length;await page.resetBinding();assert.equal(f.calls.length,count+1);assert.equal(page.state.command_error,'reset refused');assert(page.profile().ble_address);
// Every malformed/ambiguous/expired refresh fails closed with a useful error.
let negativeRevision=100;
for(const kind of ['none','unconnectable','ambiguous','truncated','expired','generation','scope','wrong-vin','bad-type','bad-address','error','timeout','foreign-save','dirty-save']){
 normal();f.values['tesla_vehicle/guidance'].profiles[0].binding_fresh=false;event('tesla_vehicle/guidance');
 const startRevision=negativeRevision;negativeRevision+=2;
 f.values['tesla_vehicle/discovery']={status:'idle',busy:false,revision:startRevision,targets:[]};event('tesla_vehicle/discovery');
 const before=f.calls.length,oldB=JSON.stringify(f.values['tesla_vehicle/profiles'].entries[1]);
 f.call=async(t,p)=>{
  if(t==='tesla_vehicle/scan'){
   let d={status:'complete',busy:false,ev_index:0,result_age_ms:0,result_ttl_ms:120000,searched_vin:page.profile().vin,ev_mac:page.profile().ev_mac,generation:'7',revision:startRevision+1,targets:[target]};
   if(kind==='none')d.targets=[];
   if(kind==='unconnectable'){d.targets=[{...target,matches_vin:false}];d.vin_not_connectable=true;} // VIN beacon present, non-connectable (official max-connections)
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
 const expected=kind==='unconnectable'?'refresh_unconnectable':kind==='none'||kind.startsWith('bad-')?'refresh_empty':['ambiguous','truncated'].includes(kind)?'refresh_ambiguous':['expired','generation','scope','wrong-vin'].includes(kind)?'result_expired':kind==='error'?'refresh_failed':kind==='timeout'?'refresh_timeout':'refresh_changed';
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
// Canonical errors: whitespace-equivalent duplicates collapse, distinct failures
// survive, backend errors belong only to their profile, late A failures cannot leak.
normal();
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false,last_error:'same   failure'};event('tesla_vehicle/state');
page.state.command_error=' same failure ';
const errorTexts=()=>Array.from(page.errors()); // Technical errors retained distinctly; canonical visible reason is localized once.
assert.equal(nodes().filter(n=>n.type==='div'&&n.props.children==='tesla_vehicle.status.action_failed').length,1);
assert.deepEqual(errorTexts(),['same failure']);
f.values['tesla_vehicle/state'].pairing_pending=true;event('tesla_vehicle/state');
assert.equal(nodes().filter(n=>n.type==='div'&&n.props.children==='tesla_vehicle.status.action_failed').length,1,'Active attempts must not hide unexpected faults');
f.values['tesla_vehicle/state'].pairing_pending=false;event('tesla_vehicle/state');
page.state.command_error='different failure';assert.deepEqual(errorTexts(),['different failure','same failure']);
page.selectProfile('1');assert.deepEqual(errorTexts(),[],'A backend failure is not a B error');page.selectProfile('0');assert.deepEqual(errorTexts(),['same failure']);
page.state.command_error='same failure';f.values['tesla_vehicle/state']={...f.values['tesla_vehicle/state'],last_error:''};event('tesla_vehicle/state');assert.deepEqual(errorTexts(),[],'Backend clearance retires mirrored local error');
normal();let refuse;f.call=async(t,p)=>{if(p.phase==='prepare_pair')await new Promise((_,reject)=>refuse=reject);};
const lateFailure=button('pair').props.onClick();await new Promise(setImmediate);page.selectProfile('1');refuse(Error('late A failure'));await lateFailure;assert.equal(page.state.command_error,false);normal();
for(const key_presence of ['present','cleanup','absent','unknown'])for(const connected of [false,true]){
 f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence,key_present:key_presence==='present',paired:false,connected};event('tesla_vehicle/state');page.state.details_open=true;
 assert(!findButton('forget_key'));assert.equal(!!(openSettings(),findButton('cleanup_key')),key_presence==='present'||key_presence==='cleanup');
 if((openSettings(),findButton('cleanup_key'))){
  f.show=async()=>false;const before=f.calls.length;await (openSettings(),findButton('cleanup_key')).props.onClick();assert.equal(f.calls.length,before);assert.equal(f.modals.at(-1).title(),'tesla_vehicle.content.cleanup_title');
  f.show=async()=>true;await (openSettings(),findButton('cleanup_key')).props.onClick();assert.equal(f.calls.at(-1).payload.phase,'cleanup');assert.equal(f.calls.at(-1).payload.profile.ev_index,0);
 }else{const before=f.calls.length;await page.command('cleanup');assert.equal(f.calls.length,before,'Unknown/absent stale handler fails closed');}
}
normal();page.state.command_error='stale failure';f.call=async(t,p)=>{if(p.phase==='prepare_pair'){f.values['tesla_vehicle/guidance'].ready=true;f.values['tesla_vehicle/guidance'].request=p.request;event('tesla_vehicle/guidance');}};await button('pair').props.onClick();assert.equal(page.state.command_error,false);assert(!page.profileStatus().paired,'HTTP acceptance is not pairing');
console.log('GUIDED_CANONICAL_ERRORS_LOCAL_CLEANUP_PASS');
normal();
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false,last_error:'previous attempt failed'};event('tesla_vehicle/state');
f.call=async(t,p)=>{if(p.phase==='prepare_pair'){f.values['tesla_vehicle/guidance'].ready=true;f.values['tesla_vehicle/guidance'].request=p.request;event('tesla_vehicle/guidance');}};
count=f.calls.length;await findButton('pair').props.onClick();assert.deepEqual(f.calls.slice(count).map(c=>c.payload.phase),['prepare_pair','commit'],'Explicit retained-key retry must not be cancelled by the old cached backend error');assert(!page.profileStatus().paired);
f.call=async(t,p)=>{if(p.phase==='prepare_pair'){
 f.values['tesla_vehicle/state'].last_error='';event('tesla_vehicle/state');
 f.values['tesla_vehicle/state'].last_error='previous attempt failed';event('tesla_vehicle/state');
 f.values['tesla_vehicle/guidance'].ready=true;f.values['tesla_vehicle/guidance'].request=p.request;event('tesla_vehicle/guidance');
}};
count=f.calls.length;await findButton('pair').props.onClick();assert.deepEqual(f.calls.slice(count).map(c=>c.payload.phase),['prepare_pair','cancel'],'A new clear->same error during preparation still blocks commit');assert.equal(page.state.command_error,'previous attempt failed');
normal();
// Rendered prerequisite explanations are component-wide, not duplicated under
// Pair/Search and Poll. Distinct diagnostic paragraphs are not swallowed.
normal();
f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false};event('tesla_vehicle/state');
f.values['bluetooth/state']={supported:true,phase:'error',restart_required:true};event('bluetooth/state');
const prerequisiteTexts=()=>nodes().filter(n=>n.type==='div'&&n.props.class==='form-text mb-2'&&n.props.role==='status'&&['tesla_vehicle.content.bluetooth_blocked','tesla_vehicle.content.action_busy'].includes(n.props.children)).map(n=>n.props.children);
assert.deepEqual(prerequisiteTexts(),['tesla_vehicle.content.bluetooth_blocked']);
page.state.edit_binding=true;
assert.deepEqual(prerequisiteTexts(),['tesla_vehicle.content.bluetooth_blocked'],'Search and Poll share the same prerequisite');
page.state.edit_binding=false;f.values['bluetooth/state']={supported:true,phase:'active'};event('bluetooth/state');f.values['tesla_vehicle/state'].action_busy=true;event('tesla_vehicle/state');
assert.deepEqual(prerequisiteTexts(),['tesla_vehicle.content.action_busy']);
normal();console.log('GUIDED_GREY_PREREQUISITE_DEDUP_PASS');
{ // Busy feedback: spinner + step text while active, concrete reason otherwise, clickable again after a failure.
 const busyText=()=>nodes().filter(n=>n.type==='Button'&&typeof n.props.children==='object'&&n.props.children?.props?.text).map(n=>[n.props.children.props.text,n.props.disabled]);
 normal();const st=extra=>{f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true,...extra};event('tesla_vehicle/state');};
 st({action_busy:true,action_kind:'soc'});assert.deepEqual(busyText(),[['tesla_vehicle.content.refreshing',true]],'Running SoC operation: "Wird aktualisiert …", locked');
 assert(!nodes().some(n=>n.type==='div'&&n.props.children==='tesla_vehicle.content.action_busy'),'Spinner replaces the generic busy reason at the same button');
 st({auto_poll:'searching'});assert.deepEqual(busyText(),[['tesla_vehicle.content.searching',false]],'Automatic scan: "Fahrzeug wird gesucht …"');
 st({auto_poll:'polling'});assert.deepEqual(busyText(),[['tesla_vehicle.content.refreshing',false]]);
 st({});assert.equal(busyText().length,0);assert.equal(button('read_soc').props.disabled,false);
 // Pairing: only the current step; the key-card instruction appears only after the request was transmitted.
 normal();const pst=extra=>{f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false,...extra};event('tesla_vehicle/state');};
 pst({pairing_pending:true,pair_request_sent:false});
 assert.deepEqual(busyText().map(x=>x[0]),['tesla_vehicle.content.pair_requesting']);assert(!nodes().some(n=>n.props?.children==='tesla_vehicle.status.pending'),'No key-card prompt before transmission');
 pst({pairing_pending:true,pair_request_sent:true});
 assert.deepEqual(busyText().map(x=>x[0]),['tesla_vehicle.content.pair_waiting']);assert(nodes().some(n=>n.props?.children==='tesla_vehicle.status.pending'),'Key-card prompt after the request was transmitted');
 pst({pair_request_sent:true,pairing_pending:false,paired:false});assert(button('pair'),'Unconfirmed attempt: pairing button back, no success claim');
 assert(!nodes().some(n=>n.props?.children==='tesla_vehicle.status.paired'),'Success only from authenticated paired status');
 // A failed explicit action re-enables the button; while it runs a second click is ignored.
 normal();st({});let release;f.call=async(t,p)=>{if(p.phase==='prepare_poll'){await new Promise(r=>release=r);throw new Error('synthetic rejection');}};
 const n0=f.calls.length;const run=button('read_soc').props.onClick();await new Promise(setImmediate);
 assert.deepEqual(busyText().map(x=>x[0]),['tesla_vehicle.content.refreshing'],'Own explicit request shows its spinner');
 await nodes().find(n=>n.type==='Button'&&n.props.children?.props?.text==='tesla_vehicle.content.refreshing').props.onClick();
 assert.equal(f.calls.filter(c=>c.payload?.phase==='prepare_poll').length,f.calls.slice(0,n0).filter(c=>c.payload?.phase==='prepare_poll').length+1,'No parallel second action');
 release();await run;assert.equal(busyText().length,0);assert.equal(button('read_soc').props.disabled,false,'Clickable again after failure');
 f.call=async()=>{};normal();console.log('GUIDED_BUSY_FEEDBACK_PASS');
}
{ // Live findings 2.13.5+6ae25bce: BLE value/time vs estimate, pairing busy scoping, structured SoC failure text.
 const busyText=()=>nodes().filter(n=>n.type==='Button'&&typeof n.props.children==='object'&&n.props.children?.props?.text).map(n=>[n.props.children.props.text,n.props.disabled]);
 const rowValue=label=>{const row=nodes().find(n=>n.type==='FormRow'&&n.props.label===label);return row&&walk(row).find(n=>n.type==='InputText')?.props.value;};
 const grey=()=>nodes().filter(n=>n.type==='div'&&n.props.class==='form-text mb-2'&&n.props.children==='tesla_vehicle.content.action_busy').length;
 normal();const st=extra=>{f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:true,last_soc:94,last_update:12345,last_update_unix:1793223270,...extra};event('tesla_vehicle/state');};
 // 1) Shared WARP value 95.7 (estimated) never shown as the Tesla measurement beside the BLE receive time.
 f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:95.7};event('ev/state');st({});
 assert.equal(rowValue('tesla_vehicle.status.soc'),'94.0 %','Main Tesla value is the BLE measurement');
 assert(rowValue('tesla_vehicle.status.soc_time'),'BLE receive time shown with the BLE value');
 assert.equal(rowValue('tesla_vehicle.status.soc_estimated'),'95.7 %','Estimate shown separately and labeled');
 f.values['ev/state'].soc=94;event('ev/state');assert.equal(rowValue('tesla_vehicle.status.soc_estimated'),undefined,'No estimate row when equal');
 assert.equal(f.values['ev/state'].soc,94,'Shared WARP SoC untouched by the view');
 // 3/4) Pairing in progress (incl. approval hand-back): no Refresh spinner, no duplicate grey busy text, button stays disabled.
 for(const extra of [{paired:false,pairing_pending:true,pair_request_sent:true,action_busy:true,action_kind:'pair'},
                     {paired:false,pairing_pending:false,action_busy:true,action_kind:'pair'},
                     {paired:true,pairing_pending:false,action_busy:true,action_kind:'pair'}]){
  st(extra);
  assert(!busyText().some(x=>x[0]==='tesla_vehicle.content.refreshing'),'Pairing never shows the SoC spinner: '+JSON.stringify(extra));
  assert.equal(button('read_soc').props.disabled,true,'Refresh gated while pairing is busy');
  if(extra.pairing_pending)assert.equal(grey(),0,'No grey duplicate under Refresh while the pairing button shows progress');
 }
 st({action_busy:true,action_kind:'soc'});assert.deepEqual(busyText(),[['tesla_vehicle.content.refreshing',true]],'Real SoC action keeps its spinner');
 st({action_busy:true,action_kind:'other'});assert.equal(busyText().length,0,'Other busy work: disabled, no SoC spinner');
 // 5) Classified failure: localized main text from the structured field; raw English only under the labeled diagnostics.
 const raw='Tesla SoC request: session setup unanswered (info_auth_wait)';
 st({poll_outcome:'failed',poll_outcome_automatic:true,poll_failure:'challenge_cap',last_error:raw});
 const alertText=JSON.stringify(walk(nodes().find(n=>n.type==='Alert'&&n.props.variant==='danger')));
 assert(alertText.includes('tesla_vehicle.status.poll_session')&&!alertText.includes(raw),'Localized classified failure, no raw English in the main alert');
 page.state.details_open=true;
 const rawCount=nodes().filter(n=>n.type==='div'&&n.props.children===raw).length;
 assert.equal(rawCount,1,'Raw detail exactly once');
 assert(walk(nodes().find(n=>n.type==='FormRow'&&n.props.label==='tesla_vehicle.content.technical_details')).some(n=>n.props?.children===raw),'Raw detail only under the technical label');
 page.state.details_open=false;
 st({poll_outcome:'failed',poll_failure:'deadline',last_error:'Tesla SoC request: no vehicle answer (deadline, status_wait, 40000 ms)'});
 assert(JSON.stringify(walk(nodes().find(n=>n.type==='Alert'))).includes('tesla_vehicle.status.poll_no_answer'));
 f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};event('ev/state');normal();console.log('GUIDED_LIVE_FINDINGS_PASS');
}
{ // Review BLOCK SoC provenance: A BLE 94 must never be shown under profile B (common 51, no B sample).
 // Backend contract (tesla_profiles.inc publish_profile): on identity change last_soc=NaN (JSON null) and last_update_unix=0 together; last_update kept.
 const rowValue=label=>{const row=nodes().find(n=>n.type==='FormRow'&&n.props.label===label);return row&&walk(row).find(n=>n.type==='InputText')?.props.value;};
 const macB=f.values['ev/config'].evs[1].mac,macA=f.values['ev/config'].evs[0].mac;
 const st=extra=>{f.values['tesla_vehicle/state']={key_presence:'present',key_present:true,paired:true,...extra};event('tesla_vehicle/state');};
 const noA=(why)=>{assert(!nodes().some(n=>n.props?.value==='94.0 %'),'No A 94 under '+why);assert.equal(rowValue('tesla_vehicle.status.soc_time'),undefined,'No A receive time under '+why);
  assert(!nodes().some(n=>typeof n.props?.value==='string'&&/\d{2}:\d{2}:\d{2}$/.test(n.props.value)),'No time-like value under '+why);};
 normal();
 // A measured via BLE: 94 with its own time.
 f.values['ev/state']={active_ev_index:0,mac:macA,soc:94};event('ev/state');st({profile_ev_index:0,last_soc:94,last_update:12345,last_update_unix:1793223270});
 assert.equal(rowValue('tesla_vehicle.status.soc'),'94.0 %');assert.match(String(rowValue('tesla_vehicle.status.soc_time')),/\d{2}:\d{2}:\d{2}$/);
 // B active/selected, common SoC 51 from another source, no B sample; backend withdrew (NaN in-process, null over JSON).
 for(const withdrawn of [NaN,null,undefined]){
  page.selectProfile('1');f.values['ev/state']={active_ev_index:1,mac:macB,soc:51};event('ev/state');
  st({profile_ev_index:1,last_soc:withdrawn,last_update:12345,last_update_unix:0});noA('B/common51 last_soc='+withdrawn);
 }
 // Real B sample: shown with its own time.
 st({profile_ev_index:1,last_soc:51,last_update:12400,last_update_unix:1793223900});
 assert.equal(rowValue('tesla_vehicle.status.soc'),'51.0 %','B BLE sample shown under B');assert.match(String(rowValue('tesla_vehicle.status.soc_time')),/\d{2}:\d{2}:\d{2}$/);
 // A->B->A: back on A without a new A sample, nothing stale reappears as fresh (neither A 94 nor B 51/time).
 page.selectProfile('0');f.values['ev/state']={active_ev_index:0,mac:macA,soc:94};event('ev/state');
 st({profile_ev_index:0,last_soc:NaN,last_update:12400,last_update_unix:0});noA('A->B->A without new A sample');
 assert(!nodes().some(n=>n.props?.value==='51.0 %'),'No B value reappears under A');
 f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};event('ev/state');normal();console.log('GUIDED_SOC_IDENTITY_ORIGIN_PASS');
}
let prepareResponse;f.call=async(t,p)=>{if(p.phase==='prepare_pair')await new Promise(r=>{prepareResponse=r;});};const pendingUnmount=button('pair').props.onClick();await new Promise(setImmediate);
count=f.calls.length;page.componentWillUnmount();prepareResponse();await pendingUnmount;assert.equal(f.calls.length,count+1);assert.equal(f.calls.at(-1).payload.phase,'cancel');
assert(f.calls.every(c=>['tesla_vehicle/scan','tesla_vehicle/profiles_update','tesla_vehicle/guided','tesla_vehicle/forget_key'].includes(c.topic)));
console.log('GUIDED_BEHAVIOR_PASS no-auto/toggle-save/disabled-reasons/modal-cancel/double-click/A-B-A/config/SLAC/generation/rejection/paired-poll/common-SoC/ambiguous/invalid-VIN/stale-save/unmount');
