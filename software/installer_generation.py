"""Host-owned stage-generation broker: immutable boundaries, not per-load capture.

A stage may use mutable private scratch. Exposed snapshot inventories are
conservative supersets. Records are created only after namespace completion and
output sealing; external source/runtime authorization remains a prerequisite.
"""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
from installer_sdk_verify import require
from installer_snapshot import canonical, sealed, verify_elf_closure
from installer_namespace import spawn
from installer_safeio import file_fd
from installer_stage_verify import verify_stage_records, output_names


class Generation:
    def __init__(self, outputs, record):
        self.files={}
        self.record=record
        self.identity=hashlib.sha256(canonical(record)).hexdigest()
        try:
            for name,data in outputs.items(): self.files[name]=sealed(data,'warp4-stage-generation')
        except BaseException:
            self.close()
            raise

    def read(self,name):
        import fcntl
        require(name in self.files,'GENERATION_MEMBER_MISSING')
        fd=self.files[name]
        require(fcntl.fcntl(fd,1034)&15==15,'GENERATION_UNSEALED')
        item=self.record['outputs'][name]
        data=os.pread(fd,item['size']+1,0)
        require(len(data)==item['size'] and hashlib.sha256(data).hexdigest()==item['sha256'],
                'GENERATION_BYTES_MISMATCH')
        require(hashlib.sha256(canonical(self.record)).hexdigest()==self.identity,'GENERATION_RECORD_CHANGED')
        return data

    def close(self):
        for fd in self.files.values(): os.close(fd)
        self.files.clear()

    def __enter__(self): return self
    def __exit__(self,*unused): self.close()


def merge_parents(parents):
    """Merge immutable parent versions; equal duplicate bytes are harmless."""
    require(type(parents) in (list, tuple), 'GENERATION_PARENT_TYPE')
    merged = {}
    for parent in parents:
        require(type(parent) is Generation, 'GENERATION_PARENT_TYPE')
        require(set(parent.files) == set(parent.record['outputs']), 'GENERATION_FILE_SET_CHANGED')
        output_names(parent.record['outputs'])
        for name in sorted(parent.files):
            data = parent.read(name)
            if name in merged:
                require(data == merged[name].read(name), 'GENERATION_PARENT_CONFLICT: ' + name)
            else:
                require(not any(PurePosixPath(name).is_relative_to(old) or
                                PurePosixPath(old).is_relative_to(name) for old in merged),
                        'GENERATION_PARENT_CONFLICT: ' + name)
                merged[name] = parent
    return merged


