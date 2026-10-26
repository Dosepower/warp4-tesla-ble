#!/usr/bin/env python3
"""Compile the untouched esp_littlefs 1.22.3 VFS file hooks over full lfs.c.

Unlike build_littlefs_close_harness.py (close ownership only), every POSIX
call the production Tesla adapter makes on target (open/close/read/write/
fsync/fstat/stat/rename/unlink) is routed through the production
vfs_littlefs_* bodies, including their open-FD EBUSY guards, FD cache and
mtime attribute handling. Only the IDF public-FD table and the FreeRTOS lock
are native shims. No package writes; output in scratch.
"""
from pathlib import Path
import hashlib, json, os, re, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bench_confinement import output_plan
root = Path(__file__).resolve().parents[4]
builder = Path(os.environ['TESLA_BLE_HOST_FRAMEWORK_SOURCE'])
component = builder / 'managed_components/joltwallet__littlefs'
backend = component / 'src/esp_littlefs.c'
api = component / 'src/littlefs_api.h'
out = Path(os.environ.get('TESLA_CLEAN_TEST_OUT', str(Path(os.environ['TMPDIR']) / 'warp-tesla-host-tests'))) / 'littlefs-vfs'
output_plan(roots=(out,)).revalidate()
out.mkdir(parents=True, exist_ok=True)

def definition(path, name):
    text = path.read_text()
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

names = ['esp_littlefs_calloc', 'esp_littlefs_free_fds', 'lfs_errno_remap', 'sem_take', 'sem_give',
         'esp_littlefs_flags_conv', 'esp_littlefs_allocate_fd', 'esp_littlefs_free_fd', 'compute_hash',
         'esp_littlefs_get_fd_by_name', 'esp_littlefs_get_updated_time', 'esp_littlefs_get_mtime_attr',
         'esp_littlefs_file_sync', 'vfs_littlefs_open', 'vfs_littlefs_write', 'vfs_littlefs_read',
         'vfs_littlefs_close', 'vfs_littlefs_fsync', 'vfs_littlefs_fstat', 'vfs_littlefs_stat',
         'vfs_littlefs_unlink', 'vfs_littlefs_rename']
parts = ['#include "littlefs_vfs_support.h"\n', lines(api, 23, 99),
         'static int esp_littlefs_file_sync(esp_littlefs_t *efs, vfs_littlefs_file_t *file);\n'
         'static time_t esp_littlefs_get_mtime_attr(esp_littlefs_t *efs, const char *path);\n'
         'static time_t esp_littlefs_get_updated_time(esp_littlefs_t *efs, vfs_littlefs_file_t *file, const char *path);\n'
         'static int vfs_littlefs_fsync(void* ctx, int fd);\n']
for name in names:
    parts.append(definition(backend, name))
parts += ['#include "littlefs_vfs_device.inc"\n']
source = out / 'backend.c'; source.write_text(''.join(parts))
inputs = [backend, api, component/'src/littlefs/lfs.c', component/'src/littlefs/lfs_util.c', component/'idf_component.yml']
(out/'source-hashes.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, indent=2)+'\n')
print('Production VFS harness input hashes:', out/'source-hashes.json', flush=True)
instrument = ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if os.environ.get('TESLA_CLEAN_HARNESS_SANITIZE') == '1' else []
common = ['-std=gnu11','-DLFS_MULTIVERSION','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-I'+str(component/'src/littlefs')]
subprocess.run(['gcc',*instrument,*common,'-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',
                '-I'+str(root/'software/tests/tesla_vehicle/host'),'-c',str(source),'-o',str(out/'backend.o')],check=True)
for name in ['lfs','lfs_util']:
    subprocess.run(['gcc',*instrument,*common,'-c',str(component/f'src/littlefs/{name}.c'),'-o',str(out/f'{name}.o')],check=True)
