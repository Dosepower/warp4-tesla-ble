# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
root=Path(__file__).resolve().parents[4]
module=root/'software/src/modules/tesla_vehicle'
for p in sorted(module.glob('*')):
    if p.suffix not in ('.h','.cpp'): continue
    text=p.read_text()
    assert 'TeslaBLE::' not in text, str(p)
    for header in ('<vehicle.h>','<adapters.h>','<checked_mutex.h>','<checked_function.h>'):
        assert header not in text, (str(p),header)
    for op in ('charge_start','start_charging','charging_start','ACTION_CHARGE_START'):
        assert op not in text, (str(p),op)
main=(module/'tesla_vehicle.cpp').read_text()
assert 'tesla_clean::Vehicle' in main
assert 'tesla_clean::WakePolicy::NoWakeSkip' in main
assert 'result.authenticated' in main and 'result.generation' in main
assert 'ev.get_active_ev_mac()' in main and 'ev.set_soc(soc)' in main
ini=(root/'software/warp4_tesla_ble.ini').read_text()
assert 'lib_ignore = TeslaBLE' in ini
assert 'TeslaBLEClean' in ini
print('Clean production source contract passed (not interoperability evidence)')
