"""Real byte/FD/parser contracts. These are NOT firmware integration evidence."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest


def pin(data):
    return hashlib.sha256(data).hexdigest()


class ImmutableClosureTests(unittest.TestCase):
    def test_snapshot_survives_original_change_and_rejects_writes(self):
        import installer_snapshot as s
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "source.cpp"; path.write_bytes(b"approved")
            inputs = {"/snapshot/source.cpp": {"path": str(path), "sha256": pin(b"approved"), "mode": 0o444, "role": "source"}}
            with s.Snapshot(inputs) as snap:
                path.write_bytes(b"changed")
                fd = snap.files["/snapshot/source.cpp"]
                self.assertEqual(os.pread(fd, 100, 0), b"approved")
                with self.assertRaises(OSError): os.pwrite(fd, b"changed", 0)
                self.assertEqual(snap.manifest["files"]["/snapshot/source.cpp"]["sha256"], pin(b"approved"))
                self.assertEqual(snap.identity, pin(s.canonical(snap.manifest)))
                context = snap.context({"run_id": "fixture"})
                self.assertEqual(s.read_context(context)["snapshot_sha256"], snap.identity)
                with self.assertRaises(OSError): os.pwrite(context, b"evil", 0)
                os.close(context)

    def test_foreign_loader_tool_hostpath_and_socket_rejected(self):
        import installer_snapshot as s
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "tool"; p.write_bytes(b"tool")
            good = {"path": str(p), "sha256": pin(b"tool"), "mode": 0o555, "role": "tool"}
            for target, item in (("/host/tool", good), ("/snapshot/tool", good | {"sha256": "0"*64}),
                                 ("/snapshot/../tool", good), ("/snapshot/tool", good | {"mode": 0o777})):
                with self.subTest(target=target, item=item), self.assertRaises(ValueError):
                    with s.Snapshot({target: item}): pass
            import socket
            sock = socket.socket(socket.AF_UNIX)
            try:
                sock.bind(str(Path(td)/"agent"))
                with self.assertRaises(ValueError):
                    with s.Snapshot({"/snapshot/agent": good | {"path": str(Path(td)/"agent")}}): pass
            finally: sock.close()
            p.chmod(0o555)
            for role in ("tool", "loader"):
                good = good | {"role": role}
                with self.assertRaises(ValueError):
                    s.verify_bootstrap({role: good}, {role: str(p)+"foreign"})
                s.verify_bootstrap({role: good}, {role: str(p)})
                with self.assertRaises(ValueError):
                    s.verify_bootstrap({role: good | {"sha256": "0"*64}}, {role: str(p)})

    def test_unsealed_context_rejected(self):
        import installer_snapshot as s
        fd = os.memfd_create("unsealed")
        try:
            os.write(fd, b"{}")
            with self.assertRaises(ValueError): s.read_context(fd)
        finally: os.close(fd)


class GraphContracts(unittest.TestCase):
    def test_script_search_include_group_and_nested_response(self):
        import installer_graph as g
        files = {"/snapshot/main.ld": b'SEARCH_DIR("/snapshot/libs") INCLUDE child.ld GROUP(-lx) ',
                 "/snapshot/child.ld": b'INPUT(/snapshot/obj.o)',
                 "/snapshot/libs/libx.a": b"archive", "/snapshot/obj.o": b"object",
                 "/snapshot/one.rsp": b"@/snapshot/two.rsp /snapshot/obj.o",
                 "/snapshot/two.rsp": b"-T /snapshot/main.ld"}
        seen = []
        def read(path):
            seen.append(path)
            if path not in files: raise ValueError("foreign input")
            return files[path]
        graph = g.LinkSemantics(read, "/run/work")
        self.assertEqual(graph.expand(["@/snapshot/one.rsp"]), ["-T", "/snapshot/main.ld", "/snapshot/obj.o"])
        graph.script("/snapshot/main.ld")
        self.assertTrue(set(files)-{"/snapshot/one.rsp", "/snapshot/two.rsp"} <= set(seen))
        for bad in (b'INCLUDE /host/evil.ld', b'GROUP(/host/evil.a)', b'SEARCH_DIR("/host")',
                    b'UNKNOWN(/snapshot/obj.o)', b'INCLUDE missing.ld', b'GROUP(-lmissing)'):
            files["/snapshot/main.ld"] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                g.LinkSemantics(read, "/run/work").script("/snapshot/main.ld")
        files["/snapshot/two.rsp"] = b"@/snapshot/one.rsp"
        with self.assertRaises(ValueError): g.LinkSemantics(read, "/run/work").expand(["@/snapshot/one.rsp"])

    def test_depfiles_complete_and_make_escapes(self):
        import installer_graph as g
        good = b"/run/work/a.o: /snapshot/a.cpp /snapshot/space\\ name.h \\\n /snapshot/x.h\n/snapshot/x.h:\n"
        rules = g.depfile(good)
        self.assertEqual(rules["/run/work/a.o"], ["/snapshot/a.cpp", "/snapshot/space name.h", "/snapshot/x.h"])
        for bad in (b"", b"a.o:", b"a.o: a.cpp\\", b"a.o a2.o: a.cpp", b"a.o: $(shell evil)", b"a.o: a.cpp\na.o: other.cpp"):
            with self.subTest(bad=bad), self.assertRaises(ValueError): g.depfile(bad)

    def test_pair_common_closure_cannot_differ(self):
        import installer_graph as g
        base = {"schema": "warp4-action-closure-v1", "common": {"/snapshot/sdk/a": "a"*64}, "tesla": {}, "actions": []}
        action={"tool":"/snapshot/sdk/a","kind":"python","argv":["/snapshot/sdk/a"],"cwd":"/output","inputs":base["common"],"outputs":{}}
        base["actions"]=[action]
        b=base|{"tesla":{"/snapshot/tesla/b":"b"*64},"actions":[action|{"inputs":base["common"]|{"/snapshot/tesla/b":"b"*64}}]}
        g.verify_pair_closures(base,b,{"/snapshot/tesla/b":"b"*64})
        for other in (base | {"common": {}}, base | {"tesla": {"/snapshot/foreign": "b"*64}}):
            with self.assertRaises(ValueError): g.verify_pair_closures(base, other, {})


class ProductionLinkContracts(unittest.TestCase):
    def test_real_verifier_rejects_hidden_script_and_incomplete_dependencies(self):
        import installer_link_verify as link
        from installer_sdk_verify import inventory
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); src = root / "src"; src.mkdir(); out = root / "out"; out.mkdir()
            for name in ("ld", "source.cpp", "lib.a"): (src / name).write_text("fixture")
            (src / "lib.a").write_bytes(b'!<arch>\nfixture.o/      0           0     0     100644  2         `\nxx')
            (out / "a.o").write_text("fixture")
            script = src / "main.ld"; script.write_text("INCLUDE /host/evil.ld")
            dep = out / "a.d"; dep.write_text(str(out / "a.o") + ": " + str(src / "source.cpp"))
            closure = {"cwd": str(out), "immutable_roots": {str(src): inventory(src)}, "output_root": str(out),
                       "link_command": [str(src / "ld"), str(out / "a.o"), "-T", str(script)], "dependency_files": [str(dep)]}
            text = "Memory Configuration\nLinker script and memory map\nLOAD " + str(src / "lib.a") + "\n"
            with self.assertRaises(ValueError): link.verify_link_inputs(text, closure)
            script.write_text("INPUT(" + str(src / "lib.a") + ")")
            closure["immutable_roots"] = {str(src): inventory(src)}
            link.verify_link_inputs(text, closure)
            for bad in ("", str(out / "a.o") + ":", str(out / "other.o") + ": " + str(src / "source.cpp")):
                dep.write_text(bad)
                with self.subTest(dep=bad), self.assertRaises(ValueError): link.verify_link_inputs(text, closure)
            dep.write_text(str(out / "a.o") + ": " + str(src / "source.cpp"))
            closure["link_command"].append("--unknown-link-option")
            with self.assertRaises(ValueError): link.verify_link_inputs(text, closure)


class NamespaceContracts(unittest.TestCase):
    def test_exact_mounts_clean_environment_and_explicit_fd_transport(self):
        import installer_namespace as n
        import installer_snapshot as s
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); tool = root / "pio"; tool.write_bytes(b"fixture")
            scratch = root / "scratch"; scratch.mkdir(); output = root / "output"; output.mkdir()
            inputs = {"/snapshot/python/bin/python": {"path": str(tool), "sha256": pin(b"fixture"), "mode": 0o555, "role": "python"},
                      "/snapshot/project/platformio.ini": {"path": str(tool), "sha256": pin(b"fixture"), "mode": 0o444, "role": "source"}}
            with s.Snapshot(inputs) as snap:
                fd = snap.context({"environment": "warp4_ble_installer"})
                try:
                    command = n.command("/approved/bwrap", snap, fd, "warp4_ble_installer")
                    self.assertIn("--clearenv", command); self.assertIn("--unshare-all", command)
                    self.assertIn("--ro-bind-data", command)
                    self.assertNotIn("--ro-bind", command)
                    self.assertNotIn("--bind", command, "hostwriter could inject a socket into rw host tree")
                    self.assertNotIn("--preserve-fds", command)
                    self.assertNotIn("BASH_ENV", command)
                    self.assertEqual(command[command.index("--chdir")+1], "/snapshot/project")
                    for bad in ("/", "/host/socket", "/approved/../bwrap"):
                        with self.subTest(bad=bad), self.assertRaises(ValueError): n.command(bad, snap, fd, "warp4_ble_installer")
                finally: os.close(fd)

    def test_actual_process_boundary_drops_bash_env_and_inherited_fd(self):
        import installer_namespace as n
        import sys, json
        from unittest.mock import patch
        fd = os.memfd_create("must-not-inherit", 0)
        os.set_inheritable(fd, True)
        try:
            with patch.dict(os.environ, {"BASH_ENV": "/host/inject", "SSH_AUTH_SOCK": "/host/agent"}):
                result = n.spawn([sys.executable, "-I", "-c", "import os,json; print(json.dumps(dict(env=dict(os.environ), leaked=os.path.exists('/proc/self/fd/"+str(fd)+"'))))"], (), Path.cwd())
            value = json.loads(result.stdout)
            self.assertFalse(value["leaked"])
            self.assertNotIn("BASH_ENV", value["env"])
            self.assertNotIn("SSH_AUTH_SOCK", value["env"])
        finally: os.close(fd)


class PairProductionContracts(unittest.TestCase):
    def test_pin_equality_without_actual_closure_is_not_pair_evidence(self):
        import installer_link_verify as link
        provenance = {k: "1"*64 for k in ("sdk_manifest_sha256", "sdk_inventory_sha256", "runtime_manifest_sha256", "public_key_sha256")}
        a = {"provenance": provenance | {"environment": "warp4_ble_installer"}, "verification": {"product_id": "warp4"}}
        b = {"provenance": provenance | {"environment": "warp4_multi_tesla_installer"}, "verification": {"product_id": "warp4"}}
        with self.assertRaises(ValueError): link.verify_pair(a, b)


class AdditionalNegativeContracts(unittest.TestCase):
    def test_response_escape_cycle_and_duplicate_noncyclic_use(self):
        import installer_graph as g
        blobs = {"/snapshot/a": b"@/snapshot/b @/snapshot/b", "/snapshot/b": b"/snapshot/object.o"}
        reader = lambda p: blobs[p]
        self.assertEqual(g.LinkSemantics(reader, "/run/work").expand(["@/snapshot/a"]), ["/snapshot/object.o", "/snapshot/object.o"])
        blobs["/snapshot/b"] = b"@/snapshot/../host"
        with self.assertRaises(ValueError): g.LinkSemantics(reader, "/run/work").expand(["@/snapshot/a"])

    def test_snapshot_rejects_file_directory_collisions_and_unpinned_source(self):
        import installer_snapshot as s
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)/"source"; p.write_bytes(b"source")
            spec = {"path": str(p), "sha256": pin(b"source"), "mode": 0o444, "role": "source"}
            for bad in ({"/snapshot/a": spec, "/snapshot/a/b": spec}, {"/snapshot/a": spec | {"sha256": None}}):
                with self.assertRaises(ValueError):
                    with s.Snapshot(bad): pass
            with s.Snapshot({"/snapshot/a": spec}) as snap:
                self.assertEqual(snap.mounts()[-1], "/snapshot/a")
                spec["sha256"] = "0"*64
                self.assertEqual(snap.manifest["files"]["/snapshot/a"]["sha256"], pin(b"source"))

    def test_context_mount_from_other_snapshot_rejected(self):
        import installer_snapshot as s
        import installer_namespace as n
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); p=root/"source"; p.write_bytes(b"source")
            scratch=root/"scratch"; scratch.mkdir(); output=root/"output"; output.mkdir()
            spec={"path":str(p),"sha256":pin(b"source"),"mode":0o444,"role":"source"}
            with s.Snapshot({"/snapshot/a":spec}) as a, s.Snapshot({"/snapshot/b":spec}) as b:
                fd=a.context({"environment":"warp4_ble_installer"})
                try:
                    with self.assertRaisesRegex(ValueError,"NAMESPACE_CONTEXT_MISMATCH"):
                        n.command("/approved/bwrap",b,fd,"warp4_ble_installer")
                finally: os.close(fd)


class ArchiveContracts(unittest.TestCase):
    def test_map_archive_member_must_exist_in_hashed_archive(self):
        import installer_link_verify as link
        from installer_sdk_verify import inventory
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); src=root/"src"; src.mkdir(); out=root/"out"; out.mkdir()
            (src/"ld").write_text("fixture")
            member=b"object fixture"
            header=("real.o/".ljust(16)+"0".ljust(12)+"0".ljust(6)+"0".ljust(6)+"100644".ljust(8)+str(len(member)).ljust(10)+"`\n").encode()
            archive=src/"lib.a"; archive.write_bytes(b"!<arch>\n"+header+member)
            (src/"a.cpp").write_text("fixture"); (out/"a.o").write_text("fixture")
            dep=out/"a.d"; dep.write_text(str(out/"a.o")+": "+str(src/"a.cpp"))
            closure={"cwd":str(out),"immutable_roots":{str(src):inventory(src)},"output_root":str(out),
                     "link_command":[str(src/"ld"),str(out/"a.o"),str(archive)],"dependency_files":[str(dep)]}
            text="Memory Configuration\nLinker script and memory map\nLOAD "+str(archive)+"\n .text 0x1 0x1 "+str(archive)+"(missing.o)\n"
            with self.assertRaises(ValueError): link.verify_link_inputs(text,closure)
            link.verify_link_inputs(text.replace("missing.o","real.o"),closure)


class RealHostFormatContracts(unittest.TestCase):
    def test_real_native_compiler_depfile_and_gnu_archive(self):
        import subprocess
        from installer_graph import depfile, archive_members
        from installer_process import clean_environment
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); header=root/"a header.h"; header.write_text("#define FIXTURE 42\n")
            source=root/"fixture.c"; source.write_text('#include "a header.h"\nint fixture(void) {return FIXTURE;}\n')
            obj=root/"a_very_long_native_fixture_object_name.o"; dependency=root/"fixture.d"
            subprocess.run(["/usr/bin/cc","-B/usr/bin/","-c",str(source),"-MMD","-MP","-MF",str(dependency),"-o",str(obj)],
                           env=clean_environment(),close_fds=True,check=True,capture_output=True)
            rules=depfile(dependency.read_bytes())
            self.assertEqual(set(rules[str(obj)]),{str(source),str(header)})
            archive=root/"fixture.a"
            subprocess.run(["/usr/bin/ar","rcs",str(archive),str(obj)],env=clean_environment(),close_fds=True,check=True,capture_output=True)
            self.assertEqual(archive_members(archive.read_bytes()),{obj.name:pin(obj.read_bytes())})


class ApprovalSnapshotContracts(unittest.TestCase):
    def test_external_approval_binds_exact_snapshot_not_original_paths(self):
        import installer_snapshot as s
        from installer_sdk_verify import inventory
        import json
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); material=root/"material"; material.mkdir(); (material/"source.cpp").write_bytes(b"source")
            spec=s.role_inputs([{"role":"source","path":str(material),"inventory":inventory(material)}],required_roles={"source"})
            with s.Snapshot(spec) as snap:
                approval=root/"approval.json"; approval.write_text(json.dumps({"schema":"warp4-snapshot-approval-v1","snapshot_sha256":snap.identity}))
                s.verify_approval(snap,approval,source_root=material)
                self.assertNotIn(str(material),s.canonical(snap.manifest).decode())
                self.assertEqual(set(snap.manifest["files"]),{"/snapshot/source/source.cpp"})
                approval.write_text(json.dumps({"schema":"warp4-snapshot-approval-v1","snapshot_sha256":"0"*64}))
                with self.assertRaises(ValueError): s.verify_approval(snap,approval,source_root=material)
            with self.assertRaises(ValueError): s.role_inputs([],required_roles={"source"})
            with self.assertRaises(ValueError): s.role_inputs([{"role":"../source","path":str(material),"inventory":inventory(material)}],required_roles={"../source"})


class LoaderContracts(unittest.TestCase):
    def test_actual_host_elf_loader_and_library_bindings(self):
        import installer_snapshot as s
        import io, sysconfig
        from elftools.elf.elffile import ELFFile
        exe=Path("/usr/bin/true").resolve()
        elf=ELFFile(io.BytesIO(exe.read_bytes()))
        interpreter=next(seg.get_interp_name() for seg in elf.iter_segments() if seg["p_type"]=="PT_INTERP")
        inputs={"/snapshot/tools/true":{"path":str(exe),"sha256":pin(exe.read_bytes()),"mode":0o555,"role":"system_tools"}}
        libraries={}; queue=[exe]; seen=set()
        search=[Path("/usr/lib")/sysconfig.get_config_var("MULTIARCH"),Path("/lib")/sysconfig.get_config_var("MULTIARCH"),Path("/lib64")]
        loader=Path(interpreter).resolve(); target="/snapshot/loader_runtime/"+loader.name
        inputs[target]={"path":str(loader),"sha256":pin(loader.read_bytes()),"mode":0o555,"role":"loader_runtime"}
        while queue:
            path=queue.pop()
            if path in seen: continue
            seen.add(path); current=ELFFile(io.BytesIO(path.read_bytes()))
            for segment in current.iter_segments():
                if segment["p_type"]!="PT_DYNAMIC": continue
                for tag in segment.iter_tags():
                    if tag.entry.d_tag!="DT_NEEDED": continue
                    soname=tag.needed; actual=next((d/soname).resolve() for d in search if (d/soname).is_file())
                    logical="/snapshot/loader_runtime/"+actual.name
                    libraries[soname]=logical
                    inputs[logical]={"path":str(actual),"sha256":pin(actual.read_bytes()),"mode":0o555,"role":"loader_runtime"}
                    queue.append(actual)
        binding={"executables":["/snapshot/tools/true"],"interpreters":{interpreter:target},"libraries":libraries}
        with s.Snapshot(inputs) as snap:
            s.verify_elf_closure(snap,binding)
            for bad in (binding | {"interpreters":{interpreter:"/host/loader"}}, binding | {"libraries":{}}):
                with self.assertRaises(ValueError): s.verify_elf_closure(snap,bad)


class ActionEvidenceContracts(unittest.TestCase):
    def test_empty_and_unbound_action_lists_never_count_as_actual_closure(self):
        import installer_graph as g
        empty={"schema":"warp4-action-closure-v1","common":{"/snapshot/tool":"a"*64},"tesla":{},"actions":[]}
        with self.assertRaises(ValueError): g.verify_pair_closures(empty,empty,{})
        action={"tool":"/snapshot/tool","kind":"python","argv":["/snapshot/tool","-I"],"cwd":"/output",
                "inputs":{"/snapshot/tool":"a"*64},"outputs":{"/output/result":"b"*64}}
        valid=empty|{"actions":[action]}
        g.verify_pair_closures(valid,valid,{})
        for changed in (action|{"tool":"/host/tool"},action|{"inputs":{"/output/unrecorded":"b"*64}},
                        action|{"outputs":{"/host/result":"b"*64}},action|{"inputs":{"/snapshot/tool":"c"*64}}):
            with self.subTest(action=changed), self.assertRaises(ValueError): g.verify_pair_closures(valid,valid|{"actions":[changed]}, {})


class KernelNamespaceContracts(unittest.TestCase):
    def test_real_private_tmpfs_sealed_bind_chroot_and_fd_cut(self):
        """Linux primitive test, NOT Bubblewrap or PlatformIO integration."""
        import subprocess, sys
        from installer_process import clean_environment
        helper = r"""
