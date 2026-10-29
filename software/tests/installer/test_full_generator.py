"""Real full production pio_hooks execution, not extracted code or Node fragments."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from installer_snapshot import Snapshot
from installer_generation import StageDAG
from full_generator_fixture import full_inputs

@unittest.skipUnless(os.environ.get('WARP4_TEST_BWRAP'), 'explicit real namespace required')
class FullGeneratorTests(unittest.TestCase):
    def test_actual_full_generator_through_production_recipe_and_linked_key_export(self):
        import test_stage_e2e
        test_stage_e2e.StageProductionE2E(methodName='runTest').exercise('warp4_ble_installer',full_generator=True)

    def test_full_generator_then_bound_fresh_consumer(self):
        self.exercise_generator('warp4_ble_installer')

    def test_B_full_generator_includes_actual_tesla_frontend(self):
        self.exercise_generator('warp4_multi_tesla_installer')

    def exercise_generator(self,name):
        with tempfile.TemporaryDirectory(prefix='warp4-full-generator-') as td:
            root=Path(td)
            inputs,binding=full_inputs(root)
            def add(relative,data,role='source'):
                p=root/str(len(inputs));p.write_bytes(data)
                target='/snapshot/project/'+relative
                inputs[target]={'path':str(p),'sha256':hashlib.sha256(data).hexdigest(),
                                'mode':0o444,'role':role}
            add('full_consumer.py',b"""import sys,runpy,json,hashlib
from pathlib import Path
sys.path.insert(0,'/snapshot/project')
from installer_generators import generator_environment,consume
from installer_snapshot import read_build_context
ctx=read_build_context(sys.argv[1])
env=generator_environment(sys.argv[1],ctx)
assert not Path('/work/project/src/build.h').exists()
assert not Path('/home/hermes').exists()
assert not Path('/work/project/.pio').exists()
assert not Path('/work/project/web/build').exists()
Path(env.subst('$BUILD_DIR')).mkdir(parents=True)
consume(env)
assert Path('/work/project/src/build.h').is_file()
assert Path('/work/project/web/build/index.standalone.html').is_file()
# Invoking the actual hook must take the sealed consumption branch. Rebuilding
# would fail on the now-present output, not silently use a digest/cache.
def imported(name):
 import inspect
 inspect.currentframe().f_back.f_globals['env']=env
