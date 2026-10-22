"""Private content-addressed byte closure, backed by kernel-sealed memfds.

No original path is mounted. Each consumer file is materialized from verified
bytes and sealed before publication. This primitive is not the PIO integration.
The trusted interpreter/kernel and its startup closure MUST be established by
an external launcher before importing this module; checking oneself afterwards
cannot retroactively establish a trustworthy bootstrap.
"""
import fcntl
import hashlib
import json
import os
from pathlib import PurePosixPath, Path
from installer_safeio import read_bytes
from installer_sdk_verify import require, hexpin, loads

SEALS = 15  # Linux F_SEAL_SEAL|SHRINK|GROW|WRITE


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def sealed(data, label="warp4-closure"):
    fd = os.memfd_create(label, os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        offset = 0
        while offset < len(data):
            offset += os.write(fd, data[offset:])
        fcntl.fcntl(fd, 1033, SEALS)
        os.lseek(fd, 0, os.SEEK_SET)
        return fd
    except BaseException:
        os.close(fd)
        raise


def sealed_bundle(records, label):
    """Stream W4B1 records (name, sealed fd or bytes, size, mode)."""
    import struct
    fd = os.memfd_create(label, os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    digest = hashlib.sha256()
    def emit(data):
        digest.update(data)
        view = memoryview(data)
        while view:
            written = os.write(fd, view)
            require(written > 0, 'BUNDLE_SHORT_WRITE')
            view = view[written:]
    try:
        emit(b'W4B1' + struct.pack('<I', len(records)))
        for name, source, size, mode in records:
            encoded = name.encode()
            emit(struct.pack('<IIQ', len(encoded), mode, size))
            emit(encoded)
            if type(source) is int:
                require(fcntl.fcntl(source, 1034) & SEALS == SEALS,
                        'UNSEALED_BUNDLE_SOURCE')
                require(os.fstat(source).st_size == size, 'BUNDLE_SOURCE_SIZE')
            else:
                require(type(source) is bytes and len(source) == size, 'BUNDLE_SOURCE_SIZE')
            offset = 0
            while offset < size:
                count = min(1024 * 1024, size - offset)
                chunk = os.pread(source, count, offset) if type(source) is int else source[offset:offset + count]
                require(bool(chunk), 'BUNDLE_SHORT_READ')
                emit(chunk)
                offset += len(chunk)
        fcntl.fcntl(fd, 1033, SEALS)
        os.lseek(fd, 0, os.SEEK_SET)
        return fd, digest.hexdigest()
    except BaseException:
        os.close(fd)
        raise


def read_context(fd):
    require(type(fd) is int and fd >= 0, "CONTEXT_FD_REQUIRED")
    require(fcntl.fcntl(fd, 1034) & SEALS == SEALS, "UNSEALED_RUN_CONTEXT")
    size = os.fstat(fd).st_size
    require(0 < size <= 16 * 1024 * 1024, "CONTEXT_SIZE_INVALID")
    return _parse_context(os.pread(fd, size, 0))


def _parse_context(data):
    value = loads(data)
    require(type(value) is dict and set(value) == {"schema", "snapshot_sha256", "manifest", "run"}, "CONTEXT_SCHEMA")
    require(type(value["run"]) is dict and type(value["manifest"]) is dict,
            "CONTEXT_SCHEMA")
    require(value["schema"] == "warp4-sealed-run-v1" and
            hashlib.sha256(canonical(value["manifest"])).hexdigest() == value["snapshot_sha256"], "CONTEXT_CLOSURE_MISMATCH")
    return value


def _mounted_file(path):
    """Namespace-only reader; host safeio deliberately still requires nlink=1.

    Bubblewrap 0.12 ro-bind-data uses unlinked regular files (nlink=0).
    Accept that ONLY on readonly mounts, via no-follow fd-relative traversal.
    O_NONBLOCK prevents a hostile special file from blocking before fstat.
    """
    import stat
    from installer_safeio import directory_fd
    p = PurePosixPath(path)
    require(p.is_absolute() and str(p) == str(path) and '..' not in p.parts,
            'CONTEXT_PATH_INVALID')
    parent = directory_fd(p.parent)
    try:
        fd = os.open(p.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=parent)
    finally:
        os.close(parent)
    try:
        st = os.fstat(fd)
        require(stat.S_ISREG(st.st_mode) and st.st_nlink in (0, 1),
                'CONTEXT_SPECIAL_OR_HARDLINK_FORBIDDEN')
        require(os.fstatvfs(fd).f_flag & os.ST_RDONLY, 'CONTEXT_MOUNT_NOT_READONLY')
        require(not st.st_mode & 0o222, 'CONTEXT_FILE_MODE_WRITABLE')
        return fd, st
    except BaseException:
        os.close(fd)
        raise


def read_mounted_context(path, environment):
    """Verify readonly context AND actual readonly snapshot file bytes.

    Self-consistency is not authorization: the protected launcher authenticates
    the approval before creating the namespace. No host preflight bypass token.
    """
    fd, st = _mounted_file(path)
    try:
        require(0 < st.st_size <= 16 * 1024 * 1024, 'CONTEXT_SIZE_INVALID')
        value = _parse_context(os.pread(fd, st.st_size + 1, 0))
    finally:
        os.close(fd)
    require(value['run'].get('environment') == environment, 'CONTEXT_ENVIRONMENT_MISMATCH')
    manifest = value['manifest']
    require(set(manifest) == {'schema', 'files'} and
            manifest['schema'] == 'warp4-sealed-closure-v1' and
            type(manifest['files']) is dict and bool(manifest['files']), 'CONTEXT_MANIFEST_SCHEMA')
    import stat
    for target, item in manifest['files'].items():
        p = PurePosixPath(target)
        require(p.is_absolute() and str(p) == target and p.is_relative_to('/snapshot') and
                p != PurePosixPath('/snapshot') and '..' not in p.parts,
                'CONTEXT_FILE_PATH_INVALID')
        require(type(item) is dict and set(item) == {'sha256', 'size', 'mode', 'role'} and
                hexpin(item['sha256']) and type(item['size']) is int and item['size'] >= 0 and
                type(item['mode']) is int and item['mode'] in (0o444, 0o555) and
                type(item['role']) is str and bool(item['role']), 'CONTEXT_FILE_SCHEMA')
        fd, actual = _mounted_file(target)
        try:
            require(actual.st_size == item['size'] and stat.S_IMODE(actual.st_mode) == item['mode'],
                    'CONTEXT_FILE_METADATA_MISMATCH')
            h = hashlib.sha256()
            offset = 0
            while offset < actual.st_size:
                data = os.pread(fd, min(1024 * 1024, actual.st_size - offset), offset)
                require(bool(data), 'CONTEXT_FILE_SHORT_READ')
                h.update(data)
                offset += len(data)
            require(h.hexdigest() == item['sha256'], 'CONTEXT_FILE_DIGEST_MISMATCH')
        finally:
            os.close(fd)
    return value


def read_build_context(environment, guarded=None):
    # This fixed namespace mount is the sole hook transport. The environment
    # dictionary is not authorization and must match the mounted bytes exactly.
    import re
    value = read_mounted_context("/run/verified-context.json", environment)
    verified = value["run"].get("verified")
    require(type(verified) is dict and verified.get("environment") == environment and
            type(verified.get("build_id")) is str and
            re.fullmatch(r"[0-9a-f]{32}", verified["build_id"]) is not None and
            type(verified.get("start_ns")) is int and verified["start_ns"] > 0 and
            type(verified.get("package_roots")) is dict and
            type(verified.get("effective_flags")) is dict, "BUILD_CONTEXT_SCHEMA")
    result = dict(verified, snapshot_sha256=value["snapshot_sha256"])
    if guarded is not None:
        require(canonical(result) == canonical(guarded), "GUARD_CONTEXT_MISMATCH")
    return result


def verify_bootstrap(approved, actual):
    require(type(approved) is dict and bool(approved) and set(approved) == set(actual), "BOOTSTRAP_ROLES_REQUIRED")
    for role, item in approved.items():
        require(type(item) is dict and set(item) == {"path", "sha256", "mode", "role"} and item["role"] == role,
                "BOOTSTRAP_ROLE_SCHEMA")
        p = Path(actual[role])
        require(p.is_absolute() and ".." not in p.parts and str(p) == item["path"], "BOOTSTRAP_PATH_MISMATCH")
        from installer_safeio import file_fd
        import stat
        fd,st=file_fd(p)
        with os.fdopen(fd,"rb") as stream:
            require(type(item["mode"]) is int and stat.S_IMODE(st.st_mode)==item["mode"] and
                    not st.st_mode & 0o6000,"BOOTSTRAP_MODE_MISMATCH")
            data=stream.read()
            end=os.fstat(stream.fileno())
            require((st.st_size,st.st_mtime_ns,st.st_ctime_ns)==(end.st_size,end.st_mtime_ns,end.st_ctime_ns),
                    "BOOTSTRAP_INPUT_CHANGED")
        require(hexpin(item["sha256"]) and hashlib.sha256(data).hexdigest()==item["sha256"],"BOOTSTRAP_BYTES_MISMATCH")
    return True


class Snapshot:
    def __init__(self, inputs):
        self.files = {}
        self.aliases = {}
        self.bundle_fds = []
        self.fd_limit_before = None
        self.manifest = {"schema": "warp4-sealed-closure-v1", "files": {}}
        try:
            require(type(inputs) is dict and bool(inputs), "SNAPSHOT_INPUTS_REQUIRED")
            # Per-process capacity only; never change kernel/system limits or
            # silently omit runtime files. Reserve independent alias FDs too.
            import resource
            soft,hard=resource.getrlimit(resource.RLIMIT_NOFILE)
            current=[int(n) for n in os.listdir("/proc/self/fd") if n.isdigit()]
            needed=max(current,default=2)+2*len(inputs)+64
            require(hard==resource.RLIM_INFINITY or needed<=hard,"SNAPSHOT_FD_HARD_LIMIT")
            if soft!=resource.RLIM_INFINITY and soft<needed:
                resource.setrlimit(resource.RLIMIT_NOFILE,(needed,hard))
                self.fd_limit_before=(soft,hard)
            directories = set()
            for target, item in sorted(inputs.items()):
                require(type(target) is str and type(item) is dict and
                        set(item) == {"path", "sha256", "mode", "role"}, "SNAPSHOT_INPUT_SCHEMA")
                p = PurePosixPath(target)
                require(str(p) == target and p.is_relative_to("/snapshot") and p != PurePosixPath("/snapshot") and
                        ".." not in p.parts and type(item["mode"]) is int and item["mode"] in (0o444, 0o555) and
                        type(item["role"]) is str and bool(item["role"]), "SNAPSHOT_TARGET_OR_MODE_INVALID")
                # File-vs-directory collisions are rejected before bwrap can mount.
                require(target not in directories and not any(str(parent) in self.files for parent in p.parents), "SNAPSHOT_TARGET_COLLISION")
                directories.update(str(parent) for parent in p.parents)
                data = read_bytes(item["path"])
                require(hexpin(item["sha256"]) and hashlib.sha256(data).hexdigest() == item["sha256"], "SNAPSHOT_BYTES_MISMATCH")
                self.files[target] = sealed(data)
                self.manifest["files"][target] = {"sha256": item["sha256"], "size": len(data), "mode": item["mode"], "role": item["role"]}
            self.identity = hashlib.sha256(canonical(self.manifest)).hexdigest()
        except BaseException:
            self.close()
            raise

    def context(self, run):
        require(type(run) is dict, "RUN_CONTEXT_REQUIRED")
        self.mounts()  # Never publish a context for a changed or incomplete closure.
        return sealed(canonical({"schema": "warp4-sealed-run-v1", "snapshot_sha256": self.identity,
                                 "manifest": self.manifest, "run": run}), "warp4-run-context")

    def mounts(self):
        require(type(self.manifest) is dict and
                set(self.manifest) == {"schema", "files"} and
                self.manifest["schema"] == "warp4-sealed-closure-v1" and
                type(self.manifest["files"]) is dict and
                hashlib.sha256(canonical(self.manifest)).hexdigest() == self.identity,
                "SNAPSHOT_MANIFEST_CHANGED")
        require(set(self.files) == set(self.manifest["files"]),
                "SNAPSHOT_FILE_SET_CHANGED")
        result = []
        for target, fd in sorted(self.files.items()):
            require(fcntl.fcntl(fd, 1034) & SEALS == SEALS, "SNAPSHOT_SEAL_LOST")
            item = self.manifest["files"][target]
            require(hashlib.sha256(os.pread(fd, item["size"] + 1, 0)).hexdigest() == item["sha256"], "SNAPSHOT_FD_MISMATCH")
            result += ["--perms", oct(item["mode"])[2:], "--ro-bind-data", str(fd), target]
        return result

    def alias_fd(self, target):
        # Bubblewrap bind sources are resolved in the OLD root, not its newly
        # assembled root. ro-bind-data also consumes each FD; use an independent
        # open-file description of the same sealed memfd for a workspace alias.
        require(target in self.files, "FOREIGN_SNAPSHOT_ALIAS")
        if target not in self.aliases:
            fd = os.open("/proc/self/fd/" + str(self.files[target]), os.O_RDONLY | os.O_CLOEXEC)
            try:
                require(fcntl.fcntl(fd, 1034) & SEALS == SEALS, "SNAPSHOT_ALIAS_UNSEALED")
                self.aliases[target] = fd
            except BaseException:
                os.close(fd)
                raise
        alias=self.aliases[target]
        item=self.manifest["files"][target]
        require(fcntl.fcntl(alias,1034) & SEALS == SEALS and os.fstat(alias).st_size==item["size"] and
                hashlib.sha256(os.pread(alias,item["size"]+1,0)).hexdigest()==item["sha256"],
                "SNAPSHOT_ALIAS_MISMATCH")
        return alias

    def transport_fds(self):
        return tuple(self.files.values()) + tuple(self.aliases.values()) + tuple(self.bundle_fds)

    def close(self):
        for fd in self.files.values(): os.close(fd)
        self.files.clear()
        for fd in self.aliases.values(): os.close(fd)
        self.aliases.clear()
        for fd in self.bundle_fds: os.close(fd)
        self.bundle_fds.clear()
        if self.fd_limit_before is not None:
            import resource
            resource.setrlimit(resource.RLIMIT_NOFILE,self.fd_limit_before)
            self.fd_limit_before=None

    def __enter__(self): return self
    def __exit__(self, *unused): self.close()


def role_inputs(entries, required_roles):
    """Project independently inventoried roles to canonical consumer paths.

    This is intentionally explicit: source, key, SDK, tools and loader roles
    must be supplied by the qualified recipe, not discovered from caches.
    """
    import re
    from installer_sdk_verify import validate_inventory
    require(type(entries) is list and type(required_roles) in (set, frozenset) and bool(required_roles), "SNAPSHOT_ROLES_REQUIRED")
    require(all(type(e) is dict and set(e) == {"role", "path", "inventory"} for e in entries), "SNAPSHOT_ROLE_SCHEMA")
    require(len(entries) == len(required_roles) and {e["role"] for e in entries} == required_roles, "SNAPSHOT_ROLES_INCOMPLETE")
    result = {}
    for entry in entries:
        role = entry["role"]
        require(type(role) is str and re.fullmatch(r"[a-z][a-z0-9_]*", role) is not None, "SNAPSHOT_ROLE_NAME")
        root = Path(entry["path"])
        require(root.is_absolute() and ".." not in root.parts, "SNAPSHOT_ROLE_ROOT")
        validate_inventory(entry["inventory"])
        require(bool(entry["inventory"]), "EMPTY_SNAPSHOT_ROLE")
        for relative, item in sorted(entry["inventory"].items()):
            result["/snapshot/" + role + "/" + relative] = {
                "path": str(root / relative), "sha256": item["sha256"],
                "mode": 0o555 if item["mode"] & 0o111 else 0o444, "role": role}
    return result


def verify_approval(snapshot, approval_path, source_root):
    """Approval is external; no approval bytes enter their own hashed payload."""
    path = Path(approval_path)
    root = Path(source_root)
    require(path.is_absolute() and root.is_absolute() and ".." not in path.parts and
            not path.resolve().is_relative_to(root.resolve()), "SNAPSHOT_APPROVAL_MUST_BE_EXTERNAL")
    approval = loads(read_bytes(path))
    require(type(approval) is dict and set(approval) == {"schema", "snapshot_sha256"} and
            approval["schema"] == "warp4-snapshot-approval-v1" and hexpin(approval["snapshot_sha256"]), "SNAPSHOT_APPROVAL_SCHEMA")
    require(approval["snapshot_sha256"] == snapshot.identity and
            hashlib.sha256(canonical(snapshot.manifest)).hexdigest() == snapshot.identity, "SNAPSHOT_APPROVAL_MISMATCH")
    # Revalidate the actual sealed files, not just a Python manifest object.
    snapshot.mounts()
    return approval["snapshot_sha256"]


def verify_elf_closure(snapshot, binding):
    """Validate ELF-declared interpreter/DT_NEEDED closure against sealed roles.

    This does not predict dlopen, Python imports or subprocess execution. Those
    require the still-pending actual action/open graph. It is not an ldd call.
    """
    import io
    from elftools.elf.elffile import ELFFile
    from elftools.common.exceptions import ELFError
    require(type(binding) is dict and set(binding) == {"executables", "interpreters", "libraries"}, "ELF_BINDING_SCHEMA")
    require(type(binding["executables"]) is list and bool(binding["executables"]) and
            all(type(binding[k]) is dict for k in ("interpreters", "libraries")), "ELF_BINDING_ROLES")
    snapshot.mounts()
    # Validate the complete declared search closure too: unused entries still
    # influence the dynamic loader search path in the namespace launcher.
    for target in list(binding["interpreters"].values()) + list(binding["libraries"].values()):
        require(type(target) is str and target in snapshot.files and ":" not in target and
                snapshot.manifest["files"][target]["role"] == "loader_runtime" and
                snapshot.manifest["files"][target]["mode"] == 0o555,
                "FOREIGN_ELF_LOADER_DECLARATION")
    declared=set(binding["executables"]) | set(binding["interpreters"].values()) | set(binding["libraries"].values())
    require(len(binding["executables"])==len(set(binding["executables"])),"DUPLICATE_ELF_EXECUTABLE")
    # dlopen targets and compiler plugins need validation even when no NEEDED
    # edge names them. Inspect all actual ELF files, not just a caller subset.
    for target,fd in snapshot.files.items():
        if os.pread(fd,4,0)!=b"\x7fELF": continue
        item=snapshot.manifest["files"][target]
        try:
            elf=ELFFile(io.BytesIO(os.pread(fd,item["size"],0)))
            if elf["e_type"] in ("ET_EXEC","ET_DYN"):
                require(target in declared,"UNBOUND_ELF_FILE: "+target)
                require(item["mode"]==0o555,"NONEXECUTABLE_ELF_ROLE")
        except (ELFError,KeyError,IndexError) as exc:
            raise ValueError("INVALID_ELF_RUNTIME_ROLE") from exc
    pending, seen = list(declared), set()
    while pending:
        path = pending.pop()
        if path in seen: continue
        seen.add(path)
        require(path in snapshot.files, "FOREIGN_ELF_TOOL_OR_LOADER: " + str(path))
        item = snapshot.manifest["files"][path]
        require(item["mode"] == 0o555, "NONEXECUTABLE_ELF_ROLE")
        data = os.pread(snapshot.files[path], item["size"], 0)
        try:
            elf = ELFFile(io.BytesIO(data))
            require(elf["e_type"] in ("ET_EXEC", "ET_DYN"), "ELF_EXECUTABLE_REQUIRED")
            interpreters = []
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_INTERP": interpreters.append(segment.get_interp_name())
                if segment["p_type"] != "PT_DYNAMIC": continue
                for tag in segment.iter_tags():
                    kind = tag.entry.d_tag
                    require(kind not in ("DT_RPATH", "DT_RUNPATH", "DT_AUDIT", "DT_DEPAUDIT", "DT_FILTER", "DT_AUXILIARY"), "UNQUALIFIED_DYNAMIC_LOADER_SEMANTICS")
                    if kind == "DT_NEEDED":
                        name = tag.needed
                        require("/" not in name and name in binding["libraries"], "UNBOUND_NEEDED_LIBRARY: " + name)
                        target = binding["libraries"][name]
                        require(target in snapshot.files and snapshot.manifest["files"][target]["role"] == "loader_runtime", "FOREIGN_NEEDED_LIBRARY")
                        pending.append(target)
            require(len(interpreters) <= 1, "AMBIGUOUS_ELF_INTERPRETER")
            for name in interpreters:
                require(name in binding["interpreters"], "UNBOUND_ELF_INTERPRETER")
                target = binding["interpreters"][name]
                require(target in snapshot.files and snapshot.manifest["files"][target]["role"] == "loader_runtime", "FOREIGN_ELF_INTERPRETER")
                pending.append(target)
        except (ELFError, KeyError, IndexError) as exc:
            raise ValueError("INVALID_ELF_RUNTIME_ROLE") from exc
    return sorted(seen)
