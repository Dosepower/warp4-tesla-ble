"""Extension-only production source/link confinement. No SDK mutations."""
from pathlib import Path
Import('env')

flags = ' '.join(map(str, env.GetProjectOption('build_flags', [])))
# Evidence source is excluded in every current environment, including bench.
def reject_evidence_source(env, node):
    if Path(str(node)).name == 'tesla_ble_radio_spike.cpp':
        return None
    return node
env.AddBuildMiddleware(reject_evidence_source)
bench = env.GetProjectOption('custom_bench_only', 'false') == 'true'
spike = env.GetProjectOption('custom_radio_spike', 'false') == 'true'
spike_macro = 'TESLA_BLE_RADIO_SPIKE_' in flags
if spike or spike_macro or 'tesla_radio_spike_dispatch' in flags:
    raise ValueError('Rejected radio spike is evidence-only; no live build profile')
if not bench:
    for token in ('TESLA_BLE_CONFIG_MEMORY_PROBE', 'TESLA_BLE_IRAM_DIAGNOSTICS', 'TESLA_BLE_BENCH_ONLY', 'TESLA_BLE_R13_MASTER_BENCH', '--wrap=esp_bt', '--wrap=esp_ble', '--wrap=pvPortMalloc'):
        if token in flags:
            raise ValueError('Production Bluetooth must not use bench macros/wrappers: ' + token)
    def production_source(env, node):
        path = Path(str(node)).as_posix()
        if '/modules/tesla_vehicle/' in '/' + path:
            name = Path(path).name
            if name.startswith('tesla_ble_bench_') or name in ('tesla_ble_iram_diagnostics.cpp', 'bluetooth_master_bench.cpp'):
                return None
        return node
    env.AddBuildMiddleware(production_source)
