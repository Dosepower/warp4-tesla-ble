import {createRequire} from 'node:module';import {fileURLToPath} from 'node:url';import {writeFileSync} from 'node:fs';
const require=createRequire(new URL('../../web/package.json',import.meta.url));const {build}=require('esbuild');
for(const tree of ['src','src_tfpp']){
const entry=`import {h,render} from 'preact';import {RemoteSettings} from './modules/tesla_vehicle/remote_settings';
window.fixture={values:{},listeners:{},calls:[]};
window.fixture.event=t=>{for(const fn of window.fixture.listeners[t]??[])fn();};
render(h(RemoteSettings,{}),document.getElementById('app'));`;
const boundary=`const f=()=>window.fixture;
export const get=t=>f().values[t];export const save=async(t,p)=>{f().calls.push({t,p});f().values[t]=p;f().event(t);};
export const call=async(t,p)=>{f().calls.push({t,p});};export const download=async p=>({text:async()=>JSON.stringify(f().values[p.slice(1)])});
export const addApiEventListener=(t,c,o)=>{(f().listeners[t]??=[]).push(()=>{if(!o?.signal?.aborted)c();});};export const render_allowed=()=>true;
export const async_modal_ref={current:{state:{show:false},show:async()=>true}};
export const __=k=>k;`;
const out=await build({stdin:{contents:entry,resolveDir:fileURLToPath(new URL(`../../web/${tree}`,import.meta.url)),loader:'tsx'},bundle:true,write:false,platform:'browser',format:'iife',alias:{react:require.resolve('preact/compat'),'react-dom':require.resolve('preact/compat'),'react/jsx-runtime':require.resolve('preact/jsx-runtime')},plugins:[{name:'boundary',setup(b){b.onResolve({filter:/\/ts\/(api|util|translation)$/},a=>({path:a.path,namespace:'boundary'}));b.onLoad({filter:/.*/,namespace:'boundary'},()=>({contents:boundary,loader:'js'}));}}]});
const dest=fileURLToPath(new URL(`../../../../evidence/07-mounted-${tree}.html`,import.meta.url));
writeFileSync(dest,`<!doctype html><meta charset="utf-8"><div id="app"></div><script>${out.outputFiles[0].text.replaceAll('</script','<\\/script')}</script>`);
console.log(dest);
}
