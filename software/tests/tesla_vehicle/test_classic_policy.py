import importlib.util,unittest
from pathlib import Path
S=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('sdk',S/'lib-builder/tesla_sdk.py')
assert spec and spec.loader
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
class ClassicPolicy(unittest.TestCase):
 def test_explicit_source_candidate_does_not_relax_configuration(self):
  policy='tesla-psram-first-single-link-classic-release-v1'
  self.assertEqual(sdk.profile_config(policy),sdk.profile_config(sdk.SINGLE_LINK_POLICY))
 def test_old_tracking_source_cannot_be_selected(self):
  old=Path('../tesla-sdk-source/builder/components/arduino/cores/esp32/esp32-hal-bt.c')
  with self.assertRaisesRegex(ValueError,'Classic release core'):
   sdk.verify_classic_core(old)
 def test_actual_framework_verified_and_both_sdk_entrypoints_wrapped(self):
  import runpy,types,sys,tempfile,os
  from unittest.mock import Mock,patch
  policy='tesla-psram-first-single-link-classic-release-v1'
  script=types.ModuleType('SCons.Script');setattr(script,'COMMAND_LINE_TARGETS',['tesla-review'])
  with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
   root=Path(td);core=root/'core';(core/'cores/esp32').mkdir(parents=True)
   source=Path('../tesla-framework-core/cores/esp32')
   for name in ('esp32-hal-bt.c','esp32-hal-misc.c'):(core/'cores/esp32'/name).write_bytes((source/name).read_bytes())
   sdkroot=root/'sdk';expected=sdkroot/'builder/components/arduino/cores/esp32';expected.mkdir(parents=True)
   (expected/'esp32-hal-misc.c').write_bytes((source/'esp32-hal-misc.c').read_bytes())
   env=Mock();env.subst.return_value=str(S);env.GetProjectOption.return_value=[]
   env.PioPlatform.return_value.get_package_dir.return_value=str(core)
   fake=types.SimpleNamespace(CLASSIC_RELEASE_POLICY=sdk.CLASSIC_RELEASE_POLICY,NIMBLE_POLICY=sdk.NIMBLE_POLICY,validate_targets=sdk.validate_targets,verify_package=Mock(return_value={'disposition':'noninstallable-fallback-review'}),verify_classic_core=Mock(wraps=sdk.verify_classic_core))
   loader=types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module:None))
   variables={'TESLA_BLE_FRAMEWORK_LIBS':str(sdkroot/'builder/out/tools/esp32-arduino-libs'),'TESLA_BLE_FRAMEWORK_CORE':str(core),'TESLA_BLE_SDK_POLICY':policy,'TESLA_BLE_SDK_SOURCE_ROOT':str(sdkroot)}
   with patch.dict(sys.modules,{'SCons':types.ModuleType('SCons'),'SCons.Script':script}),patch('importlib.util.spec_from_file_location',return_value=loader),patch('importlib.util.module_from_spec',return_value=fake),patch('subprocess.run'),patch.dict(os.environ,variables):
    runpy.run_path(str(S/'tesla_build_guard.py'),init_globals={'Import':lambda _:None,'env':env})
   fake.verify_classic_core.assert_called_once_with(core/'cores/esp32/esp32-hal-bt.c')
   self.assertTrue(any(set(c.kwargs.get('LINKFLAGS',[]))=={'-Wl,--wrap=esp_bt_mem_release','-Wl,--wrap=esp_bt_controller_mem_release'} for c in env.Append.call_args_list))
if __name__=='__main__':unittest.main()
