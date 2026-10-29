"""Synthetic architecture experiment, NOT a qualified F5 producer."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from installer_snapshot import Snapshot

@unittest.skipUnless(os.environ.get('WARP4_TEST_BWRAP'), 'explicit Bubblewrap required')
class GenerationBoundaryTests(unittest.TestCase):
    def test_real_producer_is_sealed_before_next_namespace_consumes_it(self):
        self.assertIsNotNone(importlib.util.find_spec('installer_generation'),
                             'generation-isolated alternative broker is missing')
        from installer_generation import run_stage
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); tool=root/'probe'
            source=Path(__file__).with_name('generation_probe.c').read_bytes()
            subprocess.run(['/usr/bin/cc','-static','-x','c','-','-o',str(tool)],input=source,capture_output=True,check=True)
            inputs={'/snapshot/probe':{'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),
                                      'mode':0o555,'role':'synthetic_probe'}}
            binding={'executables':['/snapshot/probe'],'interpreters':{},'libraries':{}}
            with Snapshot(inputs) as snap:
                with run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,
                               ['/snapshot/probe','produce'],['value.h']) as first:
                    self.assertEqual(first.read('value.h'),b'GENERATION-ONE')
                    from installer_link_verify import verify_link_inputs
                    from installer_graph import verify_pair_closures
                    with self.assertRaisesRegex(ValueError,'LINK_CLOSURE_EVIDENCE_REQUIRED'):
                        verify_link_inputs('not product evidence',first.record)
                    with self.assertRaisesRegex(ValueError,'ACTION_CLOSURE_SCHEMA'):
                        verify_pair_closures(first.record,first.record,{})
                    with run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,
                                   ['/snapshot/probe','consume'],['result'],generation=first) as second:
                        self.assertEqual(second.read('result'),b'GENERATION-ONE')
                        self.assertEqual(second.record['parent_generation'],first.identity)
                        self.assertEqual(second.record['execution']['cwd'],'/output')
                        self.assertEqual(second.record['execution']['elf_binding'],binding)
                        self.assertEqual(second.record['input_scope'],'conservative-exposure-not-observed-consumption')
                        self.assertEqual(second.record['contract'],'declared-action-boundary-not-actual-open-capture')


    def test_real_generator_compiler_archiver_linker_generations(self):
        from installer_generation import run_stage
        from namespace_fixture import python_inputs
        import json
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); tool=root/'probe'
            source=Path(__file__).with_name('generation_probe.c').read_bytes()
            subprocess.run(['/usr/bin/cc','-static','-x','c','-','-o',str(tool)],input=source,capture_output=True,check=True)
            tools={'/snapshot/probe':tool,'/snapshot/compiler/gcc':Path('/usr/bin/gcc').resolve(),
                   '/snapshot/compiler/cc1':Path(subprocess.check_output(['/usr/bin/gcc','-print-prog-name=cc1'],text=True).strip()),
                   '/snapshot/compiler/as':Path('/usr/bin/as').resolve(),
                   '/snapshot/compiler/ar':Path('/usr/bin/ar').resolve(),
                   '/snapshot/compiler/ld':Path('/usr/bin/ld').resolve()}
            inputs,binding=python_inputs(root,{'main.c':b'#include <value.h>\nint fixture=VALUE; void _start(void) { for(;;) {} }\n'},native_tools=tools)
            inputs={p:v for p,v in inputs.items() if not p.startswith('/snapshot/python/')}
            binding['executables']=[p for p in binding['executables'] if p in inputs]
            from contextlib import ExitStack
            with Snapshot(inputs) as snap, ExitStack() as stack:
                def stage(argv,names,parent=None):
                    return stack.enter_context(run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,
                                                         argv,names,generation=parent))
                header=stage(['/snapshot/probe','header'],['value.h'])
                asm=stage(['/snapshot/compiler/gcc','-B/snapshot/compiler/','-nostdinc','-ffreestanding','-MMD',
                           '-MF','/output/main.d','-I/generation','-S','/snapshot/project/main.c','-o','/output/main.s'],
                          ['main.s','main.d'],header)
                self.assertIn(b'/generation/value.h',asm.read('main.d'))
                obj=stage(['/snapshot/compiler/as','-o','/output/main.o','/generation/main.s'],['main.o'],asm)
                self.assertTrue(obj.read('main.o').startswith(bytes([127])+b'ELF'))
                archive=stage(['/snapshot/compiler/ar','rcsD','/output/lib.a','/generation/main.o'],['lib.a'],obj)
                from installer_graph import archive_members
                self.assertEqual(archive_members(archive.read('lib.a')),
                                 {'main.o':hashlib.sha256(obj.read('main.o')).hexdigest()})
                linked=stage(['/snapshot/compiler/ld','-static','-e','_start','-Map','/output/main.map',
                              '-o','/output/main.elf','--whole-archive','/generation/lib.a','--no-whole-archive'],
                             ['main.elf','main.map'],archive)
                self.assertTrue(linked.read('main.elf').startswith(bytes([127])+b'ELF'))
                self.assertIn(b'/generation/lib.a(main.o)',linked.read('main.map'))
                destination=os.environ.get('WARP4_TEST_EVIDENCE_DIR')
                if destination:
                    out=Path(destination); out.mkdir(parents=True,exist_ok=True)
                    records=[gen.record for gen in (header,asm,obj,archive,linked)]
                    (out/'generation-native-records.json').write_text(json.dumps(records,indent=2))
                    (out/'generation-native.map').write_bytes(linked.read('main.map'))
                    (out/'generation-native.d').write_bytes(asm.read('main.d'))
                    (out/'generation-native.s').write_bytes(asm.read('main.s'))

    def test_real_special_and_alias_outputs_are_never_promoted(self):
        from installer_generation import run_stage
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); tool=root/'probe'
            source=Path(__file__).with_name('generation_probe.c').read_bytes()
            subprocess.run(['/usr/bin/cc','-static','-x','c','-','-o',str(tool)],input=source,capture_output=True,check=True)
            with Snapshot({'/snapshot/probe':{'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),
                          'mode':0o555,'role':'synthetic'}}) as snap:
                binding={'executables':['/snapshot/probe'],'interpreters':{},'libraries':{}}
                for mode,names in [('symlink',['value.h']),('fifo',['value.h']),('hardlink',['value.h','alias'])]:
                    with self.subTest(mode=mode), self.assertRaisesRegex(ValueError,'FILE_LINK_OR_MISSING_FORBIDDEN|FILE_SPECIAL_OR_HARDLINK_FORBIDDEN'):
                        run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,['/snapshot/probe',mode],names)

    def test_kernel_rejects_writes_and_writable_shared_maps_of_sealed_generation(self):
        from installer_generation import Generation
        import mmap
        record={'outputs':{'value.h':{'size':3,'sha256':hashlib.sha256(b'one').hexdigest()}}}
        with Generation({'value.h':b'one'},record) as gen:
            fd=gen.files['value.h']
            with self.assertRaises(PermissionError): os.pwrite(fd,b'two',0)
            with self.assertRaises(PermissionError):
                mmap.mmap(fd,3,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE)
            self.assertEqual(gen.read('value.h'),b'one')

    def test_undeclared_output_semantics_fail_closed(self):
        from installer_generation import run_stage
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); tool=root/'probe'
            source=Path(__file__).with_name('generation_probe.c').read_bytes()
            subprocess.run(['/usr/bin/cc','-static','-x','c','-','-o',str(tool)],input=source,capture_output=True,check=True)
            with Snapshot({'/snapshot/probe':{'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),
                          'mode':0o555,'role':'synthetic'}}) as snap:
                binding={'executables':['/snapshot/probe'],'interpreters':{},'libraries':{}}
                with self.assertRaisesRegex(ValueError,'GENERATION_UNDECLARED_OUTPUT'):
                    run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,
                              ['/snapshot/probe','extra'],['value.h'])

    def test_mutated_generation_metadata_is_rejected_before_consumer_exec(self):
        from installer_generation import Generation, run_stage
        from unittest.mock import patch
        # No filesystem mutations in doubles. A forged output inventory must
        # fail before any spawn, even when its canonical digest is recomputed.
        from installer_snapshot import canonical
        with Generation({'value.h':b'one'}, {'outputs':{'value.h':{'size':3,'sha256':hashlib.sha256(b'one').hexdigest()}}}) as gen:
            gen.record['outputs']['extra']={'size':0,'sha256':hashlib.sha256(b'').hexdigest()}
            gen.identity=hashlib.sha256(canonical(gen.record)).hexdigest()
            with tempfile.TemporaryDirectory() as td:
                tool=Path(td)/'tool'; tool.write_bytes(b'not an ELF')
                with Snapshot({'/snapshot/probe':{'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),
                              'mode':0o555,'role':'synthetic'}}) as snap:
                    with patch('installer_generation.verify_elf_closure'), patch('installer_generation.spawn') as spawn:
                        with self.assertRaisesRegex(ValueError,'GENERATION_FILE_SET_CHANGED'):
                            run_stage(os.environ['WARP4_TEST_BWRAP'],snap,
                                      {'executables':['/snapshot/probe'],'interpreters':{},'libraries':{}},
                                      ['/snapshot/probe'],['result'],generation=gen)
                        spawn.assert_not_called()

    def test_real_namespace_reaps_daemon_writers_before_promotion(self):
        from installer_generation import run_stage
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); tool=root/'probe'
            source=Path(__file__).with_name('generation_probe.c').read_bytes()
            subprocess.run(['/usr/bin/cc','-static','-x','c','-','-o',str(tool)],input=source,capture_output=True,check=True)
            with Snapshot({'/snapshot/probe':{'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),
                          'mode':0o555,'role':'synthetic'}}) as snap:
                binding={'executables':['/snapshot/probe'],'interpreters':{},'libraries':{}}
                with run_stage(os.environ['WARP4_TEST_BWRAP'],snap,binding,
                               ['/snapshot/probe','daemon'],['value.h']) as gen:
                    self.assertEqual(gen.read('value.h'),b'DAEMON-READY')


if __name__ == '__main__': unittest.main()
