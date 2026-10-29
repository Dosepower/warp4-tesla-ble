"""Isolated real Xtensa fixture through production entry/recipe/guard/posthook."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid
from unittest.mock import patch
from namespace_fixture import python_inputs
from installer_snapshot import Snapshot, canonical
import build_warp4_installer as entry
import installer_recipe

@unittest.skipUnless(os.environ.get("WARP4_TEST_BWRAP"), "explicit real namespace required")
class StageProductionE2E(unittest.TestCase):
    def test_positive_production_entry_recipe_guard_posthook_and_host_publish(self):
        self.exercise('warp4_ble_installer')

    def test_positive_B_production_guard_runs_full_native_vfs_before_export(self):
        self.exercise('warp4_multi_tesla_installer')

    def exercise(self,name, *, full_generator=False):
        with tempfile.TemporaryDirectory(prefix="warp4-production-e2e-") as td:
            root=Path(td); software=root/"software"; software.mkdir()
            project={p.name:p.read_bytes() for p in Path(".").glob("installer_*.py")}
            for n in ("warp4_installer_guard.py","warp4_ota_only_hook.py","default_16MB_secure.csv"):
                project[n]=Path(n).read_bytes()
            # Test-only build dependency replacement: no production flag or env bypass.
            # The real guard and final posthook remain byte-identical.
            project["platformio.ini"]=b"[platformio]\n[env:warp4_ble_installer]\nplatform=xtensafixture@0.0.1\nextra_scripts=pre:warp4_installer_guard.py, pre:fixture_consume.py, post:warp4_ota_only_hook.py\ncustom_warp4_ota_only=true\n"
            project["platformio.ini"]=project["platformio.ini"].replace(b"warp4_ble_installer",name.encode())
            project['fixture_consume.py']=b"Import('env')\nfrom installer_generators import consume\nconsume(env)\n"
            project['installer_generator_entry.py']=b"""import sys,json,base64
