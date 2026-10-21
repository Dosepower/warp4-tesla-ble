// UX-spec update: explicit guided prepare+commit replaces a separate user Start.
// Shared production VDOM/handler suite retains cancellation, restart, busy,
// identity/config changes, A-B-A, no-auto, OFF, adoption, and poll boundaries.
await import('./test_guided_frontend.mjs');
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source=p=>readFileSync(new URL('../../'+p,import.meta.url),'utf8');
const firmware=source('src/modules/tesla_vehicle/tesla_vehicle.cpp');
const schema=source('web/src/modules/tesla_vehicle/api.ts');
const fields=[...firmware.split('\n    tesla->state = Config::Object({')[1].split('});')[0].matchAll(/\{"(\w+)", Config::(\w+)/g)];
const tsFields=[...schema.split('export interface state {')[1].split('}')[0].matchAll(/(\w+): ([^;]+);/g)];
assert.deepEqual(fields.map(m=>m[1]).sort(),tsFields.map(m=>m[1]).sort());
for(const[,field,type]of fields){const ts=tsFields.find(m=>m[1]===field)[2];if(type==='Bool')assert.equal(ts,'boolean');if(['Float','Uptime'].includes(type))assert.equal(ts,'number');}
for(const command of ['start','pair','poll','forget_key']){
 assert(schema.includes(`export type ${command} = {ev_index: number};`));assert(firmware.includes(`api.addCommand("tesla_vehicle/${command}"`));
}
assert(firmware.includes('api.addCommand("tesla_vehicle/guided"'));
const keys=language=>[...source(`web/src/modules/tesla_vehicle/translation_${language}.tsx`).matchAll(/"(\w+)":/g)].map(m=>m[1]).sort();
assert.deepEqual(keys('de'),keys('en'));
const main=source('web/src/modules/tesla_vehicle/main.tsx'),guided=source('web/src/modules/tesla_vehicle/guided_vehicle.tsx');
assert(main.includes('registerVehicleExtension(TeslaVehicle)'));
assert(!main.includes('TeslaVehicleNavbar'));assert(!guided.includes('<SubPage'));
assert(!guided.includes('bench_'));assert(!guided.includes('__(`'));
const ev=source('web/src/modules/ev/main.tsx');
assert(ev.indexOf('{vehicleExtension ? h(vehicleExtension, {})')>ev.indexOf('ev.content.ev_profiles'));
assert(source('src/modules/tesla_vehicle/module.ini').includes('           Ev'));
assert.equal((source('warp4_tesla_ble.ini').match(/Tesla Vehicle/g)??[]).length,3);
console.log('Tesla BLE guided integration/schema/translation/inline placement PASS; old independent Start and duplicate-SoC prohibitions intentionally superseded by approved UX.');
