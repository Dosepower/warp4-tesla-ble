# Bench-only insertion through the stock module generator/stage hook.
Import('env')
if env.GetProjectOption('custom_bench_only', 'false') != 'true':
    raise ValueError('Pre-MQTT metrics are bench-only')
flags = ' '.join(env.GetProjectOption('build_flags', []))
# The profile retains ordinary production lifecycle code and the complete
# controller closure: legacy master-bench/diagnostic wrappers are not permitted.
if '-DTESLA_BLE_CONFIG_MEMORY_PROBE=1' not in flags.split():
    raise ValueError('Missing dedicated config memory probe gate')
if any(x in flags for x in ('TESLA_BLE_BENCH_ONLY', 'TESLA_BLE_IRAM_DIAGNOSTICS', 'TESLA_BLE_R13_MASTER_BENCH', '--wrap=')):
    raise ValueError('Memory probe must preserve the production lifecycle/link closure')
entries = [x.strip() for x in env.GetProjectOption('custom_backend_modules').splitlines() if x.strip()]
if entries.count('Mqtt') != 1 or 'Hidden Tesla Iram Probe' in entries or any(x.startswith('<') for x in entries):
    raise ValueError('Probe requires one explicit MQTT entry and no unresolved placeholders')
entries.insert(entries.index('Mqtt'), 'Hidden Tesla Iram Probe')
env.GetProjectConfig().set('env:' + env['PIOENV'], 'custom_backend_modules', '\n'.join(entries))
