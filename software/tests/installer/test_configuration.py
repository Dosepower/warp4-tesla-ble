import importlib
from pathlib import Path
import sys
import unittest
S=Path(__file__).resolve().parents[2]; sys.path.insert(0,str(S))
class ConfigurationTests(unittest.TestCase):
    def test_full_semantic_delta_and_security_invariants(self):
        m=importlib.import_module("installer_sdk_verify")
        self.assertTrue(hasattr(m,"verify_config_maps"), "full baseline/delta configuration verifier missing")
        base={"CONFIG_SECURE_BOOT":"y", "CONFIG_SPIRAM":"y", "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL":"2048", "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE":"y", "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK":"n", "CONFIG_PARTITION_TABLE_OFFSET":"0xe000", "CONFIG_ESPTOOLPY_FLASHSIZE":"\"16MB\"", "CONFIG_BT_ENABLED":"n"}
        cfg=base|m.REQUIRED_BLE
        deltas=[{"key":k,"before":base.get(k),"after":cfg.get(k),"reason":"synthetic BLE delta"} for k in sorted(base.keys()|cfg.keys()) if base.get(k)!=cfg.get(k)]
        m.verify_config_maps(base,cfg,deltas)
        for key,val in (("CONFIG_SECURE_BOOT","n"),("CONFIG_SPIRAM","n"),("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL","0"),("CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK","y"),("CONFIG_BT_NIMBLE_ENABLED","y"),("CONFIG_BT_GATTC_ENABLE","n"),("CONFIG_BT_BTC_TASK_STACK_SIZE","1024"),("CONFIG_PARTITION_TABLE_OFFSET","0x8000"),("CONFIG_NEW_UNAUDITED","y")):
            with self.subTest(key=key),self.assertRaises(ValueError): m.verify_config_maps(base,cfg|{key:val},deltas)
        with self.assertRaises(ValueError): m.verify_config_maps(base,cfg,deltas[:-1])
    def test_actual_archives_are_inspected_not_just_named(self):
        m=importlib.import_module("installer_sdk_verify")
        self.assertTrue(hasattr(m,"verify_archive"), "archive member/architecture verifier missing")
        # Ordinary ar with a synthetic little-endian ELF32 Xtensa relocatable.
        elf=bytearray(52); elf[:7]=b"\x7fELF\x01\x01\x01"; elf[16:20]=b"\x01\x00\x5e\x00"
        header=("fixture.o/".ljust(16)+"0".ljust(12)+"0".ljust(6)+"0".ljust(6)+"100644".ljust(8)+str(len(elf)).ljust(10)+"`\n").encode()
        data=b"!<arch>\n"+header+elf
        m.verify_archive(data)
        for bad in (b"",b"!<arch>\n",data[:-1],data.replace(b"\x5e\x00",b"\x3e\x00")):
            with self.assertRaises(ValueError): m.verify_archive(bad)
    def test_pair_requires_same_sdk_and_runtime(self):
        m=importlib.import_module("installer_link_verify")
        self.assertTrue(hasattr(m,"verify_pair"), "A/B manifest identity verifier missing")
        p={"sdk_manifest_sha256":"1"*64,"sdk_inventory_sha256":"2"*64,"runtime_manifest_sha256":"3"*64,"public_key_sha256":"4"*64}
        a={"provenance":p|{"environment":"warp4_ble_installer"},"verification":{"product_id":"warp4"}}
        b={"provenance":p|{"environment":"warp4_multi_tesla_installer"},"verification":{"product_id":"warp4"}}
        common={"schema":"warp4-action-closure-v1","common":{"/snapshot/sdk/a":"a"*64},"tesla":{},"actions":[]}
        common["actions"]=[{"tool":"/snapshot/sdk/a","kind":"python","argv":["/snapshot/sdk/a"],"cwd":"/output","inputs":common["common"],"outputs":{}}]
        a["action_closure"]=common
        b["action_closure"]=common
        m.verify_pair(a,b,approved_tesla={})
        for key in p:
            changed={**b,"provenance":b["provenance"]|{key:"5"*64}}
            with self.assertRaises(ValueError): m.verify_pair(a,changed,approved_tesla={})
if __name__=="__main__": unittest.main()
