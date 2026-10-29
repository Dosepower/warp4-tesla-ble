"""All generated images here are explicitly synthetic test data."""
import hashlib
import importlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
META = {"firmware_info_product_id":"warp4", "firmware_info_product_name":"WARP4 Charger", "version":["2","13","5","255"], "build_timestamp":123456789}
S = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(S))

def synthetic_app():
    header = bytearray(24); header[0:4] = bytes([0xe9, 1, 2, 0x40]); header[23] = 1
    payload = bytes(range(32))
    raw = bytes(header) + struct.pack("<II", 0x3f400020, len(payload)) + payload
    checksum = 0xef
    for b in payload: checksum ^= b
    raw += b"\0" * ((-len(raw)-1) % 16) + bytes([checksum])
    return raw + hashlib.sha256(raw).digest()

class OTATests(unittest.TestCase):
    def api(self):
        for name in ("installer_ota", "installer_ota_verify"):
            self.assertTrue((S / (name + ".py")).exists(), "independent OTA producer/verifier missing: " + name)
        return importlib.import_module("installer_ota"), importlib.import_module("installer_ota_verify")
    def test_container_and_upstream_info_golden(self):
        m, v = self.api(); app = synthetic_app()
        meta = {"firmware_info_product_id":"warp4", "firmware_info_product_name":"WARP4 Charger", "version":["2","13","5","255"], "build_timestamp":123456789}
        data = m.make_container(app, meta)
        self.assertEqual(data[0xf000:], app)
        self.assertEqual(data[:0xc000], b"\xff"*0xc000)
        self.assertEqual(data[0xd000:0xf000], b"\xff"*0x2000)
        self.assertEqual(v.verify_container(data, app)["app_bytes"], len(app))
        # Execute real upstream serializer only, never its global actions/import.
        import ast, types
        tree = ast.parse((S / "merge_firmware_hook.py").read_text())
        node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "write_firmware_info")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); (root / "firmware.bin").write_bytes(app)
            class Env:
                def subst(self, text): return str(root / ("firmware_info.bin" if "firmware_info" in text else "firmware.bin"))
            import pathlib, os
            scope = {"metadata":meta, "env":Env(), "pathlib":pathlib,"os":os,"hashlib":hashlib,"crc32":zlib.crc32}
            exec(compile(ast.Module(body=[node], type_ignores=[]), "upstream_info_only", "exec"), scope)
            scope["write_firmware_info"]()
            self.assertEqual(data[0xc000:0xd000], (root / "firmware_info.bin").read_bytes())
    def test_byte_mutations_and_trailing_data_fail(self):
        m,v=self.api(); app=synthetic_app(); data=m.make_container(app, META)
        for offset in (0,0x1000,0xafff,0xb000,0xc000,0xc007,0xc04d,0xc08a,0xc100,0xcffc,0xd000,0xe000,0xefff,0xf000,len(data)-1):
            changed=bytearray(data); changed[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError): v.verify_container(bytes(changed), app)
        for bad in (app,data[:-1],data+b"extra",data+b"\xff"*4096):
            with self.assertRaises(ValueError): v.verify_container(bad)
    def test_padded_sha_is_checked_even_with_valid_info_crc(self):
        m,v=self.api(); app=synthetic_app(); data=bytearray(m.make_container(app,META))
        data[0xc08a] ^= 1
        data[0xcffc:0xd000] = zlib.crc32(data[0xc000:0xcffc]).to_bytes(4,"little")
        with self.assertRaisesRegex(ValueError,"padded app SHA mismatch"):
            v.verify_container(bytes(data),app)
    def test_fit_and_partition_boundaries(self):
        m,v=self.api()
        self.assertEqual(v.verify_fit(0x62f000), 0x630000)
        for n in (0, -1, True, 0x62f001, 0x630000, 16777216):
            with self.assertRaises(ValueError): v.verify_fit(n)
        csv=(S / "default_16MB_secure.csv").read_text(); v.verify_partitions(csv)
        for old,new in (("0x630000","0x640000"),("0x010000","0x020000"),("0x642000","0x643000"),("0xff0000","0xfe0000")):
            with self.assertRaises(ValueError): v.verify_partitions(csv.replace(old,new))
    def test_staging_refuses_symlink_parents_and_hardlinked_app(self):
        m,v=self.api()
        import os
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); real=root/"real"; real.mkdir(); link=root/"alias"; link.symlink_to(real,target_is_directory=True)
            app=root/"app.bin"; app.write_bytes(synthetic_app())
            with self.assertRaisesRegex(ValueError,"LINK"):
                m.stage_ota(app,link/"candidate",META,0,{"build_id":"fixture"})
            self.assertFalse((real/"candidate").exists())
            os.link(app,root/"duplicate.bin")
            with self.assertRaisesRegex(ValueError,"LINK"):
                m.stage_ota(app,root/"candidate",META,0,{"build_id":"fixture"})
    def test_stale_output_and_separate_publication(self):
        m,v=self.api()
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); app=root/"firmware.bin"; app.write_bytes(synthetic_app())
            with self.assertRaisesRegex(ValueError,"STALE"):
                m.stage_ota(app, root/"out", META, app.stat().st_mtime_ns+1, {"build_id":"fixture"})
            self.assertFalse((root/"out").exists())
            result=m.stage_ota(app, root/"out", META, 0, {"build_id":"fixture"})
            self.assertEqual(set(p.name for p in (root/"out").iterdir()), {"firmware-UNSIGNED_ota.bin","firmware_info.bin","manifest.json"})
            self.assertTrue(result.is_file())
            with self.assertRaisesRegex(ValueError,"EXISTS"):
                m.stage_ota(app, root/"out", META, 0, {"build_id":"fixture"})
if __name__ == "__main__": unittest.main()
