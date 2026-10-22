"""Bounded private-tmpfs output transport. Integrity is NOT build approval.

No host output directory is exposed to the build. The protected namespace
supervisor owns stdout; build logs must go to stderr. Host callers must still
verify OTA/ELF/key/link evidence before publishing any decoded bytes.
"""
import base64
import binascii
import hashlib
from pathlib import Path, PurePosixPath
from installer_safeio import directory_fd, file_fd
from installer_sdk_verify import require, loads, hexpin

MAX_OUTPUT_BYTES = 64 * 1024 * 1024


def _names(names):
    require(type(names) is list and bool(names) and len(names) == len(set(names)), "OUTPUT_NAMES_REQUIRED")
    for name in names:
        require(type(name) is str and str(PurePosixPath(name)) == name and
                not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts and
                name not in ("", "."), "OUTPUT_PATH_INVALID")
    return set(names)


def encode_output(directory, names, snapshot_sha256, build_id):
    import os
    import stat
    import json
    allowed = _names(names)
    root = Path(directory)
    require(root.is_absolute() and ".." not in root.parts and root.is_relative_to("/output") and
            root != Path("/output"), "OUTPUT_PRIVATE_ROOT_REQUIRED")
    require(hexpin(snapshot_sha256) and hexpin(build_id, 32), "OUTPUT_IDENTITY_REQUIRED")
    parent = directory_fd(root)
    records = {}
    total = 0
    try:
        # Hold the directory for the whole export; a generator rename cannot
        # redirect later reads. No links/specials or unlisted files are exported.
        import os
        require(set(os.listdir(parent)) == allowed and all("/" not in n for n in names), "OUTPUT_FILE_SET_MISMATCH")
        for name in sorted(names):
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=parent)
            try:
                st = os.fstat(fd)
                require(stat.S_ISREG(st.st_mode) and st.st_nlink == 1 and st.st_size >= 0,
                        "OUTPUT_SPECIAL_OR_HARDLINK_FORBIDDEN")
                total += st.st_size
                require(total <= MAX_OUTPUT_BYTES, "OUTPUT_SIZE_LIMIT")
                data = os.pread(fd, st.st_size + 1, 0)
                end = os.fstat(fd)
                require(len(data) == st.st_size and
                        (st.st_size, st.st_mtime_ns, st.st_ctime_ns) ==
                        (end.st_size, end.st_mtime_ns, end.st_ctime_ns), "OUTPUT_CHANGED_DURING_READ")
                records[name] = {"size":len(data), "sha256":hashlib.sha256(data).hexdigest(),
                                 "base64":base64.b64encode(data).decode("ascii")}
            finally: os.close(fd)
    finally: os.close(parent)
    return json.dumps({"schema":"warp4-private-output-v1", "snapshot_sha256":snapshot_sha256,
                       "build_id":build_id, "files":records}, sort_keys=True, separators=(",", ":"))


def decode_output(text, names, snapshot_sha256, build_id):
    allowed = _names(names)
    require(type(text) is str and len(text) <= MAX_OUTPUT_BYTES * 2, "OUTPUT_SIZE_LIMIT")
    value = loads(text)
    require(type(value) is dict and set(value) == {"schema", "snapshot_sha256", "build_id", "files"} and
            value["schema"] == "warp4-private-output-v1", "OUTPUT_SCHEMA")
    require(value["snapshot_sha256"] == snapshot_sha256 and value["build_id"] == build_id,
            "OUTPUT_RUN_MISMATCH")
    require(type(value["files"]) is dict and set(value["files"]) == allowed, "OUTPUT_FILE_SET_MISMATCH")
    result = {}
    total = 0
    for name, record in value["files"].items():
        require(type(record) is dict and set(record) == {"size", "sha256", "base64"} and
                type(record["size"]) is int and record["size"] >= 0 and hexpin(record["sha256"]) and
                type(record["base64"]) is str, "OUTPUT_RECORD_SCHEMA")
        total += record["size"]
        require(total <= MAX_OUTPUT_BYTES, "OUTPUT_SIZE_LIMIT")
        try: data = base64.b64decode(record["base64"], validate=True)
        except (ValueError, binascii.Error) as exc: raise ValueError("OUTPUT_ENCODING_INVALID") from exc
        require(len(data) == record["size"] and hashlib.sha256(data).hexdigest() == record["sha256"],
                "OUTPUT_DIGEST_MISMATCH")
        result[name] = data
    return result


