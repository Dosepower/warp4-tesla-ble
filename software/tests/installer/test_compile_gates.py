import importlib
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
S=Path(__file__).resolve().parents[2]; sys.path.insert(0,str(S))
class CompileGates(unittest.TestCase):
    def test_secure_layout_compile_time_proof_is_forced(self):
        self.assertTrue((S/"installer_secure_layout.h").exists(), "SDK struct-size compile-time proof missing")
        header=(S/"installer_secure_layout.h").read_text()
        self.assertIn("sizeof(ets_secure_boot_sig_block_t) == 1216",header)
        self.assertIn("sizeof(ets_secure_boot_signature_t) == 4096",header)
        self.assertIn("installer_secure_layout.h",(S/"warp4_installer_guard.py").read_text())
    def test_vfs_acceptance_is_not_the_review_verifier(self):
        guard=(S/"warp4_installer_guard.py").read_text()
        self.assertNotIn("tools/tesla_vfs_acceptance.py",guard,"installer must not invoke review-only SDK qualification")
        self.assertTrue((S/"installer_vfs_acceptance.py").exists(),"independent installer acceptance runner missing")
        m=importlib.import_module("installer_vfs_acceptance")
        with patch.object(m.subprocess,"run") as run:
            m.run_acceptance(S,{"builder":str(S/".pio/fixture-builder"),"idf":str(S/".pio/fixture-idf")})
        self.assertEqual(run.call_count,2)
        for call in run.call_args_list:
            self.assertNotIn("TESLA_BLE_FRAMEWORK_LIBS",call.kwargs["env"])
            self.assertIn("sanitize",call.args[0])
            self.assertEqual(call.kwargs["env"]["TESLA_BLE_HOST_IDF_SOURCE"],str(S/".pio/fixture-idf"))
if __name__=="__main__": unittest.main()
