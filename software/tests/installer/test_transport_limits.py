"""Real namespace regression: transport allowance is not payload authority."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from installer_snapshot import Snapshot
from installer_generation import run_stage
import installer_namespace as namespace
from namespace_fixture import python_inputs

@unittest.skipUnless(os.environ.get("WARP4_TEST_BWRAP") and os.environ.get("WARP4_TEST_MATERIALIZER"), "explicit native test tools required")
class TransportLimitTests(unittest.TestCase):
    def test_large_transport_both_named_paths_keep_hard_payload_limit(self):
        # No giant Python byte strings: many bounded files exceed 512MiB in total.
        code = b'''import errno, json, resource
from pathlib import Path
cap=512*1024*1024
assert resource.getrlimit(resource.RLIMIT_FSIZE)==(cap,cap), resource.getrlimit(resource.RLIMIT_FSIZE)
try: resource.setrlimit(resource.RLIMIT_FSIZE,(cap+1,cap+1))
except (ValueError,OSError): pass
else: raise AssertionError("payload raised hard limit")
with open("/output/too-large","wb",buffering=0) as f:
 f.seek(cap)
 try: f.write(b"x"); f.flush()
 except OSError as exc: assert exc.errno==errno.EFBIG, exc
 else: raise AssertionError("payload exceeded hard limit")
Path("/output/too-large").unlink()
assert Path("/snapshot/project/padding32").stat().st_size==16*1024*1024
print("bounded-payload-after-large-materialization",flush=True)
Path("/output/result.json").write_text(json.dumps({"hard_limit":cap}))
'''
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            files={"probe.py":code,"installer_runner.py":code,
                   "installer_stage_entry.py":Path("installer_stage_entry.py").read_bytes()}
            tools={"/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                   "/snapshot/system_tools/installer-materializer":Path(os.environ["WARP4_TEST_MATERIALIZER"])}
            inputs,binding=python_inputs(root,files,native_tools=tools)
            for target in tools: inputs[target]["role"]="system_tools"
            padding=root/"padding"
            with padding.open("wb") as f: f.truncate(16*1024*1024)
            digest=hashlib.sha256(padding.read_bytes()).hexdigest()
            for i in range(33):
                inputs["/snapshot/project/padding"+str(i)]={"path":str(padding),"sha256":digest,"mode":0o444,"role":"source"}
            with Snapshot(inputs) as snap:
                self.assertGreater(sum(v["size"] for v in snap.manifest["files"].values()),512*1024*1024)
                python="/snapshot/python/bin/python"; entry="/snapshot/project/probe.py"
                with run_stage(os.environ["WARP4_TEST_BWRAP"],snap,binding,[python,"-I","-B",entry],["result.json"],named=True,
                               script_policy={entry:{"interpreter":python,"arguments":["-I","-B"]}}) as result:
                    self.assertIn(b"536870912",result.read("result.json"))
                for fd in snap.bundle_fds: os.close(fd)
                snap.bundle_fds.clear()
                fd=snap.context({"environment":"warp4_ble_installer","elf_binding":binding,"private_workspace":True,"supervised":True})
                try:
                    args=namespace.command(os.environ["WARP4_TEST_BWRAP"],snap,fd,"warp4_ble_installer")
                    result=namespace.spawn(args,snap.transport_fds(),root,file_limit=8*1024*1024*1024)
                    self.assertIn("bounded-payload-after-large-materialization",result.stdout)
                finally: os.close(fd)
