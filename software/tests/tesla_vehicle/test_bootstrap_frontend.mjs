// Production VDOM and event handlers, synthetic-only API boundary.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {readFileSync} from 'node:fs';
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
const compiled=await build({entryPoints:[fileURLToPath(new URL(`../../web/${tree}/modules/tesla_vehicle/main.tsx`,import.meta.url))],metafile:true,bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundary',setup(b){if(process.env.ASTRA_BASELINE_COMPONENT)b.onLoad({filter:/guided_vehicle\.tsx$/},()=>({contents:readFileSync(process.env.ASTRA_BASELINE_COMPONENT,'utf8'),loader:'tsx'}));b.onResolve({filter:/^(\.\.\/\.\.\/ts\/|react-bootstrap$|react-feather$|\.\.\/ev\/main$)/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:mock,loader:'js'}));}}]});
// This is the real import-resolved implementation, never a detached authored fixture.
assert(Object.keys(compiled.metafile.inputs).some(p=>p.endsWith(`/web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)||p.endsWith(`web/${tree}/modules/tesla_vehicle/guided_vehicle.tsx`)), 'Production entrypoint must resolve the final guided component');
const module={exports:{}};
vm.runInNewContext(compiled.outputFiles[0].text,{module,exports:module.exports,fixture:f,structuredClone,Date:{now:()=>Date.now()+(f.offset??0)},setTimeout:(fn,ms)=>setTimeout(()=>{f.offset=(f.offset??0)+(f.advance??0);fn();},f.advance?0:ms),clearTimeout,AbortController,crypto:globalThis.crypto});
f.values['ev/config']={evs:[0,1].map(i=>({name:`Synthetic ${i}`,mac:`11:22:33:44:55:0${i}`}))};
f.values['ev/state']={active_ev_index:0,mac:'11:22:33:44:55:00',soc:null};
f.values['bluetooth/config']={enable:true};
f.values['bluetooth/state']={supported:true,phase:'active'};
f.values['tesla_vehicle/profiles']={entries:[0,1].map(i=>({ev_index:i,ev_mac:f.values['ev/config'].evs[i].mac,enable:false,vin:i?'5YJ3E1EA7KF000002':'5YJ3E1EA7KF000001',ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}))};
f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};
f.values['tesla_vehicle/guidance']={generation:'7',request:'',ready:false,profiles:[{ev_index:0,paired:false,key_presence:'absent',binding_fresh:true}]};
f.values['tesla_vehicle/discovery']={status:'idle',busy:false,targets:[]};

const event=t=>{for(const cb of f.listeners[t]??[])cb();};
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const initialEvs=structuredClone(f.values['ev/config']);
for(const scenario of ['hex-mac','initial','second','missing','unknown','io','orphan','retained','paired','stale-mac','stale-revision','cross-vin']) {
 f.listeners={};f.calls=[];f.values['ev/config']=structuredClone(initialEvs);
 f.values['tesla_vehicle/profiles']={entries:[]};
 f.values['tesla_vehicle/state']={profile_ev_index:-1,running:false,key_presence:'unknown'};
 const index=scenario==='second'?1:0;
 if(scenario==='hex-mac')f.values['ev/config'].evs[0].mac='AA:BB:CC:DD:00:11';
 const row={ev_index:index,ev_mac:f.values['ev/config'].evs[index].mac,namespace_clean:true,paired:false,key_presence:'absent',binding_fresh:false};
 if(scenario==='second')f.values['tesla_vehicle/profiles'].entries=[{ev_index:0,ev_mac:initialEvs.evs[0].mac,vin:'5YJ3E1EA7KF000001',enable:false,ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}];
 if(['unknown','io','orphan','retained','paired'].includes(scenario)){row.namespace_clean=false;row.key_presence=scenario==='orphan'?'cleanup':['retained','paired'].includes(scenario)?'present':'unknown';row.paired=scenario==='paired';}
 f.values['tesla_vehicle/guidance']={generation:'7',revision:0,request:'',ready:false,profiles:scenario==='missing'?[]:[row]};
 const page=new module.exports.TeslaVehicle();
 if(index)page.selectProfile(String(index));
 if(scenario==='stale-mac'){f.values['ev/config'].evs[0].mac='11:22:33:44:55:09';event('ev/config');}
 if(scenario==='stale-revision'){f.values['tesla_vehicle/profiles']={entries:[{ev_index:0,ev_mac:initialEvs.evs[0].mac,vin:'5YJ3E1EA7KF000001',enable:false,ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}]};event('tesla_vehicle/profiles');}
 if(scenario==='cross-vin'){row.namespace_clean=false;row.key_presence='present';f.values['tesla_vehicle/state']={profile_ev_index:0,key_presence:'present',key_present:true,paired:false};event('tesla_vehicle/state');page.setState({entries:[{ev_index:0,ev_mac:initialEvs.evs[0].mac,vin:'5YJ3E1EA7KF000001',enable:false,ble_address:'',ble_address_type:0,discovery_revision:0,poll_interval:300,wake_once:false}]});}
 const input=walk(page.render({},page.state)).find(n=>n.type==='InputText'&&typeof n.props.onValue==='function');assert(input);
 const before=page.profile().vin,clean=['hex-mac','initial','second'].includes(scenario);
 input.props.onValue('5YJ3E1EA7KF000009');
 assert.equal(!!input.props.disabled,!clean,scenario+' input');
 assert.equal(page.profile().vin,clean?'5YJ3E1EA7KF000009':before,scenario+' handler');
 if(clean){page.setProfile({vin:'5YJ3E1EA7KF000008'});assert.equal(page.profile().vin,'5YJ3E1EA7KF000008','draft remains editable with same namespace evidence');}
 assert.equal(f.calls.length,0,'VIN editing never calls API/radio');page.componentWillUnmount();
 console.log('BOOTSTRAP_FRONTEND_PASS '+tree+' '+scenario);
}
