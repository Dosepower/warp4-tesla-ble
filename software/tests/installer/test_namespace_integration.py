"""Real namespace/guard/posthook/PIO/native-generator fixtures, NOT WARP firmware.

Explicit locally supplied Bubblewrap/materializer/Python/Node/host compiler bytes
are test inputs only. No production runtime approval or official firmware/key
qualification is issued by these tests. Actual raw trace/MAP/depfiles are kept
only when WARP4_TEST_EVIDENCE_DIR explicitly names a scratch destination.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import installer_namespace as namespace
import installer_snapshot as snapshot


@unittest.skipUnless(os.environ.get('WARP4_TEST_BWRAP'), 'explicit local Bubblewrap test binary required')
class RealNamespaceTests(unittest.TestCase):
    def test_real_python_reads_mounted_context_and_rejects_wrong_file_hash(self):
        import copy
        from namespace_fixture import python_inputs
        software = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = {name: (software / name).read_bytes() for name in
                     ('installer_snapshot.py', 'installer_safeio.py', 'installer_sdk_verify.py')}
            files['platformio.ini'] = b'context reader fixture, not a PIO build'
            files['probe.py'] = b'''import sys\nsys.path.insert(0, '/snapshot/project')\nfrom installer_snapshot import read_mounted_context\nv = read_mounted_context('/run/verified-context.json', 'warp4_ble_installer')\nprint('mounted context verified')\n'''
            inputs, binding = python_inputs(root, files)
            with snapshot.Snapshot(inputs) as snap:
                fd = snap.context({'environment': 'warp4_ble_installer', 'elf_binding': binding})
                try:
                    args = namespace.command(os.environ['WARP4_TEST_BWRAP'], snap, fd, 'warp4_ble_installer')
                    # Execute a real Python context consumer, not a fake PIO API.
                    payload = args.index('--')
                    args[payload+1:] = ['/snapshot/python/bin/python', '-I', '-B', '/snapshot/project/probe.py']
                    try:
                        result = namespace.spawn(args, snap.transport_fds() + (fd,), root)
                    except subprocess.CalledProcessError as exc:
                        self.fail('Real Python namespace: ' + exc.stderr)
                    self.assertEqual(result.stdout.strip(), 'mounted context verified')
                    forged = copy.deepcopy(snapshot.read_context(fd))
                    forged['manifest']['files']['/snapshot/project/platformio.ini']['sha256'] = '0' * 64
                    forged['snapshot_sha256'] = hashlib.sha256(snapshot.canonical(forged['manifest'])).hexdigest()
                    altered = snapshot.sealed(snapshot.canonical(forged))
                    try:
                        # Deliberately substitute a self-consistent but wrong
                        # context mount to challenge the actual consumer.
                        bad_args = list(args)
                        at = bad_args.index('/run/verified-context.json')
                        bad_args[at-1] = str(altered)
                        with self.assertRaises(subprocess.CalledProcessError) as raised:
                            namespace.spawn(bad_args, snap.transport_fds() + (altered,), root)
                        self.assertIn('CONTEXT_FILE_DIGEST_MISMATCH', raised.exception.stderr)
                    finally:
                        os.close(altered)
                finally:
                    os.close(fd)

    def test_actual_supervisor_python_layout_is_materialized_and_foreign_prefix_rejected(self):
        from namespace_fixture import python_inputs
        software=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            names=("installer_runner.py","installer_output.py","installer_snapshot.py","installer_safeio.py",
                   "installer_sdk_verify.py","installer_policy.py")
            files={name:(software/name).read_bytes() for name in names}
            files["platformio.ini"]=b"Python relocation fixture, not WARP firmware"
            files["probe.py"]=b"""import sys
