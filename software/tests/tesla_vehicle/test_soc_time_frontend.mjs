// Unit contract for the SoC receive-time formatter (production module, real Intl/Date).
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const require=createRequire(new URL('../../web/package.json',import.meta.url));
const {build}=require('esbuild');
const tree=process.env.TESLA_WEB_SOURCE_TREE??'src_tfpp';
assert(['src','src_tfpp'].includes(tree));
const out=await build({entryPoints:[fileURLToPath(new URL(`../../web/${tree}/modules/tesla_vehicle/soc_time.ts`,import.meta.url))],bundle:true,write:false,platform:'node',format:'esm'});
const {formatSocReceiveTime}=await import('data:text/javascript;base64,'+Buffer.from(out.outputFiles[0].text).toString('base64'));
const labels={unsynced:'UNSYNCED',today:'TODAY'};
// Unsynchronized WARP clock (0) or garbage: never a time of day, never uptime.
for(const v of [0,-1,NaN,undefined])assert.equal(formatSocReceiveTime(v,'Europe/Berlin',Date.UTC(2026,9,28,12),labels),'UNSYNCED');
// 2026-10-28 22:30:05 UTC = 2026-10-28 23:30:05 Europe/Berlin (CET).
const t=Date.UTC(2026,9,28,22,30,5)/1000;
assert.match(formatSocReceiveTime(t,'Europe/Berlin',Date.UTC(2026,9,28,22,40),labels),/^TODAY, 23:30:05$/);
// Same instant, viewer "now" already 2026-10-29 in Berlin: unambiguous date instead of "today".
const tomorrow=formatSocReceiveTime(t,'Europe/Berlin',Date.UTC(2026,9,29,6),labels);
assert(!tomorrow.startsWith('TODAY')&&tomorrow.includes('2026')&&tomorrow.endsWith('23:30:05'),tomorrow);
// The WARP zone decides the calendar day, not the viewing device: 23:30 Berlin is "yesterday" in UTC terms only if the zone is ignored.
assert.match(formatSocReceiveTime(t,'UTC',Date.UTC(2026,9,28,23),labels),/^TODAY, 22:30:05$/);
// Unknown zone name falls back to the browser zone instead of throwing.
assert.match(formatSocReceiveTime(t,'Not/AZone',Date.now(),labels),/\d{2}:\d{2}:\d{2}$/);
console.log('SOC_TIME_FORMAT_PASS '+tree);
