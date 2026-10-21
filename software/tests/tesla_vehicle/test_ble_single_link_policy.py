"""Explicit, bounded candidate; never elevate original trust/disposition."""
import importlib.util
from pathlib import Path
import unittest
S=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('single_sdk',S/'lib-builder/tesla_sdk.py')
assert spec is not None and spec.loader is not None
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
POLICY='tesla-psram-first-single-link-v1'
LINK={'CONFIG_BT_ACL_CONNECTIONS':'1','CONFIG_BTDM_CTRL_BLE_MAX_CONN':'1','CONFIG_BT_MULTI_CONNECTION_ENBALE':'n'}
class SingleLink(unittest.TestCase):
 def test_explicit_policy_selects_one_controller_and_host_link(self):
  cfg=sdk.profile_config(POLICY)
  self.assertEqual({k:cfg.get(k) for k in LINK},LINK)
  self.assertEqual(cfg['CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'],'0')
  self.assertNotEqual(sdk.profile_config(sdk.TESLA_POLICY).get('CONFIG_BT_ACL_CONNECTIONS'),'1')
 def test_no_pool_or_security_or_stack_change_can_hide_in_new_policy(self):
  base=sdk.parse_config((S/'lib-builder/defconfig.esp32secure').read_text());cfg=sdk.profile_config(POLICY)
  for k,v in LINK.items():
   with self.subTest(k=k), self.assertRaises(ValueError):sdk.validate_config(cfg|{k:'2' if v=='1' else 'y'},base,policy=POLICY)
  for k in cfg.keys()|{'CONFIG_SECURE_FOREIGN','CONFIG_SPIRAM_FOREIGN'}:
   if k.startswith(('CONFIG_SECURE_','CONFIG_BOOTLOADER_APP_','CONFIG_SPIRAM','CONFIG_PARTITION_TABLE','CONFIG_ESPTOOLPY_FLASH','CONFIG_ESP32_REV_MIN','CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM')) or k in ('CONFIG_BT_BLE_SMP_ENABLE','CONFIG_BT_GATTC_ENABLE','CONFIG_BT_BTC_TASK_STACK_SIZE','CONFIG_BT_BTU_TASK_STACK_SIZE','CONFIG_BT_GATTS_ENABLE','CONFIG_BT_CLASSIC_ENABLED','CONFIG_BLE_MESH'):
    with self.subTest(k=k),self.assertRaises(ValueError):sdk.validate_config(cfg|{k:'n' if cfg.get(k)=='y' else 'y'},base,policy=POLICY)
 def test_unknown_policy_still_rejected(self):
  with self.assertRaises(ValueError):sdk.profile_config(POLICY+'-unapproved')
 def test_effective_and_legacy_counts_cannot_override_single_pool(self):
  base=sdk.parse_config((S/'lib-builder/defconfig.esp32secure').read_text());cfg=sdk.profile_config(POLICY)
  for key in ('CONFIG_BTDM_CTRL_BLE_MAX_CONN_EFF','CONFIG_BTDM_CONTROLLER_BLE_MAX_CONN','CONFIG_BTDM_CONTROLLER_BLE_MAX_CONN_EFF'):
   with self.subTest(key=key),self.assertRaises(ValueError):sdk.validate_config(cfg|{key:'3'},base,policy=POLICY)
 def test_build_guard_passes_explicit_candidate_policy_to_verifier(self):
  import runpy,types,sys
  from unittest.mock import Mock,patch
  script=types.ModuleType('SCons.Script');setattr(script,'COMMAND_LINE_TARGETS',['tesla-review'])
  env=Mock();env.subst.return_value=str(S);env.GetProjectOption.return_value=[]
  fake=types.SimpleNamespace(CLASSIC_RELEASE_POLICY=sdk.CLASSIC_RELEASE_POLICY,NIMBLE_POLICY=sdk.NIMBLE_POLICY,validate_targets=sdk.validate_targets,verify_package=Mock(return_value={'disposition':'noninstallable-fallback-review'}))
  loader=types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module:None))
  with patch.dict(sys.modules,{'SCons':types.ModuleType('SCons'),'SCons.Script':script}),patch('importlib.util.spec_from_file_location',return_value=loader),patch('importlib.util.module_from_spec',return_value=fake),patch('subprocess.run'),patch.dict('os.environ',{'TESLA_BLE_FRAMEWORK_LIBS':str(S/'candidate'),'TESLA_BLE_SDK_POLICY':POLICY}):
   runpy.run_path(str(S/'tesla_build_guard.py'),init_globals={'Import':lambda _:None,'env':env})
  fake.verify_package.assert_called_once_with(S/'candidate',policy=POLICY)
 def test_vfs_acceptance_uses_explicit_candidate_policy_and_source_tree(self):
  import runpy,types
  from unittest.mock import Mock,patch
  fake=types.SimpleNamespace(verify_package=Mock())
  loader=types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module:None))
  root=S/'candidate';package=root/'builder/out/tools/esp32-arduino-libs'
  with patch('importlib.util.spec_from_file_location',return_value=loader),patch('importlib.util.module_from_spec',return_value=fake),patch('subprocess.run') as run,patch.dict('os.environ',{'TESLA_BLE_FRAMEWORK_LIBS':str(package),'TESLA_BLE_SDK_POLICY':POLICY,'TESLA_BLE_SDK_SOURCE_ROOT':str(root),'TESLA_VFS_TEST_OUT':str(root/'vfs-test-output')}):
   runpy.run_path(str(S/'tools/tesla_vfs_acceptance.py'))
  fake.verify_package.assert_called_once_with(package,policy=POLICY)
  self.assertEqual(run.call_args_list[0].kwargs['env']['TESLA_BLE_HOST_IDF_SOURCE'],str(root/'esp-idf-secure'))
  self.assertEqual(run.call_args_list[0].kwargs['env']['TESLA_CLEAN_TEST_OUT'],str(root/'vfs-test-output'))
  with patch('importlib.util.spec_from_file_location',return_value=loader),patch('importlib.util.module_from_spec',return_value=fake),patch('subprocess.run') as run,patch.dict('os.environ',{'TESLA_BLE_FRAMEWORK_LIBS':str(package),'TESLA_BLE_SDK_POLICY':POLICY,'TESLA_BLE_SDK_SOURCE_ROOT':str(root/'wrong-source')}):
   with self.assertRaisesRegex(ValueError,'exact explicit SDK source/package tree'):runpy.run_path(str(S/'tools/tesla_vfs_acceptance.py'))
   run.assert_not_called()
if __name__=='__main__':unittest.main()
