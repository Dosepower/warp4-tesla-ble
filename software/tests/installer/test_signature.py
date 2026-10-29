import configparser
import json
import os
from pathlib import Path
import runpy
import sys
import types
import unittest
from unittest.mock import patch
S = Path(__file__).resolve().parents[2]
class SignatureTests(unittest.TestCase):
    def prepare(self, metadata):
        generated = {}
        util = types.ModuleType("software.util")
        util.write_generated_file = lambda p, s: generated.update({p:s})
        tf = types.ModuleType("tinkerforge_util")
        tf.create_parent_module = lambda *a: None
        software = types.ModuleType("software"); software.util = util
        # Synthetic public bytes test only; never a Tinkerforge key.
        class ReadFixture(configparser.ConfigParser):
            def read(self, *a, **k):
                self.read_dict({"preset:fixture-verifier": {"publisher":"SYNTHETIC TEST PUBLISHER", "sodium_public_key_path":"fixture-public.json"}})
        from unittest.mock import mock_open
        with patch.dict(sys.modules, {"tinkerforge_util":tf, "software":software}), patch.dict(os.environ, {"PLATFORMIO_METADATA":json.dumps(metadata)}), patch("configparser.ConfigParser", ReadFixture), patch("builtins.open", mock_open(read_data=json.dumps({"sodium_public_key":"12"*32}))):
            runpy.run_path(str(S / "src/modules/firmware_update/prepare.py"))
        return generated
    def test_unsigned_outer_retains_explicit_verifier(self):
        out = self.prepare({"signature_preset":"", "signature_verify_preset":"fixture-verifier"})
        self.assertIn("#define signature_sodium_public_key_length 32", out["generated/signature_verify.h"])
        self.assertIn("SYNTHETIC TEST PUBLISHER", out["generated/signature_verify.cpp"])
    def test_existing_implicit_defaults_unchanged(self):
        self.assertEqual(self.prepare({"signature_preset":""}), self.prepare({"signature_preset":"", "signature_verify_preset":""}))
        self.assertEqual(self.prepare({"signature_preset":"fixture-verifier"}), self.prepare({"signature_preset":"fixture-verifier", "signature_verify_preset":"fixture-verifier"}))
    def test_metadata_transports_separate_verifier_option(self):
        text = (S / "pio_hooks.py").read_text()
        self.assertIn("custom_signature_verify_preset", text, "separate verifier option not transported")
        self.assertIn("signature_verify_preset", text)
        self.assertIn("len(signature_preset) == 0", text)
    def test_default_metadata_has_no_new_field_without_opt_in(self):
        import ast
        tree = ast.parse((S / "pio_hooks.py").read_text())
        main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        expression = next(n.value for n in main.body if isinstance(n, ast.Assign) and any(isinstance(t,ast.Name) and t.id == "metadata" for t in n.targets))
        scope = {name:"fixture" for name in ("product_id","firmware_basename","secure_boot","branding_mod_path","firmware_info_product_id","firmware_info_product_name")}
        scope.update(os=os, json=json, env=types.SimpleNamespace(subst=lambda value: value), signature_preset="", signature_verify_preset=None, split_esptool_ota=True, frontend_modules=[], options_dict={}, version=["2","13","5","255"], build_timestamp=0)
        metadata = json.loads(eval(compile(ast.Expression(expression), "metadata-only", "eval"), scope))
        self.assertNotIn("signature_verify_preset", metadata, "existing targets must retain exact metadata defaults")
    def test_unknown_verifier_diagnostic_names_verifier_not_outer_signer(self):
        import io
        output=io.StringIO()
        with patch("sys.stdout",output):
            with self.assertRaises(SystemExit): self.prepare({"signature_preset":"", "signature_verify_preset":"missing-verifier"})
        self.assertIn("Preset missing-verifier is unknown",output.getvalue())
    def test_independent_verifier_does_not_claim_own_publisher(self):
        out=self.prepare({"signature_preset":"", "signature_verify_preset":"fixture-verifier"})
        self.assertIn("#define SIGNATURE_VERIFIER_INDEPENDENT 1",out["generated/signature_verify.h"],"independent update identity must not impersonate the current firmware publisher")
        code=(S/"src/modules/firmware_update/firmware_update.cpp").read_text()
        self.assertIn("#if defined(SIGNATURE_VERIFIER_INDEPENDENT)",code)
        self.assertIn("Update verifier expects publisher: %s",code)
        self.assertIn("{\"publisher\", Config::Str(\"\", 0, 0)}",code)
        implicit=self.prepare({"signature_preset":"fixture-verifier"})
        self.assertNotIn("SIGNATURE_VERIFIER_INDEPENDENT",implicit["generated/signature_verify.h"])
    def test_authentic_key_gate_missing_by_default(self):
        path = S / "installer_identity.py"
        self.assertTrue(path.exists(), "authentic official public-key gate missing")
        import importlib.util
        spec = importlib.util.spec_from_file_location("installer_identity", path)
        m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
        with self.assertRaisesRegex(ValueError, "OFFICIAL_WARP_PUBLIC_KEY_ANCHOR_MISSING"):
            m.verify_identity(S)
if __name__ == "__main__": unittest.main()