sys.path.insert(0,"/snapshot/project")
from installer_snapshot import read_build_context
from installer_runner import verify_python_runtime
context=read_build_context("warp4_ble_installer")
assert verify_python_runtime(context)
sys.path.append("/work/foreign-import")
try: verify_python_runtime(context)
except ValueError as exc: assert "PYTHON_RUNTIME_FOREIGN_MODULE" in str(exc),str(exc)
else: raise AssertionError("foreign search path accepted")
sys.path.pop()
sys.prefix="/usr"
try: verify_python_runtime(context)
except ValueError as exc: assert "PYTHON_RUNTIME_PREFIX" in str(exc),str(exc)
else: raise AssertionError("ambient interpreter layout accepted")
print("actual Python materialization and rejection passed")
"""
            inputs,binding=python_inputs(root,files)
            for item in inputs.values():
                if item["role"]=="python": item["role"]="python_environment"
            with snapshot.Snapshot(inputs) as snap:
                verified={"environment":"warp4_ble_installer","build_id":"a"*32,"start_ns":1,"package_roots":{},"effective_flags":{}}
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"verified":verified})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    args[args.index("--")+1:]=["/snapshot/python/bin/python","-I","-B","/snapshot/project/probe.py"]
                    try: result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc: self.fail(exc.stdout+exc.stderr)
                    self.assertIn("actual Python materialization",result.stdout)
                finally: os.close(fd)

    def test_actual_posthook_revalidates_mount_and_rejects_missing_stage_phase(self):
        from namespace_fixture import python_inputs
        software=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            native=root/"native.elf"; native_map=root/"native.map"
            subprocess.run(["/usr/bin/cc","-x","c","-","-nostdlib","-static","-Wl,-Map,"+str(native_map),"-o",str(native)],
                           input=b"void _start(void) { for (;;) {} }",capture_output=True,check=True)
            names=("installer_snapshot.py","installer_safeio.py","installer_sdk_verify.py","installer_ota.py",
                   "installer_ota_verify.py","installer_identity.py","installer_link_verify.py","installer_policy.py",
                   "warp4_ota_only_hook.py")
            files={name:(software/name).read_bytes() for name in names}
            files["platformio.ini"]=b"posthook fixture only"
            files["native.map"]=native_map.read_bytes()
            files["probe.py"]=b"""import sys,runpy,json
sys.path.insert(0,"/snapshot/project")
from pathlib import Path
from installer_snapshot import read_build_context
context=read_build_context("warp4_ble_installer")
build=Path("/output/build/fixture"); build.mkdir(parents=True)
(build/"metadata.json").write_text(json.dumps({"product_id":"warp4","signature_preset":"","secure_boot":""}))
(build/"fixture.map").write_bytes(Path("/snapshot/project/native.map").read_bytes())
class Env(dict):
 def subst(self,value): return value.replace("$BUILD_DIR",str(build)).replace("${PROGNAME}","fixture")
 def AddPostAction(self,*args): pass
def invoke(verified):
 e=Env(WARP4_INSTALLER_VERIFIED=verified,WARP4_INSTALLER_NOT_FOR_DISTRIBUTION=False)
 scope=runpy.run_path("/snapshot/project/warp4_ota_only_hook.py",init_globals={"Import":lambda name:None,"env":e})
 scope["emit_ota"]([],[],e)
