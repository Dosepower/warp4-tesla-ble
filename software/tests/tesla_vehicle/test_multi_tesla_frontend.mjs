import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const text=await readFile(new URL('../../web/src/modules/tesla_vehicle/guided_vehicle.tsx',import.meta.url),'utf8');
assert(text.includes('"tesla_vehicle/profiles"'));
assert(text.includes('profile_ev_index'));
assert(text.includes('profile: {...this.profile()}'),'Guided radio consent captures full selected profile, not a mutable index');
assert(text.includes('state.ev_state?.soc'),'Displayed SoC comes from common EV model');
await import('./test_guided_frontend.mjs');
console.log('MULTI_TESLA_FRONTEND_CONTRACT_PASS guided profile/generation/action isolation');
