#!/usr/bin/env python3
"""Pinned WARP4 Bluetooth source-review candidate. Source/build only.

Run from software: python3 lib-builder/tesla_sdk.py prepare [--build]
No signing keys, upload helpers, device discovery or provisioning are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

S = Path(__file__).resolve().parents[1]
ROOT = S / '.pio/tesla-sdk'
# Exact upstream archive provenance is currently not retrievable from the
# advertised public repositories. These are identifiers, NOT validated pins.
UPSTREAM_PACKAGE_REVISIONS = {'builder': 'd5a56a1', 'esp-idf': 'd2a6bf5f2b4', 'arduino': 'fa903c6b9'}
PINS = {
    'builder': ('https://github.com/espressif/esp32-arduino-lib-builder.git', '27f49ef07016649a7de23fb91f3392961b3b4098'),
    'esp-idf-secure': ('https://github.com/espressif/esp-idf.git', 'd2a01b4ca9b8589272fb0d69791d8c02ee524ad3'),
    'builder/components/arduino': ('https://github.com/Tinkerforge/arduino-esp32.git', '0ca24d74b6ec582e2780a9fdc9aadead8e272739'),
}

def run(*args, cwd=None, **kw):
    print('+', *map(str, args), flush=True)
    return subprocess.run(list(map(str, args)), cwd=cwd, check=True, **kw)

def parse_config(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r'(CONFIG_\w+)=(.*)', line.strip())
        off = re.fullmatch(r'# (CONFIG_\w+) is not set', line.strip())
        if match:
            result[match[1]] = match[2]
        elif off:
            result[off[1]] = 'n'
    return result

def validate_config(cfg, base):
    for key, val in base.items():
        if key.startswith(('CONFIG_SECURE_', 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK', 'CONFIG_SPIRAM_')):
            if cfg.get(key, 'n') != val:
                raise ValueError('Upstream security/PSRAM setting changed: ' + key)
    required = {'CONFIG_PARTITION_TABLE_OFFSET': '0xe000', 'CONFIG_BT_ENABLED': 'y',
                'CONFIG_BT_BLUEDROID_ENABLED': 'y', 'CONFIG_BT_BLE_ENABLED': 'y',
                'CONFIG_BTDM_CTRL_MODE_BLE_ONLY': 'y',
                'CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST': 'n'}
    for key, val in required.items():
        if cfg.get(key, 'n') != val:
            raise ValueError('Required secure Bluetooth SDK setting: ' + key + '=' + val)

def validate_targets(targets):
    if list(targets) != ['tesla-review']:
        raise ValueError('Source review only: use pio run -e warp4_tesla_ble -t tesla-review; no image/device targets')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def inputs():
    paths = [S / 'lib-builder/defconfig.esp32secure', S / 'lib-builder/defconfig.warp4-tesla-ble']
    paths += sorted((S / 'patches/lib-builder').rglob('*.patch'))
    paths += sorted((S / 'patches/lib-builder').rglob('*.rawpatch'))
    paths += sorted((S / 'patches/lib-builder-pre').rglob('*.patch'))
    return {str(p.relative_to(S)): sha(p) for p in paths}

def validate_files(expected, actual):
    # pioarduino creates one MCU-specific backup before component filtering.
    # Permit only an exact copy, never unverified new code or archive changes.
    backup = 'esp32/pioarduino-build.py.esp32'
    if backup in actual and backup not in expected:
        if actual[backup] != expected.get('esp32/pioarduino-build.py'):
            raise ValueError('PlatformIO build-script backup differs')
        actual = {k: v for k, v in actual.items() if k != backup}
    if actual != expected:
        raise ValueError('SDK package content differs from its build manifest')

def verify_package(package):
    package = Path(package)
    manifest = json.loads((package / 'tesla-sdk.json').read_text())
    # This verifier supports ONLY the pinned fallback. Exact-package support
    # needs a separate provenance verifier, never a self-asserted manifest label.
    if (manifest.get('installable') is not False or
            manifest.get('exact_upstream_package') is not False or
            manifest.get('unresolved_upstream_revisions') != unresolved_revisions()):
        raise ValueError('SDK must remain a non-installable fallback with all unresolved official pins')
    if manifest.get('schema') != 1 or manifest.get('pins') != PINS_as_json() or manifest.get('inputs') != inputs():
        raise ValueError('Missing/stale pinned SDK provenance; rebuild, never reuse checkpoint archives')
    files = manifest.get('files', {})
    if not files or 'esp32/lib/libbt.a' not in files:
        raise ValueError('Missing Bluetooth archives')
    actual = {str(p.relative_to(package)): sha(p) for p in package.rglob('*')
              if p.is_file() and p.name != 'tesla-sdk.json'}
    validate_files(files, actual)
    cfg = parse_config((package / 'esp32/sdkconfig').read_text())
    validate_config(cfg, parse_config((S / 'lib-builder/defconfig.esp32secure').read_text()))
    return manifest | {'disposition': 'noninstallable-fallback-review'}

def unresolved_revisions():
    return [{'component': key, 'revision': rev} for key, rev in UPSTREAM_PACKAGE_REVISIONS.items()]

def PINS_as_json():
    return {k: list(v) for k, v in PINS.items()}

def apply_patches(tree, patches):
    for patch in sorted(patches.rglob('*')):
        if patch.suffix not in ('.patch', '.rawpatch'):
            continue
        relative = patch.parent.relative_to(patches)
        target = tree / relative
        # All patches are required, including managed components. Missing source
        # is a real preparation blocker rather than permission to skip a patch.
        if not target.is_dir():
            raise ValueError('Patch target unavailable: ' + str(relative))
        reverse = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=target,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if reverse.returncode:
            run('git', 'apply', '--check', patch, cwd=target)
            run('git', 'apply', patch, cwd=target)

def prepare(build=False):
    ROOT.mkdir(parents=True, exist_ok=True)
    for relative, (url, rev) in PINS.items():
        target = ROOT / relative
        if not target.exists():
            run('git', 'clone', '--no-checkout', '--filter=blob:none', url, target)
            run('git', 'checkout', '--detach', rev, cwd=target)
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=target, text=True).strip()
        if actual != rev:
            raise ValueError('Wrong source revision at ' + relative + '; use a fresh owned build directory')
    builder = ROOT / 'builder'
    idf = ROOT / 'esp-idf-secure'
    apply_patches(builder, S / 'patches/lib-builder-pre')
    run('git', 'submodule', 'update', '--init', '--recursive', '--depth=1', cwd=idf)
    # Download exact upstream managed versions before freezing override paths.
    arduino = builder / 'components/arduino'
    manifest = subprocess.check_output(['git', 'show', 'HEAD:idf_component.yml'], cwd=arduino, text=True)
    for old, new in (('^1.11.2', '1.13.1'), ('^1.0.22', '1.0.22~1'), ('^1.22.1', '1.22.3')):
        manifest = manifest.replace('version: "' + old + '"', 'version: "' + new + '"')
    (arduino / 'idf_component.yml').write_text(manifest)
    base = parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
    cfg = base | parse_config((S / 'lib-builder/defconfig.warp4-tesla-ble').read_text())
    validate_config(cfg, base)
    (builder / 'configs/defconfig.esp32').write_text(''.join(k + '=' + v + '\n' for k, v in cfg.items()))
    (builder / 'sdkconfig').write_text(''.join(k + '=' + v + '\n' for k, v in cfg.items()))
    # A CMake build tree is bound to its IDF source path. Retain earlier failed
    # experiments, but never mix their generated objects into this build.
    cache = builder / 'build/CMakeCache.txt'
    if cache.exists() and str(idf) not in cache.read_text():
        import shutil
        shutil.move(builder / 'build', builder / 'build-plain-tag-rejected')
    env = dict(os.environ, IDF_PATH=str(idf), IDF_TOOLS_PATH=str(ROOT / 'tools'), AR_SOURCE_BRANCH='tf-3-3-12')
    run('bash', idf / 'install.sh', 'esp32', env=env, cwd=builder)
    command = '. "$IDF_PATH/export.sh" && ./build.sh -s -t esp32 -b reconfigure'
    run('bash', '-c', command, cwd=builder, env=env)
    for component in ('espressif/mdns', 'espressif/libsodium', 'joltwallet/littlefs'):
        local = component.replace('/', '__')
        manifest = manifest.replace('  ' + component + ':\n',
            '  ' + component + ':\n    override_path: ../../managed_components/' + local + '\n')
    (arduino / 'idf_component.yml').write_text(manifest)
    apply_patches(builder / 'managed_components', S / 'patches/lib-builder/managed_components')
    # SDK is a sibling, not builder/esp-idf; apply its full patch set explicitly.
    apply_patches(idf, S / 'patches/lib-builder/esp-idf')
    if build:
        run('bash', '-c', '. "$IDF_PATH/export.sh" && ./build.sh -s -t esp32', cwd=builder, env=env)
        package = builder / 'out/tools/esp32-arduino-libs'
        config = parse_config((package / 'esp32/sdkconfig').read_text())
        validate_config(config, base)
        files = {str(p.relative_to(package)): sha(p) for p in package.rglob('*')
                 if p.is_file() and p.name != 'tesla-sdk.json'}
        manifest = {'schema': 1, 'pins': PINS_as_json(), 'inputs': inputs(), 'files': files,
                    'installable': False, 'exact_upstream_package': False,
                    'unresolved_upstream_revisions': unresolved_revisions(),
                    'sdk': 'ESP-IDF 5.5.5 / Arduino ESP32 3.3.12 source-review candidate'}
        (package / 'tesla-sdk.json').write_text(json.dumps(manifest, indent=2) + '\n')
        verify_package(package)
        print('Verified source-review SDK:', package)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'verify'])
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--package', type=Path, default=ROOT / 'builder/out/tools/esp32-arduino-libs')
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.build)
    else:
        verify_package(args.package)