try: invoke(context | {"build_id":"c"*32})
except ValueError as exc: assert "GUARD_CONTEXT_MISMATCH" in str(exc),str(exc)
else: raise AssertionError("forged guard accepted")
try: invoke(context)
except ValueError as exc: assert "PRODUCTION_STAGE_PHASE" in str(exc),str(exc)
else: raise AssertionError("missing capture accepted")
assert not Path("/output/candidates").exists()
print("actual posthook mounted context and fail-closed capture gate passed")
"""
            inputs,binding=python_inputs(root,files)
            with snapshot.Snapshot(inputs) as snap:
                verified={"environment":"warp4_ble_installer","build_id":"a"*32,"start_ns":1,"package_roots":{},
                          "effective_flags":{"warp4_ble_installer":{k:[] for k in ("BUILD_FLAGS","CPPDEFINES","CCFLAGS","CFLAGS","CXXFLAGS","LINKFLAGS")}}}
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"verified":verified})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    args[args.index("--")+1:]=["/snapshot/python/bin/python","-I","-B","/snapshot/project/probe.py"]
                    try: result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc: self.fail(exc.stdout+exc.stderr)
                    self.assertIn("actual posthook mounted context",result.stdout)
                finally: os.close(fd)

    def test_real_materializer_rejects_digest_paths_modes_and_duplicates(self):
        import struct
        helper=Path(os.environ.get("WARP4_TEST_MATERIALIZER",str(Path(os.environ["WARP4_TEST_BWRAP"]).parents[3]/"installer-materializer")))
        def record(path,mode=0o444,data=b"fixture"):
            encoded=path.encode()
            return struct.pack("<IIQ",len(encoded),mode,len(data))+encoded+data
        cases=[(b"W4B1"+struct.pack("<I",0),"bundle file count"),
               (b"W4B1"+struct.pack("<I",1)+record("snapshot/../escape"),"unsafe target"),
               (b"W4B1"+struct.pack("<I",1)+record("snapshot/file",0o666),"record schema"),
               (b"W4B1"+struct.pack("<I",2)+record("args")+record("args"),"exclusive file"),
               (b"W4B1"+struct.pack("<I",1)+record("snapshot/file")[:-1],"short bundle")]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            for data,message in cases+[(b"bad","BUNDLE_DIGEST_MISMATCH")]:
                tool=snapshot.sealed(helper.read_bytes())
                bundle=snapshot.sealed(data if message!="BUNDLE_DIGEST_MISMATCH" else cases[0][0])
                digest=hashlib.sha256(data).hexdigest()
                try:
                    args=[os.environ["WARP4_TEST_BWRAP"],"--unshare-all","--cap-drop","ALL","--tmpfs","/","--tmpfs","/scratch","--dir","/run",
                          "--perms","555","--ro-bind-data",str(tool),"/run/materializer",
                          "--perms","444","--ro-bind-data",str(bundle),"/run/snapshot.bundle",
                          "--remount-ro","/","--","/run/materializer",digest]
                    with self.assertRaises(subprocess.CalledProcessError) as raised:
                        namespace.spawn(args,(tool,bundle),root)
                    self.assertIn(message,raised.exception.stderr)
                    self.assertFalse((root/"escape").exists())
                finally:
                    os.close(tool); os.close(bundle)

    def test_real_node_typescript_from_readonly_dependencies_to_private_output(self):
        from namespace_fixture import python_inputs
        software=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            names=("installer_generators.py","installer_snapshot.py","installer_safeio.py",
                   "installer_sdk_verify.py","installer_policy.py","installer_python_entry.py")
            files={name:(software/name).read_bytes() for name in names}
            files["__init__.py"]=b""  # Actual software package exists in every full production snapshot.
            files["platformio.ini"]=b"native Node generator fixture, NOT WARP firmware"
            files["web/src/input.ts"]=b"export const answer: number = 42;\n"
            files["sibling.py"]=b"CONTENT = b'fresh private Python generator output'\n"
            files["prepare.py"]=b"""from pathlib import Path
from sibling import CONTENT
Path("generated").mkdir()
Path("generated/fixture.hpp").write_bytes(CONTENT)
"""

            files["probe.py"]=b"""import sys
