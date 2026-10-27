"""Pairing first-cause trace: field incident 2026-10-27 reproduced per mechanism.

Native production worker/main/queue/adapter/NimBLE central, fixture crypto,
deterministic host timing. Each mode is one source-confirmed loss mechanism
after a fully ATT-acknowledged PRESENT_KEY. Every mode must reproduce exactly
the visible field signature captured from the device (nonsecret numbers and the
literal error text) and, with the trace, a distinct first cause. Followed by an
immediate second explicit attempt that runs the full 60 s no-approval wait.
Not an RF or vehicle recording; no historical attribution.

usage: test_pair_trace.py [sanitize] [--expect-red]
"""
from pathlib import Path
import os, sys, tempfile, subprocess
T = Path(__file__).resolve().parent
MODES = os.environ.get('TESLA_PAIR_TRACE_MODES', 'trace-remote trace-security trace-overflow trace-write-error').split()
# Visible device fields after the incident (nonsecret; private capture not read here).
FIELD = ('error=Pairing link/session lost; transmission may have occurred; approval not authenticated|outcome=link_lost|'
         'result=3|tx=88/88/88|complete=1|seen=0|whitelist=0|flags=51|submit=0|cancel=-1|status=0|restart=0|busy=0|'
         'key=1/present|paired=0|pending=0|sent=0|timeout=')
expect_red = '--expect-red' in sys.argv
args = [a for a in sys.argv[1:] if a != '--expect-red']
with tempfile.TemporaryDirectory(prefix='pair-trace-', dir=os.environ['TMPDIR']) as tmp:
    binary = Path(tmp) / 'native'
    env = {**os.environ, 'TESLA_NIMBLE_MODULE_BINARY_OUT': str(binary), 'TESLA_NIMBLE_MODULE_MODES': ''}
    subprocess.run([sys.executable, str(T / 'test_nimble_module.py'), *args], env=env, check=True)
    legacy, causes, failed, extra = {}, {}, [], []
    for mode in MODES:
        r = subprocess.run([str(binary), mode], env=env, timeout=180, capture_output=True, text=True)
        sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
        for line in r.stdout.splitlines():
            if line.startswith('TRACE_LEGACY mode=%s ' % mode):
                legacy[mode] = line.split(' ', 2)[2]
            if line.startswith('PAIR_TRACE_PASS mode=%s ' % mode):
                causes[mode] = line.split()[2]
        red = r.returncode == 3 and 'PAIR_TRACE_RED ' in r.stdout
        ok = red if expect_red else r.returncode == 0 and mode in causes
        print('PAIR_TRACE_RESULT', mode, 'rc=%d' % r.returncode, 'RED' if red else 'PASS' if ok else 'FAIL', flush=True)
        if not ok:
            failed.append(mode)
    same = len(legacy) == len(MODES) and all(v == FIELD for v in legacy.values())
    print('PAIR_TRACE_FIELD_SIGNATURE', 'IDENTICAL_TO_DEVICE' if same else 'MISMATCH', flush=True)
    if not same:
        for mode, value in legacy.items():
            if value != FIELD:
                print('  mismatch', mode, value, flush=True)
        failed.append('field-signature')
    if not expect_red:
        distinct = len(set(causes.values())) == len(MODES)
        print('PAIR_TRACE_FIRST_CAUSE', 'DISTINCT' if distinct else 'AMBIGUOUS', sorted(causes.items()), flush=True)
        if not distinct:
            failed.append('distinct-cause')
        # Concurrency/freeze contract of the trace itself (needs the trace API,
        # so it has no original-production RED; see make_red_tree/freeze RED).
        for mode in os.environ.get('TESLA_PAIR_TRACE_EXTRA', 'trace-freeze trace-reset trace-dirty-log').split():
            r = subprocess.run([str(binary), mode], env=env, timeout=600, capture_output=True, text=True)
            sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
            ok = r.returncode == 0 and ('PAIR_TRACE_PASS mode=%s ' % mode) in r.stdout
            print('PAIR_TRACE_RESULT', mode, 'rc=%d' % r.returncode, 'PASS' if ok else 'FAIL', flush=True)
            extra.append(mode)
            if not ok:
                failed.append(mode)
    print('PAIR_TRACE_SUMMARY mode=%s expect_red=%d executed=%d failed=%d %s' %
          ('sanitize' if 'sanitize' in args else 'normal', expect_red, len(MODES) + len(extra), len(failed), ' '.join(failed)), flush=True)
    raise SystemExit(1 if failed else 0)
