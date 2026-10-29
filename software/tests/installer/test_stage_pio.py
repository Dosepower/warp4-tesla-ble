"""Real offline PIO/SCons stage smoke test. No WARP SDK/key approval."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from installer_snapshot import Snapshot
from installer_generation import run_stage, verify_stage_records, StageDAG
from namespace_fixture import python_inputs

@unittest.skipUnless(os.environ.get('WARP4_TEST_BWRAP'), 'explicit Bubblewrap required')
class StagePIOTests(unittest.TestCase):
    def test_actual_pio_scons_build_promotes_only_sealed_final_products(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            project={
                'platformio.ini':b'[platformio]\nbuild_dir=/tmp/pio\n[env:warp4_ble_installer]\nplatform=nativefixture@0.0.1\n',
                'src/main.cpp':b'#include <value.h>\nextern "C" { int fixture_value=VALUE; void _start(){for(;;){}} }\n',
                'installer_stage_entry.py':Path('installer_stage_entry.py').read_bytes(),
                'pio_driver.py':b'import sys\nsys.argv=["platformio","run","-e","warp4_ble_installer"]\nfrom platformio.__main__ import main\nmain()\n',
                'header.py':b'from pathlib import Path\nPath("/output/value.h").write_bytes(b"#define VALUE 7\\n")\n'}
            tools={
                '/snapshot/system_tools/sh':Path('/usr/bin/dash'),
                '/snapshot/system_tools/bwrap':Path(os.environ['WARP4_TEST_BWRAP']),
                '/snapshot/system_tools/installer-materializer':Path(os.environ['WARP4_TEST_MATERIALIZER']),
                '/snapshot/compiler/g++':Path('/usr/bin/g++').resolve(),
                '/snapshot/compiler/cc1plus':Path(subprocess.check_output(['/usr/bin/g++','-print-prog-name=cc1plus'],text=True).strip()),
                '/snapshot/compiler/collect2':Path(subprocess.check_output(['/usr/bin/g++','-print-prog-name=collect2'],text=True).strip()),
                '/snapshot/compiler/liblto_plugin.so':Path(subprocess.check_output(['/usr/bin/g++','-print-file-name=liblto_plugin.so'],text=True).strip()),
                '/snapshot/compiler/as':Path('/usr/bin/as').resolve(),
                '/snapshot/compiler/ar':Path('/usr/bin/ar').resolve(),
                '/snapshot/compiler/ld':Path('/usr/bin/ld').resolve()}
            inputs,binding=python_inputs(root,project,native_tools=tools,include_pio=True)
            for target,item in inputs.items():
                if target.startswith('/snapshot/system_tools/'): item['role']='system_tools'
            def add(target,data):
                path=root/('extra-'+str(len(inputs)));path.write_bytes(data)
                inputs[target]={'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),'mode':0o444,'role':'pio_fixture'}
            prefix='/snapshot/platformio/platforms/nativefixture/'
            add(prefix+'platform.json',json.dumps({'name':'nativefixture','title':'native stage fixture NOT WARP4','description':'Offline fixture','version':'0.0.1','packages':{}}).encode())
            add(prefix+'.piopm',json.dumps({'type':'platform','name':'nativefixture','version':'0.0.1','spec':{'owner':None,'id':None,'name':'nativefixture','requirements':'0.0.1','uri':None}}).encode())
            add(prefix+'builder/main.py',b"""from SCons.Script import DefaultEnvironment, Default
env=DefaultEnvironment()
env.Replace(SHELL='/snapshot/system_tools/sh',CXX='/snapshot/compiler/g++',AR='/snapshot/compiler/ar',ARFLAGS=['rcsD'],LINK='/snapshot/compiler/g++',CXXFLAGS=['-B/snapshot/compiler/','-ffreestanding','-nostdinc','-fno-exceptions','-fno-rtti','-MMD','-MF','/output/main.d','-I/generation'],LINKFLAGS=['-B/snapshot/compiler/','-nostdlib','-static','-Wl,-e,_start,-Map,/output/main.map'])
obj=env.Object('/output/main.o','/work/project/src/main.cpp')
archive=env.StaticLibrary('/output/libfixture',obj)
program=env.Program('/output/main.elf',archive)
Default(program)
""")
            for path in sorted(Path('/home/hermes/.platformio/packages/tool-scons').rglob('*')):
                if path.is_file() and not path.is_symlink() and '__pycache__' not in path.parts:
                    add('/snapshot/platformio/packages/tool-scons/'+path.relative_to('/home/hermes/.platformio/packages/tool-scons').as_posix(),path.read_bytes())
            python='/snapshot/python/bin/python'
            def policy(script): return {script:{'interpreter':python,'arguments':['-I','-B']}}
            with Snapshot(inputs) as snap, StageDAG(os.environ['WARP4_TEST_BWRAP'],snap,binding) as dag:
                with dag.run('header',
                               [python,'-I','-B','/snapshot/project/header.py'],['value.h'],named=True,
                               script_policy=policy('/snapshot/project/header.py')) as header:
                    try:
                        result=dag.run('pio',
                                         [python,'-I','-B','/snapshot/project/pio_driver.py'],
                                         ['main.o','main.d','libfixture.a','main.elf','main.map'],parents=['header'],
                                         named=True,workspace=True,
                                         script_policy=policy('/snapshot/project/pio_driver.py'))
                    except subprocess.CalledProcessError as exc:
                        self.fail(exc.stdout+exc.stderr)
                    with result:
                        self.assertTrue(hasattr(result,'stdout'), 'actual stage diagnostics missing')
                        self.assertIn('SUCCESS',result.stdout)
                        self.assertTrue(result.read('main.elf').startswith(b'\x7fELF'))
                        self.assertIn(b'/generation/value.h',result.read('main.d'))
                        self.assertIn(b'/output/libfixture.a(main.o)',result.read('main.map'))
                        self.assertTrue(verify_stage_records([header.record,result.record],snap.identity,[result.identity]))
                        ledger=dag.seal_evidence()
                        import fcntl
                        self.assertEqual(fcntl.fcntl(ledger,1034)&15,15)
                        evidence=json.loads(os.pread(ledger,os.fstat(ledger).st_size,0))
                        self.assertEqual(evidence['heads'],[result.identity])
                        self.assertEqual(evidence['records'],[header.record,result.record])
                        from installer_graph import archive_members
                        self.assertEqual(archive_members(result.read('libfixture.a')),
                                         {'main.o':hashlib.sha256(result.read('main.o')).hexdigest()})
                        if os.environ.get('WARP4_TEST_EVIDENCE_DIR'):
                            out=Path(os.environ['WARP4_TEST_EVIDENCE_DIR']);out.mkdir(parents=True,exist_ok=True)
                            (out/'stage-pio-records.json').write_text(json.dumps([header.record,result.record],indent=2))
                            (out/'stage-pio.stdout').write_text(result.stdout)
                            (out/'stage-pio.stderr').write_text(result.stderr)
                            for name in ('main.map','main.d'):(out/('stage-pio-'+name)).write_bytes(result.read(name))
