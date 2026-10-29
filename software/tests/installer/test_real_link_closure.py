"""Real native GNU formats; NOT ESP32/A-B or capture provenance qualification."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from installer_link_verify import verify_link_inputs
from installer_sdk_verify import inventory

class RealNativeLinkClosure(unittest.TestCase):
    def test_real_nested_response_depfiles_and_generated_archive_require_member_coverage(self):
        with tempfile.TemporaryDirectory(prefix="warp4-native-link-") as td:
            root=Path(td); source=root/"src"; source.mkdir(); out=root/"output"; out.mkdir()
            (source/"entry.c").write_text("extern int helper(void); int result; void _start(void) { result=helper(); for (;;) {} }\n")
            (source/"member.c").write_text("int helper(void) { return 7; }\n")
            shutil.copyfile("/usr/bin/ld",source/"ld"); (source/"ld").chmod(0o555)
            for name in ("entry","member"):
                subprocess.run(["/usr/bin/cc","-ffreestanding","-fno-pie","-MMD","-MF",str(out/(name+".d")),"-c",str(source/(name+".c")),"-o",str(out/(name+".o"))],check=True,capture_output=True)
            archive=out/"libfixture.a"
            subprocess.run(["/usr/bin/ar","rcs",str(archive),str(out/"member.o")],check=True,capture_output=True)
            (out/"inner.rsp").write_text(str(out/"entry.o")+" "+str(archive))
            (out/"outer.rsp").write_text("@"+str(out/"inner.rsp"))
            argv=[str(source/"ld"),"@"+str(out/"outer.rsp"),"-Map",str(out/"fixture.map"),"-o",str(out/"fixture.elf")]
            subprocess.run(argv,check=True,capture_output=True)
            text=(out/"fixture.map").read_text()
            self.assertIn("Archive member included",text)
            closure={"cwd":str(out),"immutable_roots":{str(source):inventory(source)},"output_root":str(out),
                     "link_command":argv,"dependency_files":[str(out/"entry.d")]}
            with self.assertRaisesRegex(ValueError,"ARCHIVE_MEMBER_DEPENDENCY"):
                verify_link_inputs(text,closure)
            closure["archive_dependencies"]={str(archive):{"member.o":str(out/"member.o")}}
            with self.assertRaisesRegex(ValueError,"INCOMPLETE_COMPILER_DEPENDENCY"):
                verify_link_inputs(text,closure)
            closure["dependency_files"].append(str(out/"member.d"))
            self.assertTrue(verify_link_inputs(text,closure))
            (out/"member.o").write_bytes((out/"entry.o").read_bytes())
            with self.assertRaisesRegex(ValueError,"ARCHIVE_MEMBER_OBJECT_DIGEST"):
                verify_link_inputs(text,closure)
