// Strict API-boundary contract extracted from the actual firmware Config::Object.
// Not a permissive mock: required keys, primitive types/ranges and nested profile.
import {readFileSync} from 'node:fs';
import {execFileSync,spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const native=execFileSync('python3',[fileURLToPath(new URL('./build_native_guided_schema.py',import.meta.url))],{encoding:'utf8'}).trim();
const base=new URL('../../',import.meta.url);
const cpp=readFileSync(new URL('src/modules/tesla_vehicle/tesla_vehicle.cpp',base),'utf8');
const inc=readFileSync(new URL('src/modules/tesla_vehicle/tesla_profiles.inc',base),'utf8');
const object=(text,start,end)=>text.slice(text.indexOf(start)+start.length,text.indexOf(end,text.indexOf(start)));
const parse=text=>[...text.matchAll(/\{"([a-z_]+)",\s*(Config::(Str|Uint|Int|Bool)\(([^)]*)\)|make_profile\(\))/g)].map(m=>({key:m[1],kind:m[3]??'profile',args:m[4]?.split(',').map(x=>x.trim())}));
const schema=parse(object(cpp,'tesla->guided_command = Config::Object({','});'));
const profile=parse(object(inc,'Config TeslaVehicle::make_profile()','\nConfig *TeslaVehicle::find_profile'));
if(schema.length!==5||profile.length!==9)throw Error('Firmware schema extraction incomplete');
export const guidedSchema={schema,profile};
const evHeader=readFileSync(new URL('src/modules/ev/ev.h',base),'utf8');
const evMax=Number(evHeader.match(/^#define EV_MAX_EVS (\d+)$/m)?.[1]);
if(!Number.isInteger(evMax)||evMax<1)throw Error('EV schema bound unavailable');
const number=s=>s==='UINT32_MAX'?0xffffffff:s==='EV_MAX_EVS - 1'?evMax-1:Number(s);
export function validateGuided(payload){
 const result=spawnSync(native,[],{input:JSON.stringify(payload)+'\n',encoding:'utf8'});
 if(result.status!==0)throw Error('HTTP 400 Bad Request: '+(result.stdout||result.stderr||String(result.error)).trim());
 const check=(p,fields)=>{if(!p||Array.isArray(p)||typeof p!=='object')throw Error('JSON object required');
  for(const f of fields){if(!Object.hasOwn(p,f.key))throw Error(`HTTP 400 Bad Request: JSON object is missing key '${f.key}'`);
   const v=p[f.key];if(f.kind==='profile'){check(v,profile);continue;}
   const [_,min,max]=f.args;
   if(f.kind==='Bool'?typeof v!=='boolean':f.kind==='Str'?typeof v!=='string'||v.length<number(min)||v.length>number(max):!Number.isInteger(v)||v<number(min)||v>number(max))throw Error('HTTP 400 Bad Request: invalid '+f.key);
  }
  for(const k of Object.keys(p))if(!fields.some(f=>f.key===k))throw Error('HTTP 400 Bad Request: unknown key '+k);
 };check(payload,schema);
}
