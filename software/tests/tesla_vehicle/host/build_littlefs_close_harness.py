#!/usr/bin/env python3
"""Extract untouched production definitions; compile full LittleFS in scratch.
No package writes. SDK scheduling/dispatch shims are native test boundaries.
"""
from pathlib import Path
import hashlib, json, os, re, subprocess
root = Path(__file__).resolve().parents[4]
builder = Path(os.environ.get('TESLA_BLE_HOST_FRAMEWORK_SOURCE', '/home/hermes/.hermes/cache/scratch/esp32-arduino-lib-builder'))
component = builder / 'managed_components/joltwallet__littlefs'
backend = component / 'src/esp_littlefs.c'
vfs = builder / 'esp-idf/components/vfs/vfs.c'
api = component / 'src/littlefs_api.h'
private = builder / 'esp-idf/components/vfs/private_include/esp_vfs_private.h'
out = Path(os.environ.get('TESLA_CLEAN_TEST_OUT', str(Path(os.environ['TMPDIR']) / 'warp-tesla-host-tests'))) / 'littlefs-close'
out.mkdir(parents=True, exist_ok=True)

def definition(path, name):
    text = path.read_text()
    # Match definitions, never prototypes; comments/strings excluded for braces.
    for match in re.finditer(r'(?m)^[\w *]+\b'+re.escape(name)+r'\s*\(', text):
        begin = match.start()
        brace = text.find('{', match.end())
        semi = text.find(';', match.end())
        if semi >= 0 and semi < brace: continue
        depth = 0
        tokens = re.finditer(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', text[brace:])
        for token in tokens:
            if token.group() == '{': depth += 1
            elif token.group() == '}':
                depth -= 1
                if depth == 0:
                    end = brace + token.end()
                    return f'\n#line {text.count(chr(10),0,begin)+1} "{path}"\n' + text[begin:end] + '\n'
    raise RuntimeError(f'No definition: {name}')

def lines(path, start, stop):
    return f'\n#line {start} "{path}"\n'+'\n'.join(path.read_text().splitlines()[start-1:stop])+'\n'

# Preserve production layout/config. Do not invent a reduced wrapper/cache type.
parts = ['#include "littlefs_close_support.h"\n', lines(api,23,99), lines(private,22,29), lines(vfs,49,65)]
parts += ['static vfs_entry_t *s_vfs[VFS_MAX_COUNT];\nstatic fd_table_t s_fd_table[MAX_FDS];\nstatic _lock_t s_fd_table_lock;\n']
for name in ['esp_littlefs_calloc','esp_littlefs_free_fds','lfs_errno_remap','sem_take','sem_give','esp_littlefs_allocate_fd','esp_littlefs_free_fd','esp_littlefs_get_updated_time','vfs_littlefs_close']:
    parts.append(definition(backend,name))
parts.append(lines(vfs,861,870))
for name in ['get_vfs_for_index','fd_valid','get_vfs_for_fd','get_local_fd','esp_vfs_close']:
    parts.append(definition(vfs,name))
parts += ['#include "littlefs_close_device.inc"\n']
source=out/'backend.c'; source.write_text(''.join(parts))
inputs=[backend,vfs,api,private,component/'src/littlefs/lfs.c',component/'src/littlefs/lfs_util.c',component/'idf_component.yml']
manifest={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
(out/'source-hashes.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Production ownership harness input hashes:',out/'source-hashes.json',flush=True)
instrument = ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if os.environ.get('TESLA_CLEAN_HARNESS_SANITIZE') == '1' else []
subprocess.run(['gcc',*instrument,'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DLFS_MULTIVERSION','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-I'+str(root/'software/tests/tesla_vehicle/host'),'-I'+str(component/'src/littlefs'),'-c',str(source),'-o',str(out/'backend.o')],check=True)
for name in ['lfs','lfs_util']:
    subprocess.run(['gcc',*instrument,'-std=gnu11','-DLFS_MULTIVERSION','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-I'+str(component/'src/littlefs'),'-c',str(component/f'src/littlefs/{name}.c'),'-o',str(out/f'{name}.o')],check=True)
