"""Actual WARP module preparation in sealed namespace; synthetic key only."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import subprocess
import unittest
from installer_snapshot import Snapshot
from installer_generation import run_stage
from namespace_fixture import python_inputs

@unittest.skipUnless(os.environ.get('WARP4_TEST_BWRAP'), 'explicit Bubblewrap required')
class StageWARPGeneratorTests(unittest.TestCase):
    def test_actual_firmware_update_prepare_uses_snapshot_key_not_mutated_original(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            key=bytes(range(32))
            metadata={'signature_preset':'','signature_verify_preset':'synthetic:v1',
                      'installer_public_key_sha256':hashlib.sha256(key).hexdigest(),
                      'project_dir':'/work/project','build_dir':'/tmp/build'}
            wrapper=b"""import os,sys,runpy,json,shutil
from pathlib import Path
sys.path.insert(0,'/snapshot/project')
from installer_python_entry import publish_source_package
publish_source_package()
Path('/tmp/build').mkdir()
os.environ['PLATFORMIO_METADATA']=Path('/snapshot/project/test-metadata.json').read_text()
os.chdir('/work/project/src/modules/firmware_update')
runpy.run_path('prepare.py',run_name='__main__')
for name in ('signature_verify.h','signature_verify.cpp'):
 shutil.copyfile('generated/'+name,'/output/'+name)
"""
            files={name:Path(name).read_bytes() for name in ('util.py','__init__.py','src/modules/firmware_update/prepare.py','installer_stage_entry.py',
                'installer_python_entry.py','installer_generators.py','installer_snapshot.py','installer_safeio.py','installer_sdk_verify.py')}
            files.update({'prepare_driver.py':wrapper,'test-metadata.json':json.dumps(metadata).encode(),
                          'signature/config.ini':b'[preset:synthetic:v1]\npublisher = SYNTHETIC TEST PUBLISHER\nsodium_public_key_path = fixture.json\n',
                          'signature/keys/fixture.json':json.dumps({'sodium_public_key':key.hex()}).encode()})
            tools={'/snapshot/system_tools/bwrap':Path(os.environ['WARP4_TEST_BWRAP']),
                   '/snapshot/system_tools/installer-materializer':Path(os.environ['WARP4_TEST_MATERIALIZER'])}
            inputs,binding=python_inputs(root,files,native_tools=tools)
            for target in tools:inputs[target]['role']='system_tools'
            site=Path('.venv/lib/python3.12/site-packages')
            for path in sorted((site/'tinkerforge_util').rglob('*')):
                if not path.is_file() or '__pycache__' in path.parts:continue
                target='/snapshot/python/lib/python3/dist-packages/'+path.relative_to(site).as_posix()
                copy=root/('util-'+str(len(inputs)));copy.write_bytes(path.read_bytes())
                inputs[target]={'path':str(copy),'sha256':hashlib.sha256(copy.read_bytes()).hexdigest(),'mode':0o444,'role':'python'}
            with Snapshot(inputs) as snap:
                # Mutating original input cannot redirect the named snapshot key.
                Path(inputs['/snapshot/project/signature/keys/fixture.json']['path']).write_bytes(b'not a key')
                script='/snapshot/project/prepare_driver.py';python='/snapshot/python/bin/python'
                try:
                    result=run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,[python,'-I','-B',script],
                               ['signature_verify.h','signature_verify.cpp'],named=True,workspace=True,
                               script_policy={script:{'interpreter':python,'arguments':['-I','-B']}})
                except subprocess.CalledProcessError as exc:
                    self.fail(exc.stdout+exc.stderr)
                with result:
                    self.assertIn(b'#define signature_sodium_public_key_length 32',result.read('signature_verify.h'))
                    self.assertIn(b'SIGNATURE_VERIFIER_INDEPENDENT',result.read('signature_verify.h'))
                    self.assertIn(b'0x00, 0x01, 0x02',result.read('signature_verify.cpp'))