runpy.run_path('/snapshot/project/pio_hooks.py',init_globals={'Import':imported,'COMMAND_LINE_TARGETS':[]})
Path('/output/consumed.json').write_text(json.dumps({'metadata':json.loads(Path(env.subst('$BUILD_DIR')+'/metadata.json').read_bytes()),'web_sha256':hashlib.sha256(Path('/work/project/src/index_html.embedded.cpp').read_bytes()).hexdigest()}))
import shutil
shutil.rmtree('/output/build')
""")
            key=bytes(range(32))
            add('signature/config.ini',b'[preset:warp:v1]\npublisher = SYNTHETIC TEST ONLY\nsodium_public_key_path = fixture.json\n')
            add('signature/keys/fixture.json',json.dumps({'sodium_public_key':key.hex()}).encode(),'official_identity')
            flags={k:[] for k in ('BUILD_FLAGS','CPPDEFINES','CCFLAGS','CFLAGS','CXXFLAGS','LINKFLAGS')}
            # Test dependency substitution only: first actual run records the
            # generator flags, production preflight still requires external pins.
            from platformio.project.config import ProjectConfig
            flags['BUILD_FLAGS']=ProjectConfig.get_instance().get('env:'+name,'build_flags')
            context={'environment':name,'build_id':'1'*32,'start_ns':1,'package_roots':{},
                     'effective_flags':{name:flags},'public_key_sha256':hashlib.sha256(key).hexdigest(),
                     'source_head':'a'*40,'source_metadata':{'is_release':False,'is_dirty':True,
                     'git_url':'https://example.invalid/synthetic-test','git_commit_id':'a'*15,'branch_name':'main'},
                     'stage_phase':'generate','web_outputs':{}}
            baseline={p:i['sha256'] for p,i in inputs.items()}
            python='/snapshot/python/bin/python';script='/snapshot/project/installer_generator_entry.py'
            with Snapshot(inputs) as snap, StageDAG(os.environ['WARP4_TEST_BWRAP'],None,binding) as dag:
                dag.snapshot=snap
                # The real key source changes AFTER sealing. The namespace must
                # still execute updater prepare with the original sealed key.
                Path(inputs['/snapshot/project/signature/keys/fixture.json']['path']).write_bytes(b'poisoned host key')
                fd=snap.context({'environment':name,'private_workspace':True,'verified':context})
                try:
                    result=dag.run('generate',[python,'-I','-B',script,name],['generated.json'],
                        named=True,workspace=True,context_fd=fd,
                        script_policy={script:{'interpreter':python,'arguments':['-I','-B']}})
                except subprocess.CalledProcessError as e:
                    self.fail(e.stdout+'\n'+e.stderr)
                finally:os.close(fd)
                # Fresh second namespace imports the same production hook. It
                # must consume exactly the sealed parent without rerunning tools.
                consume_script='/snapshot/project/full_consumer.py'
                data=json.loads(result.read('generated.json'))
                self.assertEqual(data['schema'],'warp4-generated-products-v1')
                self.assertGreater(len(data['files']),100)
                import base64
                files={p:base64.b64decode(v) for p,v in data['files'].items()}
                self.assertGreater(len(files['web/build/index.standalone.html']),100000)
                self.assertGreater(len(files['src/index_html.embedded.cpp']),100000)
                self.assertIn(b'0x00, 0x01, 0x02',files['src/modules/firmware_update/generated/signature_verify.cpp'])
                self.assertEqual(data['metadata']['product_id'],'warp4')
                self.assertEqual(data['metadata']['firmware_info_product_name'],'WARP4 Charger')
                self.assertEqual(data['metadata']['installer_public_key_sha256'],hashlib.sha256(key).hexdigest())
                if name=='warp4_multi_tesla_installer':
                    self.assertIn('tesla_vehicle',data['metadata']['frontend_modules'])
                    self.assertIn('web/src_tfpp/modules/tesla_vehicle/main.tsx',files)
                context.update(stage_phase='build',generator_output_sha256=hashlib.sha256(result.read('generated.json')).hexdigest())
                fd=snap.context({'environment':name,'private_workspace':True,'verified':context})
                try:
                    consumer=dag.run('consumer',[python,'-I','-B',consume_script,name],['consumed.json'],parents=['generate'],
                        named=True,workspace=True,context_fd=fd,
                        script_policy={consume_script:{'interpreter':python,'arguments':['-I','-B']}})
                except subprocess.CalledProcessError as e:self.fail(e.stdout+'\n'+e.stderr)
                finally:os.close(fd)
                # Host-bound parent hash is mandatory even when all original
                # source and web dependency bytes remain valid.
                poisoned=dict(context,generator_output_sha256='f'*64)
                fd=snap.context({'environment':name,'private_workspace':True,'verified':poisoned})
                try:
                    with self.assertRaises(subprocess.CalledProcessError) as caught:
                        dag.run('wrong-generator-parent',[python,'-I','-B',consume_script,name],['consumed.json'],
                            parents=['generate'],named=True,workspace=True,context_fd=fd,
                            script_policy={consume_script:{'interpreter':python,'arguments':['-I','-B']}})
                    self.assertIn('GENERATOR_PARENT_MISMATCH',caught.exception.stderr)
                finally:os.close(fd)
                consumed=json.loads(consumer.read('consumed.json'))
                self.assertEqual(consumed['metadata'],data['metadata'])
                self.assertEqual(consumed['web_sha256'],hashlib.sha256(files['src/index_html.embedded.cpp']).hexdigest())
                self.assertEqual(consumer.record['parents'],[result.identity])
                evidence=Path(os.environ.get('WARP4_TEST_EVIDENCE_DIR',str(root)))
                evidence.mkdir(parents=True,exist_ok=True)
                evidence=evidence/('A' if name=='warp4_ble_installer' else 'B');evidence.mkdir(parents=True,exist_ok=True)
                (evidence/'full-generator.json').write_bytes(result.read('generated.json'))
                (evidence/'full-generator.stdout').write_text(result.stdout)
                (evidence/'full-generator.stderr').write_text(result.stderr)
                (evidence/'full-generator-dag.json').write_text(json.dumps(dag.evidence(),indent=2))
            # Every actual host source/dependency stays byte-identical. Only
            # the explicitly poisoned private key fixture is exempted.
            for target,item in inputs.items():
                if target=='/snapshot/project/signature/keys/fixture.json':continue
                self.assertEqual(hashlib.sha256(Path(item['path']).read_bytes()).hexdigest(),baseline[target],target)
