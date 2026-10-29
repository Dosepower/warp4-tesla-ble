from pathlib import Path
import subprocess
import sys
import unittest
S=Path(__file__).resolve().parents[2]
class CLITests(unittest.TestCase):
    def test_external_sdk_cli_cannot_succeed_without_anchor(self):
        r=subprocess.run([sys.executable,"-B",str(S/"installer_sdk_verify.py")],text=True,capture_output=True)
        self.assertNotEqual(r.returncode,0,"external verifier must not silently return success without checking anything")
        self.assertIn("EXTERNAL_SDK_MANIFEST_PIN_MISSING",r.stderr)
    def test_ota_cli_requires_real_input(self):
        r=subprocess.run([sys.executable,"-B",str(S/"installer_ota_verify.py")],text=True,capture_output=True)
        self.assertNotEqual(r.returncode,0,"OTA verifier must require input")
if __name__=="__main__": unittest.main()
