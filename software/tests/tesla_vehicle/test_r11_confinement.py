#!/usr/bin/env python3
"""R11 actual-entrypoint output confinement; no SDK/firmware compilation."""
import importlib.util
import ast
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import test_r9_driver_confinement as r9
from test_r10_confinement import state

ROOT = r9.ROOT
sys.path.insert(0, str(ROOT / 'software/tools'))
import bench_confinement as c
import bench_build_identity as identity

SDK_FILES = ('bench_sdk_start.unchecked.json',
    'out/tools/esp32-arduino-libs/package.json', 'out/tools/esp32-arduino-libs/tools.json',
    'out/tools/esp32-arduino-libs/versions.txt', 'out/tools/esp32-arduino-libs/bench_sdk_provenance.json',
    'out/tools/esp32-arduino-libs/esp32/lib/libmbedtls_2.a',
    'out/tools/esp32-arduino-libs/esp32/lib/libesp_driver_sdm_2.a',
    'out/tools/esp32-arduino-libs/esp32/bin/bootloader_qio_80m.bin')


def install(path, kind, victim):
    path.parent.mkdir(parents=True, exist_ok=True)
    if kind == 'hardlink': os.link(victim, path)
    elif kind == 'symlink': path.symlink_to(victim)
    elif kind == 'directory': path.mkdir()
    else: path.write_bytes(b'old ordinary output')