class LinkedProducts:
    """Readonly view of sealed parent bytes plus the approved snapshot.

    Generated JSON is data only: it never supplies a ledger, a key approval or
    an immutable input inventory. The latter is derived from host context.
    """
    def __init__(self, data, context, manifest, snapshot_read, *, generated_data=None):
        import os
        value=loads(data)
        require(type(value) is dict and set(value)=={'schema','build_id','snapshot_sha256','files','link_command','flags'} and
                value['schema']=='warp4-linked-products-v1' and value['build_id']==context['build_id'] and
                value['snapshot_sha256']==context['snapshot_sha256'], 'LINKED_PRODUCT_CONTEXT')
        files=value['files']
        require(type(files) is dict and 0<len(files)<=20000, 'LINKED_PRODUCT_FILES')
        self.files={}; total=0
        self.output='/output/build/'+context['environment']
        for path,item in files.items():
            p=PurePosixPath(path)
            require(type(path) is str and str(p)==path and p.is_relative_to(self.output) and
                    p!=PurePosixPath(self.output) and '..' not in p.parts and
                    p.suffix in ('.o','.d','.a','.elf','.map','.bin','.json','.ld','.rsp'), 'LINKED_PRODUCT_PATH')
            require(type(item) is str, 'LINKED_PRODUCT_BYTES')
            try: blob=base64.b64decode(item,validate=True)
            except (ValueError,binascii.Error) as exc: raise ValueError('LINKED_PRODUCT_BYTES') from exc
            total+=len(blob)
            require(len(blob)<=64*1024*1024 and total<=128*1024*1024,'LINKED_PRODUCT_LIMIT')
            self.files[path]=blob
        self.roots={'/snapshot':{},'/work/project':{}}
        self.manifest=manifest
        self.snapshot_read=snapshot_read
        for path,item in manifest['files'].items():
            entry={'sha256':item['sha256'],'mode':item['mode']}
            self.roots['/snapshot'][path[len('/snapshot/'):]]=entry
            if path.startswith('/snapshot/project/'):
                self.roots['/work/project'][path[len('/snapshot/project/'):]]=entry
        self.generated={}
        if generated_data is not None:
            require(hashlib.sha256(generated_data).hexdigest()==context.get('generator_output_sha256'),
                    'GENERATOR_PARENT_MISMATCH')
            from installer_generators import decode_generated
            _,self.generated=decode_generated(generated_data,context,manifest['files'])
            for name,blob in self.generated.items():
                self.roots['/work/project'][name]={'sha256':hashlib.sha256(blob).hexdigest(),'mode':0o444}
        self.flags=value['flags']
        from installer_graph import archive_members
        objects={hashlib.sha256(blob).hexdigest():path for path,blob in self.files.items() if path.endswith('.o')}
        archives={}
        for path,blob in self.files.items():
            if path.endswith('.a'):
                members=archive_members(blob)
                require(all(h in objects for h in members.values()),'ARCHIVE_MEMBER_DEPENDENCY_REQUIRED')
                archives[path]={member:objects[h] for member,h in members.items()}
        self.closure={'cwd':'/work/project','output_root':self.output,'immutable_roots':self.roots,
                      'link_command':value['link_command'],
                      'dependency_files':sorted(p for p in self.files if p.endswith('.d')),
                      'archive_dependencies':archives}

    def inventory(self, root):
        require(str(root) in self.roots,'LINKED_PRODUCT_ROOT')
        return self.roots[str(root)]

    def read(self,path):
        if path in self.files: return self.files[path]
        if path.startswith('/work/project/') and path[len('/work/project/'):] in self.generated:
            return self.generated[path[len('/work/project/'):]]
        target=path
        if path.startswith('/work/project/'):
            target='/snapshot/project/'+path[len('/work/project/'):]
        require(target in self.manifest['files'],'LINKED_PRODUCT_FOREIGN_INPUT: '+path)
        blob=self.snapshot_read(target)
        require(hashlib.sha256(blob).hexdigest()==self.manifest['files'][target]['sha256'], 'LINKED_PRODUCT_SNAPSHOT_BYTES')
        return blob
