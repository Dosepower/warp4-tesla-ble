// Charge-aware SoC polling: the help texts must describe the firmware schedule
// honestly (tesla_auto_poll.inc): one plug-in request that may wake (bounded
// retries) even without charging, no periodic idle requests afterwards, an
// already started request completes, and a charge start/resume request follows
// shortly (settle delay + minimum gap), not "immediately".
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const web = join(dirname(fileURLToPath(import.meta.url)), '../../web/src/modules/tesla_vehicle');
const text = (file, key) => {
    const m = readFileSync(join(web, file), 'utf8').match(new RegExp(`^\\s*"${key}": "([^"\\n]*)",$`, 'm'));
    assert.ok(m, `${file}: ${key} missing`);
    return m[1];
};

const de = {poll: text('translation_de.tsx', 'poll_interval_help'), wake: text('translation_de.tsx', 'auto_wake_help'),
    idle: text('translation_de.tsx', 'auto_poll_not_charging')};
const en = {poll: text('translation_en.tsx', 'poll_interval_help'), wake: text('translation_en.tsx', 'auto_wake_help'),
    idle: text('translation_en.tsx', 'auto_poll_not_charging')};

// No absolute "never woken without charging" claim: the plug-in request may wake while idle.
assert.doesNotMatch(de.wake, /nie automatisch geweckt|niemals/i);
assert.doesNotMatch(en.wake, /never woken|never wakes/i);
assert.match(de.wake, /Abfrage nach dem Anstecken \(auch wenn noch nicht geladen wird, einschließlich ihrer begrenzten Wiederholungen\)/);
assert.match(en.wake, /request after plugging in \(even if the vehicle is not charging yet, including its limited retries\)/);
assert.match(de.wake, /keine neuen automatischen Abfragen und damit kein regelmäßiges Wecken/);
assert.match(en.wake, /no new automatic requests and therefore no periodic waking/);
assert.match(de.wake, /bereits begonnene Abfrage wird noch zu Ende geführt und kann dabei einmal wecken/);
assert.match(en.wake, /request already started is still completed and may wake once/);

// Charge start/resume: shortly, bounded by the settle delay and the 30 s minimum gap; never "immediately".
for (const s of [de.poll, de.idle]) assert.doesNotMatch(s, /sofort|unmittelbar/i);
for (const s of [en.poll, en.idle]) assert.doesNotMatch(s, /immediately|right away|instantly/i);
assert.match(de.poll, /folgt kurz darauf eine zusätzliche Abfrage \(nach einigen Sekunden, frühestens 30 Sekunden nach der letzten Abfrage\)/);
assert.match(en.poll, /additional request follows shortly \(after a few seconds, at the earliest 30 seconds after the previous request\)/);
assert.match(de.idle, /kurz nach Ladebeginn/);
assert.match(en.idle, /shortly after charging starts/);

// Idle/pause: no NEW automatic request; an already started request completes; bounded plug-in retries.
assert.match(de.poll, /startet keine neue automatische Abfrage; eine bereits begonnene Abfrage wird noch zu Ende geführt/);
assert.match(en.poll, /no new automatic request is started; a request already started is still completed/);
assert.match(de.poll, /einmal nach dem Anstecken \(bei Fehlschlag begrenzt wiederholt\)/);
assert.match(en.poll, /once after plugging in \(retried a limited number of times if it fails\)/);
assert.doesNotMatch(de.poll, /nicht automatisch abgefragt oder geweckt/);
assert.doesNotMatch(en.poll, /nothing is requested or woken automatically/);
console.log('CHARGE_AWARE_HELP_TEXT_PASS');