class SDKTests(unittest.TestCase):
    def driver(self):
        case = r9.DriverConfinementTests(); case.setUp(); self.addCleanup(case.doCleanups)
        case.exports(case.pio / 'bench-exports')
        for sdk in c.SDKS:
            for name in ('build', 'out'): case.sentinel(case.pio / sdk / name)
        victim = case.outside / 'VICTIM'; victim.write_bytes(b'UNRELATED')
        return case, victim

    def test_actual_driver_complete_sdk_output_class_before_first_mutation(self):
        for sdk in c.SDKS:
            for relative in SDK_FILES:
                for kind in ('hardlink', 'symlink', 'directory'):
                    with self.subTest(sdk=sdk, output=relative, kind=kind):
                        case, victim = self.driver()
                        install(case.pio / sdk / relative, kind, victim)
                        before = state(case.root); result = case.run_driver()
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertFalse(case.audit.exists(), 'partial cleanup')
                        self.assertEqual(state(case.root), before)
                        self.assertTrue(all(p.exists() for p in case.sentinels))

    def test_actual_driver_safe_retained_identity(self):
        case, victim = self.driver()
        install(case.pio / c.SDKS[0] / SDK_FILES[0], 'regular', victim)
        result = case.run_driver()
        self.assertNotEqual(result.returncode, 2)
        self.assertEqual(len(case.audit.read_text().splitlines()), 4)
        self.assertEqual(victim.read_bytes(), b'UNRELATED')

    def test_actual_driver_output_ancestor_alias_preserves_all_sentinels(self):
        case,victim=self.driver();sdk=case.pio/c.SDKS[0]
        (sdk/'out/tools').symlink_to(case.outside,target_is_directory=True)
        before=state(case.root);result=case.run_driver()
        self.assertEqual(result.returncode,2);self.assertFalse(case.audit.exists())
        self.assertEqual(state(case.root),before)

    def test_begin_identity_substitution_rejects_before_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            b=Path(tmp);target=b/SDK_FILES[0];target.write_bytes(b'old');victim=b/'victim';victim.write_bytes(b'UNRELATED')
            def inject(_):
                target.unlink();target.symlink_to(victim)
                return {'fixture':'0'*64}
            with patch.object(identity,'sdk_sources',side_effect=inject),patch.object(identity,'sha',return_value='0'*64),patch.object(identity,'export_header_suffix',return_value='fixture'):
                with self.assertRaises(ValueError):identity.begin_sdk(b)
            self.assertEqual(victim.read_bytes(),b'UNRELATED')

    def test_firmware_identity_rejects_before_sdk_lookup_or_blob_copy(self):
        for kind in ('hardlink','symlink','directory'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp);b=p/'.pio/build/bench';b.mkdir(parents=True);victim=p/'victim';victim.write_bytes(b'UNRELATED')
                install(b/'bench_build_inputs.unchecked.json',kind,victim);before=state(p)
                with patch.object(identity,'verify_sdk') as verify:
                    with self.assertRaises(ValueError):identity.capture_firmware({},b,p/'sdk',p/'builder',p)
                    verify.assert_not_called()
                self.assertEqual(state(p),before)

    def test_begin_sdk_aliases_and_ordinary_control(self):
        for kind in ('hardlink', 'symlink', 'directory', 'regular'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); b=root/'builder'; b.mkdir(); victim=root/'victim'; victim.write_bytes(b'UNRELATED')
                target=b/SDK_FILES[0]; install(target,kind,victim); before=state(root)
                with patch.object(identity,'sdk_sources',return_value={'fixture':'0'*64}), patch.object(identity,'sha',return_value='0'*64), patch.object(identity,'export_header_suffix',return_value='fixture'):
                    if kind=='regular': identity.begin_sdk(b); self.assertEqual(json.loads(target.read_text())['schema'],2)
                    else:
                        with self.assertRaises(ValueError):identity.begin_sdk(b)
                        self.assertEqual(state(root),before)
                self.assertEqual(victim.read_bytes(),b'UNRELATED')

    def test_finalize_all_outputs_validated_before_package_write(self):
        spec=importlib.util.spec_from_file_location('r11_prepare',ROOT/'software/lib-builder/prepare_warp4_bench.py')
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        for relative in SDK_FILES[1:]:
            for kind in ('hardlink','symlink','directory'):
                with self.subTest(output=relative,kind=kind),tempfile.TemporaryDirectory() as tmp:
                    root=Path(tmp); b=root/'builder'; src=root/'source/out/tools/esp32-arduino-libs';src.mkdir(parents=True)
                    (src/'package.json').write_text('{"version":"fixture"}')
                    b.mkdir(); (b/'bench_sdk_inputs.json').write_text(json.dumps({'source_builder':str(root/'source'),'idf_upstream_commit':'0'*40}))
                    victim=root/'victim';victim.write_bytes(b'UNRELATED');install(b/relative,kind,victim);before=state(root)
                    with self.assertRaises(ValueError):m.finalize(b,'/no-strip')
                    self.assertEqual(state(root),before)

    def test_finalize_safe_control_reaches_missing_input_without_victim_write(self):
        spec=importlib.util.spec_from_file_location('r11_prepare_control',ROOT/'software/lib-builder/prepare_warp4_bench.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);b=root/'builder';s=root/'source/out/tools/esp32-arduino-libs';s.mkdir(parents=True);(s/'package.json').write_text('{}')
            b.mkdir();(b/'bench_sdk_inputs.json').write_text(json.dumps({'source_builder':str(root/'source'),'idf_upstream_commit':'0'*40}))
            (b/'out/tools/esp32-arduino-libs').mkdir(parents=True)
            with self.assertRaises(FileNotFoundError):m.finalize(b,'/no-strip')


class HookTests(unittest.TestCase):
    def execute(self, entry, kind, substitution=False):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'software';p.mkdir();o=root/'outside';o.mkdir();victim=o/'bench_image_report.json';victim.write_bytes(b'UNRELATED')
            b=p/'.pio/build/warp4_tesla_ble_iram_bench';b.parent.mkdir(parents=True)
            if kind=='root':b.symlink_to(o,target_is_directory=True)
            elif kind=='ancestor':
                b.parent.rmdir();b.parent.symlink_to(o,target_is_directory=True)
            elif kind=='wrong':b.write_bytes(b'wrong type')
            else:
                b.mkdir(); leaf=b/'bench_image_report.json'
                if kind=='leaf':leaf.symlink_to(victim)
                elif kind=='dangling-leaf':leaf.symlink_to(o/'missing')
                elif kind=='leaf-directory':leaf.mkdir()
                else:leaf.write_bytes(b'report')
            exports=[]
            for n in ('ordinary','ordinary.unchecked'):
                e=p/'.pio/bench-exports'/n;e.mkdir(parents=True);(e/'sentinel').write_bytes(b'keep');exports.append(e)
            tools=p/'tools';tools.mkdir()
            for n in ('bench_image_report.py','bench_build_identity.py'):(tools/n).symlink_to(ROOT/'software/tools'/n)
            class Env:
                def subst(self,s):return s.replace('$PROJECT_DIR',str(p)).replace('$BUILD_DIR',str(b))
                def __getitem__(self,k):return 'warp4_tesla_ble_iram_bench'
                def IsCleanTarget(self):return True
            class Done(Exception):pass
            def ret():raise Done()
            original=c.preflight; hits=[]; saved=[]
            def inject(*args,**kwargs):
                plan=original(*args,**kwargs)
                if substitution and b/'bench_image_report.json' in plan.paths and not hits:
                    hits.append(True);os.rename(b,b.with_name('saved'));b.mkdir();saved.append(state(root))
                return plan
            before=state(root);error=None
            with patch.object(c,'preflight',side_effect=inject),patch('shutil.rmtree',wraps=shutil.rmtree) as delete:
                try:runpy.run_path(str(ROOT/'software'/entry),init_globals={'Import':lambda _:None,'env':Env(),'Return':ret})
                except Done:pass
                except (ValueError,OSError) as ex:error=ex
                safe=kind in ('ordinary','leaf','dangling-leaf') and not substitution
                if safe:
                    self.assertIsNone(error);self.assertEqual(delete.call_count,2);self.assertFalse((b/'bench_image_report.json').is_symlink())
                else:
                    self.assertIsNotNone(error);self.assertEqual(delete.call_count,0)
                    self.assertEqual(state(root),saved[0] if saved else before)
                self.assertEqual(victim.read_bytes(),b'UNRELATED')
            if substitution:self.assertEqual(hits,[True])

    def test_both_actual_hook_matrices(self):
        for entry in ('bench_guard_hook.py','bench_image_hook.py'):
            for kind in ('root','ancestor','wrong','leaf-directory','ordinary','leaf','dangling-leaf'):
                with self.subTest(entry=entry,kind=kind):self.execute(entry,kind)
    def test_both_hook_identity_substitution(self):
        for entry in ('bench_guard_hook.py','bench_image_hook.py'):
            with self.subTest(entry=entry):self.execute(entry,'ordinary',True)


class QuarantineTests(unittest.TestCase):
    def test_exact_astra_jump_parent_case_preserves_unrelated_alias(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'project';b=p/'build';b.mkdir(parents=True);o=root/'outside';(o/'inner').mkdir(parents=True)
            (b/'a.bin').write_bytes(b'logged');(o/'a.bin').write_bytes(b'UNRELATED')
            (b/'jump').symlink_to(o/'inner',target_is_directory=True)
            alias=b/'unrelated-latest.bin';alias.symlink_to('jump/../a.bin')
            direct=b/'direct.bin';direct.symlink_to('a.bin')
            log=root/'old.log';log.write_text('Copying .pio/build/warp4_tesla_ble_iram_bench/firmware_esptool.bin -> build/a.bin\n')
            self.assertEqual(alias.read_bytes(),b'UNRELATED');identity.quarantine_legacy(p,[log])
            self.assertTrue(alias.is_symlink());self.assertEqual(alias.read_bytes(),b'UNRELATED');self.assertFalse(direct.is_symlink())


class HostTests(unittest.TestCase):
    def test_actual_scripts_output_aliases_and_compiler_stop_controls(self):
        for script in ('run_host_tests.sh','cleanroom/run_test.sh'):
            for name in ('test_tesla_ble_callback_gate.includes','mbedtls-configure.log','mbedtls-build.log'):
                if script=='run_host_tests.sh' and name!='test_tesla_ble_callback_gate.includes':continue
                for kind in ('hardlink','symlink','ancestor','regular'):
                    with self.subTest(script=script,name=name,kind=kind),tempfile.TemporaryDirectory() as tmp:
                        root=Path(tmp);out=root/'output';out.mkdir();victim=root/'victim';victim.write_bytes(b'UNRELATED')
                        target=out/('module-helpers/'+name if script=='run_host_tests.sh' else name)
                        if script!='run_host_tests.sh' and name.startswith('test_'):target=out/'test_dummy.includes'
                        if script!='run_host_tests.sh' and name.startswith('test_') and kind!='ancestor':
                            archive=out/'mbedtls/library/libmbedcrypto.a';archive.parent.mkdir(parents=True);archive.write_bytes(b'LOCAL INPUT FIXTURE; compiler stops')
                        if kind=='ancestor':
                            target.parent.mkdir(parents=True,exist_ok=True)
                            if target.parent==out:out.rmdir();out.symlink_to(root/'outside',target_is_directory=True);(root/'outside').mkdir()
                            else:target.parent.rmdir();target.parent.symlink_to(root,target_is_directory=True)
                        else:install(target,kind,victim)
                        tools=root/'bin';tools.mkdir()
                        for command in ('g++','cmake'):
                            f=tools/command;f.write_text('#!/bin/sh\nprintf "LOCAL COMPILER STOP\\n" >&2\nexit 86\n');f.chmod(0o755)
                        env=dict(os.environ,TESLA_CLEAN_TEST_OUT=str(out),TESLA_CLEAN_MBEDTLS_BUILD=str(out/'mbedtls'),PATH=str(tools)+os.pathsep+os.environ['PATH'])
                        before=state(root);args=['bash',str(ROOT/'software/tests/tesla_vehicle'/script)]
                        if script!='run_host_tests.sh':args.append('test_dummy')
                        result=subprocess.run(args,env=env,capture_output=True,text=True)
                        self.assertNotEqual(result.returncode,0)
                        self.assertEqual(victim.read_bytes(),b'UNRELATED')
                        if kind!='regular':self.assertEqual(state(root),before)
                        else:
                            logs=''.join(p.read_text() for p in out.rglob('*') if p.is_file())
                            self.assertIn('LOCAL COMPILER STOP',result.stdout+result.stderr+logs)


class EquivalentWriterTests(unittest.TestCase):
    def test_sibling_directory_activity_does_not_substitute_output_ancestor(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);output=p/'outputs';output.mkdir()
            plan=c.preflight(directories=(output,),files=(output/'log',))
            (p/'unrelated-sibling').mkdir()
            plan.revalidate()

    def test_unreadable_output_tree_rejects_before_driver_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);sdk=p/'.pio'/c.SDKS[0];(sdk/'build').mkdir(parents=True)
            def walk(*args,**kwargs):
                kwargs['onerror'](PermissionError('output subtree unavailable'))
                return iter(())
            with patch.object(c.os,'walk',side_effect=walk),patch.object(c.shutil,'rmtree') as delete:
                with self.assertRaises(PermissionError):c.initialize_driver(p)
                delete.assert_not_called()
            self.assertFalse((p/'.pio/bench-evidence').exists())

    def test_real_harness_entrypoints_reject_before_input_reads_or_compiler(self):
        cases=(('host/build_littlefs_close_harness.py','littlefs-close/backend.c',[]),
               ('host/build_target_oom_harness.py','target-oom/WString.cpp',[]),
               ('host/build_module_validation_harness.py','target-oom/module_validation_definitions.inc',[]),
               ('host/audit_external_publication.py','tesla-phase1-eleventh-external/external_contract.cpp',[]),
               ('host/audit_reachable_stack.py','stack/symbols.txt',['stack']),
               ('host/run_protocol_test.sh','warp-tesla-host-tests/mbedtls-configure.log',[]))
        for script,output,args in cases:
            for kind in ('hardlink','symlink'):
                with self.subTest(script=script,kind=kind),tempfile.TemporaryDirectory() as tmp:
                    root=Path(tmp);v=root/'victim';v.write_bytes(b'UNRELATED');install(root/output,kind,v)
                    before=state(root);env=dict(os.environ,TMPDIR=str(root),TESLA_CLEAN_TEST_OUT=str(root))
                    cmd=['bash' if script.endswith('.sh') else sys.executable,str(ROOT/'software/tests/tesla_vehicle'/script)]
                    result=subprocess.run(cmd+[str(root/a) for a in args],env=env,capture_output=True,text=True)
                    self.assertNotEqual(result.returncode,0)
                    self.assertRegex(result.stdout+result.stderr,'confinement:')
                    self.assertEqual(state(root),before)

    def test_pio_main_preflight_before_any_generated_cleanup(self):
        # Execute the exact main function AST: no third-party SCons/frontend
        # dependencies or synthetic successful firmware build are involved.
        source=ast.parse((ROOT/'software/pio_hooks.py').read_text())
        function=next(n for n in source.body if isinstance(n,ast.FunctionDef) and n.name=='main')
        for kind in ('hardlink','symlink','ancestor','regular'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp);v=p/'victim';v.write_bytes(b'UNRELATED')
                target=p/'src/build.cpp'
                if kind=='ancestor':(p/'src').symlink_to(p/'outside',target_is_directory=True);(p/'outside').mkdir()
                else:install(target,kind,v)
                class Env:
                    def subst(self,s):return str(p/'.pio/build/bench') if s=='$BUILD_DIR' else str(p)
                    def IsCleanTarget(self):return True
                clean=[];ns={'env':Env(),'pathlib':__import__('pathlib'),'output_plan':c.output_plan,'remove_all_generated_files':lambda:clean.append(True)}
                exec(compile(ast.Module(body=[function],type_ignores=[]),'actual-pio-main','exec'),ns)
                before=state(p)
                if kind=='regular':ns['main']();self.assertEqual(clean,[True])
                else:
                    with self.assertRaises(ValueError):ns['main']()
                    self.assertEqual(clean,[])
                self.assertEqual(state(p),before)


if __name__=='__main__':unittest.main()