sys.path.insert(0,"/snapshot/project")
from installer_snapshot import read_build_context
from installer_generators import run
from pathlib import Path
context=read_build_context("warp4_ble_installer")
run(["uv","run","prepare.py"],"warp4_ble_installer",context)
assert Path("generated/fixture.hpp").read_bytes() == b"fresh private Python generator output"
run(["uv","run","npx","tsc","--target","es2020","--module","es2020","--skipLibCheck","--outDir","/output/web","web/src/input.ts"],"warp4_ble_installer",context)
result=Path("/output/web/input.js").read_text()
assert "export const answer = 42;" in result,result
assert not Path("/snapshot/project/web/src/input.js").exists()
print("actual Node/TypeScript private generation passed")
"""
            files["installer_runner.py"]=files.pop("probe.py")
            tools={"/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                   "/snapshot/system_tools/installer-materializer":Path(os.environ.get("WARP4_TEST_MATERIALIZER",str(Path(os.environ["WARP4_TEST_BWRAP"]).parents[3]/"installer-materializer"))),
                   "/snapshot/node_environment/bin/node":software/".venv/bin/node",
                   "/snapshot/project/web/node_modules/@typescript/typescript-linux-x64/lib/tsc":software/"web/node_modules/@typescript/typescript-linux-x64/lib/tsc"}
            inputs,binding=python_inputs(root,files,native_tools=tools)
            for target in ("/snapshot/system_tools/bwrap","/snapshot/system_tools/installer-materializer"):
                inputs[target]["role"]="system_tools"
            inputs["/snapshot/python/bin/python"]["role"]="python_environment"
            inputs["/snapshot/node_environment/bin/node"]["role"]="node_environment"
            package_root=software/"web/node_modules"
            for package in ("typescript","@typescript/typescript-linux-x64"):
                for path in sorted((package_root/package).rglob("*")):
                    if not path.is_file() or path.is_symlink(): continue
                    target="/snapshot/project/web/node_modules/"+path.relative_to(package_root).as_posix()
                    if target in inputs:
                        inputs[target]["role"]="web_dependencies"
                        continue
                    copy=root/("typescript-"+str(len(inputs))); copy.write_bytes(path.read_bytes())
                    inputs[target]={"path":str(copy),"sha256":hashlib.sha256(copy.read_bytes()).hexdigest(),"mode":0o444,"role":"web_dependencies"}
            with snapshot.Snapshot(inputs) as snap:
                verified={"environment":"warp4_ble_installer","build_id":"a"*32,"start_ns":1,"package_roots":{},"effective_flags":{}}
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"private_workspace":True,"supervised":True,"verified":verified})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    try: result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc: self.fail(exc.stdout+exc.stderr)
                    self.assertIn("actual Node/TypeScript private generation passed",result.stdout)
                finally: os.close(fd)

    def test_real_platformio_scons_native_cpp_fixture_offline(self):
        # Actual PIO/SCons/GNU compiler processes, not a firmware qualification.
        from namespace_fixture import python_inputs
        import json
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            files={"platformio.ini":b"[env:warp4_ble_installer]\nplatform = nativefixture@0.0.1\n"}
            files["src/main.cpp"]=b"#include \"generated.hpp\"\nextern \"C\" { int fixture_value = FIXTURE_VALUE; void _start() { for (;;) {} } }\n"
            gcc=Path("/usr/lib/gcc/x86_64-linux-gnu")
            version=sorted(gcc.iterdir())[-1]
            tools={"/snapshot/system_tools/sh":Path("/usr/bin/dash"),
                   "/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                   "/snapshot/system_tools/installer-materializer":Path(os.environ.get("WARP4_TEST_MATERIALIZER",str(Path(os.environ["WARP4_TEST_BWRAP"]).parents[3]/"installer-materializer"))),
                   "/snapshot/compiler/g++":Path("/usr/bin/g++").resolve(),
                   "/snapshot/compiler/cc1plus":Path(subprocess.check_output(["/usr/bin/g++","-print-prog-name=cc1plus"],text=True).strip()),
                   "/snapshot/compiler/liblto_plugin.so":Path(subprocess.check_output(["/usr/bin/g++","-print-file-name=liblto_plugin.so"],text=True).strip()),
                   "/snapshot/compiler/collect2":Path(subprocess.check_output(["/usr/bin/g++","-print-prog-name=collect2"],text=True).strip()),
                   "/snapshot/compiler/as":Path("/usr/bin/as").resolve(),
                   "/snapshot/compiler/ld":Path("/usr/bin/ld").resolve()}
            inputs,binding=python_inputs(root,files,native_tools=tools,include_pio=True)
            for t in ("/snapshot/system_tools/bwrap","/snapshot/system_tools/installer-materializer","/snapshot/system_tools/sh"):
                inputs[t]["role"]="system_tools"
            def add(target,data):
                path=root/("extra-"+str(len(inputs))); path.write_bytes(data)
                inputs[target]={"path":str(path),"sha256":hashlib.sha256(data).hexdigest(),"mode":0o444,"role":"native_pio_fixture"}
            platform="/snapshot/platformio/platforms/nativefixture/"
            add(platform+"platform.json",json.dumps({"name":"nativefixture","title":"Explicit native test fixture, NOT WARP4","description":"Offline integration fixture","version":"0.0.1","packages":{}}).encode())
            add(platform+".piopm",json.dumps({"type":"platform","name":"nativefixture","version":"0.0.1","spec":{"owner":None,"id":None,"name":"nativefixture","requirements":"0.0.1","uri":None}}).encode())
            add(platform+"builder/main.py",b"""from SCons.Script import DefaultEnvironment, Default