def run_stage(bwrap,snapshot,binding,argv,outputs,*,generation=None, parents=None,
              script_policy=None, timeout=7200, named=False, workspace=False, context_fd=None):
    """Execute one bounded stage and seal exact declared regular output paths.

    No caller-supplied success label is accepted. An exception, timeout, surviving
    pipe writer, missing/extra output or invalid file prevents promotion.
    """
    require(type(argv) is list and bool(argv) and all(type(a) is str and chr(0) not in a for a in argv),
            'GENERATION_ARGV')
    require(type(outputs) is list and bool(outputs) and len(outputs)<=20000 and
            len(outputs)==len(set(outputs)), 'GENERATION_OUTPUT_NAMES')
    for name in outputs:
        p=PurePosixPath(name) if type(name) is str else PurePosixPath('/')
        require(type(name) is str and str(p)==name and not p.is_absolute() and
                '..' not in p.parts and name not in ('', '.') and len(p.parts)<=32 and
                len(name)<=4095 and not any(c in name for c in '\0\n\r'),
                'GENERATION_OUTPUT_NAMES')
    require(type(named) is bool and type(workspace) is bool and (not workspace or named), 'GENERATION_WORKSPACE')
    require(type(timeout) in (int,float) and 0<timeout<=7200, 'GENERATION_TIMEOUT')
    require(Path(bwrap).is_absolute() and Path(bwrap).name=='bwrap','GENERATION_BWRAP_PATH')
    require(type(binding) is dict and argv[0] in binding.get('executables',[]),
            'GENERATION_ELF_TOOL_REQUIRED')
    require(argv[0] in snapshot.files and snapshot.manifest['files'][argv[0]]['mode']==0o555,
            'GENERATION_TOOL_UNBOUND')
    verify_elf_closure(snapshot,binding)
    script=None
    # Scripts are never executable ambient aliases or implicitly resolved shebangs.
    # The exact immutable interpreter, flags and script are a reviewed policy.
    scripts=[(i,a) for i,a in enumerate(argv[1:],1)
             if a in snapshot.files and not os.pread(snapshot.files[a],4,0)==b'\x7fELF'
             and (a.endswith(('.py','.js','.sh')) or a in (script_policy or {}))]
    interpreter=PurePosixPath(argv[0]).name
    if interpreter.startswith('python') or interpreter in ('node','sh','bash'):
        require(bool(scripts), 'GENERATION_SCRIPT_POLICY')
    if scripts:
        require(len(scripts)==1, 'GENERATION_SCRIPT_POLICY')
        index,path=scripts[0]
        policy=(script_policy or {}).get(path)
        require(type(policy) is dict and set(policy)=={'interpreter','arguments'} and
                policy['interpreter']==argv[0] and policy['arguments']==argv[1:index],
                'GENERATION_SCRIPT_POLICY')
        script={'path':path,'sha256':snapshot.manifest['files'][path]['sha256'],
                'interpreter':argv[0],'arguments':list(policy['arguments'])}
    if context_fd is not None:
        from installer_snapshot import read_context
        require(read_context(context_fd)['snapshot_sha256']==snapshot.identity, 'GENERATION_CONTEXT_MISMATCH')
    mounts=snapshot.mounts()
    passed=snapshot.transport_fds()
    if context_fd is not None:
        mounts += ['--perms','444','--ro-bind-data',str(context_fd),'/run/verified-context.json']
        passed += (context_fd,)
    require(generation is None or parents is None, 'GENERATION_PARENT_ARGUMENTS')
    parents=([generation] if generation is not None else []) if parents is None else parents
    merged=merge_parents(parents)
    for name,parent in sorted(merged.items()):
        fd=parent.files[name]
        mounts += ['--perms','444','--ro-bind-data',str(fd),'/generation/'+name]
    passed+=tuple(dict.fromkeys(parent.files[name] for name,parent in sorted(merged.items())))
    with tempfile.TemporaryDirectory(prefix='warp4-stage-') as td:
        root=Path(td)
        args=[bwrap,'--die-with-parent','--new-session','--unshare-all','--clearenv',
              '--cap-drop','ALL','--tmpfs','/','--proc','/proc','--dev','/dev','--tmpfs','/tmp']
        args+=mounts
        for alias,target in sorted(binding['interpreters'].items()):
            p=PurePosixPath(alias)
            require(p.is_absolute() and str(p)==alias and '..' not in p.parts and
                    any(p.is_relative_to(base) and p!=PurePosixPath(base) for base in ('/lib','/lib64','/usr/lib')),
                    'GENERATION_INTERPRETER_ALIAS')
            args+=['--symlink',target,alias]
        library_dirs=sorted({str(PurePosixPath(p).parent) for p in binding['libraries'].values()})
        from installer_process import clean_environment
        environment=clean_environment({'LD_LIBRARY_PATH':':'.join(library_dirs)} if library_dirs else {})
        if workspace:
            environment.update(HOME='/tmp/home',PLATFORMIO_CORE_DIR='/work/core',
                PLATFORMIO_SETTING_ENABLE_TELEMETRY='No',PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL='0',
                PLATFORMIO_SETTING_CHECK_PLATFORMS_INTERVAL='0',PLATFORMIO_SETTING_CHECK_LIBRARIES_INTERVAL='0',
                PLATFORMIO_LIBDEPS_DIR='/snapshot/libdeps',UV_OFFLINE='1',PIP_NO_INDEX='1',
                PATH='/snapshot/system_tools:/snapshot/node_environment/bin:/snapshot/python/bin')
        if context_fd is not None:
            environment.update(WARP4_INSTALLER_OFFLINE_SANDBOX='1',PLATFORMIO_BUILD_DIR='/output/build')
        for key,value in environment.items(): args+=['--setenv',key,value]
        args+=['--bind',td,'/output','--remount-ro','/','--chdir','/output','--']+argv
        if named:
            args=stage_materialized_command(bwrap,snapshot,binding,argv,merged,td,environment,workspace,context_fd)
            passed=snapshot.transport_fds()
        result=spawn(args,passed,root,stdout_limit=1024*1024,stderr_limit=16*1024*1024, timeout=timeout,
                     # Only the native named transport needs the 8GiB allowance.
                     # The digest-bound materializer lowers hard+soft FSIZE to
                     # 512MiB before exec of inner bwrap / any payload.
                     file_limit=(8*1024*1024*1024 if named else 512*1024*1024))
        import stat
        found=set(); directories=set()
        for base,dirs,files in os.walk(root,followlinks=False):
            for name in dirs:
                path=Path(base)/name
                require(stat.S_ISDIR(path.lstat().st_mode), 'GENERATION_DIRECTORY_LINK_FORBIDDEN')
                directories.add(path.relative_to(root).as_posix())
            for name in files:
                found.add((Path(base)/name).relative_to(root).as_posix())
            require(len(found)+len(directories)<=40000, 'GENERATION_OUTPUT_LIMIT')
        expected_dirs={str(p) for name in outputs for p in PurePosixPath(name).parents if str(p)!='.'}
        require(found==set(outputs) and directories==expected_dirs,'GENERATION_UNDECLARED_OUTPUT')
        data={}
        total=0
        for name in outputs:
            fd,st=file_fd(root/name)
            try:
                total+=st.st_size
                require(st.st_size<=64*1024*1024 and total<=512*1024*1024,'GENERATION_OUTPUT_LIMIT')
                data[name]=os.pread(fd,st.st_size+1,0)
                require(len(data[name])==st.st_size,'GENERATION_OUTPUT_SIZE')
            finally: os.close(fd)
        record={'schema':'warp4-stage-generation-v1',
                'contract':'declared-action-boundary-not-actual-open-capture',
                'snapshot_sha256':snapshot.identity,'argv':list(argv),
                'input_scope':'conservative-exposure-not-observed-consumption',
                'execution':{'cwd':'/work/project' if workspace else '/output','elf_binding':json.loads(canonical(binding)),
                             'environment':environment,'script':script,
                             'materialization':'named-private-readonly' if named else 'sealed-ro-bind-data'},
                'parent_generation':None if generation is None else generation.identity,
                'parents':[p.identity for p in parents],
                'outputs':{n:{'size':len(d),'sha256':hashlib.sha256(d).hexdigest()} for n,d in data.items()},
                'stdout_sha256':hashlib.sha256(result.stdout.encode()).hexdigest()}
        if context_fd is not None:
            record['execution']['context_sha256']=hashlib.sha256(os.pread(context_fd,os.fstat(context_fd).st_size,0)).hexdigest()
        completed=Generation(data,record)
        # Bounded actual diagnostics, not caller-supplied acceptance labels.
        completed.stdout=result.stdout
        completed.stderr=result.stderr
        return completed


