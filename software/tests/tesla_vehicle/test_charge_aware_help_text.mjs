// Charge-aware SoC polling with automatic wake coupling (no wake switch any more).
// The remaining help text (poll interval) must describe the firmware schedule honestly
// (tesla_auto_poll.inc): plug-in request (bounded retries), charge start/resume, periodic
// only while charging; waking allowed exactly there and spaced; no new request in
// pauses/after the end; never "immediately", never a charging action. Removed
// explanatory/wake texts must stay removed in DE and EN.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const web = join(dirname(fileURLToPath(import.meta.url)), '../../web/src/modules/tesla_vehicle');
const source = file => readFileSync(join(web, file), 'utf8');
const text = (file, key) => {
    const m = source(file).match(new RegExp(`^\\s*"${key}": "([^"\\n]*)",?$`, 'm'));
    assert.ok(m, `${file}: ${key} missing`);
    return m[1];
};
for (const file of ['translation_de.tsx', 'translation_en.tsx']) {
    for (const removed of ['auto_wake', 'auto_wake_desc', 'auto_wake_help', 'manual_wake_help', 'pair_help', 'key_connection_help', 'shared_soc', 'soc_unavailable'])
        assert.doesNotMatch(source(file), new RegExp(`"${removed}":`), `${file}: removed text ${removed} is back`);
}
assert.doesNotMatch(source('guided_vehicle.tsx'), /auto_wake|wake_once: !|<Switch/, 'No wake switch in the vehicle UI');
const de = text('translation_de.tsx', 'poll_interval_help'), en = text('translation_en.tsx', 'poll_interval_help');
assert.match(de, /einmal nach dem Anstecken \(begrenzt wiederholt\), bei Ladebeginn bzw\. Fortsetzung und während des Ladens in diesem Intervall/);
assert.match(en, /once after plugging in \(retried a limited number of times\), when charging starts or resumes and during charging at this interval/);
assert.match(de, /darf WARP ein schlafendes Fahrzeug wecken – höchstens einmal je Abfrage und einmal alle 2 Minuten/);
assert.match(en, /may wake a sleeping vehicle for these requests – at most once per request and once every 2 minutes/);
assert.match(de, /In Ladepausen und nach Ladeende keine neue Abfrage/);
assert.match(en, /No new request during charging pauses or after charging ends/);
assert.match(de, /startet oder ändert nie eine Ladung/);
assert.match(en, /never start or change charging/);
assert.doesNotMatch(de, /sofort|unmittelbar|nie geweckt/i);
assert.doesNotMatch(en, /immediately|right away|never woken/i);
assert.match(text('translation_de.tsx', 'auto_poll_not_charging'), /kurz nach Ladebeginn/);
assert.match(text('translation_en.tsx', 'auto_poll_not_charging'), /shortly after charging starts/);
// Honest wake wording: "woken" and "wake sent" are distinct outcomes in both languages.
assert.match(text('translation_de.tsx', 'outcome_wake_sent_no_soc'), /^Weckversuch gesendet/);
assert.match(text('translation_en.tsx', 'outcome_wake_sent_no_soc'), /^Wake request sent/);
assert.match(text('translation_de.tsx', 'outcome_woken_no_soc'), /^Fahrzeug wurde geweckt/);
console.log('CHARGE_AWARE_HELP_TEXT_PASS wake-coupled');