env=DefaultEnvironment()
env.Replace(SHELL="/snapshot/system_tools/sh",CXX="/snapshot/compiler/g++",LINK="/snapshot/compiler/g++",CXXFLAGS=["-B/snapshot/compiler/","-ffreestanding","-fno-exceptions","-fno-rtti","-MMD","-MP","-MF","/output/build/native-fixture.d"],LINKFLAGS=["-B/snapshot/compiler/","-nostdlib","-static","-Wl,-e,_start,-Map,/output/build/native-fixture.map"])
from pathlib import Path
import hashlib,json,base64
header=Path("/work/project/src/generated.hpp")
assert not header.exists()
header.write_bytes(b"#define FIXTURE_VALUE 7\\n")
header_hash=hashlib.sha256(header.read_bytes()).hexdigest()
program=env.Program("/output/build/native-fixture.elf","/work/project/src/main.cpp")
def capture(source,target,env):
 data={"map":base64.b64encode(Path("/output/build/native-fixture.map").read_bytes()).decode(),
       "depfile":base64.b64encode(Path("/output/build/native-fixture.d").read_bytes()).decode(),
       "elf_sha256":hashlib.sha256(Path("/output/build/native-fixture.elf").read_bytes()).hexdigest(),
       "observed_generated_header_sha256":header_hash}
 print("WARP4_NATIVE_EVIDENCE "+base64.b64encode(json.dumps(data).encode()).decode())
env.AddPostAction(program,capture)
Default(program)
""")
            scons=Path("/home/hermes/.platformio/packages/tool-scons")
            for path in sorted(scons.rglob("*")):
                if path.is_file() and not path.is_symlink() and "__pycache__" not in path.parts:
                    add("/snapshot/platformio/packages/tool-scons/"+path.relative_to(scons).as_posix(),path.read_bytes())
            with snapshot.Snapshot(inputs) as snap:
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"private_workspace":True})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    try:
                        tracer=Path(os.environ["WARP4_TEST_BWRAP"]).with_name("strace")
                        result=namespace.spawn(args,snap.transport_fds()+(fd,),root,tracer=str(tracer),trace_path=root/"actual-native.trace")
                    except subprocess.CalledProcessError as exc:
                        self.fail("Actual PIO native fixture: "+exc.stdout+exc.stderr)
                    trace=(root/"actual-native.trace").read_text()
                    for tool in ("cc1plus","collect2","/snapshot/compiler/as","/snapshot/compiler/ld","liblto_plugin.so","main.cpp"):
                        self.assertIn(tool,trace)
                    self.assertIn("execve(",trace)
                    self.assertIn("openat(",trace)
                    import base64
                    marker=next(line for line in result.stdout.splitlines() if line.startswith("WARP4_NATIVE_EVIDENCE "))
                    generated=json.loads(base64.b64decode(marker.split(" ",1)[1]))
                    native_map=base64.b64decode(generated["map"])
                    native_dep=base64.b64decode(generated["depfile"])
                    from installer_graph import depfile
                    parsed=depfile(native_dep)
                    self.assertTrue(any("src/generated.hpp" in item for items in parsed.values() for item in items))
                    self.assertIn(b"Linker script and memory map",native_map)
                    if os.environ.get("WARP4_TEST_EVIDENCE_DIR"):
                        evidence=Path(os.environ["WARP4_TEST_EVIDENCE_DIR"])
                        evidence.mkdir(parents=True,exist_ok=True)
                        (evidence/"native-pio.stdout").write_text(result.stdout)
                        (evidence/"native-pio.stderr").write_text(result.stderr)
                        (evidence/"native-pio.trace").write_text(trace)
                        (evidence/"native-fixture.map").write_bytes(native_map)
                        (evidence/"native-fixture.d").write_bytes(native_dep)
                        (evidence/"native-observed-hashes.json").write_text(json.dumps({k:v for k,v in generated.items() if k not in ("map","depfile")},indent=2))
                    self.assertIn("SUCCESS",result.stdout)
                    self.assertIn("native-fixture.elf",result.stdout)
                    self.assertFalse((root/"native-fixture.elf").exists())
                finally: os.close(fd)

    def test_private_output_export_is_run_bound_and_digest_checked(self):
        import installer_output
        from namespace_fixture import python_inputs
        software=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            files={name:(software/name).read_bytes() for name in
                   ("installer_output.py","installer_safeio.py","installer_sdk_verify.py")}
            files["platformio.ini"]=b"output transport fixture, no firmware"
            files["probe.py"]=b"""from pathlib import Path
