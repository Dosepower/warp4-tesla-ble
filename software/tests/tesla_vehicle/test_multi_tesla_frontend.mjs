import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const text=await readFile(new URL('../../web/src/modules/tesla_vehicle/main.tsx',import.meta.url),'utf8');
assert(text.includes('"tesla_vehicle/profiles"'),'multi-profile form API required');
assert(!text.includes('status?.last_soc'),'no duplicate SoC display');
assert(text.includes('profile_ev_index'),'status must identify its WARP profile');
assert(text.includes('ev_index: confirmedIndex'),'actions must carry confirmed profile');
console.log('MULTI_TESLA_FRONTEND_CONTRACT_PASS');
