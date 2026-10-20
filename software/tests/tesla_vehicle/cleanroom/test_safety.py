# SPDX-License-Identifier: Apache-2.0
"""Negative integration/source contract. Never read the tainted library."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[4]
module=ROOT/'software/src/modules/tesla_vehicle'
allowed={'charge_state_poll','pair','tick','receive','open','close','generation','status'}
observed=set()
for path in sorted(module.glob('*')):
    if path.suffix not in ('.cpp','.h'): continue
    text=path.read_text()
    observed.update(re.findall(r'\bvehicle\s*->\s*([A-Za-z_]\w*)\s*\(',text))
    assert not re.search(r'\b(?:charge_start|start_charging|ChargingStartStopAction|chargingStartStopAction)\b',text), path
# Exhaustion is a read-only local diagnostic, not a new vehicle operation.
assert 'bool exhausted() const' in (ROOT/'software/lib/TeslaBLEClean/include/tesla_clean/vehicle.hpp').read_text()
assert observed-{'exhausted'}==allowed and 'exhausted' in observed, (observed,allowed)
code=(module/'tesla_vehicle.cpp').read_text()
assert re.search(r'charge_state_poll\(decision.allow_wake\s*\?\s*tesla_clean::WakePolicy::WakeIfNeeded\s*:\s*tesla_clean::WakePolicy::NoWakeSkip',code)
# Phase 2 binding must never resolve a retained legacy header or namespace.
for path in sorted(module.glob('*')):
    if path.suffix in ('.cpp','.h'):
        assert not re.search(r'TeslaBLE::|#include\s*[<"](?:vehicle|adapters|checked_mutex|checked_function)\.h',path.read_text()), path
clean=ROOT/'software/lib/TeslaBLEClean'
for path in sorted(clean.rglob('*')):
    if path.suffix not in ('.cpp','.hpp'): continue
    text=path.read_text()
    assert 'SPDX-License-Identifier: Apache-2.0' in text, path
    assert not re.search(r'AGPL|TeslaBLE/|TeslaBLE::|#include\s*[<"](?:vehicle|client|peer|adapters)\.h',text), path
    assert not re.search(r'\b(?:charge_start|start_charging|ChargingStartStopAction)\b',text), path
print('Clean safety contract: bound WARP operation allowlist, NoWakeSkip default, no charge-start call and no tainted-library imports passed')
