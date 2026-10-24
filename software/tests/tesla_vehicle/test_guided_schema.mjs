import assert from 'node:assert/strict';
import {validateGuided} from './guided_schema.mjs';
const profile={ev_index:0,ev_mac:'11:22:33:44:55:00',enable:true,vin:'5YJ3E1EA7KF000001',ble_address:'01:02:03:04:05:06',ble_address_type:1,discovery_revision:4,poll_interval:300,wake_once:false};
let accepted=0,rejected=0;
for(const phase of ['prepare_pair','prepare_poll','commit','cancel','reset']){
 const payload={phase,request:'abcdef0123456789abcdef0123456789',generation:'7',revision:12,profile};
 validateGuided(payload);accepted++;
 for(const key of Object.keys(payload)){
  const p=structuredClone(payload);delete p[key];assert.throws(()=>validateGuided(p),new RegExp("JSON object is missing key '"+key+"'"));rejected++;
 }
 for(const key of Object.keys(profile)){
  const p=structuredClone(payload);delete p.profile[key];assert.throws(()=>validateGuided(p),new RegExp("JSON object is missing key '"+key+"'"));rejected++;
 }
 for(const patch of [{revision:-1},{revision:4294967296},{revision:'12'},{profile:{...profile,ev_index:16}},{profile:{...profile,enable:'true'}},{unexpected:true}]){
  assert.throws(()=>validateGuided({...payload,...patch}),/HTTP 400/);rejected++;
 }
}
assert.throws(()=>validateGuided({request:'abcdef0123456789abcdef0123456789',generation:'7',profile}),/JSON object is missing key 'revision'/);
console.log(JSON.stringify({contract:'native authored Config::Object + verbatim production from_json overloads, storage adapters only; supplementary range checks',accepted,rejected:rejected+1,legacy_missing_revision_reproduced:true}));
