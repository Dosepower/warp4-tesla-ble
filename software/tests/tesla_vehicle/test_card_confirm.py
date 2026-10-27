"""Card-confirmed pairing: corrected field sequence (pair -> card -> vehicle prompt -> confirm).

Native production worker/main/queue/NimBLE adapter, fixture crypto, deterministic
host timing. Mechanism: MAIN's zero-wait outcome publication overlapping a worker
WRITE gate mid-frame during verification must not refuse the frame
(field signature frame_refused/verifying/-3, link_lost). Not an RF or vehicle
recording; no historical attribution of the field event.

usage: test_card_confirm.py [sanitize] [--expect-red]
  default    : all modes must PASS (candidate GREEN)
  --expect-red: control must PASS, deterministic contention modes must end with
                CARD_CONFIRM_RED rc=3 (baseline RED); hammer is reported only.
"""
from pathlib import Path
import os, sys, tempfile, subprocess
T = Path(__file__).resolve().parent
MODES = os.environ.get('TESLA_CARD_CONFIRM_MODES', 'card-control card-confirm card-repeat card-hammer card-identity-change card-authority-expired').split()
DETERMINISTIC = {'card-confirm', 'card-repeat'}
expect_red = '--expect-red' in sys.argv
args = [a for a in sys.argv[1:] if a != '--expect-red']
with tempfile.TemporaryDirectory(prefix='card-confirm-', dir=os.environ['TMPDIR']) as tmp:
    binary = Path(tmp) / 'native'
    env = {**os.environ, 'TESLA_NIMBLE_MODULE_BINARY_OUT': str(binary), 'TESLA_NIMBLE_MODULE_MODES': ''}
    subprocess.run([sys.executable, str(T / 'test_nimble_module.py'), *args], env=env, check=True)
    failed = []
    for mode in MODES:
        r = subprocess.run([str(binary), mode], env=env, timeout=600, capture_output=True, text=True)
        sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
        red = r.returncode == 3 and ('CARD_CONFIRM_RED mode=%s ' % mode) in r.stdout
        passed = r.returncode == 0 and ('CARD_CONFIRM_PASS mode=%s ' % mode) in r.stdout
        if expect_red and mode in DETERMINISTIC:
            ok = red
        elif expect_red and mode == 'card-hammer':
            ok = red or passed  # informational: probabilistic concurrent contention
        else:
            ok = passed
        verdict = 'RED' if red else 'PASS' if passed else 'FAIL'
        print('CARD_CONFIRM_RESULT', mode, 'rc=%d' % r.returncode, verdict, 'OK' if ok else 'UNEXPECTED', flush=True)
        if not ok:
            failed.append(mode)
    print('CARD_CONFIRM_SUMMARY mode=%s expect_red=%d executed=%d failed=%d %s' %
          ('sanitize' if 'sanitize' in args else 'normal', expect_red, len(MODES), len(failed), ' '.join(failed)), flush=True)
    raise SystemExit(1 if failed else 0)
