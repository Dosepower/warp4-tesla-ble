"""Observed local dependencies for isolated tests, never production approval."""
import hashlib
import io
import os
from pathlib import Path
import subprocess
from elftools.elf.elffile import ELFFile

def full_inputs(root):
    inputs={}; executables=[]; interpreters={}; libraries={}; needed=[]
    pythonhome=Path('.venv/bin/python').resolve().parents[1]
    def add(target,path,role,executable=None):
        path=Path(path).resolve(); data=path.read_bytes()
        if path.stat().st_nlink!=1:
            copy=root/('dependency-'+str(len(inputs)));copy.write_bytes(data);path=copy
        native=data.startswith(b'\x7fELF')
        if native:
            elf=ELFFile(io.BytesIO(data))
            rpath=any(t.entry.d_tag in ('DT_RPATH','DT_RUNPATH') for s in elf.iter_segments()
                      if s['p_type']=='PT_DYNAMIC' for t in s.iter_tags())
            if rpath:
                # Test-only relocation of the real local CPython runtime. The
                # production loader policy is not bypassed or relaxed.
                copy=root/('relocated-'+str(len(inputs)));copy.write_bytes(data)
                subprocess.run([os.environ['WARP4_TEST_PATCHELF'],'--remove-rpath',str(copy)],check=True)
                path=copy;data=path.read_bytes()
        if executable is None: executable=bool(path.stat().st_mode&0o111)
        inputs[target]={'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),
                        'mode':0o555 if executable else 0o444,'role':role}
        if native and 'watcher-linux-x64-musl' not in target:
            elf=ELFFile(io.BytesIO(data))
            if elf['e_type'] not in ('ET_EXEC','ET_DYN'): return
            inputs[target]['mode']=0o555
            if role!='loader_runtime': executables.append(target)
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_INTERP':
                    original=segment.get_interp_name()
                    interpreters[original]='/snapshot/loader_runtime/'+Path(original).name
                    needed.append(Path(original).name)
                if segment['p_type']=='PT_DYNAMIC':
                    needed.extend(t.needed for t in segment.iter_tags() if t.entry.d_tag=='DT_NEEDED')
    def tree(prefix,root,role,skip=()):
        for p in sorted(Path(root).rglob('*')):
            if not p.is_file() or '__pycache__' in p.parts or p.suffix in ('.pyc','.pyo'):continue
            rel=p.relative_to(root)
            if any(s in rel.parts for s in skip):continue
            add(prefix+'/'+rel.as_posix(),p,role)
    add('/snapshot/python/bin/python',pythonhome/'bin/python3.12','python_environment',True)
    tree('/snapshot/python/lib/python3.12',pythonhome/'lib/python3.12','python_environment',('site-packages',))
    tree('/snapshot/python/lib/python3.12/site-packages',Path('.venv/lib/python3.12/site-packages'),'python_environment',('nodeenv',))
    add('/snapshot/node_environment/bin/node','.venv/bin/node','node_environment',True)
    tree('/snapshot/project/web/node_modules',Path('web/node_modules'),'web_dependencies',('.bin','watcher-linux-x64-musl'))
    # Complete tracked software source, no generated files, cache, .git or worktree extras.
    tracked=subprocess.check_output(['git','ls-files','-z'],cwd='..').decode().split('\0')
    for p in tracked:
        if p.startswith('software/') and Path(p[9:]).is_file():
            add('/snapshot/project/'+p[9:],Path(p[9:]),'source')
    for p in Path('.').glob('installer_*.py'):
        add('/snapshot/project/'+p.name,p,'source')
    for p in ('build_warp4_installer.py','warp4_installer.ini','warp4_installer_guard.py','warp4_ota_only_hook.py','installer_secure_layout.h'):
        add('/snapshot/project/'+p,p,'source')
    # Official config includes keys not needed by this positive synthetic-key test.
    # The key/config replacement itself is built in the test, never this tree.
    scons=Path('/home/hermes/.platformio/packages/tool-scons')
    local=list(scons.glob('scons-local-*')); assert len(local)==1
    tree('/snapshot/python/lib/python3.12/site-packages',local[0],'python_environment')
    for name,env in [('bwrap','WARP4_TEST_BWRAP'),('installer-materializer','WARP4_TEST_MATERIALIZER')]:
        add('/snapshot/system_tools/'+name,os.environ[env],'system_tools',True)
    while needed:
        name=needed.pop()
        if name in libraries:continue
        candidates=[pythonhome/'lib'/name,Path('/usr/lib/x86_64-linux-gnu')/name]
        path=next((p for p in candidates if p.is_file()),None)
        if path is None: raise AssertionError('exact local native dependency absent: '+name)
        target='/snapshot/loader_runtime/'+name; libraries[name]=target
        add(target,path,'loader_runtime',True)
    return inputs,{'executables':sorted(set(executables)), 'interpreters':interpreters,'libraries':libraries}