import ctypes, json, os, pathlib, resource, sys, fcntl
root=pathlib.Path(sys.argv[1]); original=sys.argv[2]; sentinel=sys.argv[3]
libc=ctypes.CDLL(None,use_errno=True)
libc.mount.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_ulong,ctypes.c_char_p]
def mount(source,target,kind,flags):
    b=lambda s: None if s is None else os.fsencode(s)
    if libc.mount(b(source),b(target),b(kind),flags,None):
        raise OSError(ctypes.get_errno(),"namespace mount failed")
fd=os.memfd_create("sealed-source",os.MFD_ALLOW_SEALING)
os.write(fd,b"approved"); fcntl.fcntl(fd,1033,15)
# Mutation is real, but the consumed inode is the sealed snapshot, not original.
pathlib.Path(original).write_bytes(b"host mutation")
mount("tmpfs",root,"tmpfs",2|4) # nosuid,nodev; private namespace only
(root/"snapshot").mkdir(); target=root/"snapshot/source.cpp"; target.touch()
# Match ro-bind-data's private materialization, not an invalid memfd bind.
target.write_bytes(os.pread(fd,8,0))
mount(target,target,None,4096)
mount(None,target,None,4096|32|1) # bind remount readonly
(root/"scratch").mkdir(); (root/"output").mkdir()
mount("tmpfs",root/"scratch","tmpfs",2|4)
mount("tmpfs",root/"output","tmpfs",2|4)
os.chroot(root); os.chdir("/")
os.closerange(3,resource.getrlimit(resource.RLIMIT_NOFILE)[0])
assert pathlib.Path("/snapshot/source.cpp").read_bytes()==b"approved"
try: pathlib.Path("/snapshot/source.cpp").write_bytes(b"evil")
except OSError: pass
else: raise AssertionError("snapshot writable")
assert not pathlib.Path(original).exists()
assert not pathlib.Path(sentinel).exists()
try: os.fstat(300)
except OSError: pass
else: raise AssertionError("inherited descriptor survived")
pathlib.Path("/output/generated").write_bytes(b"run-only")
assert pathlib.Path("/output/generated").read_bytes()==b"run-only"
print(json.dumps({"sealed":True,"host_hidden":True,"fd_cut":True,"private_output":True}))
"""
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); jail=root/"jail"; jail.mkdir(); original=root/"original"; original.write_bytes(b"approved")
            import socket
            sock=socket.socket(socket.AF_UNIX); sentinel=root/"host-agent"; sock.bind(str(sentinel))
            fd=os.open(original,os.O_RDONLY); os.dup2(fd,300,inheritable=True); os.close(fd)
            try:
                result=subprocess.run(["/usr/bin/unshare","--user","--map-root-user","--mount","--pid","--fork","--net",sys.executable,"-I","-c",helper,str(jail),str(original),str(sentinel)],
                                      env=clean_environment(),close_fds=True,pass_fds=(300,),capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                import json
                self.assertEqual(json.loads(result.stdout),{"sealed":True,"host_hidden":True,"fd_cut":True,"private_output":True})
                self.assertEqual(original.read_bytes(),b"host mutation")
                self.assertFalse((jail/"output/generated").exists())
            finally: os.close(300); sock.close()


class ThinArchiveContracts(unittest.TestCase):
    def test_load_never_accepts_thin_archive_external_members(self):
        import installer_link_verify as link
        from installer_sdk_verify import inventory
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); src=root/"src"; src.mkdir(); out=root/"out"; out.mkdir()
            (src/"ld").write_bytes(b"tool"); (src/"lib.a").write_bytes(b"!<thin>\n/host/member.o")
            (src/"a.cpp").write_bytes(b"source"); (out/"a.o").write_bytes(b"object")
            dep=out/"a.d"; dep.write_text(str(out/"a.o")+": "+str(src/"a.cpp"))
            closure={"cwd":str(out),"immutable_roots":{str(src):inventory(src)},"output_root":str(out),
                     "link_command":[str(src/"ld"),str(out/"a.o"),str(src/"lib.a")],"dependency_files":[str(dep)]}
            with self.assertRaises(ValueError): link.verify_link_inputs("Memory Configuration\nLinker script and memory map\nLOAD "+str(src/"lib.a")+"\n",closure)


class PreReadConfinementContracts(unittest.TestCase):
    def test_foreign_input_rejected_before_open_not_only_after_hash(self):
        from unittest.mock import patch
        import installer_link_verify as link
        from installer_sdk_verify import inventory
        import installer_safeio
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); src=root/"src"; src.mkdir(); out=root/"out"; out.mkdir(); foreign=root/"foreign.o"; foreign.write_bytes(b"foreign")
            (src/"ld").write_bytes(b"tool")
            closure={"cwd":str(out),"immutable_roots":{str(src):inventory(src)},"output_root":str(out),"link_command":[str(src/"ld"),str(foreign)],"dependency_files":[]}
            original=installer_safeio.read_bytes
            opened=[]
            def read(path): opened.append(str(path)); return original(path)
            with patch.object(installer_safeio,"read_bytes",side_effect=read):
                with self.assertRaises(ValueError): link.verify_link_inputs("Memory Configuration\nLinker script and memory map\nLOAD "+str(foreign)+"\n",closure)
            self.assertNotIn(str(foreign),opened)