import sys
sys.path.insert(0,"/snapshot/project")
from installer_output import encode_output
p=Path("/output/result"); p.mkdir()
(p/"evidence.txt").write_bytes(b"real private namespace output")
print(encode_output(p,["evidence.txt"],"a"*64,"b"*32))
"""
            inputs,binding=python_inputs(root,files)
            with snapshot.Snapshot(inputs) as snap:
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    args[args.index("--")+1:]=["/snapshot/python/bin/python","-I","-B","/snapshot/project/probe.py"]
                    try:
                        result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc:
                        self.fail(exc.stderr)
                    exported=installer_output.decode_output(result.stdout,["evidence.txt"],"a"*64,"b"*32)
                    self.assertEqual(exported,{"evidence.txt":b"real private namespace output"})
                    with self.assertRaisesRegex(ValueError,"OUTPUT_RUN_MISMATCH"):
                        installer_output.decode_output(result.stdout,["evidence.txt"],"a"*64,"c"*32)
                    import json
                    altered=json.loads(result.stdout)
                    altered["files"]["evidence.txt"]["sha256"]="0"*64
                    with self.assertRaisesRegex(ValueError,"OUTPUT_DIGEST_MISMATCH"):
                        installer_output.decode_output(json.dumps(altered),["evidence.txt"],"a"*64,"b"*32)
                    self.assertFalse((root/"result").exists())
                finally: os.close(fd)

    def test_private_workspace_keeps_source_readonly_and_generators_fresh(self):
        from namespace_fixture import python_inputs
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = {"platformio.ini": b"workspace fixture", "src/original.cpp": b"immutable source"}
            files["probe.py"] = b"""from pathlib import Path
p=Path("/work/project")
assert Path.cwd() == p
assert (p/"src/original.cpp").read_bytes() == b"immutable source"
try: (p/"src/original.cpp").write_bytes(b"injected")
except OSError: pass
else: raise AssertionError("source writable")
assert not (p/"src/generated.cpp").exists()
(p/"src/generated.cpp").write_bytes(b"fresh fixture generation")
(p/"web/generated").mkdir(parents=True)
(p/"web/generated/fixture.ts").write_bytes(b"fresh web fixture")
assert not Path("/snapshot/project/src/generated.cpp").exists()
print("fresh private C++/web workspace passed")
"""
            inputs, binding = python_inputs(root, files)
            with snapshot.Snapshot(inputs) as snap:
                fd=snap.context({"environment":"warp4_ble_installer", "elf_binding":binding, "private_workspace":True})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    args[args.index("--")+1:]=["/snapshot/python/bin/python","-I","-B","/work/project/probe.py"]
                    try:
                        result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc:
                        self.fail(exc.stderr)
                    self.assertIn("fresh private C++/web workspace passed", result.stdout)
                    self.assertFalse((root/"src/generated.cpp").exists())
                finally:
                    os.close(fd)

    def test_actual_guard_reads_context_and_preserves_launcher_run_identity(self):
        from namespace_fixture import python_inputs
        software = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = {name: (software / name).read_bytes() for name in
                ("installer_snapshot.py", "installer_safeio.py", "installer_sdk_verify.py",
                 "installer_policy.py", "installer_env.lock.json", "warp4_installer_guard.py")}
            files["platformio.ini"] = b"guard fixture only"
            files["probe.py"] = b"""import sys, types, json, runpy
sys.path.insert(0, "/snapshot/project")
script=types.ModuleType("SCons.Script"); script.COMMAND_LINE_TARGETS=[]
sys.modules["SCons"]=types.ModuleType("SCons"); sys.modules["SCons.Script"]=script
options=json.load(open("/snapshot/project/installer_env.lock.json"))["warp4_ble_installer"]
class Env(dict):
 def subst(self, value): return "/snapshot/project"
 def GetProjectConfig(self): return self
 def items(self, section): return options.items()
 def GetProjectOption(self, key): return options[key]
 def Replace(self, **kwargs): self.update(kwargs)
 def Append(self, **kwargs): pass
 def AddBuildMiddleware(self, function): pass
