#!/usr/bin/env python3
"""Adversarial software packaging fixtures; not hardware evidence."""
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import struct
import unittest
from unittest.mock import patch
from test_iram_bench_image import BenchImageTests, image, report, ROOT


class ReviewPackagingTests(BenchImageTests):
    def setUp(self):
        BenchImageTests.setUp(self)
        # Genuine section/address/length rows for the synthetic ELF fixtures.
        for elf, m in [(self.build/'firmware.elf', self.build/'firmware.map'),
                       (self.builder/'build/bootloader/bootloader.elf', self.builder/'build/bootloader/bootloader.map')]:
            e=report.Elf(elf)
            m.write_text('\n'.join(f'{e.string(e.names,s[0])} 0x{s[3]:08x} 0x{s[5]:x}' for s in e.allocated())+'\n')
        # Bind canonical OTA initialization among pre-compilation inputs.
        self.fixture_identity['files'][str(self.ota)]=report.identity.sha(self.ota)
        store=Path(self.fixture_identity['store']);shutil.copyfile(self.ota,store/report.identity.sha(self.ota))
        (self.build/'bench_build_inputs.unchecked.json').write_text(json.dumps(self.fixture_identity))
        # Rebind SDK proof outputs after synthetic map creation.
        p=self.sdk/'bench_sdk_provenance.json';proof=json.loads(p.read_text())
        proof['builder_outputs']=report.identity.snapshot([self.builder/'build'])
        p.write_text(json.dumps(proof))
        self.fixture_identity['sdk_provenance_sha256']=report.identity.sha(p)
        (self.build/'bench_build_inputs.unchecked.json').write_text(json.dumps(self.fixture_identity))
        self.write_app();self.merge()

    def test_valid_layout_control(self):
        r=self.validate();self.assertEqual(r['partition_table_offset'],0xe000)
        self.assertEqual(r['bootloader_partition_literal']['offset'],0xe000)

    def test_same_length_boot_app0_substitution_rejected(self):
        self.ota.write_bytes(b'X'*8192);self.merge()
        with self.assertRaisesRegex(ValueError,'boot_app0|OTA|otadata|identity'):self.validate()

    def test_unrelated_map_rejected(self):
        (self.build/'firmware.map').write_text('unrelated map from another build\n')
        with self.assertRaisesRegex(ValueError,'map|section'):self.validate()

    def test_map_address_substitution_rejected(self):
        p=self.build/'firmware.map';p.write_text(p.read_text().replace('0x3f400020','0x3f400024'))
        with self.assertRaisesRegex(ValueError,'map|section'):self.validate()

    def test_header_and_sdk_offset_disagreement_rejected(self):
        (self.sdk/'esp32/qio_qspi/include/sdkconfig.h').write_text('#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n#define CONFIG_ESPTOOLPY_FLASHSIZE "16MB"\n')
        with self.assertRaises(ValueError):self.validate()

    def test_bootloader_binary_identity_rejected(self):
        (self.build/'bootloader.bin').write_bytes(image(b'other bootloader'));self.merge()
        with self.assertRaisesRegex(ValueError,'bootloader'):self.validate()

    def test_table_layout_substitution_rejected(self):
        p=self.build/'partitions.bin';b=bytearray(p.read_bytes());b[4]^=1;p.write_bytes(b);self.merge()
        with self.assertRaises(ValueError):self.validate()

    def test_app_elf_substitution_rejected(self):
        p=self.build/'firmware.elf';b=bytearray(p.read_bytes());b[55]^=1;p.write_bytes(b)
        with self.assertRaises(ValueError):self.validate()

    def test_unbacked_loadable_segment_rejected(self):
        elf=report.Elf(self.build/'firmware.elf')
        old=report.esp_image((self.build/'firmware.bin').read_bytes(),'application')
        header=bytearray((self.build/'firmware.bin').read_bytes()[:24]);header[1]+=1
        pieces=[(address,payload) for address,_,payload in old]+[(0x3ffb0000,b'\0'*4)]
        data=bytes(header);check=0xef
        for address,payload in pieces:
            data+=struct.pack('<II',address,len(payload))+payload
            for value in payload:check^=value
        data+=b'\0'*((15-len(data))%16)+bytes([check]);data+=hashlib.sha256(data).digest()
        with self.assertRaisesRegex(ValueError,'unbacked|ELF|segment'):report.match_elf(data,elf,'application')

    def test_capacity_and_nonoverlap_rejected(self):
        boot=image(b'X'*0xd000,0x40078000)
        for p in (self.build/'bootloader.bin',self.sdk/'esp32/bin/bootloader_qio_80m.bin',self.builder/'build/bootloader/bootloader.bin'):p.write_bytes(boot)
        self.merge()
        with self.assertRaises(ValueError):self.validate()

    def test_sdk_archive_or_config_identity_rejected(self):
        (self.sdk/'esp32/sdkconfig').write_text(self.config+'CONFIG_NEW_BOUND_VALUE=y\n')
        with self.assertRaises(ValueError):self.validate()

    def test_security_and_chip_revision_constraints(self):
        b=bytearray((self.build/'firmware.bin').read_bytes());struct.pack_into('<H',b,15,302);b[-32:]=hashlib.sha256(b[:-32]).digest();(self.build/'firmware.bin').write_bytes(b);self.merge()
        with self.assertRaisesRegex(ValueError,'revision'):self.validate()
        (self.builder/'build/bootloader/config/sdkconfig.h').write_text('#define CONFIG_PARTITION_TABLE_OFFSET 0xe000\n#define CONFIG_ESPTOOLPY_FLASHSIZE "16MB"\n#define CONFIG_SECURE_BOOT 1\n')
        with self.assertRaisesRegex(ValueError,'secure|security'):self.validate()

    def hook(self):
        actions=[];outer=self
        class Env:
            def subst(self,s):return s.replace('$PROJECT_DIR',str(outer.root)).replace('$BUILD_DIR',str(outer.build))
            def __getitem__(self,k):return 'warp4_tesla_ble_iram_bench'
            def GetProjectOption(self,k,default=None):return {'board_build.arduino.custom_bootloader':str(outer.build/'bootloader.bin'),'custom_provisioning':'false','custom_secure_boot':'','custom_bench_builder':str(outer.builder)}.get(k,default)
            def PioPlatform(self):return self
            def get_package_dir(self,k):return str(outer.sdk)
            def IsCleanTarget(self):return False
            def AddPostAction(self,t,a):actions.append(a)
            def Append(self,**kw):pass
        (self.root/'tools').mkdir(exist_ok=True)
        (self.root/'tools/bench_image_report.py').symlink_to(ROOT/'software/tools/bench_image_report.py')
        env=Env()
        with patch.object(report.identity,'capture_firmware',return_value=self.fixture_identity):
            ns=runpy.run_path(str(ROOT/'software/bench_image_hook.py'),init_globals={'Import':lambda n:None,'env':env})
        return actions,env,ns

    def execute_hook(self,mutate=False):
        actions,env,ns=self.hook();original=shutil.copy2;done=False
        def copying(src,dst,*a,**kw):
            nonlocal done
            if mutate and not done and Path(src).name=='firmware_esptool.bin':
                done=True;b=bytearray(Path(src).read_bytes());b[0]^=1;Path(src).write_bytes(b)
            return original(src,dst,*a,**kw)
        with patch.object(ns['report'].identity,'verify_live_inputs'),patch('subprocess.check_call'),patch('shutil.copy2',side_effect=copying):
            for a in actions:a(source=[],target=[],env=env)
        return self.root/'.pio/bench-exports/ordinary'

    def test_copy_time_mutation_cannot_export_stale_success(self):
        try:self.execute_hook(mutate=True)
        except ValueError:pass
        else:self.fail('export accepted bytes changed after validation')
        self.assertFalse((self.root/'.pio/bench-exports/ordinary').exists())

    def test_report_bound_to_exact_exported_bytes_and_noninstallable_label(self):
        dest=self.execute_hook();reports=list(dest.glob('*report.json'));self.assertEqual(len(reports),1)
        r=json.loads(reports[0].read_text());images=[p for p in dest.iterdir() if 'firmware_esptool' in p.name]
        self.assertEqual(len(images),1);self.assertEqual(report.identity.sha(images[0]),r['image']['sha256'])
        self.assertEqual(r['image']['path'],str(images[0].resolve()))
        self.assertEqual(r.get('installable'),False)
        self.assertIn('NON_INSTALLABLE',images[0].name)

    def test_transaction_failure_does_not_publish_partial_directory(self):
        actions,env,ns=self.hook()
        with patch.object(ns['report'],'validate',side_effect=ValueError('gate rejection')),patch('subprocess.check_call'):
            with self.assertRaises(ValueError):
                for a in actions:a(source=[],target=[],env=env)
        self.assertFalse((self.root/'.pio/bench-exports/ordinary').exists())
        self.assertFalse((self.root/'.pio/bench-exports/ordinary.unchecked').exists())

    def test_export_variant_label_cannot_substitute_identity(self):
        import bench_review_export
        with self.assertRaisesRegex(ValueError,'variant'):
            bench_review_export.export_review(self.build,self.sdk,self.builder,self.ota,self.root/'.pio/bench-exports/minimal')
        self.assertFalse((self.root/'.pio/bench-exports/minimal').exists())

    def test_publish_rename_failure_removes_stage(self):
        import bench_review_export
        dest=self.root/'.pio/bench-exports/ordinary'
        stage=dest.with_name('ordinary.unchecked');original=bench_review_export.os.rename
        reached=[]
        def rename(source,target,*args,**kwargs):
            if Path(source)==stage and Path(target)==dest:
                reached.append((Path(source),Path(target)))
                self.assertTrue(stage.is_dir())
                self.assertTrue((stage/'NON_INSTALLABLE-README.txt').is_file())
                self.assertEqual(len(list(stage.glob('*report.json'))),1)
                self.assertFalse(dest.exists())
                raise OSError('injected final publication failure')
            return original(source,target,*args,**kwargs)
        with patch.object(bench_review_export.os,'rename',side_effect=rename):
            with self.assertRaisesRegex(OSError,'injected final publication failure'):
                bench_review_export.export_review(self.build,self.sdk,self.builder,self.ota,dest)
        self.assertEqual(reached,[(stage,dest)])
        self.assertFalse(dest.exists());self.assertFalse(stage.exists())


def load_tests(loader, tests, pattern):
    return unittest.TestSuite(ReviewPackagingTests(name) for name in sorted(ReviewPackagingTests.__dict__)
                              if name.startswith('test_'))


if __name__=='__main__':unittest.main()
