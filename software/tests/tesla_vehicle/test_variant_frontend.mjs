// Execute the actual generated page for each compiled feature selection.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import vm from 'node:vm';
import path from 'node:path';
const root=process.env.WARP_VARIANT_SOFTWARE;
assert(root,'WARP_VARIANT_SOFTWARE must identify an actual generated build tree');
const tesla=process.env.WARP_TEST_TESLA==='1',remote=process.env.WARP_TEST_REMOTE==='1';
const require=createRequire(path.join(root,'web/package.json'));
const {build}=require('esbuild');
const f={values:{'bluetooth/config':{enable:true},'bluetooth/state':{supported:true,phase:'active',restart_required:false,persistence_error:false},'bluetooth/remote_config':{enable:false},'bluetooth/remote_bindings':{entries:[]},'tesla_vehicle/state':{action_busy:true}},calls:[],extensions:0};
const boundary=`const f=globalThis.fixture;
export const get=t=>f.values[t];export const save=async(t,p)=>{f.calls.push(t);};export const call=async(t,p)=>{f.calls.push(t);};
export const addApiEventListener=()=>{};export const render_allowed=()=>true;export const __=k=>k;
export const download=()=>{};export const async_modal_ref={current:{state:{show:false},show:async()=>false}};export const postReboot=()=>{};
export const registerVehicleExtension=()=>{f.extensions++;};export const TeslaVehicle=()=>null;
export const Alert='Alert',Button='Button',Bluetooth='Bluetooth',NavbarItem='NavbarItem',SubPage='SubPage',FormRow='FormRow',Switch='Switch',PageHeader='PageHeader';`;
const output=await build({entryPoints:[path.join(root,'web/src_tfpp/modules/tesla_vehicle/main.tsx')],bundle:true,write:false,platform:'node',format:'cjs',plugins:[{name:'boundaries',setup(b){b.onResolve({filter:/(^|\/)(api|util|translation|guided_vehicle|form_row|switch|navbar_item|sub_page|page_header)$|react-bootstrap$|react-feather$|\.\.\/ev\/main$/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:boundary,loader:'js'}));}}]});
const module={exports:{}};vm.runInNewContext(output.outputFiles[0].text,{module,exports:module.exports,fixture:f,AbortController,Error,JSON});
module.exports.pre_init();assert.equal(f.extensions,tesla?1:0,'Only compiled Tesla registers an EV extension');
const nav=module.exports.BluetoothPageNavbar();assert.equal(nav.props.module,tesla?'tesla_vehicle':'warp_remote','Navbar must follow actual registered backend');
const page=new module.exports.BluetoothSettings();page.setState=v=>Object.assign(page.state,v);
const walk=n=>!n||typeof n!=='object'?[]:[n,...[n.props?.children].flat(Infinity).flatMap(walk)];
const nodes=walk(page.render({},page.state));
const control=nodes.find(n=>n.type==='Switch');assert(control);
assert.equal(control.props.disabled,tesla,'Excluded Tesla state cannot disable remote-only Bluetooth controls');
const remoteNode=nodes.find(n=>n.type?.name==='RemoteSettings');assert.equal(!!remoteNode,remote,'No remote component in Tesla-only generated page');
if(remote){
 const component=new remoteNode.type();component.setState=v=>Object.assign(component.state,v);
 assert(walk(component.render()).some(n=>n.type==='Switch'));
 f.values['bluetooth/config'].enable=false;
 assert.equal(component.render(),null,'Remote setting hidden while Bluetooth disabled');
 component.componentWillUnmount();
}
assert.deepEqual(f.calls,[],'Page construction/registration/render performs no actions');
page.componentWillUnmount();
console.log(JSON.stringify({pass:true,tesla,remote,navbar_owner:nav.props.module,automatic_calls:f.calls.length}));