def stage_materialized_command(bwrap,snapshot,binding,argv,merged,output,environment,workspace,context_fd=None):
    """Reuse the W4B1 native materializer for named stage runtime/parent bytes.

    Only this stage's fresh output directory is writable. Named inputs are in
    outer private tmpfs and mounted readonly into the inner PID namespace.
    """
    import struct
    from installer_process import clean_environment
    helper='/snapshot/system_tools/installer-materializer'
    inner_bwrap='/snapshot/system_tools/bwrap'
    entry='/snapshot/project/installer_stage_entry.py'
    for target in (helper,inner_bwrap):
        require(target in snapshot.files and snapshot.manifest['files'][target]['role']=='system_tools'
                and target in binding['executables'], 'GENERATION_MATERIALIZER_REQUIRED')
    require(entry in snapshot.files and '/snapshot/python/bin/python' in binding['executables'],
            'GENERATION_MATERIALIZER_ENTRY_REQUIRED')
    require(not any(t.startswith('/snapshot/stage-control/') for t in snapshot.files),
            'GENERATION_RESERVED_PATH')
    root='/scratch/closure'
    inner=['--die-with-parent','--new-session','--unshare-all','--clearenv','--cap-drop','ALL',
           '--tmpfs','/','--proc','/proc','--dev','/dev','--tmpfs','/tmp',
           '--ro-bind',root+'/snapshot','/snapshot','--bind','/stage-output','/output']
    if context_fd is not None:
        inner += ['--ro-bind',root+'/snapshot/stage-control/context.json','/run/verified-context.json']
    if merged:
        inner+=['--ro-bind',root+'/snapshot/stage-control/parents','/generation']
    outer=[bwrap,'--die-with-parent','--new-session','--unshare-all','--clearenv','--cap-drop','ALL',
           '--tmpfs','/','--proc','/proc','--dev','/dev','--tmpfs','/tmp','--tmpfs','/scratch',
           '--bind',output,'/stage-output','--dir','/run']
    for alias,target in sorted(binding['interpreters'].items()):
        inner+=['--symlink',target,alias]
        outer+=['--symlink',root+target,alias]
    from installer_namespace import tool_aliases
    inner+=tool_aliases(snapshot,binding)
    effective=clean_environment(environment)
    for key,value in effective.items(): inner+=['--setenv',key,value]
    if workspace:
        inner+=['--tmpfs','/work','--dir','/work/project','--dir','/work/core','--symlink','/work/project','/work/software']
        mounted_web=False
        for target in sorted(snapshot.files):
            if target.startswith('/snapshot/project/web/node_modules/'):
                if not mounted_web:
                    inner+=['--ro-bind',root+'/snapshot/project/web/node_modules','/work/project/web/node_modules']
                    mounted_web=True
            elif target.startswith('/snapshot/project/'):
                inner+=['--ro-bind',root+target,'/work/project/'+target[len('/snapshot/project/'):]]
        for sub in ('packages','platforms'):
            if any(t.startswith('/snapshot/platformio/'+sub+'/') for t in snapshot.files):
                inner+=['--ro-bind',root+'/snapshot/platformio/'+sub,'/work/core/'+sub]
    inner+=['--remount-ro','/','--chdir','/work/project' if workspace else '/output']
    records=[(target.lstrip('/'),snapshot.files[target],item['size'],item['mode'])
             for target,item in sorted(snapshot.manifest['files'].items())]
    immutable_records = records
    records = []
    records += [('snapshot/stage-control/parents/'+name,parent.read(name),0o444)
                for name,parent in sorted(merged.items())]
    if context_fd is not None:
        records += [('snapshot/stage-control/context.json',os.pread(context_fd,os.fstat(context_fd).st_size,0),0o444)]
    payload=['/snapshot/python/bin/python' ,'-I','-B',entry]
    records += [('snapshot/stage-control/argv.json',canonical(argv),0o444),
                ('args',b'\0'.join(a.encode() for a in inner)+b'\0',0o444),
                ('payload',b'\0'.join(a.encode() for a in payload)+b'\0',0o444)]
    from installer_snapshot import sealed_bundle
    fd, bundle_digest = sealed_bundle(immutable_records +
        [(name,data,len(data),mode) for name,data,mode in records], 'warp4-named-stage')
    snapshot.bundle_fds.append(fd)
    outer+=['--perms','555','--ro-bind-data',str(snapshot.files[helper]),'/run/materializer',
            '--perms','444','--ro-bind-data',str(fd),'/run/snapshot.bundle']
    dirs=sorted({str(PurePosixPath(t).parent) for t in binding['libraries'].values()})
    for key,value in clean_environment({'LD_LIBRARY_PATH':':'.join(root+d for d in dirs)}).items():
        outer+=['--setenv',key,value]
    outer+=['--remount-ro','/','--','/run/materializer',bundle_digest,'--payload-fsize-512m']
    return outer


