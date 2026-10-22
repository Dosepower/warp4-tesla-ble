from pathlib import Path
import importlib.util,unittest
S=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('sdk',S/'lib-builder/tesla_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
class Policy(unittest.TestCase):
 def test_explicit_nimble_security_and_single_link(self):
  p=sdk.NIMBLE_POLICY;c=sdk.profile_config(p);base=sdk.parse_config((S/'lib-builder/defconfig.esp32secure').read_text())
  sdk.validate_config(c,base,p)
  for key,val in sdk.NIMBLE_SETTINGS.items():
   with self.subTest(key=key):
    with self.assertRaises(ValueError):sdk.validate_config(c|{key:('n' if val!='n' else 'y')},base,p)
  self.assertEqual(sdk.profile_config(sdk.CLASSIC_RELEASE_POLICY)['CONFIG_BT_BLUEDROID_ENABLED'],'y')
 def test_no_silent_host_or_memory_policy(self):
  c=sdk.profile_config(sdk.NIMBLE_POLICY);self.assertEqual(c['CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE'],'8192')
  self.assertEqual(c['CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_DEFAULT'],'y')
  self.assertEqual(c['CONFIG_BT_NIMBLE_SECURITY_ENABLE'],'y');self.assertEqual(c['CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS'],'n')
if __name__=='__main__':unittest.main()
