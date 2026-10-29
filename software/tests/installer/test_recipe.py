"""Recipe assembly fixtures, never external product approvals."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import build_warp4_installer as entry


class RecipeTests(unittest.TestCase):
    def test_complete_assembly_binds_every_inventory_to_fixed_consumer_layout(self):
        import installer_recipe as recipe
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            source=root/"software"; source.mkdir()
            (source/"platformio.ini").write_bytes(b"recipe fixture")
            roles=[]
            for role in entry.RUNTIME_ROLES:
                p=root/role; p.mkdir()
                relative="packages/tool-scons/scons.py" if role=="platformio_core" else "input"
                (p/relative).parent.mkdir(parents=True,exist_ok=True)
                (p/relative).write_bytes(role.encode())
                roles.append({"role":role,"path":str(p),"inventory":entry.inventory(p)})
            sdk=root/"sdk"; sdk.mkdir(); (sdk/"library.a").write_bytes(b"not a real SDK")
            key=root/"key"; key.write_bytes(b"public test bytes, not an official key")
            verified={"source_inventory":{"software/platformio.ini":entry.sha(source/"platformio.ini")},
                      "runtime":{"roots":roles},"sdk_snapshot":{"path":str(sdk),"inventory":entry.inventory(sdk)},
                      "identity_snapshot":{"signature/keys/test.json":{"path":str(key),"sha256":entry.sha(key)}}}
            inputs=recipe.assemble_inputs(verified,source)
            self.assertEqual(inputs["/snapshot/project/platformio.ini"]["sha256"],entry.sha(source/"platformio.ini"))
            self.assertEqual(inputs["/snapshot/python/input"]["role"],"python_environment")
            self.assertEqual(inputs["/snapshot/platformio/packages/tool-scons/scons.py"]["role"],"platformio_core")
            self.assertEqual(inputs["/snapshot/platformio/platforms/espressif32/input"]["role"],"platform")
            self.assertEqual({x["role"] for x in inputs.values()},set(entry.RUNTIME_ROLES)|{"source","sdk","official_identity"})
            (source/"platformio.ini").write_bytes(b"swapped after preflight")
            from installer_snapshot import Snapshot
            with self.assertRaisesRegex(ValueError,"SNAPSHOT_BYTES_MISMATCH"):
                Snapshot(recipe.assemble_inputs(verified,source))
            verified["runtime"]["roots"].pop()
            with self.assertRaisesRegex(ValueError,"RECIPE_ROLES_INCOMPLETE"):
                recipe.assemble_inputs(verified,source)

    def test_supervisor_rejects_unmaterialized_python_before_child_start(self):
        import installer_runner
        context={"environment":"warp4_ble_installer","build_id":"a"*32,"snapshot_sha256":"b"*64}
        with patch.object(installer_runner,"read_build_context",return_value=context), patch.object(installer_runner.subprocess,"run") as child:
            with self.assertRaisesRegex(ValueError,"PYTHON_RUNTIME_PREFIX"):
                installer_runner.run("warp4_ble_installer")
            child.assert_not_called()

    def test_supervisor_runs_only_offline_default_and_exports_exact_final_files(self):
        import installer_runner
        context={"environment":"warp4_ble_installer","build_id":"a"*32,"snapshot_sha256":"b"*64}
        with patch.object(installer_runner,"verify_python_runtime",create=True), patch.object(installer_runner,"read_build_context",return_value=context), patch.object(installer_runner.subprocess,"run") as child, patch.object(installer_runner,"collect_output",return_value="fixture transport only") as collect:
            self.assertEqual(installer_runner.run("warp4_ble_installer"),"fixture transport only")
            argv=child.call_args.args[0]
            self.assertEqual(argv,["/snapshot/python/bin/python","-I","-B","-m","platformio","run","-e","warp4_ble_installer"])
            self.assertTrue(child.call_args.kwargs["close_fds"])
            self.assertTrue(child.call_args.kwargs["check"])
            collect.assert_called_once_with(context)
        with patch.object(installer_runner.subprocess,"run") as child:
            with self.assertRaisesRegex(ValueError,"OFFLINE_DEFAULT_ONLY"):
                installer_runner.run("upload")
            child.assert_not_called()

    def test_preflight_checks_approved_bootstrap_before_first_git(self):
        import installer_recipe
        with tempfile.TemporaryDirectory() as td:
            software=Path(td)/"software"; software.mkdir()
            for name in ("installer_env.lock.json","installer_sdk.lock.json"):
                (software/name).write_text("{}")
            (software/"default_16MB_secure.csv").write_text("fixture")
            with patch.object(entry,"S",software), patch.object(entry,"verify",return_value={"_manifest":{"config_deltas":[]}}), patch("installer_sdk_verify.qualify_contents"), patch.object(entry,"verify_identity",return_value={}), patch.object(entry,"verify_partitions"), patch.object(entry,"load_runtime_payload",return_value=({"snapshot_recipe":{"fixture":True}},"a"*64)), patch("installer_recipe.check_bootstrap",side_effect=ValueError("bootstrap-stop")) as check, patch.object(entry,"firmware_identity") as git:
                with self.assertRaisesRegex(ValueError,"bootstrap-stop"):
                    entry.preflight("warp4_ble_installer",runtime_approval="/external/fixture")
                check.assert_called_once_with({"fixture":True})
                git.assert_not_called()

    def test_vfs_native_children_have_fixed_snapshot_tools_and_private_tmp(self):
        import installer_vfs_acceptance as vfs
        with patch.object(vfs.subprocess,"run") as run:
            vfs.run_acceptance(Path('/work/project'), {'builder':'/snapshot/sdk_builder_sources','idf':'/snapshot/sdk_idf_sources'})
            for call in run.call_args_list:
                environment=call.kwargs['env']
                self.assertEqual(environment.get('TMPDIR'),'/tmp')
                self.assertEqual(environment.get('LD_LIBRARY_PATH'),'/snapshot/loader_runtime')
                self.assertEqual(environment['PATH'],'/snapshot/system_tools:/snapshot/python/bin')
                self.assertEqual(environment['TESLA_HOST_ARDUINOJSON_SOURCE'],'/snapshot/libdeps/ArduinoJson')

    def test_build_uses_new_snapshot_recipe_not_mutable_legacy_sandbox(self):
        with patch.object(entry,"preflight",return_value={"snapshot_recipe":{"fixture":True}}), patch("installer_recipe.execute",return_value=0) as execute, patch.object(entry,"sandbox_command",side_effect=AssertionError("legacy sandbox forbidden")):
            self.assertEqual(entry.build("warp4_ble_installer"),0)
            execute.assert_called_once_with("warp4_ble_installer",{"snapshot_recipe":{"fixture":True}},entry.S)
