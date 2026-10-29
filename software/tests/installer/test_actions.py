import ast
import importlib
import json
from pathlib import Path
import runpy
import sys
import types
import unittest
from unittest.mock import Mock, patch
S=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(S))
class ActionTests(unittest.TestCase):
    def api(self):
        for name in ("warp4_installer_guard.py","warp4_ota_only_hook.py","build_warp4_installer.py"):
            self.assertTrue((S/name).exists(), "offline guard/posthook/entry point missing: "+name)
        return importlib.import_module("build_warp4_installer")
    def test_gates_before_any_subprocess(self):
        m=self.api()
        for name in ("warp4_ble_installer","warp4_multi_tesla_installer"):
            with patch.object(m.subprocess,"run") as run:
                with self.assertRaisesRegex(ValueError,"EXTERNAL_SDK_MANIFEST_PIN_MISSING"):
                    m.build(name)
                run.assert_not_called()
    def test_guard_rejects_target_chain_before_provenance_or_actions(self):
        self.api()
        script=types.ModuleType("SCons.Script"); script.COMMAND_LINE_TARGETS=["upload"]
        env=Mock(); env.subst.return_value=str(S); env.__getitem__=Mock(return_value="warp4_ble_installer")
        with patch.dict(sys.modules,{"SCons":types.ModuleType("SCons"),"SCons.Script":script}), patch("subprocess.run") as run:
            with self.assertRaisesRegex(ValueError,"OFFLINE_DEFAULT_ONLY"):
                runpy.run_path(str(S/"warp4_installer_guard.py"), init_globals={"Import":lambda x:None,"env":env})
            run.assert_not_called(); env.AddPostAction.assert_not_called()
    def test_guard_consumes_fixed_context_without_host_preflight(self):
        self.api()
        script=types.ModuleType("SCons.Script"); script.COMMAND_LINE_TARGETS=[]
        name="warp4_ble_installer"
        options=json.loads((S/"installer_env.lock.json").read_text())[name]
        verified={"environment":name,"build_id":"a"*32,"start_ns":1,"package_roots":{}}
        env=Mock(); env.__getitem__=Mock(side_effect=lambda k: {"PIOENV":name,"ENV":{}}[k])
        env.__setitem__=Mock(); env.subst.return_value=str(S)
        env.GetProjectConfig.return_value.items.return_value=list(options.items())
        env.GetProjectOption.side_effect=options.__getitem__
        with patch.dict(sys.modules,{"SCons":types.ModuleType("SCons"),"SCons.Script":script}), patch("installer_snapshot.read_build_context",return_value=verified) as read, patch("build_warp4_installer.preflight",side_effect=AssertionError("host preflight forbidden")):
            runpy.run_path(str(S/"warp4_installer_guard.py"),init_globals={"Import":lambda x:None,"env":env})
        read.assert_called_once_with(name)
        self.assertEqual(verified["build_id"],"a"*32)
        env.__setitem__.assert_called_once_with("WARP4_INSTALLER_VERIFIED",verified)

    def test_posthook_registers_only_final_app_and_no_side_effects(self):
        self.api()
        env=Mock()
        env.__getitem__=Mock(return_value={"build_id":"fixture"})
        with patch("subprocess.run") as run, patch("subprocess.check_call") as check:
            runpy.run_path(str(S/"warp4_ota_only_hook.py"), init_globals={"Import":lambda x:None,"env":env})
            run.assert_not_called(); check.assert_not_called()
        self.assertEqual(env.AddPostAction.call_count,1)
        self.assertEqual(env.AddPostAction.call_args.args[0], "$BUILD_DIR/${PROGNAME}.bin")
        env.AddCustomTarget.assert_not_called()
        source=(S/"warp4_ota_only_hook.py").read_text()
        for bad in ("merge_firmware_hook", "secure_boot.py", "sign.py", "latest", "esptool", "symlink"):
            self.assertNotIn(bad,source)
    def test_installer_preparation_does_not_run_package_updater_or_global_cleanup(self):
        self.api()
        tree=ast.parse((S/"pio_hooks.py").read_text())
        main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=="main")
        # The two opt-in branches must dominate updater and latest cleanup.
        text=ast.unparse(main)
        self.assertIn("if not installer_ota_only:",text)
        self.assertIn("if build and (not installer_ota_only):",text)
        self.assertIn("INSTALLER_GUARD_NOT_VERIFIED",text)
    def test_posthook_map_matches_unique_program_name(self):
        self.api()
        self.assertIn("$BUILD_DIR/${PROGNAME}.map",(S/"warp4_ota_only_hook.py").read_text(), "posthook must verify the map of this run, not stale firmware.map")
    def test_missing_guard_stops_even_clean_before_any_preparation(self):
        tree=ast.parse((S/"pio_hooks.py").read_text())
        main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=="main")
        env=Mock(); env.GetProjectOption.return_value="true"; env.get.return_value=None; env.IsCleanTarget.return_value=True
        cleanup=Mock()
        scope={"env":env,"remove_all_generated_files":cleanup}
        exec(compile(ast.Module(body=[main],type_ignores=[]),"main-only","exec"),scope)
        with self.assertRaisesRegex(ValueError,"INSTALLER_GUARD_NOT_VERIFIED"):
            scope["main"]()
        cleanup.assert_not_called()
    def test_execute_postaction_with_synthetic_bytes_only(self):
        self.api()
        import tempfile
        from test_ota import synthetic_app, META
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); build=root/".pio/build/fixture"; build.mkdir(parents=True)
            meta=META|{"product_id":"warp4","signature_preset":"","secure_boot":""}
            (build/"metadata.json").write_text(json.dumps(meta))
            (build/"fixture.bin").write_bytes(synthetic_app())
            (build/"fixture.elf").write_text("synthetic test placeholder, NOT a firmware ELF")
            (build/"fixture.map").write_text("LOAD libFrameworkArduino.a")
            context={k:"1"*64 for k in ("sdk_manifest_sha256","sdk_inventory_sha256","runtime_manifest_sha256","resolved_config_sha256","public_key_sha256")}
            context.update(environment="warp4_ble_installer",build_id="synthetic-test-only",start_ns=0)
            env=Mock(); env.__getitem__=Mock(return_value=context)
            context["effective_flags"] = {context["environment"]: {key: [] for key in ("BUILD_FLAGS", "CPPDEFINES", "CCFLAGS", "CFLAGS", "CXXFLAGS", "LINKFLAGS")}}
            env.get.side_effect=lambda key, default=None: False if key == "WARP4_INSTALLER_NOT_FOR_DISTRIBUTION" else default
            def subst(value):
                return value.replace("$PROJECT_DIR",str(root)).replace("$BUILD_DIR",str(build)).replace("${PROGNAME}","fixture")
            env.subst.side_effect=subst
            with patch("build_warp4_installer.preflight",side_effect=AssertionError("host preflight forbidden")), patch("installer_snapshot.read_build_context",return_value=context) as read, patch("subprocess.run") as run:
                scope=runpy.run_path(str(S/"warp4_ota_only_hook.py"),init_globals={"Import":lambda x:None,"env":env})
                with self.assertRaisesRegex(ValueError, "PRODUCTION_STAGE_PHASE"):
                    scope["emit_ota"]([],[],env)
                read.assert_called_once_with(context["environment"], guarded=context)
                run.assert_not_called()
            output=root/"build/installer/warp4_ble_installer/synthetic-test-only"
            self.assertFalse(output.exists(), "synthetic ELF/map must never publish a candidate")
    def test_discarded_tesla_is_not_positive_link_evidence(self):
        self.api(); m=importlib.import_module("installer_link_verify")
        discarded="Discarded input sections\n.text.fixture 0x0 0x10 src/tesla_vehicle/tesla_vehicle.cpp.o\nLOAD libTeslaBLEClean.a\nLinker script and memory map\nLOAD libFrameworkArduino.a\n"
        with self.assertRaisesRegex(ValueError,"retained"):
            m.verify_tesla_presence("warp4_multi_tesla_installer",discarded)
    def test_linkgraph_checks_are_symmetric(self):
        self.api(); m=importlib.import_module("installer_link_verify")
        stock="LOAD libFrameworkArduino.a\nLOAD libbt.a\n"
        tesla="Linker script and memory map\n .text._ZN12TeslaVehicle5setupEv 0x400d1000 0x40 .pio/build/x/tesla_vehicle/tesla_vehicle.cpp.o\n .text.fixture 0x400d2000 0x30 .pio/build/x/libTeslaBLEClean.a(fixture.cpp.o)\n"
        m.verify_tesla_presence("warp4_ble_installer",stock)
        m.verify_tesla_presence("warp4_multi_tesla_installer",stock+tesla)
        with self.assertRaises(ValueError): m.verify_tesla_presence("warp4_ble_installer",stock+tesla)
        with self.assertRaises(ValueError): m.verify_tesla_presence("warp4_multi_tesla_installer",stock)
        for token in ("BENCH","--wrap=","RADIO_SPIKE","r13_psram_policy"):
            with self.assertRaises(ValueError): m.verify_tesla_presence("warp4_multi_tesla_installer",stock+tesla+token)
if __name__=="__main__": unittest.main()