class StageDAG:
    """Protected host orchestration ledger, not a replacement compiler scheduler.

    Schedulers may request reviewed stages; only this broker's completed stages
    can be parents. Build JSON is ordinary untrusted output, never a ledger input.
    The host-pinned head digests must be transported in a readonly run context.
    """
    def __init__(self,bwrap,snapshot,binding):
        self.bwrap=bwrap
        self.snapshot=snapshot
        self.binding=binding
        self._completed={}
        self._ledger_fds=[]

    def run(self,name,argv,outputs,*,parents=(),**policy):
        require(type(name) is str and bool(name) and name not in self._completed, 'STAGE_NAME')
        require(type(parents) in (list,tuple) and all(type(p) is str and p in self._completed for p in parents),
                'STAGE_PARENT_UNKNOWN')
        require(not ({'parents','generation'} & set(policy)), 'STAGE_PARENT_UNKNOWN')
        result=run_stage(self.bwrap,self.snapshot,self.binding,argv,outputs,
                         parents=[self._completed[p] for p in parents],**policy)
        self._completed[name]=result
        try: self.evidence()
        except BaseException:
            del self._completed[name]
            result.close()
            raise
        return result

    def evidence(self):
        records={}
        for generation in self._completed.values():
            for name in generation.files: generation.read(name)
            records[generation.identity]=generation.record
        children={parent for record in records.values() for parent in record['parents']}
        heads=sorted(set(records)-children)
        if records: verify_stage_records(list(records.values()),self.snapshot.identity,heads)
        # Caller cannot mutate the broker's original record objects via export.
        return json.loads(canonical({'schema':'warp4-stage-dag-v1','snapshot_sha256':self.snapshot.identity,
                                     'records':list(records.values()),'heads':heads}))

    def seal_evidence(self):
        evidence=self.evidence()
        require(bool(evidence['records']), 'STAGE_EMPTY_DAG')
        fd=sealed(canonical(evidence),'warp4-host-stage-ledger')
        self._ledger_fds.append(fd)
        return fd

    def close(self):
        for generation in self._completed.values(): generation.close()
        self._completed.clear()
        for fd in self._ledger_fds: os.close(fd)
        self._ledger_fds.clear()

    def __enter__(self): return self
    def __exit__(self,*unused): self.close()
