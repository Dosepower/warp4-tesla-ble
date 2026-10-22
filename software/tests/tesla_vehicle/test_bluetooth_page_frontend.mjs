// Real production Bluetooth component handlers; only API/util/visual leaf boundaries synthetic.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';import {fileURLToPath} from 'node:url';import vm from 'node:vm';
const require=createRequire(new URL('../../web/package.json',import.meta.url));const {build}=require('esbuild');
const tree=process.env.TESLA_WEB_SOURCE_TREE??'src_tfpp';assert(['src','src_tfpp'].includes(tree));
const f={values:{},listeners:{},calls:[],modals:[],reboots:0,show:async()=>true,save:async()=>{},call:async()=>{},read:async t=>structuredClone(f.values[t]),modalOpen:false};
const boundary=`const f=globalThis.fixture;
export const get=t=>f.values[t];export const extract=(t,s)=>({enable:s.enable});
export const save=async(t,p)=>{f.calls.push({t,p});await f.save(t,p);};
export const call=async(t,p)=>{f.calls.push({t,p});await f.call(t,p);};
export const download=async p=>({text:async()=>JSON.stringify(await f.read(p.slice(1)))});
export const addApiEventListener=(t,c,o)=>{(f.listeners[t]??=[]).push(()=>{if(!o?.signal?.aborted)c();});};export const render_allowed=()=>true;
export const postReboot=()=>{f.reboots++;};export const async_modal_ref={current:{get state(){return {show:f.modalOpen};},show:async o=>{f.modals.push(o);return f.show();}}};
export const __=k=>k;export const registerVehicleExtension=()=>{};export const TeslaVehicle=()=>null;
export const Alert='Alert',Button='Button',Bluetooth='Bluetooth',NavbarItem='NavbarItem',SubPage='SubPage',FormRow='FormRow',Switch='Switch',PageHeader='PageHeader';`;
const compiled=await build({entryPoints:[fileURLToPath(new URL(`../../web/${tree}/modules/tesla_vehicle/main.tsx`,import.meta.url))],bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundary',setup(b){
b.onResolve({filter:/(^|\/)(api|util|translation|guided_vehicle|form_row|switch|navbar_item|sub_page|page_header)$|react-bootstrap$|react-feather$|\.\.\/ev\/main$/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:boundary,loader:'js'}));}}]});
const module={exports:{}};vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture:f,AbortController,Error,JSON});
const event=t=>{for(const c of f.listeners[t]??[])c();};
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
let page;const nodes=()=>walk(page.render({},page.state));const toggle=()=>nodes().find(n=>n.type==='Switch');
const reset=(enable=false,pending=false)=>{f.listeners={};f.calls=[];f.modals=[];f.reboots=0;f.modalOpen=false;
f.values={'bluetooth/config':{enable},'bluetooth/state':{supported:true,phase:enable&&!pending?'active':'disabled',restart_required:pending,restart_reason:pending?'configuration_changed':'',persistence_error:false,legacy_tesla_enabled:false}};
f.show=async()=>true;f.call=async()=>{};f.read=async t=>structuredClone(f.values[t]);f.save=async(t,p)=>{f.values[t]=structuredClone(p);f.values['bluetooth/state'].restart_required=true;f.values['bluetooth/state'].restart_reason='configuration_changed';event(t);event('bluetooth/state');};
page=new module.exports.BluetoothSettings();page.setState=v=>Object.assign(page.state,v);event('bluetooth/config');};
reset();
assert(!nodes().some(n=>n.type.name==='ConfigForm'),'No Save form/button in approved toggle-only UI');
assert.equal(nodes().filter(n=>n.type==='PageHeader').length,1);assert.equal(toggle().props.desc,'tesla_vehicle.bluetooth.desired');
f.show=async()=>false;await toggle().props.onClick();assert.equal(f.modals.length,1);assert.equal(toggle().props.checked,false);assert.equal(f.calls.length,0);assert.equal(f.reboots,0);
console.log('BLUETOOTH_TOGGLE_CANCEL_PASS');
for(const enable of [false,true]){
 reset(enable);let ack;f.save=async(t,p)=>{assert.equal(p.enable,!enable);assert.equal(f.reboots,0);await new Promise(r=>ack=r);f.values[t]=structuredClone(p);f.values['bluetooth/state'].restart_required=true;f.values['bluetooth/state'].restart_reason='configuration_changed';event(t);event('bluetooth/state');};
 const pending=toggle().props.onClick();await new Promise(setImmediate);
 assert.equal(f.modals.at(-1).title(),enable?'tesla_vehicle.bluetooth.deactivate_title':'tesla_vehicle.bluetooth.activate_title');
 assert.equal(f.modals.at(-1).body(),'tesla_vehicle.bluetooth.change_confirm');assert.equal(f.modals.at(-1).no_text(),'tesla_vehicle.content.cancel');
 assert.equal(f.modals.at(-1).yes_text(),enable?'tesla_vehicle.bluetooth.deactivate_restart':'tesla_vehicle.bluetooth.activate_restart');
 assert.equal(f.calls.length,1,'Confirmed toggle must save exact intended value');assert.equal(f.reboots,0);assert.equal(toggle().props.checked,enable,'Prior value until persistence ACK');
 ack();await pending;assert.deepEqual(f.calls.map(c=>c.t),['bluetooth/config','bluetooth/restart']);assert.equal(f.reboots,1);assert.equal(toggle().props.checked,!enable);
}
console.log('BLUETOOTH_EXACT_SAVE_READBACK_BEFORE_REBOOT_PASS');
// Failure/ACK readback mismatch must be visible, never a restart.
for(const fail of ['save','read','mismatch','unsupported','persistence','busy','restart']){
 reset();if(fail==='save')f.save=async()=>{throw Error('injected commit failed');};
 if(fail==='read')f.read=async()=>{throw Error('readback timeout');};
 if(fail==='mismatch')f.read=async t=>t==='bluetooth/config'?{enable:false}:f.values[t];
 if(['unsupported','persistence','busy'].includes(fail))f.read=async t=>t==='bluetooth/state'?{...f.values[t],...(fail==='unsupported'?{supported:false}:fail==='persistence'?{persistence_error:true}:{phase:'stopping'})}:f.values[t];
 if(fail==='restart')f.call=async()=>{throw Error('restart refused');};
 await toggle().props.onClick();assert.equal(f.reboots,0,fail);assert(nodes().some(n=>n.type==='Alert'&&n.props.variant==='danger'),`Real visible error: ${fail}`);
 if(fail!=='restart')assert(!f.calls.some(c=>c.t==='bluetooth/restart'),fail);
 if(fail==='restart'){
  const retry=nodes().find(n=>n.type==='Button');assert(retry,'Persisted pending change has explicit retry, not inverted toggle');
  f.call=async()=>{};await retry.props.onClick();assert.equal(f.reboots,1);assert.equal(f.calls.filter(c=>c.t==='bluetooth/config').at(-1).p.enable,true);
 }
}
// Revision/lifetime guards, including external ABA; pending modal is single-use.
for(const when of ['modal','save','read'])for(const mutation of ['config','aba','state','unmount']){
 reset();let resume;const hold=()=>new Promise(r=>resume=r);
 if(when==='modal')f.show=hold;
 if(when==='save'){const save=f.save;f.save=async(t,p)=>{await hold();await save(t,p);};}
 if(when==='read'){const read=f.read;let first=true;f.read=async t=>{if(first){first=false;await hold();}return read(t);};}
 const pending=toggle().props.onClick();await new Promise(setImmediate);
 const count=f.calls.length;await toggle().props.onClick();assert.equal(f.calls.length,count,'Duplicate guarded');
 if(mutation==='config'||mutation==='aba'){f.values['bluetooth/config']={enable:true,...(mutation==='config'?{unexpected:1}:{})};event('bluetooth/config');if(mutation==='aba'){f.values['bluetooth/config']={enable:false};event('bluetooth/config');}}
 if(mutation==='state'){f.values['bluetooth/state'].phase='stopping';event('bluetooth/state');}
 if(mutation==='unmount')page.componentWillUnmount();
 resume(true);await pending;assert.equal(f.reboots,0,`${when}/${mutation}`);assert(!f.calls.some(c=>c.t==='bluetooth/restart'));
}
reset();f.modalOpen=true;await toggle().props.onClick();assert.equal(f.modals.length,0,'Never replace another WARP modal');assert.equal(f.calls.length,0);
for(const s of [{supported:false},{phase:'starting'},{phase:'stopping'}]){reset();Object.assign(f.values['bluetooth/state'],s);event('bluetooth/state');await toggle().props.onClick();assert.equal(f.calls.length,0);}
reset();f.values['tesla_vehicle/state']={action_busy:true};await toggle().props.onClick();assert.equal(f.calls.length,0);
reset();f.values['bluetooth/state'].legacy_tesla_enabled=true;event('bluetooth/state');await toggle().props.onClick();assert.equal(f.calls[0].p.enable,true,'Never reinterpret legacy flag as global enable');
reset(true,true);assert(toggle().props.checked);assert(!nodes().some(n=>n.type==='Alert'&&n.props.variant==='warning'),'No permanent yellow restart box');assert.equal(f.calls.length,0,'Mount and updates never reboot');
const retry=nodes().find(n=>n.type==='Button');assert(retry);f.show=async()=>false;await retry.props.onClick();assert.equal(f.calls.length,0);
reset();f.save=async(t,p)=>{f.values[t]=p;f.values['bluetooth/state'].restart_required=false;event(t);};await toggle().props.onClick();assert.equal(f.reboots,0,'No-op runtime ACK never reboots');
reset();let prevented=false;f.show=async()=>false;await toggle().props.onClick({preventDefault(){prevented=true;}});assert(prevented,'Native switch does not visually change before confirmation');
console.log('BLUETOOTH_FAILURE_RACES_NOOP_RETRY_PASS');
