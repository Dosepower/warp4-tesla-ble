"""Regression fixtures are synthetic; no firmware or approval is produced."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
S = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(S))
import installer_sdk_verify as sdk
import installer_policy as policy
import build_warp4_installer as entry

class JSONReviewTests(unittest.TestCase):
    def test_nonfinite_nested_json_rejected(self):
        for constant in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(constant=constant), self.assertRaises(ValueError):
                sdk.loads("{\"nested\":[" + constant + "]}")

    def test_config_equality_does_not_confuse_bool_int_float(self):
        for wrong in (True, 1.0):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                policy.validate(policy.ENVS[0], [], {"nested": [wrong]},
                                {policy.ENVS[0]: {"nested": [1]}})

class HeaderReviewTests(unittest.TestCase):
    def test_header_is_exact_and_duplicate_free(self):
        self.assertTrue(hasattr(sdk, "verify_config_header"), "missing exact header verifier")
        cfg = {"CONFIG_SECURE_BOOT": "y", "CONFIG_OFF": "n", "CONFIG_VALUE": "0xe000"}
        header = "#define CONFIG_SECURE_BOOT 1\n#define CONFIG_VALUE 0xe000\n"
        sdk.verify_config_header(cfg, header)
        for extra in ("#define CONFIG_SECURE_FLASH_ENC_ENABLED 1\n",
                      "#define CONFIG_SECURE_BOOT 1\n", "#define CONFIG_OFF 0\n",
                      "#undef CONFIG_SECURE_BOOT\n", "#define CONFIG_UNKNOWN\n"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                sdk.verify_config_header(cfg, header + extra)

class SourceReviewTests(unittest.TestCase):
    def test_sparse_checkout_has_explicit_prerequisite(self):
        # The authorized real worktree may now be fully materialized. Exercise
        # this prerequisite in a genuine isolated sparse repository instead.
        import subprocess
        with tempfile.TemporaryDirectory() as td:
            repo = Path(td) / "repo"
            subprocess.run(["/usr/bin/git", "init", "-q", str(repo)], check=True)
            (repo / "software").mkdir()
            subprocess.run(["/usr/bin/git", "-C", str(repo), "config", "core.sparseCheckout", "true"], check=True)
            with patch.object(entry, "S", repo / "software"):
                with self.assertRaisesRegex(ValueError, "FULL_CHECKOUT_REQUIRED"):
                    entry.firmware_identity()

    def test_external_approval_roundtrip_actual_git_inventory(self):
        self.assertTrue(hasattr(entry, "verify_runtime_approval"), "missing two-stage external approval")
        import subprocess
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); repo = root / "repo"; repo.mkdir(); (repo / "software").mkdir()
            subprocess.run(["/usr/bin/git", "init", "-q", str(repo)], check=True)
            subprocess.run(["/usr/bin/git", "-C", str(repo), "-c", "user.name=fixture", "-c", "user.email=fixture@example.invalid", "commit", "--allow-empty", "-qm", "fixture"], check=True)
            (repo / "software/installer_runtime.lock.json").write_text("{\"manifest_sha256\":null}")
            (repo / "source.cpp").write_text("synthetic source inventory fixture")
            with patch.object(entry, "S", repo / "software"):
                before = entry.firmware_identity()
                self.assertIn("software/installer_runtime.lock.json", before["source_inventory"])
                roots = []
                for role in entry.RUNTIME_ROLES:
                    path = root / role; path.mkdir(); (path / "fixture").write_text(role)
                    roots.append({"role": role, "path": str(path), "inventory": sdk.inventory(path)})
                payload = {"schema": "warp4-offline-inputs-v1", "resolved_configs": {}, "roots": roots, **before}
                runtime = root / "runtime.json"; runtime.write_text(json.dumps(payload))
                approval = root / "approval.json"
                approval.write_text(json.dumps({"schema": "warp4-offline-runtime-v1", "manifest_sha256": sdk.sha(runtime)}))
                entry.verify_runtime_approval(runtime, approval, {}, before)
                self.assertEqual(before, entry.firmware_identity())
                (repo / "source.cpp").write_text("changed")
                with self.assertRaises(ValueError):
                    entry.verify_runtime_approval(runtime, approval, {}, entry.firmware_identity())
                with self.assertRaisesRegex(ValueError, "external"):
                    entry.verify_runtime_approval(runtime, repo / "software/installer_runtime.lock.json", {}, before)

class PrivateReviewTests(unittest.TestCase):
    def test_ignored_wifi_rejected_before_prepare(self):
        self.assertTrue(hasattr(policy, "reject_private_inputs"), "private-input gate missing")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name in ("default_wifi.json", "warp4_wifi.json", "default_wifi.json.backup", "data/wifi_sta_config.json"):
                path = root / name; path.parent.mkdir(exist_ok=True)
                path.write_text(json.dumps({"debug_fs_enable": 1, "mqtt_enable": "1 -DUNREVIEWED_DEFINE=1"}))
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "PRIVATE"):
                    policy.reject_private_inputs(root)
                path.unlink()

    def test_effective_policy_after_all_preparations(self):
        self.assertTrue(hasattr(policy, "validate_effective"), "effective post-preparation gate missing")
        for flags, private in ((["-DDEBUG_FS_ENABLE=1"], False),
                               (["-DDEFAULT_MQTT_ENABLE=1 -DUNREVIEWED_DEFINE=1"], False),
                               (["-DDEFAULT_WIFI_STA_PASSPHRASE=secret"], False), ([], True)):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                policy.validate_effective({"BUILD_FLAGS": flags}, private)
        import ast
        tree = ast.parse((S / "pio_hooks.py").read_text())
        main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        text = ast.unparse(main)
        self.assertIn("reject_private_inputs", text)
        self.assertGreater(text.rindex("validate_effective"), text.rindex("build_web("))
        self.assertIn("validate_effective", (S / "warp4_ota_only_hook.py").read_text())

class StagingRaceReviewTests(unittest.TestCase):
    def test_parent_swap_does_not_redirect_publication(self):
        import installer_ota as ota
        from test_ota import synthetic_app, META
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); parent = root / "parent"; parent.mkdir()
            outside = root / "outside"; outside.mkdir()
            app = root / "app"; app.write_bytes(synthetic_app())
            original = ota.make_container
            def swap(*args):
                parent.rename(root / "held")
                parent.symlink_to(outside, target_is_directory=True)
                return original(*args)
            with patch.object(ota, "make_container", side_effect=swap):
                try:
                    ota.stage_ota(app, parent / "candidate", META, 0, {})
                except ValueError:
                    pass
            self.assertFalse((outside / "candidate").exists(), "publication followed swapped parent")

    def test_app_read_uses_held_fd(self):
        import installer_ota as ota
        self.assertTrue(hasattr(ota, "read_app_fd"), "app is not read through a held validated fd")
        from test_ota import synthetic_app
        with tempfile.TemporaryDirectory() as td:
            app = Path(td) / "app"; app.write_bytes(synthetic_app())
            other = Path(td) / "other"; other.write_bytes(b"attacker")
            real_fstat = os.fstat
            swapped = False
            def swap(fd):
                nonlocal swapped
                st = real_fstat(fd)
                import stat
                if stat.S_ISREG(st.st_mode) and not swapped:
                    swapped = True
                    app.unlink(); app.symlink_to(other)
                return st
            with patch.object(os, "fstat", side_effect=swap):
                self.assertEqual(ota.read_app_fd(app, 0), synthetic_app())

class WebReviewTests(unittest.TestCase):
    def test_mutated_embedded_output_with_matching_input_digests(self):
        self.assertTrue(hasattr(entry, "verify_web_outputs"), "generated web bytes are unauthenticated")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); (root / "src").mkdir()
            outputs = {}
            for name in ("src/index_html.embedded.cpp", "src/index_html.embedded.h"):
                path = root / name; path.write_text("authenticated synthetic web output")
                outputs[name] = sdk.sha(path)
            (root / "src/index_html.digest").write_text("matching input digest")
            entry.verify_web_outputs(root, outputs)
            (root / "src/index_html.embedded.cpp").write_text("mutated with matching input digests")
            with self.assertRaisesRegex(ValueError, "WEB_OUTPUT"):
                entry.verify_web_outputs(root, outputs)
            with self.assertRaises(ValueError):
                entry.verify_web_outputs(root, {})
        self.assertIn("verify_web_outputs", (S / "pio_hooks.py").read_text())

class EnvironmentReviewTests(unittest.TestCase):
    def test_first_git_and_vfs_receive_only_explicit_environment(self):
        import installer_vfs_acceptance as vfs
        poison = {k: "/attacker/injection" for k in ("BASH_ENV", "PYTHONPATH", "PYTHONHOME", "LD_PRELOAD", "LD_LIBRARY_PATH", "GIT_CONFIG_GLOBAL", "PLATFORMIO_CORE_DIR", "NPM_CONFIG_USERCONFIG", "CC")}
        with patch.dict(os.environ, poison), patch.object(entry.subprocess, "run") as run:
            run.return_value.stdout = "true"
            with self.assertRaises(ValueError): entry.firmware_identity()
            call = run.call_args
            self.assertTrue(Path(call.args[0][0]).is_absolute())
            self.assertIn("env", call.kwargs, "first subprocess inherits ambient environment")
            self.assertFalse(set(poison) & set(call.kwargs["env"]))
        with patch.dict(os.environ, poison), patch.object(vfs.subprocess, "run") as run:
            vfs.run_acceptance(S, {"builder": "/approved/builder", "idf": "/approved/idf"})
            for call in run.call_args_list:
                self.assertTrue(Path(call.args[0][0]).is_absolute(), "PATH-resolved VFS executable")
                child_env = call.kwargs["env"]
                # Namespace consumers deliberately use an explicit immutable
                # loader directory. Reject ambient values, not that key itself.
                self.assertFalse((set(poison) - {"LD_LIBRARY_PATH"}) & set(child_env))
                self.assertEqual(child_env.get("LD_LIBRARY_PATH"), "/snapshot/loader_runtime")
                self.assertNotIn("/attacker/injection", child_env.values())
                self.assertTrue(call.kwargs.get("close_fds"))

    def test_sandbox_has_no_host_root_ipc_or_inherited_environment(self):
        self.assertTrue(hasattr(entry, "sandbox_command"), "minimal sandbox recipe missing")
        verified = {"tools": {"bwrap": "/approved/tools/bwrap", "pio": "/approved/python/bin/pio"},
                    "read_only_roots": ["/private/snapshot", "/approved/tools"],
                    "writable_roots": ["/private/outputs"], "project": "/private/snapshot/software"}
        command = entry.sandbox_command(policy.ENVS[0], verified)
        self.assertEqual(command[0], "/approved/tools/bwrap")
        for flag in ("--clearenv", "--unshare-all", "--new-session", "--cap-drop"):
            self.assertIn(flag, command)
        for i, arg in enumerate(command):
            if arg in ("--bind", "--ro-bind"):
                self.assertNotEqual(command[i+1], "/")
                self.assertNotIn(command[i+1], ("/run", "/tmp", "/home"))
        self.assertIn("/approved/python/bin/pio", command)

class LinkClosureReviewTests(unittest.TestCase):
    def test_garbage_and_foreign_maps_never_grant_closure(self):
        import installer_link_verify as link
        for name in policy.ENVS:
            for text in ("garbage", "Linker script and memory map\n .text 0x400d1000 0x40 /unlocked/tesla_vehicle.cpp.o\n .text 0x400d2000 0x40 /stock/libTeslaBLEClean.a(fake.o)\nLOAD /unlocked/libmbedtls.a\n"):
                with self.subTest(name=name, text=text), self.assertRaises(ValueError):
                    link.verify_linkgraph(name, text)

    def test_link_command_response_dependency_and_map_paths_are_bound(self):
        import installer_link_verify as link
        self.assertTrue(hasattr(link, "verify_link_inputs"), "no real link-input confinement")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); inputs = root / "inputs"; inputs.mkdir()
            outputs = root / "outputs"; outputs.mkdir()
            for name in ("ld", "libbt.a", "sections.ld", "source.cpp"):
                (inputs / name).write_text("synthetic fixture")
            (inputs / "libbt.a").write_bytes(b'!<arch>\nfixture.o/      0           0     0     100644  2         `\nxx')
            (inputs / "sections.ld").write_text("INPUT(" + str(inputs / "libbt.a") + ")")
            obj = outputs / "source.o"; obj.write_text("synthetic object")
            rsp = outputs / "link.rsp"; rsp.write_text(str(inputs / "libbt.a") + " -T " + str(inputs / "sections.ld"))
            dep = outputs / "source.d"; dep.write_text(str(obj) + ": " + str(inputs / "source.cpp"))
            closure = {"cwd": str(outputs), "immutable_roots": {str(inputs): sdk.inventory(inputs)},
                       "output_root": str(outputs), "link_command": [str(inputs / "ld"), str(obj), "@"+str(rsp)],
                       "dependency_files": [str(dep)]}
            text = "Memory Configuration\nLinker script and memory map\nLOAD " + str(inputs / "libbt.a") + "\n"
            link.verify_link_inputs(text, closure)
            for foreign in ("/unlocked/libmbedtls.a", "/unlocked/sections.ld"):
                with self.subTest(foreign=foreign), self.assertRaises(ValueError):
                    link.verify_link_inputs(text + "LOAD " + foreign + "\n", closure)
                rsp.write_text(foreign)
                with self.assertRaises(ValueError): link.verify_link_inputs(text, closure)
            rsp.write_text(str(inputs / "libbt.a"))
            dep.write_text(str(obj) + ": /unlocked/source.cpp")
            with self.assertRaises(ValueError): link.verify_link_inputs(text, closure)

class SnapshotReviewTests(unittest.TestCase):
    def test_manifest_hash_and_parse_use_the_same_opened_bytes(self):
        # Reuse the complete SDK fixture, then replace the path exactly after the
        # old hash operation. The verified bytes must be the parsed bytes.
        from test_sdk import SDKTests
        helper = SDKTests()
        with tempfile.TemporaryDirectory() as td:
            package, materials, manifest, mp, lock = helper.fixture(sdk, Path(td))
            original_read = sdk.read_bytes
            def swap(path):
                value = original_read(path)
                if Path(path) == mp:
                    changed = dict(manifest); changed["secure_boot_sig_block_size"] = 4096
                    mp.write_text(json.dumps(changed))
                return value
            with patch.object(sdk, "read_bytes", side_effect=swap):
                try:
                    result = sdk.verify(package, mp, lock, materials, S)
                    self.assertEqual(result["_manifest"]["secure_boot_sig_block_size"], 1216)
                    self.assertEqual(json.loads(mp.read_text())["secure_boot_sig_block_size"], 4096)
                except ValueError as exc:
                    self.fail("manifest reopened after hashing: " + str(exc))

    def test_sealed_snapshot_survives_source_replacement(self):
        self.assertTrue(hasattr(entry, "sealed_input"), "immutable source snapshot missing")
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / "source.cpp"; source.write_bytes(b"approved")
            fd = entry.sealed_input(source, hashlib.sha256(b"approved").hexdigest())
            try:
                source.write_bytes(b"attacker")
                self.assertEqual(os.pread(fd, 100, 0), b"approved")
                with self.assertRaises(OSError): os.pwrite(fd, b"attacker", 0)
            finally:
                os.close(fd)

    def test_prepare_checks_the_key_bytes_it_actually_embeds(self):
        from test_signature import SignatureTests
        helper = SignatureTests()
        with self.assertRaisesRegex(ValueError, "embedded public-key fingerprint"):
            helper.prepare({"signature_preset": "", "signature_verify_preset": "fixture-verifier",
                            "installer_public_key_sha256": "0" * 64})

class ConsumerReviewTests(unittest.TestCase):
    def test_every_runtime_role_is_bound_before_consumption(self):
        self.assertTrue(hasattr(entry, "verify_consumers"), "runtime roots may be correctly hashed but unused")
        roots = [{"role": role, "path": "/approved/" + role} for role in entry.RUNTIME_ROLES]
        expected = {item["role"]: item["path"] for item in roots}
        entry.verify_consumers(roots, expected)
        for role in entry.RUNTIME_ROLES:
            with self.subTest(role=role), self.assertRaisesRegex(ValueError, "consumed"):
                entry.verify_consumers(roots, expected | {role: "/ambient/" + role})
        with self.assertRaises(ValueError): entry.verify_consumers(roots, {})
        self.assertIn("assemble_inputs(verified,software)", (S / "installer_recipe.py").read_text())
        self.assertNotIn("verify_consumers(runtime[", (S / "build_warp4_installer.py").read_text())

    def test_missing_immutable_recipe_cannot_start_platformio(self):
        verified = {"read_only_roots": ["/mutable/but-hashed"]}
        with patch.object(entry, "preflight", return_value=verified), patch.object(entry.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "IMMUTABLE_RUNTIME_RECIPE_REQUIRED"):
                entry.build(policy.ENVS[0])
            run.assert_not_called()

class SchemaReviewTests(unittest.TestCase):
    def test_unknown_sdk_manifest_and_lock_fields_rejected(self):
        from test_sdk import SDKTests
        helper = SDKTests()
        with tempfile.TemporaryDirectory() as td:
            package, materials, manifest, mp, lock = helper.fixture(sdk, Path(td))
            changed = manifest | {"unexpected_security_policy": True}
            mp.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                sdk.verify(package, mp, lock | {"manifest_sha256": sdk.sha(mp)}, materials, S)
            mp.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                sdk.verify(package, mp, lock | {"unknown": 1}, materials, S)

    def test_inventory_integer_fields_have_exact_types(self):
        from test_sdk import SDKTests
        helper = SDKTests()
        with tempfile.TemporaryDirectory() as td:
            package, materials, manifest, mp, lock = helper.fixture(sdk, Path(td))
            manifest["outputs"]["libbt.a"]["size"] = float(manifest["outputs"]["libbt.a"]["size"])
            mp.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                sdk.verify(package, mp, lock | {"manifest_sha256": sdk.sha(mp)}, materials, S)

class EmbeddedKeyReviewTests(unittest.TestCase):
    def test_final_app_key_at_linked_address_matches_anchor(self):
        import installer_identity as identity
        self.assertTrue(hasattr(identity, "verify_app_key"), "final app embedded verifier key not checked")
        from test_ota import synthetic_app
        app = synthetic_app(); key = bytes(range(32)); pin = hashlib.sha256(key).hexdigest()
        identity.verify_app_key(app, 0x3f400020, key, pin)
        for address, actual_key, expected in ((0x3f400021, key, pin), (0x3f400020, b"a"*32, pin),
                                               (0x3f400020, key, "0"*64)):
            with self.assertRaises(ValueError): identity.verify_app_key(app, address, actual_key, expected)
        self.assertIn("verify_elf_key", (S / "warp4_ota_only_hook.py").read_text())

class ApprovalEntryReviewTests(unittest.TestCase):
    def test_build_forwards_explicit_external_approval(self):
        import inspect
        self.assertIn("runtime_approval", inspect.signature(entry.build).parameters,
                      "external approval cannot reach supported builder")
        with patch.object(entry, "preflight", side_effect=ValueError("stop-before-execution")) as preflight:
            with self.assertRaisesRegex(ValueError, "stop-before-execution"):
                entry.build(policy.ENVS[0], runtime_approval="/external/approval.json")
            preflight.assert_called_once_with(policy.ENVS[0], runtime_approval="/external/approval.json")
        self.assertIn("--runtime-approval", (S / "build_warp4_installer.py").read_text())

class EffectiveClosureReviewTests(unittest.TestCase):
    def test_arbitrary_late_flag_injection_requires_exact_approval(self):
        import inspect
        self.assertIn("approved", inspect.signature(policy.validate_effective).parameters,
                      "effective flags have only a blacklist, not a closure")
        keys = ("BUILD_FLAGS", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "LINKFLAGS")
        baseline = {key: [] for key in keys}
        policy.validate_effective(baseline, False, approved=baseline)
        for key in keys:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "EFFECTIVE_FLAG_CLOSURE"):
                policy.validate_effective(baseline | {key: ["-DANY_NEW_COMPILER_INJECTION=1"]}, False, approved=baseline)
        with self.assertRaises(ValueError): policy.validate_effective(baseline, False, approved={})
        with self.assertRaises(ValueError): policy.validate_effective(baseline, False, approved=None)

class MetadataSchemaReviewTests(unittest.TestCase):
    def test_ota_metadata_rejects_bool_float_and_unknown_fields(self):
        import installer_ota as ota
        from test_ota import synthetic_app, META
        for changed in (META | {"version": [True, 13, 5, 255]},
                        META | {"version": [2.0, 13, 5, 255]},
                        META | {"build_timestamp": True},
                        META | {"unreviewed": 1}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                ota.make_container(synthetic_app(), changed)

class LinkedELFReviewTests(unittest.TestCase):
    def test_real_parser_reads_synthetic_readonly_xtensa_symbol(self):
        import struct
        import installer_identity as identity
        key = bytes(range(32)); address = 0x3f400020
        names = b"\0.shstrtab\0.strtab\0.symtab\0.rodata\0"
        strings = b"\0signature_sodium_public_key_data\0"
        symbols = bytes(16) + struct.pack("<IIIBBH", 1, address, 32, 0x11, 0, 4)
        data = bytearray(bytes(52)); sections = [bytes(40)]
        for name, kind, flags, addr, content, link, info, entsize in (
                (b".shstrtab", 3, 0, 0, names, 0, 0, 0),
                (b".strtab", 3, 0, 0, strings, 0, 0, 0),
                (b".symtab", 2, 0, 0, symbols, 2, 1, 16),
                (b".rodata", 1, 2, address, key, 0, 0, 0)):
            offset = len(data); data.extend(content)
            sections.append(struct.pack("<IIIIIIIIII", names.index(name), kind, flags, addr, offset, len(content), link, info, 1, entsize))
        shoff = len(data); data.extend(b"".join(sections))
        ident = b"\x7fELF\x01\x01\x01" + bytes(9)
        data[:52] = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 94, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(sections), 1)
        pin = hashlib.sha256(key).hexdigest()
        self.assertEqual(identity.verify_elf_key(bytes(data), pin), (address, key))
        for bad in (bytes(data).replace(key, b"a"*32), b"not an ELF"):
            with self.assertRaises(ValueError): identity.verify_elf_key(bad, pin)
