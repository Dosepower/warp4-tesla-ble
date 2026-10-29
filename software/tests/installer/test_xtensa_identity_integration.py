"""Real local compiler/elf2image fixture. NOT firmware or official-key approval."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from installer_identity import verify_elf_key, verify_app_key
from installer_ota_verify import verify_app

SOFTWARE=Path(__file__).resolve().parents[2]
DEFAULT_CC=SOFTWARE/".pio/tesla-sdk/tools/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc"
CC=Path(os.environ.get("WARP4_TEST_XTENSA_CC",str(DEFAULT_CC)))
ESPTOOL=Path(os.environ.get("WARP4_TEST_ESPTOOL","/home/hermes/.platformio/packages/tool-esptoolpy/esptool.py"))

@unittest.skipUnless(CC.is_file() and ESPTOOL.is_file(),"explicit local compiler/esptool fixture required")
class XtensaIdentityIntegration(unittest.TestCase):
    def test_real_linked_readonly_key_survives_offline_elf2image_and_wrong_key_rejected(self):
        # Not an Ed25519 identity, not a Tinkerforge key; no approval is issued.
        key=bytes(range(32))
        pin=hashlib.sha256(key).hexdigest()
        with tempfile.TemporaryDirectory(prefix="warp4-key-fixture-") as td:
            root=Path(td)
            source=root/"key.c"
            source.write_text("const unsigned char signature_sodium_public_key_data[32] = {"+",".join(map(str,key))+"};\nvoid _start(void) { for (;;) {} }\n")
            script=root/"fixture.ld"
            script.write_text("ENTRY(_start) SECTIONS { .rodata 0x3f400020 : { *(.rodata*) } .text 0x400d0020 : { *(.text*) } }\n")
            elf=root/"fixture.elf"
            app=root/"fixture-app.bin"
            compile_run=subprocess.run([str(CC),"-nostdlib","-ffreestanding","-Wl,--build-id=none","-T",str(script),str(source),"-o",str(elf)],text=True,capture_output=True)
            self.assertEqual(compile_run.returncode,0,compile_run.stdout+compile_run.stderr)
            address,linked=verify_elf_key(elf.read_bytes(),pin)
            self.assertEqual(linked,key)
            convert=subprocess.run([sys.executable,"-B",str(ESPTOOL),"--chip","esp32","elf2image","--output",str(app),str(elf)],text=True,capture_output=True)
            self.assertEqual(convert.returncode,0,convert.stdout+convert.stderr)
            verify_app(app.read_bytes())
            self.assertTrue(verify_app_key(app.read_bytes(),address,linked,pin))
            with self.assertRaisesRegex(ValueError,"fingerprint mismatch"):
                verify_elf_key(elf.read_bytes(),"f"*64)
            other=bytes(reversed(key))
            with self.assertRaisesRegex(ValueError,"does not match"):
                verify_app_key(app.read_bytes(),address,other,hashlib.sha256(other).hexdigest())
            source.write_text(source.read_text().replace("const unsigned","unsigned"))
            # Force the same symbol into an allocated WRITE section, not readonly.
            script.write_text("ENTRY(_start) SECTIONS { .data 0x3ffb0000 : { *(.data*) } .text 0x400d0020 : { *(.text*) } }\n")
            writable=subprocess.run([str(CC),"-nostdlib","-ffreestanding","-Wl,--build-id=none","-T",str(script),str(source),"-o",str(elf)],text=True,capture_output=True)
            self.assertEqual(writable.returncode,0,writable.stdout+writable.stderr)
            with self.assertRaisesRegex(ValueError,"allocated readonly"):
                verify_elf_key(elf.read_bytes(),pin)
            print("Real Xtensa ELF/offline esptool app-key fixture passed; SYNTHETIC public bytes only")

    @unittest.skipUnless(os.environ.get("WARP4_TEST_BWRAP"),"explicit local Bubblewrap required")
    def test_actual_xtensa_compiler_from_snapshot_exports_real_elf_with_fixture_key(self):
        from namespace_fixture import python_inputs
        import installer_snapshot as snapshot
        import installer_namespace as namespace
        import base64,json
        key=bytes(range(32))
        compiler_root=CC.parent.parent
        version=compiler_root/"libexec/gcc/xtensa-esp-elf/14.2.0"
        with tempfile.TemporaryDirectory(prefix="warp4-xtensa-namespace-") as td:
            root=Path(td)
            files={name:(SOFTWARE/name).read_bytes() for name in ("installer_snapshot.py","installer_sdk_verify.py","installer_safeio.py")}
            files["platformio.ini"]=b"cross compiler fixture only, NOT WARP firmware"
            files["test-public.bin"]=key
            files["fixture.ld"]=b"ENTRY(_start) SECTIONS { .rodata 0x3f400020 : { *(.rodata*) } .text 0x400d0020 : { *(.text*) } }\n"
            files["probe.py"]=b'''import sys,base64,json,subprocess,os,hashlib
sys.path.insert(0,"/snapshot/project")
from pathlib import Path
from installer_snapshot import read_mounted_context
read_mounted_context("/run/verified-context.json","warp4_ble_installer")
key=Path("/snapshot/project/test-public.bin").read_bytes()
source=Path("/output/key.c")
assert not source.exists()
source.write_text("const unsigned char signature_sodium_public_key_data[32]={"+",".join(map(str,key))+"};\\nvoid _start(void) { for (;;) {} }\\n")
env=dict(os.environ,GCC_EXEC_PREFIX="/snapshot/compiler/libexec/gcc/",COMPILER_PATH="/snapshot/compiler/bin:/snapshot/compiler/libexec/gcc/xtensa-esp-elf/14.2.0")
subprocess.run(["/snapshot/compiler/bin/xtensa-esp32-elf-gcc","-B/snapshot/compiler/bin/","-nostdlib","-ffreestanding","-Wl,--build-id=none,-Map,/output/fixture.map","-T","/snapshot/project/fixture.ld",str(source),"-o","/output/fixture.elf"],env=env,stdout=sys.stderr,stderr=sys.stderr,close_fds=True,check=True)
print(json.dumps({"elf":base64.b64encode(Path("/output/fixture.elf").read_bytes()).decode(),"map":base64.b64encode(Path("/output/fixture.map").read_bytes()).decode()}))
'''
            files["installer_runner.py"]=files.pop("probe.py")
            tools={"/snapshot/compiler/bin/xtensa-esp32-elf-gcc":CC,
                   "/snapshot/compiler/bin/xtensa-esp-elf-gcc":compiler_root/"bin/xtensa-esp-elf-gcc",
                   "/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                   "/snapshot/system_tools/installer-materializer":Path(os.environ.get("WARP4_TEST_MATERIALIZER",str(Path(os.environ["WARP4_TEST_BWRAP"]).parents[3]/"installer-materializer"))),
                   "/snapshot/compiler/libexec/gcc/xtensa-esp-elf/14.2.0/cc1":version/"cc1",
                   "/snapshot/compiler/libexec/gcc/xtensa-esp-elf/14.2.0/collect2":version/"collect2",
                   "/snapshot/compiler/libexec/gcc/xtensa-esp-elf/14.2.0/liblto_plugin.so":version/"liblto_plugin.so",
                   "/snapshot/compiler/lib/xtensa_esp32.so":compiler_root/"lib/xtensa_esp32.so",
                   "/snapshot/compiler/bin/as":compiler_root/"xtensa-esp-elf/bin/as",
                   "/snapshot/compiler/bin/ld":compiler_root/"xtensa-esp-elf/bin/ld"}
            inputs,binding=python_inputs(root,files,native_tools=tools)
            for target in ("/snapshot/system_tools/bwrap","/snapshot/system_tools/installer-materializer"):
                inputs[target]["role"]="system_tools"
            with snapshot.Snapshot(inputs) as snap:
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"private_workspace":True,"supervised":True})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    try: result=namespace.spawn(args,snap.transport_fds()+(fd,),root)
                    except subprocess.CalledProcessError as exc: self.fail(exc.stdout+exc.stderr)
                    evidence=json.loads(result.stdout)
                    elf=base64.b64decode(evidence["elf"])
                    actual_map=base64.b64decode(evidence["map"])
                    self.assertIn(b"Linker script and memory map",actual_map)
                    address,linked=verify_elf_key(elf,hashlib.sha256(key).hexdigest())
                    self.assertEqual(linked,key)
                    # Offline conversion only. No serial discovery/connection.
                    ep=root/"test-only.elf"; ep.write_bytes(elf)
                    ap=root/"test-only-app.bin"
                    convert=subprocess.run([sys.executable,"-B",str(ESPTOOL),"--chip","esp32","elf2image","--output",str(ap),str(ep)],capture_output=True,text=True)
                    self.assertEqual(convert.returncode,0,convert.stdout+convert.stderr)
                    self.assertTrue(verify_app_key(ap.read_bytes(),address,linked,hashlib.sha256(key).hexdigest()))
                finally: os.close(fd)
