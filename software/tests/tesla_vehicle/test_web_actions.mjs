import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import vm from 'node:vm';
const require = createRequire(import.meta.url);
const {buildSync} = require('../../web/node_modules/esbuild');
const entry = fileURLToPath(new URL('../../web/src/modules/tesla_vehicle/action_policy.ts', import.meta.url));
const compiled = buildSync({entryPoints: [entry], bundle: true, write: false, platform: 'node', format: 'cjs'});
const module = {exports: {}};
vm.runInNewContext(compiled.outputFiles[0].text, {module, exports: module.exports});
const {teslaActions, teslaKeyStatus, teslaVehicleReady, teslaStartAllowed} = module.exports;
const bt = {supported: true, phase: 'active', restart_required: false, persistence_error: false};
const idle = {running: false, stopping: false, restart_required: false};
assert.equal(teslaStartAllowed(true, idle, bt, false, false), true);
for (const status of [undefined, {...idle, running: true}, {...idle, stopping: true}, {...idle, restart_required: true}])
    assert.equal(teslaStartAllowed(true, status, bt, false, false), false);
for (const bluetooth of [undefined, {...bt, phase: 'starting'}, {...bt, phase: 'disabled'}, {...bt, restart_required: true}, {...bt, supported: false}, {...bt, persistence_error: true}])
    assert.equal(teslaStartAllowed(true, idle, bluetooth, false, false), false);
assert.equal(teslaStartAllowed(false, idle, bt, false, false), false);
assert.equal(teslaStartAllowed(true, idle, bt, true, false), false);
assert.equal(teslaStartAllowed(true, idle, bt, false, true), false);
const active = {running: true, paired: true, key_present: true, key_presence: 'present'};
const readyActions = (enable, status, dirty = false, busy = false) => teslaActions(enable, status, dirty, true, busy, bt);
const normal = readyActions(true, active);
assert.equal(normal.pairDisabled, true); // Established identities are not paired again on vehicle switches.
assert.equal(normal.pollDisabled, false);
assert.equal(normal.deleteDisabled, false);
const stopping = readyActions(true, {...active, stopping: true});
assert.equal(stopping.transition, true);
assert.equal(stopping.pairDisabled, true);
assert.equal(stopping.pollDisabled, true);
assert.equal(stopping.deleteDisabled, true);
assert.equal(teslaActions(false, active, false).pairDisabled, true);
assert.equal(teslaActions(true, {...active, restart_required: true}, false).pollDisabled, true);
assert.equal(teslaActions(false, {running: false, key_present: false, key_presence: 'unknown'}, false).deleteDisabled, false);
assert.equal(teslaActions(false, {key_presence: 'absent'}, false).deleteDisabled, true);
assert.equal(teslaActions(false, {key_presence: 'unknown', stopping: true}, false).deleteDisabled, true);
assert.equal(teslaActions(false, {key_presence: 'unknown', restart_required: true}, false).deleteDisabled, true);
// A local key must be manually verifiable BEFORE authenticated pairing exists.
assert.equal(readyActions(true, {...active, paired: false}).pollDisabled, false);
for (const change of [{running: false}, {stopping: true}, {restart_required: true}, {pairing_pending: true}, {key_presence: 'unknown'}, {key_presence: 'absent'}]) {
    assert.equal(readyActions(true, {...active, ...change}).pollDisabled, true);
}
assert.equal(readyActions(false, active).pollDisabled, true);
for (const dirty of [false, true]) for (const busy of [false, true]) {
    const actions = readyActions(true, active, dirty, busy);
    assert.equal(actions.pairDisabled, true);
    assert.equal(actions.pollDisabled, dirty || busy);
    assert.equal(actions.deleteDisabled, dirty || busy);
}
assert.equal(teslaActions(true, active, false, false).pairDisabled, true);
assert.equal(teslaActions(true, active, false, false).pollDisabled, true);
assert.equal(teslaActions(true, undefined, false, true).deleteDisabled, true);
assert.equal(teslaKeyStatus(undefined), 'key_unknown');
assert.equal(teslaKeyStatus({key_present: false, key_presence: 'cleanup'}), 'cleanup_required');
assert.equal(readyActions(true, {...active, key_present: false, key_presence: 'cleanup'}).pollDisabled, true);
assert.equal(readyActions(true, {...active, key_present: false, key_presence: 'cleanup'}).deleteDisabled, false);
assert.equal(teslaKeyStatus({key_presence: 'absent'}), 'not_paired');
assert.equal(teslaKeyStatus({...active, paired: false}), 'key_only');
assert.equal(teslaKeyStatus(active), 'paired');
assert.equal(teslaKeyStatus({...active, pairing_pending: true, pair_request_sent: false}), 'sending');
assert.equal(teslaKeyStatus({...active, paired: false, pairing_pending: true, pair_request_sent: true}), 'pending');
// A stale sent bit alone must never claim approval is still pending.
assert.equal(teslaKeyStatus({...active, paired: false, pair_request_sent: true}), 'key_only');
assert.equal(teslaVehicleReady(2, {active_ev_index: 2, mac: 'synthetic-nonzero'}), true);
for (const state of [undefined, {active_ev_index: -1, mac: ''}, {active_ev_index: 1, mac: 'synthetic-nonzero'}, {active_ev_index: 2, mac: ''}, {active_ev_index: 2, mac: '00:00:00:00:00:00'}]) {
    assert.equal(teslaVehicleReady(2, state), false);
}
assert.equal(teslaVehicleReady(-1, {active_ev_index: -1, mac: 'synthetic-nonzero'}), false);
// Fail closed even when the Tesla status is stale and still claims running.
for (const bluetooth of [undefined, {...bt,phase:'disabled'}, {...bt,phase:'starting'}, {...bt,restart_required:true}, {...bt,persistence_error:true}, {...bt,supported:false}]) {
    const actions = teslaActions(true, active, false, true, false, bluetooth);
    assert.equal(actions.pairDisabled,true);
    assert.equal(actions.pollDisabled,true);
}
for (const change of [{action_busy:true}, {pairing_pending:true}]) {
    const actions = teslaActions(true,{...active,...change},false,true,false,bt);
    assert.equal(actions.pairDisabled,true); assert.equal(actions.pollDisabled,true); assert.equal(actions.deleteDisabled,true);
}
console.log('Tesla BLE web action tests passed.');
