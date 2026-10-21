"""Production namespace/transport used by installer_recipe; not full qualification.

Only sealed input FDs cross the host boundary. Supervised builds materialize
named immutable inputs in an outer private namespace, then start a second empty
readonly-root namespace. No host output/source directory is mounted. Payload FD
isolation, dynamic-loader aliases and real offline compiler/PIO fixture chains
are tested; complete authenticated process/open/hash capture remains missing.
Private output export is supervised separately by installer_runner/output.
"""
import os
from pathlib import Path
import subprocess
from installer_process import clean_environment
from installer_sdk_verify import require
from installer_snapshot import read_context


def command(bwrap, snapshot, context_fd, environment):
    from installer_policy import ENVS
    require(environment in ENVS, "OFFLINE_DEFAULT_ONLY")
    require(Path(bwrap).is_absolute() and Path(bwrap).name == "bwrap" and ".." not in Path(bwrap).parts, "BOOTSTRAP_BWRAP_PATH_REQUIRED")
    context = read_context(context_fd)
    require(context["snapshot_sha256"] == snapshot.identity and
            context["run"].get("environment") == environment, "NAMESPACE_CONTEXT_MISMATCH")
    require("/snapshot/python/bin/python" in snapshot.files and any(p.startswith("/snapshot/project/") for p in snapshot.files), "SNAPSHOT_PIO_LAYOUT_REQUIRED")
    workspace=context["run"].get("private_workspace",False)
    aliases=sum(t.startswith(("/snapshot/project/","/snapshot/platformio/")) for t in snapshot.files) if workspace is True else 0
    # Compiler launchers can inspect /proc/self/exe: direct ro-bind-data has
    # nlink=0 and returns a deleted pathname. Production supervised builds use
    # named private materialization regardless of snapshot size.
    if context["run"].get("supervised") is True or (len(snapshot.files)+aliases) * 5 > 8000:
        return bundled_command(bwrap, snapshot, context_fd, environment)
    args = [bwrap, "--die-with-parent", "--new-session", "--unshare-all", "--clearenv", "--cap-drop", "ALL", "--tmpfs", "/",
            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--dir", "/run"]
    args += snapshot.mounts()
    # ELF interpreter names are absolute kernel paths. Bind only the verified
    # sealed loader bytes there; never bind an ambient host /lib directory.
    binding = context["run"].get("elf_binding")
    library_dirs = []
    if binding is not None:
        from installer_snapshot import verify_elf_closure
        from pathlib import PurePosixPath
        verify_elf_closure(snapshot, binding)
        for alias, target in sorted(binding["interpreters"].items()):
            path = PurePosixPath(alias)
            require(path.is_absolute() and str(path) == alias and ".." not in path.parts and
                    any(path.is_relative_to(root) and path != PurePosixPath(root)
                        for root in ("/lib", "/lib64", "/usr/lib")),
                    "UNSAFE_ELF_INTERPRETER_ALIAS")
            # ro-bind-data consumes its FD. Reusing that descriptor for an alias
            # fails in real Bubblewrap; point the alias at the already mounted
            # sealed snapshot file instead.
            args += ["--symlink", target, alias]
        library_dirs = sorted({str(PurePosixPath(target).parent) for target in binding["libraries"].values()})
    private_workspace = context["run"].get("private_workspace", False)
    require(type(private_workspace) is bool, "PRIVATE_WORKSPACE_TYPE")
    project = "/snapshot/project"
    if private_workspace:
        # Fresh writable directory skeleton, immutable original files. Generators
        # can create new files but cannot replace the source bind mounts. No host
        # worktree/cache/output is copied or exposed.
        args += ["--tmpfs", "/work", "--dir", "/work/project", "--dir", "/work/core"]
        for target in sorted(snapshot.files):
            if target.startswith("/snapshot/project/"):
                args += ["--perms", oct(snapshot.manifest["files"][target]["mode"])[2:],
                         "--ro-bind-data", str(snapshot.alias_fd(target)), "/work/project/" + target[len("/snapshot/project/"):]]
            elif target.startswith("/snapshot/platformio/"):
                args += ["--perms", oct(snapshot.manifest["files"][target]["mode"])[2:],
                         "--ro-bind-data", str(snapshot.alias_fd(target)), "/work/core/" + target[len("/snapshot/platformio/"):]]
        project = "/work/project"
    args += tool_aliases(snapshot,binding)
    args += ["--perms", "444", "--ro-bind-data", str(context_fd), "/run/verified-context.json",
             "--tmpfs", "/scratch", "--tmpfs", "/output", "--remount-ro", "/"]
    env = clean_environment({"WARP4_INSTALLER_OFFLINE_SANDBOX": "1", "HOME": "/scratch/home", "PLATFORMIO_CORE_DIR": "/work/core" if private_workspace else "/snapshot/platformio",
                             "PATH": "/snapshot/system_tools:/snapshot/node_environment/bin:/snapshot/python/bin",
                             "PLATFORMIO_BUILD_DIR": "/output/build", "PLATFORMIO_LIBDEPS_DIR": "/snapshot/libdeps",
                             "PLATFORMIO_SETTING_ENABLE_TELEMETRY": "No", "UV_OFFLINE": "1", "PIP_NO_INDEX": "1"})
    if library_dirs:
        env["LD_LIBRARY_PATH"] = ":".join(library_dirs)
    for key, value in env.items(): args += ["--setenv", key, value]
    supervised=context["run"].get("supervised",False)
    require(type(supervised) is bool,"SUPERVISOR_TYPE")
    if supervised:
        require(private_workspace and "/snapshot/project/installer_runner.py" in snapshot.files,
                "SNAPSHOT_SUPERVISOR_REQUIRED")
        payload=["/snapshot/python/bin/python","-I","-B","/snapshot/project/installer_runner.py",environment]
    else:
        payload=["/snapshot/python/bin/python","-I","-m","platformio","run","-e",environment]
    args += ["--chdir",project,"--"]+payload
    return args


def spawn(args, transport_fds, cwd, *, tracer=None, trace_path=None,
          stdout_limit=128*1024*1024, stderr_limit=16*1024*1024, timeout=7200, file_limit=None):
    # Low-level transport, intentionally not public build authorization. Caller
    # must byte/path-verify bootstrap closure before any exec, including bwrap.
    require(type(transport_fds) is tuple and len(set(transport_fds)) == len(transport_fds), "EXPLICIT_TRANSPORT_FDS_REQUIRED")
    for fd in transport_fds:
        import fcntl
        require(type(fd) is int and fcntl.fcntl(fd, 1034) & 15 == 15, "UNSEALED_TRANSPORT_FD")
        # ro-bind-data reads shared open-file offsets; a sequential retry must
        # start at zero, including executable and context data.
        os.lseek(fd, 0, os.SEEK_SET)
    trace_fd = None
    try:
        if tracer is not None:
            # Raw kernel exec/file/process capture, NOT a qualified graph. The
            # caller must independently approve the tracer/bootstrap bytes.
            require(Path(tracer).is_absolute() and Path(tracer).name == "strace" and
                    ".." not in Path(tracer).parts and trace_path is not None,"TRACE_TOOL_PATH_REQUIRED")
            from installer_safeio import directory_fd
            path=Path(trace_path)
            require(path.is_absolute() and ".." not in path.parts,"TRACE_OUTPUT_PATH_REQUIRED")
            parent=directory_fd(path.parent)
            try:
                trace_fd=os.open(path.name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW|os.O_CLOEXEC,0o600,dir_fd=parent)
            finally: os.close(parent)
            args=[tracer,"-f","-qq","-yy","-s","1048576","-v","-e",
                  "trace=%file,%process,close,dup,dup2,dup3,fcntl,fchdir,mmap,mprotect",
                  "-o","/proc/self/fd/"+str(trace_fd)]+args
        else:
            require(trace_path is None,"TRACE_TOOL_PATH_REQUIRED")
        passed=transport_fds+((trace_fd,) if trace_fd is not None else ())
        return _bounded_process(args,cwd,passed,stdout_limit,stderr_limit,timeout,file_limit)
    finally:
        if trace_fd is not None: os.close(trace_fd)



def _bounded_process(args,cwd,passed,stdout_limit,stderr_limit,timeout,file_limit):
    import selectors
    import signal
    import time
    require(type(stdout_limit) is int and type(stderr_limit) is int and stdout_limit>0 and stderr_limit>0,
            "NAMESPACE_OUTPUT_LIMIT_REQUIRED")
    require(file_limit is None or type(file_limit) is int and file_limit>0, 'NAMESPACE_FILE_LIMIT')
    import resource
    def child_limits():
        # Only the forked child; never alter the host/broker's limits. Do not
        # cap virtual address space: ASan legitimately reserves a large range.
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        resource.setrlimit(resource.RLIMIT_FSIZE,(file_limit,file_limit))
    streams={"stdout":bytearray(),"stderr":bytearray()}
    limits={"stdout":stdout_limit,"stderr":stderr_limit}
    selector=selectors.DefaultSelector()
    process=subprocess.Popen(args,cwd=cwd,env=clean_environment(),close_fds=True,pass_fds=passed,
                             stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                             start_new_session=True,preexec_fn=child_limits if file_limit is not None else None)
    try:
        selector.register(process.stdout,selectors.EVENT_READ,"stdout")
        selector.register(process.stderr,selectors.EVENT_READ,"stderr")
        deadline=time.monotonic()+timeout
        while selector.get_map():
            remaining=deadline-time.monotonic()
            if remaining<=0: raise subprocess.TimeoutExpired(args,timeout)
            for key,_ in selector.select(min(remaining,1)):
                data=key.fileobj.read1(65536)
                if not data:
                    selector.unregister(key.fileobj)
                    continue
                target=streams[key.data]
                require(len(target)+len(data)<=limits[key.data],"NAMESPACE_OUTPUT_LIMIT: "+key.data)
                target.extend(data)
        code=process.wait(timeout=max(0.01,deadline-time.monotonic()))
        stdout=streams["stdout"].decode("utf-8",errors="replace")
        stderr=streams["stderr"].decode("utf-8",errors="replace")
        if code: raise subprocess.CalledProcessError(code,args,stdout,stderr)
        return subprocess.CompletedProcess(args,code,stdout,stderr)
    finally:
        selector.close()
        if process.poll() is None:
            # bwrap --die-with-parent covers payload sessions; kill the launcher
            # process group as well (including strace when it wraps bwrap).
            try: os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError: pass
        process.stdout.close(); process.stderr.close()
        process.wait(timeout=10)


def bundled_command(bwrap, snapshot, context_fd, environment):
    # Two isolated stages avoid Bubblewrap MAX_ARGS=9000 for runtime files.
    # The static approved helper extracts a digest-bound sealed bundle only into
    # the OUTER private tmpfs, then execs a second approved Bubblewrap. The final
    # sandbox sees readonly mounts of that private materialization, never a host
    # directory or a live writer. This is not tar extraction or a host cache.
    import hashlib
    import struct
    from pathlib import PurePosixPath
    from installer_snapshot import sealed, verify_elf_closure
    snapshot.mounts()
    context=read_context(context_fd)
    require(context["snapshot_sha256"]==snapshot.identity and context["run"].get("environment")==environment,
            "NAMESPACE_CONTEXT_MISMATCH")
    helper="/snapshot/system_tools/installer-materializer"
    inner_bwrap="/snapshot/system_tools/bwrap"
    for path in (helper,inner_bwrap):
        require(path in snapshot.files and snapshot.manifest["files"][path]["role"]=="system_tools" and
                snapshot.manifest["files"][path]["mode"]==0o555,"SCALABLE_MATERIALIZER_ROLE_REQUIRED")
    binding=context["run"].get("elf_binding")
    require(type(binding) is dict and helper in binding["executables"] and inner_bwrap in binding["executables"],
            "MATERIALIZER_ELF_BINDING_REQUIRED")
    verify_elf_closure(snapshot,binding)
    root="/scratch/closure"
    inner=["--die-with-parent","--new-session","--unshare-all","--clearenv","--cap-drop","ALL",
           "--tmpfs","/","--proc","/proc","--dev","/dev","--tmpfs","/tmp",
           "--ro-bind",root+"/snapshot","/snapshot","--dir","/run",
           "--ro-bind",root+"/run/verified-context.json","/run/verified-context.json",
           "--tmpfs","/scratch","--tmpfs","/output"]
    outer=[bwrap,"--die-with-parent","--new-session","--unshare-all","--clearenv","--cap-drop","ALL",
           "--tmpfs","/","--proc","/proc","--dev","/dev","--tmpfs","/tmp","--tmpfs","/scratch","--dir","/run"]
    for alias,target in sorted(binding["interpreters"].items()):
        p=PurePosixPath(alias)
        require(p.is_absolute() and str(p)==alias and ".." not in p.parts and
                any(p.is_relative_to(base) and p!=PurePosixPath(base) for base in ("/lib","/lib64","/usr/lib")),
                "UNSAFE_ELF_INTERPRETER_ALIAS")
        inner += ["--symlink",target,alias]
        outer += ["--symlink",root+target,alias]
    private=context["run"].get("private_workspace",False)
    require(type(private) is bool,"PRIVATE_WORKSPACE_TYPE")
    project="/snapshot/project"
    if private:
        inner += ["--tmpfs","/work","--dir","/work/project","--dir","/work/core","--symlink","/work/project","/work/software"]
        mounted_web=False
        for target in sorted(snapshot.files):
            if not target.startswith("/snapshot/project/"): continue
            if target.startswith("/snapshot/project/web/node_modules/"):
                if not mounted_web:
                    inner += ["--ro-bind",root+"/snapshot/project/web/node_modules","/work/project/web/node_modules"]
                    mounted_web=True
                continue
            inner += ["--ro-bind",root+target,"/work/project/"+target[len("/snapshot/project/"):]]
        for sub in ("packages","platforms"):
            if any(t.startswith("/snapshot/platformio/"+sub+"/") for t in snapshot.files):
                inner += ["--ro-bind",root+"/snapshot/platformio/"+sub,"/work/core/"+sub]
        project="/work/project"
    env=clean_environment({"WARP4_INSTALLER_OFFLINE_SANDBOX":"1","HOME":"/scratch/home",
        "PLATFORMIO_CORE_DIR":"/work/core" if private else "/snapshot/platformio",
        "PATH":"/snapshot/system_tools:/snapshot/node_environment/bin:/snapshot/python/bin",
        "PLATFORMIO_BUILD_DIR":"/output/build","PLATFORMIO_LIBDEPS_DIR":"/snapshot/libdeps",
        "PLATFORMIO_SETTING_ENABLE_TELEMETRY":"No","PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL":"0",
        "PLATFORMIO_SETTING_CHECK_PLATFORMS_INTERVAL":"0","PLATFORMIO_SETTING_CHECK_LIBRARIES_INTERVAL":"0",
        "UV_OFFLINE":"1","PIP_NO_INDEX":"1"})
    dirs=sorted({str(PurePosixPath(t).parent) for t in binding["libraries"].values()})
    if dirs: env["LD_LIBRARY_PATH"]=":".join(dirs)
    for key,value in env.items(): inner += ["--setenv",key,value]
    supervised=context["run"].get("supervised",False)
    require(type(supervised) is bool,"SUPERVISOR_TYPE")
    if supervised:
        require(private and "/snapshot/project/installer_runner.py" in snapshot.files,"SNAPSHOT_SUPERVISOR_REQUIRED")
        payload=["/snapshot/python/bin/python","-I","-B","/snapshot/project/installer_runner.py",environment]
    else:
        payload=["/snapshot/python/bin/python","-I","-B","-m","platformio","run","-e",environment]
    inner += tool_aliases(snapshot,binding)
    inner += ["--remount-ro","/","--chdir",project]
    require(len(inner)+len(payload)+5<=9000,"WORKSPACE_ARGUMENT_LIMIT: relocate generators before increasing source inventory")
    args_bytes=b"\0".join(a.encode() for a in inner)+b"\0"
    immutable_records=[(target.lstrip("/"),snapshot.files[target],item["size"],item["mode"])
             for target,item in sorted(snapshot.manifest["files"].items())]
    records = []
    records += [("run/verified-context.json",os.pread(context_fd,os.fstat(context_fd).st_size,0),0o444),
                ("args",args_bytes,0o444),
                ("payload",b"\0".join(a.encode() for a in payload)+b"\0",0o444)]
    from installer_snapshot import sealed_bundle
    fd, digest = sealed_bundle(immutable_records +
        [(name,data,len(data),mode) for name,data,mode in records], "warp4-private-materialization")
    snapshot.bundle_fds.append(fd)
    outer += ["--perms","555","--ro-bind-data",str(snapshot.files[helper]),"/run/materializer",
              "--perms","444","--ro-bind-data",str(fd),"/run/snapshot.bundle"]
    for key,value in clean_environment({"LD_LIBRARY_PATH":":".join(root+d for d in dirs)}).items():
        outer += ["--setenv",key,value]
    outer += ["--remount-ro","/","--","/run/materializer",digest,"--payload-fsize-512m"]
    return outer



def tool_aliases(snapshot,binding):
    result=[]
    for alias,name in (("/bin/sh","sh"),("/bin/bash","bash"),("/usr/bin/env","env")):
        target="/snapshot/system_tools/"+name
        if target not in snapshot.files: continue
        require(snapshot.manifest["files"][target]["role"]=="system_tools" and
                snapshot.manifest["files"][target]["mode"]==0o555 and
                binding is not None and target in binding["executables"],"UNBOUND_SYSTEM_TOOL_ALIAS")
        result += ["--symlink",target,alias]
    return result