from pathlib import Path
sys.path.insert(0,'/snapshot/project')
from installer_snapshot import read_build_context,canonical
ctx=read_build_context(sys.argv[1])
metadata={'product_id':'warp4','firmware_info_product_id':'warp4','firmware_info_product_name':'WARP4 Charger','version':[0,0,0,0],'build_timestamp':1,'signature_preset':'','secure_boot':'','project_dir':'/work/project','build_dir':'/output/build/'+ctx['environment'],'installer_public_key_sha256':ctx['public_key_sha256']}
files={n:base64.b64encode(b'// synthetic dependency substitution only').decode() for n in ('src/build.h','src/build.cpp','src/index_html.embedded.h','src/index_html.embedded.cpp')}
flags={k:(v if k=='BUILD_FLAGS' else []) for k,v in ctx['effective_flags'][ctx['environment']].items()}
Path('/output/generated.json').write_bytes(canonical({'schema':'warp4-generated-products-v1','snapshot_sha256':ctx['snapshot_sha256'],'build_id':ctx['build_id'],'environment':ctx['environment'],'files':files,'flags':flags,'metadata':metadata,'src_filter':['+<*>']}))
"""
            project["src/main.c"]=b"const unsigned char signature_sodium_public_key_data[32]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31}; void _start(void){for(;;){__asm__ volatile(\"nop\");}}\n"
            project["layout.ld"]=b"ENTRY(_start)\nSECTIONS { . = 0x3f400020; .rodata : { *(.rodata*) } . = 0x40080000; .text : { *(.literal*) *(.text*) } }\n"
            # Lock only the isolated fixture configuration; real protected lock untouched.
            project["installer_env.lock.json"]=json.dumps({name:{"platform":"xtensafixture@0.0.1","extra_scripts":["pre:warp4_installer_guard.py","pre:fixture_consume.py","post:warp4_ota_only_hook.py"],"custom_warp4_ota_only":"true"}}).encode()
            project["installer_secure_layout.h"]=Path("installer_secure_layout.h").read_bytes()
            toolroot=Path(".pio/tesla-sdk/tools/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf")
            tools={"/snapshot/system_tools/sh":Path("/usr/bin/dash"),
                   "/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                   "/snapshot/system_tools/installer-materializer":Path(os.environ["WARP4_TEST_MATERIALIZER"]),
                   "/snapshot/toolchain/gcc":toolroot/"bin/xtensa-esp-elf-gcc",
                   "/snapshot/toolchain/xtensa_esp32.so":toolroot/"lib/xtensa_esp32.so",
                   "/snapshot/toolchain/cc1":toolroot/"libexec/gcc/xtensa-esp-elf/14.2.0/cc1",
                   "/snapshot/toolchain/as":toolroot/"bin/xtensa-esp-elf-as",
                   "/snapshot/toolchain/ar":toolroot/"bin/xtensa-esp-elf-ar",
                   "/snapshot/toolchain/ld":toolroot/"bin/xtensa-esp-elf-ld"}
            inputs,binding=python_inputs(root,project,native_tools=tools,include_pio=True)
            for p,item in inputs.items():
                if p.startswith("/snapshot/python/"): item["role"]="python_environment"
                if p.startswith("/snapshot/system_tools/"): item["role"]="system_tools"
            def add(target,data,role="source"):
                f=root/("extra-"+uuid.uuid4().hex);f.write_bytes(data)
                inputs[target]={"path":str(f),"sha256":hashlib.sha256(data).hexdigest(),"mode":0o444,"role":role}
            prefix="/snapshot/platformio/platforms/xtensafixture/"
            add(prefix+"platform.json",json.dumps({"name":"xtensafixture","title":"Synthetic Xtensa application NOT WARP product","description":"offline test","version":"0.0.1","packages":{k:{"type":"tool","optional":True,"version":"0.0.1"} for k in ("framework-arduinoespressif32","framework-arduinoespressif32-libs","toolchain-xtensa-esp-elf","tool-esptoolpy")}}).encode(),"platform")
            add(prefix+".piopm",json.dumps({"type":"platform","name":"xtensafixture","version":"0.0.1","spec":{"owner":None,"id":None,"name":"xtensafixture","requirements":"0.0.1","uri":None}}).encode(),"platform")
            builder = "\n".join([
                "from SCons.Script import DefaultEnvironment, Default",
                "from pathlib import Path",
                "import json,struct,functools,operator",
                "env=DefaultEnvironment()",
                "env['WARP4_INSTALLER_NOT_FOR_DISTRIBUTION']=False",
                "build=env.subst('$BUILD_DIR'); prog=env.subst('$PROGNAME')",
                "env.Replace(SHELL='/snapshot/system_tools/sh',CC='/snapshot/toolchain/gcc',AR='/snapshot/toolchain/ar',ARFLAGS=['rcsD'],LINK='/snapshot/toolchain/ld',CCFLAGS=[],CFLAGS=['-mdynconfig=/snapshot/toolchain/xtensa_esp32.so','-B/snapshot/toolchain/','-ffreestanding','-nostdinc','-MMD','-MF','${TARGET}.d'],LINKFLAGS=['--dynconfig=/snapshot/toolchain/xtensa_esp32.so','-T','/work/project/layout.ld','-Map','$BUILD_DIR/${PROGNAME}.map'])",
                "obj=env.Object(build+'/main.o','/work/project/src/main.c')",
                "lib=env.StaticLibrary(build+'/libfixture',obj)",
                "elf=env.Program(build+'/'+prog+'.elf',lib)",
                "env.Depends(elf,'/work/project/layout.ld')",
                "def image(target,source,env):",
                " from elftools.elf.elffile import ELFFile",
                " with open(str(source[0]),'rb') as f:",
                "  e=ELFFile(f); sections=[s for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS' and s['sh_size']]",
                "  header=bytearray(24);header[0]=0xe9;header[1]=len(sections);struct.pack_into('<I',header,4,e['e_entry']); data=bytes(header); check=0xef",
                "  for s in sections:",
                "   d=s.data();data+=struct.pack('<II',s['sh_addr'],len(d))+d;check=functools.reduce(operator.xor,d,check)",
                " data+=bytes(15-len(data)%16)+bytes([check]);Path(str(target[0])).write_bytes(data)",
                "bin=env.Command(build+'/'+prog+'.bin',elf,image)",
                "metadata={'product_id':'warp4','firmware_info_product_id':'warp4','firmware_info_product_name':'WARP4 Charger','version':[0,0,0,0],'build_timestamp':1,'signature_preset':'','secure_boot':''}",
                "Path(build).mkdir(parents=True,exist_ok=True)",
                "Default(bin)", ""])
            add(prefix+"builder/main.py",builder.encode(),"platform")
            for p in Path("/home/hermes/.platformio/packages/tool-scons").rglob("*"):
                if p.is_file() and not p.is_symlink() and "__pycache__" not in p.parts:
                    add("/snapshot/platformio/packages/tool-scons/"+p.relative_to("/home/hermes/.platformio/packages/tool-scons").as_posix(),p.read_bytes(),"platformio_core")
            for package in ('framework-arduinoespressif32','framework-arduinoespressif32-libs','toolchain-xtensa-esp-elf','tool-esptoolpy'):
                add('/snapshot/platformio/packages/'+package+'/package.json',json.dumps({'name':package,'version':'0.0.1'}).encode(),'sdk' if package.endswith('-libs') else 'platform')
                add('/snapshot/platformio/packages/'+package+'/.piopm',json.dumps({'type':'tool','name':package,'version':'0.0.1','spec':{'owner':None,'id':None,'name':package,'requirements':'0.0.1','uri':None}}).encode(),'platform')
            add("/snapshot/sdk/fixture.a",b"synthetic SDK identity only, not installable","sdk")
            add("/snapshot/project/signature/key.json",json.dumps({"sodium_public_key":bytes(range(32)).hex()}).encode(),"official_identity")
            if name=='warp4_multi_tesla_installer':
                from stage_vfs_fixture import native_vfs_inputs
                native=root/'native';native.mkdir()
                extra,extra_binding=native_vfs_inputs(native)
                for target,item in extra.items():
                    if target in inputs:
                        self.assertEqual(inputs[target]['sha256'],item['sha256'])
                    else:
                        if target.startswith('/snapshot/project/'):item['role']='source'
                        if target.startswith('/snapshot/python/'):item['role']='python_environment'
                        inputs[target]=item
                binding['executables']=sorted(set(binding['executables'])|set(extra_binding['executables']))
                for kind in ('interpreters','libraries'):
                    binding[kind].update(extra_binding[kind])
                add('/snapshot/project/src/modules/tesla_vehicle/stage_fixture.c',b'int tesla_vehicle_stage(void){return 1;}')
                add('/snapshot/project/lib/TeslaBLEClean/stage_fixture.c',b'int teslableclean_stage(void){return 2;}')
                main=Path(inputs['/snapshot/project/src/main.c']['path']).read_bytes().replace(b'void _start(void){',b'extern int tesla_vehicle_stage(void); extern int teslableclean_stage(void); volatile int result; void _start(void){result=tesla_vehicle_stage()+teslableclean_stage();')
                add('/snapshot/project/src/main.c',main)
                builder=builder.replace('program-not-present','program-not-present')
                builder=builder.replace("elf=env.Program","t=env.Object(build+'/tesla_vehicle.o','/work/project/src/modules/tesla_vehicle/stage_fixture.c')\nbl=env.StaticLibrary(build+'/libteslableclean',env.Object(build+'/teslableclean.o','/work/project/lib/TeslaBLEClean/stage_fixture.c'))\nlib=lib+[t]+bl\nelf=env.Program")
                add(prefix+'builder/main.py',builder.encode(),'platform')
            if full_generator:
                from full_generator_fixture import full_inputs
                native_root=root/'full-native';native_root.mkdir()
                full,full_binding=full_inputs(native_root)
                # Actual local WARP generator/source/Node/Python closure. Only
                # platform/compiler application dependencies remain synthetic.
                keep={p:i for p,i in inputs.items() if not p.startswith(('/snapshot/python/', '/snapshot/project/'))}
                for target,item in keep.items():
                    if target in full:self.assertEqual(item['sha256'],full[target]['sha256'])
                    else:full[target]=item
                tool_executables=[p for p in binding['executables'] if not p.startswith('/snapshot/python/')]
                full_binding['executables']=sorted(set(full_binding['executables'])|set(tool_executables))
                full_binding['interpreters'].update(binding['interpreters'])
                full_binding['libraries'].update(binding['libraries'])
                inputs=full;binding=full_binding
                from platformio.project.config import ProjectConfig
                actual=dict(ProjectConfig.get_instance().items('env:'+name))
                options={k:v for k,v in actual.items() if k.startswith('custom_') or k in ('build_flags','monitor_speed')}
                options.update(platform='xtensafixture@0.0.1',extra_scripts=['pre:warp4_installer_guard.py','pre:pio_hooks.py','post:warp4_ota_only_hook.py'])
                def ini_value(value):
                    if isinstance(value,list):return '\n    '.join(str(v) for v in value)
                    return str(value).replace('\n','\n    ')
                config='[env:'+name+']\n'+''.join(k+' = '+ini_value(v)+'\n' for k,v in options.items())
                add('/snapshot/project/platformio.ini',config.encode())
                add('/snapshot/project/installer_env.lock.json',json.dumps({name:options}).encode())
                add('/snapshot/project/layout.ld',project['layout.ld'])
                main=b'#include "modules/firmware_update/generated/signature_verify.cpp"\nvoid _start(void){for(;;){__asm__ volatile("nop");}}\n'
                add('/snapshot/project/src/main.c',main)
                add('/snapshot/project/signature/config.ini',b'[preset:warp:v1]\npublisher = SYNTHETIC TEST ONLY\nsodium_public_key_path = fixture.json\n')
                add('/snapshot/project/signature/keys/fixture.json',json.dumps({'sodium_public_key':bytes(range(32)).hex()}).encode(),'official_identity')
                builder=builder.replace("Path(build).mkdir(parents=True,exist_ok=True)","Path(build).mkdir(parents=True,exist_ok=True)")
                add(prefix+'builder/main.py',builder.encode(),'platform')
            with Snapshot(inputs) as s: pin=s.identity
            delta={p:i['sha256'] for p,i in inputs.items() if i['role']=='source' and
                   any(t in p.lower() for t in ('tesla_vehicle','teslable','tesla_ble'))}
            common={p:i['sha256'] for p,i in inputs.items() if p not in delta}
            flags={k:[] for k in ("BUILD_FLAGS","CPPDEFINES","CCFLAGS","CFLAGS","CXXFLAGS","LINKFLAGS")}
            # Effective SCons flags are exact approvals of the isolated platform.
            flags["CFLAGS"]=["-mdynconfig=/snapshot/toolchain/xtensa_esp32.so","-B/snapshot/toolchain/","-ffreestanding","-nostdinc","-MMD","-MF","${TARGET}.d"]
            flags['LINKFLAGS']=['--dynconfig=/snapshot/toolchain/xtensa_esp32.so','-T','/work/project/layout.ld','-Map','$BUILD_DIR/${PROGNAME}.map']
            if full_generator:flags['BUILD_FLAGS']=options['build_flags']
            if name=='warp4_multi_tesla_installer':flags['CPPDEFINES']=[['WARP_BLUETOOTH_LITTLEFS_BINDING','0x57415234']]
            # Program identity is host-owned and only affects map output path; the
            # production collector records concrete LINKFLAGS at hook time.
            verified={"snapshot_recipe":{"snapshot_sha256":pin,"elf_binding":binding,"bootstrap":{"bwrap":{"path":os.environ["WARP4_TEST_BWRAP"]}}},
                      "runtime":{"stage_policy":{"schema":"warp4-production-stage-policy-v1","common":common,"tesla":delta}},
                      "effective_flags":{name:flags},"public_key_sha256":hashlib.sha256(bytes(range(32))).hexdigest(),
                      "sdk_manifest_sha256":"a"*64,"sdk_inventory_sha256":"b"*64,"runtime_manifest_sha256":"c"*64,
                      "package_roots":{},"vfs_sources":{},"source_inventory":{},"sdk_snapshot":{},"identity_snapshot":{}}
            if full_generator:
                verified.update(source_head='a'*40,source_metadata={'is_release':False,'is_dirty':True,'git_url':'https://example.invalid/synthetic-test','git_commit_id':'a'*15,'branch_name':'main'})
            # Dependency substitution is confined to this test invocation. No
            # production missing-authenticity path is changed or switched off.
            from installer_generation import StageDAG
            from installer_snapshot import read_context
            import copy
            original_run=StageDAG.run
            rejections=[]
            def checked_run(dag,stage,*args,**kwargs):
                if stage=='posthook' and not full_generator:
                    for case in ('forged-ledger','missing-stage','wrong-parent','wrong-context','changed-build-context','changed-key'):
                        run=copy.deepcopy(read_context(kwargs['context_fd'])['run'])
                        ctx=run['verified']
                        if case=='forged-ledger': ctx['stage_ledger']['records'][0]['stdout_sha256']='f'*64
                        elif case=='missing-stage': ctx['stage_ledger']['records']=[]
                        elif case=='wrong-parent': ctx['stage_ledger']['records'][0]['parents']=['f'*64]
                        elif case=='wrong-context': ctx['stage_authority']['snapshot_sha256']='f'*64
                        elif case=='changed-build-context': ctx['build_context_sha256']='f'*64
                        elif case=='changed-key': ctx['public_key_sha256']='f'*64
                        fd=dag.snapshot.context(run)
                        policy=dict(kwargs,context_fd=fd)
                        try:
                            with self.assertRaises(subprocess.CalledProcessError) as caught:
                                original_run(dag,stage+'-'+case,*args,**policy)
                            rejections.append({'case':case,'exit':caught.exception.returncode,
                                               'stdout':caught.exception.stdout,'stderr':caught.exception.stderr})
                        finally: os.close(fd)
                return original_run(dag,stage,*args,**kwargs)
            with patch.object(StageDAG,'run',checked_run), patch.object(entry,"S",software), patch.object(entry,"preflight",return_value=verified), patch.object(installer_recipe,"assemble_inputs",return_value=inputs), patch.object(installer_recipe,"check_bootstrap"):
                try: self.assertEqual(entry.build(name),0)
                except subprocess.CalledProcessError as e: self.fail(str(e)+"\n"+str(e.stdout)+"\n"+str(e.stderr))
            outputs=list((software/"build/installer"/name).glob("*/firmware-UNSIGNED_ota.bin"))
            self.assertEqual(len(outputs),1,"actual host export never published")
            for required in ('firmware.elf','firmware.map','metadata.json','host-stage-dag.json','linked.json','build.stdout','build.stderr','posthook.stdout','posthook.stderr'):
                self.assertTrue(outputs[0].with_name(required).is_file(),required+' absent from actual host export')
            from installer_ota_verify import verify_container
            self.assertEqual(verify_container(outputs[0].read_bytes())["product_id"],"warp4")
            manifest=json.loads(outputs[0].with_name("manifest.json").read_bytes())
            self.assertIn('stage_ledger',manifest['provenance'])
            self.assertEqual(len(manifest['provenance']['stage_ledger']['records']),2,'generator is not a production stage parent')
            record=manifest['provenance']['stage_ledger']['records'][-1]
            self.assertEqual(record['execution'].get('context_sha256'),
                             manifest['provenance'].get('build_context_sha256'))
            self.assertIsInstance(record['execution'].get('context_sha256'),str)

            evidence=os.environ.get("WARP4_TEST_EVIDENCE_DIR")
            if evidence:
                import shutil
                out=Path(evidence)/('production-full-generator' if full_generator else "production-e2e" if name=="warp4_ble_installer" else "production-e2e-B");out.mkdir(parents=True,exist_ok=True)
                for p in outputs[0].parent.iterdir(): shutil.copyfile(p,out/p.name)
                (out/'rejections.json').write_text(json.dumps(rejections,indent=2))
            self.assertEqual(len(rejections),0 if full_generator else 6)
            if full_generator:
                import base64
                generated=json.loads(outputs[0].with_name('generated.json').read_bytes())
                self.assertGreater(len(generated['files']),1000)
                self.assertGreater(len(base64.b64decode(generated['files']['web/build/index.standalone.html'])),100000)
                self.assertIn(b'firmware_update/generated/signature_verify.cpp',base64.b64decode(next(v for k,v in json.loads(outputs[0].with_name('linked.json').read_bytes())['files'].items() if k.endswith('.d'))))
