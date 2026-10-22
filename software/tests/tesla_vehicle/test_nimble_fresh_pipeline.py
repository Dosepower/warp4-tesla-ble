import importlib.util,unittest,subprocess,tempfile,os,shutil,json
from pathlib import Path
S=Path(__file__).resolve().parents[2];spec=importlib.util.spec_from_file_location('sdk',S/'lib-builder/tesla_sdk.py');sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk)
OLD=Path(os.environ['TESLA_NIMBLE_V1_SDK_ROOT'])
class FreshPipeline(unittest.TestCase):
 def test_explicit_root_and_offline_missing_source_fail_before_network(self):
  with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'])as td:
   base=[str(S/'.venv/bin/python'),str(S/'lib-builder/tesla_sdk.py')]
   r=subprocess.run(base+['prepare','--root',td,'--offline','--policy',sdk.NIMBLE_POLICY],capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0);self.assertIn('Offline SDK source unavailable',r.stderr);self.assertNotIn('git clone',r.stdout)
   r=subprocess.run(base+['verify','--root',td,'--policy',sdk.NIMBLE_POLICY],capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0);self.assertIn(str(Path(td)/'builder/out/tools/esp32-arduino-libs/tesla-sdk.json'),r.stderr)
 def test_explicit_cli_policy(self):
  r=subprocess.run([str(S/'.venv/bin/python'),str(S/'lib-builder/tesla_sdk.py'),'--help'],capture_output=True,text=True)
  self.assertEqual(r.returncode,0);self.assertIn(sdk.NIMBLE_POLICY,r.stdout)
 def test_fresh_source_prepare_export_and_tamper_fail_closed(self):
  with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'])as td:
   root=Path(td);builder=root/'builder';idf=root/'esp-idf-secure';core=builder/'components/arduino/cores/esp32';core.mkdir(parents=True)
   original=(OLD/'framework-core/cores/esp32/esp32-hal-bt.c').read_bytes();(core/'esp32-hal-bt.c').write_bytes(original)
   subprocess.run(['git','apply','--reverse',str(S/'patches/lib-builder/components/arduino/0002-Track-Classic-controller-memory-not-host.patch')],cwd=builder/'components/arduino',check=True)
   policy=json.loads((S/'lib-builder/nimble-init-v2/source-policy.json').read_text())
   for name in policy['files']:
    p=idf/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(OLD/'esp-idf-secure'/name,p)
   sdk.prepare_candidate_sources(builder,idf,sdk.NIMBLE_POLICY)
   sdk.prepare_candidate_sources(builder,idf,sdk.NIMBLE_POLICY)
   sdk.verify_classic_core(core/'esp32-hal-bt.c')
   inventory=sdk.source_policy_inventory(idf,sdk.NIMBLE_POLICY)
   self.assertEqual(inventory['files'],{p:v['after_sha256']for p,v in policy['files'].items()})
   package=root/'package';(package/'esp32').mkdir(parents=True)
   sdk.export_candidate_sources(package,builder,idf,sdk.NIMBLE_POLICY)
   sdk.verify_candidate_export(package,sdk.NIMBLE_POLICY)
   self.assertEqual((package/'tesla-core/esp32-hal-bt.c').read_bytes(),original)
   p=next(iter(policy['files']));(idf/p).write_text((idf/p).read_text()+'\n// unreviewed delta\n')
   with self.assertRaisesRegex(ValueError,'source policy'):sdk.prepare_candidate_sources(builder,idf,sdk.NIMBLE_POLICY)
   p=package/'tesla-sdk-source'/p;p.write_text(p.read_text()+'\n// unreviewed delta\n')
   with self.assertRaisesRegex(ValueError,'source policy'):sdk.verify_candidate_export(package,sdk.NIMBLE_POLICY)
if __name__=='__main__':unittest.main()
