# SPDX-License-Identifier: Apache-2.0
"""Negative/positive fixtures for charge_start_guard (used by test_safety.py and
test_production_binding.py). Synthetic snippets plus mutations of the real
module files; the real module must be clean and every mutation must be caught."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from charge_start_guard import violations, module_files

H = 'src/modules/tesla_vehicle/tesla_vehicle.h'
INC = 'src/modules/tesla_vehicle/tesla_auto_poll.inc'
CPP = 'src/modules/tesla_vehicle/tesla_vehicle.cpp'
STRUCT = 'struct AutoPoll {\n bool charging = false;\n bool charge_start = false; // pending\n uint32_t charge_edges = 0;\n} auto_poll;\n'

positive = {  # (file, text): must be accepted
    'autopoll-field-decl': [(H, STRUCT)],
    'field-reads-writes': [(INC, 'a.charge_start = false; if (a.charge_start && x) {} const bool s = a.charge_start || a.post_wake;')],
    'due-lambda': [(INC, 'auto charge_start_due = [&] { return 1u; };\na.next_ms = charge_start_due();\nif (int32_t(charge_start_due() - a.next_ms) < 0) {}')],
    'unrelated-identifiers': [(CPP, 'vehicle->charge_state_poll(policy); bool charged = false; uint32_t charge_edges = 0;')],
}
negative = {  # must be rejected
    'vehicle-call': [(CPP, 'vehicle->charge_start();')],
    'vehicle-call-gen': [(INC, 'r->vehicle->charge_start(generation);')],
    'dot-call': [(CPP, 'vehicle.charge_start ();')],
    'field-called': [(INC, 'a.charge_start();')],
    'method-decl': [(H, 'class Vehicle { bool charge_start(uint64_t g); };')],
    'free-function': [(CPP, 'static void charge_start() {}')],
    'pointer-to-member': [(CPP, 'auto f = &tesla_clean::Vehicle::charge_start;')],
    'string-literal': [(CPP, 'send("charge_start");')],
    'other-receiver': [(INC, 'vehicle_state.charge_start = true;')],
    'prefixed-command': [(CPP, 'vehicle->request_charge_start();')],
    'suffixed-command': [(CPP, 'send_charge_start_cmd(v);')],
    'decl-outside-autopoll': [(H, 'struct Other { bool charge_start = false; };\n' + STRUCT)],
    'decl-in-cpp': [(CPP, 'bool charge_start = false;')],
    'duplicate-decl': [(H, STRUCT.replace('} auto_poll;', ' bool charge_start = false;\n} auto_poll;'))],
    'due-lambda-other-file': [(CPP, 'auto charge_start_due = [&] { return 1u; }; charge_start_due();')],
    'due-lambda-twice': [(INC, 'auto charge_start_due = [&] { return 1u; };\nauto charge_start_due = [&] { return 2u; };')],
    'due-called-on-vehicle': [(INC, 'auto charge_start_due = [&] { return 1u; };\nvehicle->charge_start_due();')],
    'protocol-start_charging': [(CPP, 'vehicle->start_charging();')],
    'protocol-charging_start': [(CPP, 'charging_start(v);')],
    'protocol-action': [(CPP, 'ChargingStartStopAction a; chargingStartStopAction b;')],
    'protocol-enum': [(CPP, 'x = ACTION_CHARGE_START;')],
}
fail = []
for k, files in positive.items():
    if violations(files): fail.append(('positive', k, violations(files)))
for k, files in negative.items():
    if not violations(files): fail.append(('negative-not-caught', k))

root = Path(__file__).resolve().parents[4] / 'software'
real = module_files(root / 'src/modules/tesla_vehicle')
assert any(n.endswith('tesla_vehicle.h') for n, _ in real) and any(n.endswith('tesla_auto_poll.inc') for n, _ in real)
if violations(real): fail.append(('real-module', violations(real)))
mutations = {
    'field-to-method': ('tesla_vehicle.h', lambda t: t.replace('bool charge_start = false;', 'bool charge_start() const;', 1)),
    'inc-vehicle-call': ('tesla_auto_poll.inc', lambda t: t + '\nvoid x(){ vehicle->charge_start(); }\n'),
    'cpp-prefixed-call': ('tesla_vehicle.cpp', lambda t: t + '\nvoid y(){ vehicle->request_charge_start(); }\n'),
}
for k, (suffix, mut) in mutations.items():
    hit = [n for n, _ in real if n.endswith(suffix)]
    assert len(hit) == 1, (k, hit)
    files = [(n, mut(t) if n == hit[0] else t) for n, t in real]
    assert files != real, k
    if not violations(files): fail.append(('mutation-not-caught', k))
assert not fail, fail
print(f'charge_start guard fixtures passed: {len(positive)} positive, {len(negative)} negative, '
      f'real module clean ({len(real)} files incl. .inc), {len(mutations)} real-file mutations caught')
