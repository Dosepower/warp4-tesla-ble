import importlib
from pathlib import Path
import sys
import tempfile
import unittest
S=Path(__file__).resolve().parents[2]; sys.path.insert(0,str(S))
class RuntimeTests(unittest.TestCase):
    def test_complete_firmware_and_tool_closure_is_required(self):
        m=importlib.import_module("build_warp4_installer")
        self.assertTrue(hasattr(m,"verify_runtime_manifest"),"complete runtime/source closure verifier missing")
        from installer_sdk_verify import inventory
        with tempfile.TemporaryDirectory() as td:
            roots=[]
            for role in m.RUNTIME_ROLES:
                p=Path(td)/role; p.mkdir(); (p/"synthetic").write_text(role)
                roots.append({"role":role,"path":str(p),"inventory":inventory(p)})
            firmware={"source_head":"a"*40,"source_inventory":{"synthetic.cpp":"b"*64}}
            cfg={"fixture":"synthetic"}
            value={"schema":"warp4-offline-inputs-v1","resolved_configs":cfg,"roots":roots,**firmware}
            m.verify_runtime_manifest(value,cfg,firmware)
            for bad in (value|{"source_inventory":{}},value|{"source_head":"short"},value|{"roots":roots[:-1]},value|{"roots":roots+[roots[0]]}):
                with self.assertRaises(ValueError):m.verify_runtime_manifest(bad,cfg,firmware)
    def test_actual_package_paths_must_match_locked_roots(self):
        m=importlib.import_module("build_warp4_installer")
        self.assertTrue(hasattr(m,"verify_package_paths"),"actual PlatformIO package path verifier missing")
        expected={"framework-arduinoespressif32":"/synthetic/core","framework-arduinoespressif32-libs":"/synthetic/sdk"}
        m.verify_package_paths(expected,expected)
        with self.assertRaises(ValueError):m.verify_package_paths(expected,expected|{"framework-arduinoespressif32-libs":"/stock/sdk"})
if __name__=="__main__":unittest.main()
