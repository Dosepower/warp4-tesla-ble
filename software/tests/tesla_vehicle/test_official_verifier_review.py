"""Explicit public-verifier review configuration; no device/image targets."""
from pathlib import Path
import unittest
from platformio.project.config import ProjectConfig
S = Path(__file__).resolve().parents[2]
class OfficialVerifierReview(unittest.TestCase):
    def test_readonly_link_script_is_additive_and_exact(self):
        import importlib.util
        helper = S / "tesla_verifier_review_link.py"
        self.assertTrue(helper.exists(), "review-only readonly link adaptation missing")
        spec=importlib.util.spec_from_file_location("review_link", helper)
        m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
        original=(S/".pio/tesla-sdk/builder/out/tools/esp32-arduino-libs/esp32/ld/sections.ld").read_text()
        adapted=m.readonly_sections(original)
        self.assertEqual(adapted.replace(".flash.rodata (READONLY) : ALIGN(0x10)", ".flash.rodata : ALIGN(0x10)"), original)
        for invalid in ("", original+original, adapted):
            with self.assertRaises(ValueError): m.readonly_sections(invalid)

    def test_link_hook_replaces_only_expected_script_and_rejects_unsafe_scope(self):
        import importlib.util, tempfile
        from unittest.mock import Mock
        spec=importlib.util.spec_from_file_location("review_link", S/"tesla_verifier_review_link.py")
        m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
        self.assertTrue(hasattr(m, "configure"), "review-only linker hook not implemented")
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); package=root/"sdk"; (package/"esp32/ld").mkdir(parents=True)
            original="SECTIONS { .flash.rodata : ALIGN(0x10) { *(.rodata*) } }"
            (package/"esp32/ld/sections.ld").write_text(original)
            class Env(dict):
                def subst(self, value): return str(root/"build")
                def GetProjectOption(self, key): return {"custom_signature_preset":"", "custom_signature_verify_preset":"review:official-public-v1"}[key]
                def Replace(self, **values): self.update(values)
                def Depends(self, *args): pass
            def env(): return Env(PIOENV="warp4_tesla_ble_verifier_review", LINKFLAGS=["-T","memory.ld","-T","sections.ld","-other"])
            e=env(); m.configure(e, package, ["tesla-review"])
            self.assertEqual(e["LINKFLAGS"], ["-T","memory.ld","-T",str(root/"build/review-readonly-sections.ld"),"-other"])
            self.assertIn("(READONLY)", (root/"build/review-readonly-sections.ld").read_text())
            self.assertEqual((package/"esp32/ld/sections.ld").read_text(), original)
            e=env();e['PIOENV']='warp4_tesla_ble_classic_review';m.configure(e,package,['tesla-review'])
            e=env();e['PIOENV']='warp4_tesla_ble_nimble_review';m.configure(e,package,['tesla-review'])
            for targets in ([], ["upload"], ["tesla-review","upload"], ["firmware.bin"], ["monitor"]):
                with self.assertRaises(ValueError): m.configure(env(), package, targets)
            for change in ({"PIOENV":"warp4"}, {"LINKFLAGS":["-T","foreign.ld"]}, {"LINKFLAGS":["sections.ld","sections.ld"]}):
                e=env(); e.update(change)
                with self.assertRaises(ValueError): m.configure(e, package, ["tesla-review"])

    def test_explicit_review_inherits_full_tesla_and_never_signs(self):
        config = ProjectConfig(str(S / "platformio.ini"))
        section = "env:warp4_tesla_ble_verifier_review"
        self.assertIn(section, config.sections())
        self.assertEqual(config.get(section, "extends"), ["env:warp4_tesla_ble"])
        self.assertEqual(config.get(section, "custom_signature_preset"), "")
        self.assertEqual(config.get(section, "custom_signature_verify_preset"), "review:official-public-v1")
        self.assertEqual(config.get(section, "extra_scripts")[:-1], config.get("env:warp4_tesla_ble", "extra_scripts"))
        self.assertEqual(config.get(section, "extra_scripts")[-1], "post:tesla_verifier_review_link.py")
        self.assertEqual(config.get(section, "board_build.partitions"), "default_16MB_secure.csv")
        for base in ("env:warp4", "env:warp4_tesla_ble"):
            self.assertEqual(config.get(base, "custom_signature_preset"), "")
            self.assertIsNone(config.get(base, "custom_signature_verify_preset", default=None))
if __name__ == "__main__": unittest.main()
