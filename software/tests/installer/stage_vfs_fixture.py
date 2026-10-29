"""Test-only frozen native closure for actual B guard acceptance. No SDK approval."""
import hashlib, subprocess, os
from pathlib import Path
from namespace_fixture import python_inputs

def native_vfs_inputs(root):
    S=Path.cwd()
    project={'installer_stage_entry.py':(S/'installer_stage_entry.py').read_bytes()}
    for sub in ['tests/tesla_vehicle','src/modules/tesla_vehicle','tools','lib/TeslaBLEClean']:
     for f in (S/sub).rglob('*'):
      if f.is_file() and not f.is_symlink() and '__pycache__' not in f.parts:project[f.relative_to(S).as_posix()]=f.read_bytes()
    tools={'/snapshot/system_tools/bwrap':Path(os.environ['WARP4_TEST_BWRAP']),'/snapshot/system_tools/installer-materializer':Path(os.environ['WARP4_TEST_MATERIALIZER']),'/snapshot/system_tools/sh':Path('/usr/bin/dash'),'/snapshot/compiler/g++':Path('/usr/bin/g++').resolve(),'/snapshot/compiler/gcc':Path('/usr/bin/gcc').resolve(),'/snapshot/compiler/as':Path('/usr/bin/as').resolve(),'/snapshot/compiler/ld':Path('/usr/bin/ld').resolve()}
    for name in ['bash','cmake','make','dirname','mkdir','ar','ranlib']:
     tools['/snapshot/cmake/bin/cmake' if name=='cmake' else '/snapshot/system_tools/'+name]=(Path('/home/hermes/.local/lib/python3.13/site-packages/cmake/data/bin/cmake') if name=='cmake' else Path('/usr/bin/'+name)).resolve()
    for name in ['cc1plus','cc1','collect2','liblto_plugin.so']:
     tools['/snapshot/compiler/'+name]=Path(subprocess.check_output(['/usr/bin/g++',('-print-file-name=' if name.endswith('.so') else '-print-prog-name=')+name],text=True).strip())
    for name in ['libasan.so.8','libubsan.so.1']:
     tools['/snapshot/loader_runtime/'+name]=Path('/usr/lib/x86_64-linux-gnu')/name
    inputs,binding=python_inputs(root,project,native_tools=tools)
    for target in ['/snapshot/system_tools/bwrap','/snapshot/system_tools/installer-materializer','/snapshot/system_tools/sh']:inputs[target]['role']='system_tools'
    def add(target,data,mode=0o444):
     if target in inputs:return
     f=root/('file-'+str(len(inputs)));f.write_bytes(data);inputs[target]={'path':str(f),'sha256':hashlib.sha256(data).hexdigest(),'mode':mode,'role':'native_test_dependency'}
    for f in Path('/usr/include').rglob('*'):
     if f.is_file():add('/snapshot/native/usr/include/'+f.relative_to('/usr/include').as_posix(),f.read_bytes())
    gcc=Path('/usr/lib/gcc/x86_64-linux-gnu/14')
    for f in gcc.rglob('*'):
     if f.is_file() and f.suffix in ('.a','.o','.h'):
      add('/snapshot/compiler/'+f.relative_to(gcc).as_posix(),f.read_bytes())
    for name in ['libstdc++.so','libgcc_s.so','libasan.so','libubsan.so']:
     add('/snapshot/compiler/'+name,(gcc/name).read_bytes())
    for name in ['crt1.o','Scrt1.o','crti.o','crtn.o','libc.so','libc.so.6','libc_nonshared.a','libm.so','libm.so.6','libmvec.so.1','libgcc_s.so.1','libpthread.a','libdl.a','librt.a','libresolv.so.2','ld-linux-x86-64.so.2']:
     f=Path('/usr/lib/x86_64-linux-gnu')/name
     if f.is_file():
      for prefix in ['/snapshot/native/usr/lib/x86_64-linux-gnu/','/snapshot/native/lib/x86_64-linux-gnu/']:add(prefix+name,f.read_bytes())
    add('/snapshot/native/lib64/ld-linux-x86-64.so.2',Path('/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2').read_bytes())
    for name in ['gcc','g++']:
     script=('#!/bin/sh\nexec /snapshot/compiler/'+name+' -B/snapshot/compiler/ --sysroot=/snapshot/native -isystem /snapshot/native/usr/include/c++/14 -isystem /snapshot/native/usr/include/x86_64-linux-gnu/c++/14 -isystem /snapshot/native/usr/include/x86_64-linux-gnu -Wl,--dynamic-linker=/snapshot/loader_runtime/ld-linux-x86-64.so.2 -Wl,-rpath,/snapshot/loader_runtime "$@"\n').encode()
     add('/snapshot/system_tools/'+name,script,0o555)
    for source,target in [(S/'.pio/tesla-sdk/builder/managed_components/joltwallet__littlefs','/snapshot/sdk_builder_sources/managed_components/joltwallet__littlefs'),(S/'.pio/tesla-sdk/esp-idf-secure/components/vfs','/snapshot/sdk_idf_sources/components/vfs'),(S/'.pio/tesla-sdk/esp-idf-secure/components/mbedtls/mbedtls','/snapshot/sdk_idf_sources/components/mbedtls/mbedtls'),(S/'.pio/libdeps/warp4/ArduinoJson','/snapshot/libdeps/ArduinoJson')]:
     if not source.is_dir():raise RuntimeError('Missing local dependency: '+str(source))
     for f in source.rglob('*'):
      if f.is_file() and not f.is_symlink() and '.git' not in f.parts:add(target+'/'+f.relative_to(source).as_posix(),f.read_bytes())
    for source in Path('/home/hermes/.local/lib/python3.13/site-packages/cmake/data/share').glob('cmake-*'):
     if source.is_dir():
      for f in source.rglob('*'):
       if f.is_file():add('/snapshot/cmake/share/'+source.name+'/'+f.relative_to(source).as_posix(),f.read_bytes())
    add('/snapshot/system_tools/cmake',b'#!/bin/sh\nexec /snapshot/cmake/bin/cmake "$@"\n',0o555)
    add('/snapshot/system_tools/python3',b'#!/bin/sh\nexec /snapshot/python/bin/python "$@"\n',0o555)
    import io
    from elftools.elf.elffile import ELFFile
    for target,item in inputs.items():
     data=Path(item['path']).read_bytes()
     if data.startswith(b'\x7fELF') and ELFFile(io.BytesIO(data))['e_type'] in ('ET_EXEC','ET_DYN'):
      item['mode']=0o555
      if target not in binding['executables'] and target not in binding['libraries'].values():binding['executables'].append(target)
    for target,item in inputs.items():
     if target.startswith('/snapshot/system_tools/'):item['role']='system_tools'
    return inputs,binding