e=Env(PIOENV="warp4_ble_installer")
runpy.run_path("/snapshot/project/warp4_installer_guard.py", init_globals={"env":e,"Import":lambda name:None})
assert e["WARP4_INSTALLER_VERIFIED"]["build_id"] == "a"*32
assert e["WARP4_INSTALLER_VERIFIED"]["start_ns"] == 1
assert e["PROGNAME"] == "warp4-ota-candidate-"+"a"*32
print("actual guard readonly context passed; SCons API fixture only")
"""
            inputs, binding = python_inputs(root, files)
            with snapshot.Snapshot(inputs) as snap:
                verified = {"environment": "warp4_ble_installer", "build_id": "a"*32,
                            "start_ns": 1, "package_roots": {}, "effective_flags": {}}
                fd = snap.context({"environment": "warp4_ble_installer", "elf_binding": binding, "verified": verified})
                try:
                    args = namespace.command(os.environ["WARP4_TEST_BWRAP"], snap, fd, "warp4_ble_installer")
                    args[args.index("--")+1:] = ["/snapshot/python/bin/python", "-I", "-B", "/snapshot/project/probe.py"]
                    result = namespace.spawn(args, snap.transport_fds() + (fd,), root)
                    self.assertIn("actual guard readonly context passed", result.stdout)
                finally:
                    os.close(fd)

    def test_elf_binding_cannot_omit_executable_or_dlopen_runtime(self):
        from namespace_fixture import python_inputs
        with tempfile.TemporaryDirectory() as td:
            inputs,binding=python_inputs(Path(td),{"platformio.ini":b"ELF completeness fixture"},
                                         native_tools={"/snapshot/tool":Path("/usr/bin/true")})
            with snapshot.Snapshot(inputs) as snap:
                incomplete=binding | {"executables":[p for p in binding["executables"] if p!="/snapshot/tool"]}
                with self.assertRaisesRegex(ValueError,"UNBOUND_ELF_FILE"):
                    snapshot.verify_elf_closure(snap,incomplete)
                snapshot.verify_elf_closure(snap,binding)

    def test_dynamic_payload_uses_only_bound_snapshot_loader_libraries(self):
        # Actual host ELF bytes, not a PIO/Firmware integration fixture.
        from elftools.elf.elffile import ELFFile
        import io
        executable = Path('/usr/bin/true')
        elf = ELFFile(io.BytesIO(executable.read_bytes()))
        interpreter = next(seg.get_interp_name() for seg in elf.iter_segments() if seg['p_type'] == 'PT_INTERP')
        libc = Path('/usr/lib/x86_64-linux-gnu/libc.so.6')
        loader = Path(interpreter).resolve()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            config = root / 'config'; config.write_bytes(b'namespace-fixture')
            originals = {'/snapshot/python/bin/python': (executable, 'test_probe'),
                         '/snapshot/project/platformio.ini': (config, 'source'),
                         '/snapshot/loader_runtime/libc.so.6': (libc, 'loader_runtime'),
                         '/snapshot/loader_runtime/ld-linux-x86-64.so.2': (loader, 'loader_runtime')}
            # Safeio intentionally forbids aliases in host paths; materialize test
            # bytes into regular scratch files, never endorse their host paths.
            inputs = {}
            for index, (target, (path, role)) in enumerate(originals.items()):
                copy = root / str(index); copy.write_bytes(path.read_bytes())
                inputs[target] = {'path': str(copy), 'sha256': hashlib.sha256(copy.read_bytes()).hexdigest(),
                                  'mode': 0o444 if role == 'source' else 0o555, 'role': role}
            binding = {'executables': ['/snapshot/python/bin/python'],
                       'interpreters': {interpreter: '/snapshot/loader_runtime/ld-linux-x86-64.so.2'},
                       'libraries': {'libc.so.6': '/snapshot/loader_runtime/libc.so.6',
                                     'ld-linux-x86-64.so.2': '/snapshot/loader_runtime/ld-linux-x86-64.so.2'}}
            with snapshot.Snapshot(inputs) as snap:
                fd = snap.context({'environment': 'warp4_ble_installer', 'elf_binding': binding})
                try:
                    args = namespace.command(os.environ['WARP4_TEST_BWRAP'], snap, fd, 'warp4_ble_installer')
                    try:
                        result = namespace.spawn(args, snap.transport_fds() + (fd,), root)
                    except subprocess.CalledProcessError as exc:
                        self.fail('Actual Bubblewrap execution failed: ' + exc.stderr)
                    self.assertEqual(result.returncode, 0)
                finally:
                    os.close(fd)
                # Even unused declarations affect LD_LIBRARY_PATH and must be
                # validated, not only libraries found in DT_NEEDED traversal.
                bad = binding | {'libraries': binding['libraries'] | {'unused.so': '/host/injected.so'}}
                fd = snap.context({'environment': 'warp4_ble_installer', 'elf_binding': bad})
                try:
                    with self.assertRaisesRegex(ValueError, 'FOREIGN_'):
                        namespace.command(os.environ['WARP4_TEST_BWRAP'], snap, fd, 'warp4_ble_installer')
                finally:
                    os.close(fd)

    def test_actual_namespace_mounts_fd_cut_and_installer_environment(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / 'probe.c'
            source.write_text(r'''
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  const char *marker = getenv("WARP4_INSTALLER_OFFLINE_SANDBOX");
  if (!marker || strcmp(marker, "1")) return 21;
  if (getenv("BASH_ENV") || getenv("SSH_AUTH_SOCK")) return 22;
  int fd = open("/root-injected", O_WRONLY|O_CREAT|O_EXCL, 0600);
  if (fd >= 0) return 32;
  fd = open("/run/verified-context.json", O_WRONLY);
  if (fd >= 0) return 33;
  fd = open("/snapshot/project/platformio.ini", O_WRONLY);
  if (fd >= 0) return 23;
  if (errno != EROFS && errno != EACCES) return 24;
  fd = open("/run/verified-context.json", O_RDONLY);
  if (fd < 0) return 25;
  char buf[4096]; int n = read(fd, buf, sizeof(buf)-1); close(fd);
  if (n <= 0) return 26;
  buf[n] = 0;
  if (!strstr(buf, "warp4-sealed-run-v1")) return 27;
  for (int i=3; i<4096; ++i) if (fcntl(i, F_GETFD) != -1) return 28;
  fd = open("/output/probe", O_WRONLY|O_CREAT|O_EXCL, 0600);
  if (fd < 0) return 29;
  if (write(fd, "private-output", 14) != 14) return 30;
  close(fd);
  if (access("/etc/passwd", F_OK) == 0) return 31;
  puts("native namespace isolation checks passed");
  return 0;
}
''')
            executable = root / 'probe'
            subprocess.run(['/usr/bin/cc', '-static', '-O2', str(source), '-o', str(executable)], check=True, capture_output=True)
            data = executable.read_bytes()
            config = root / 'config'; config.write_bytes(b'namespace-fixture')
            inputs = {
                '/snapshot/python/bin/python': {'path': str(executable), 'sha256': hashlib.sha256(data).hexdigest(), 'mode': 0o555, 'role': 'test_probe'},
                '/snapshot/project/platformio.ini': {'path': str(config), 'sha256': hashlib.sha256(config.read_bytes()).hexdigest(), 'mode': 0o444, 'role': 'source'},
            }
            with snapshot.Snapshot(inputs) as snap:
                context = snap.context({'environment': 'warp4_ble_installer'})
                leak = os.memfd_create('host-fd-must-not-leak', 0)
                os.set_inheritable(leak, True)
                try:
                    args = namespace.command(os.environ['WARP4_TEST_BWRAP'], snap, context, 'warp4_ble_installer')
                    result = namespace.spawn(args, snap.transport_fds() + (context,), root)
                    self.assertEqual(result.stdout.strip(), 'native namespace isolation checks passed')
                    self.assertFalse((root / 'probe-output').exists())
                finally:
                    os.close(leak)
                    os.close(context)
