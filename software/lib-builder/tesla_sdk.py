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
# Official WARP 2.13.6 reference package (software/packages/config.json):
# Tinkerforge/esp32-arduino-libs tf-esp-secure-20261022, versions.txt below.
OFFICIAL_PACKAGE = {'repository': 'https://github.com/Tinkerforge/esp32-arduino-libs',
                    'branch': 'tf-esp-secure-20261022',
                    'commit': 'd80bc8e05ab1012010f5acd87ca974206a64f4ee'}
# Its versions.txt revisions are local `git am` commits (lib-builder/apply_patches.py)
# and do not exist in the advertised public repositories (GitHub HTTP 422).
# These are identifiers, NOT validated pins.
UPSTREAM_PACKAGE_REVISIONS = {'builder': 'b63070e', 'esp-idf': 'a3afb692749', 'arduino': 'b280564c6'}
# Gaps of the superseded tf-esp-secure-20260922 reference stay documented; they
# were never resolved and are not erased by the base update.
PREVIOUS_UPSTREAM_PACKAGE_REVISIONS = {'builder': 'd5a56a1', 'esp-idf': 'd2a6bf5f2b4', 'arduino': 'fa903c6b9'}
# Public bases verified for the 2026-10-22 official build window:
# esp-idf release/v5.5 tip 3ea97e75 (tree 650876b9 == tag v5.5.6 commit 880a203a),
# lib-builder master tip ce4c80a2, Tinkerforge arduino-esp32 tf-3-3-12 tip 0ca24d74.
PINS = {
    'builder': ('https://github.com/espressif/esp32-arduino-lib-builder.git', 'ce4c80a2f6cbf8c55ba246ffa2000fcdb33d9e6c'),
    'esp-idf-secure': ('https://github.com/espressif/esp-idf.git', '3ea97e7591b6a83f5d6f8e0cc7f1b16f27b2b855'),
    'builder/components/arduino': ('https://github.com/Tinkerforge/arduino-esp32.git', '0ca24d74b6ec582e2780a9fdc9aadead8e272739'),
}
IDF_BRANCH = 'release/v5.5'
# Annotated tag object and peeled commit (GitHub API); HEAD is its merge into release/v5.5.
IDF_RELEASE_TAG = ('v5.5.6', '28871d80f730e6976c9bdc9c4cbff1c916e905a9', '880a203ae603d08a94d06aa01e0d6761958e5e9e')
IDF_VERSION = (5, 5, 6)
# Managed component versions recorded in the official versions.txt.
MANAGED_VERSIONS = (('^1.11.2', '1.14.0'), ('^1.0.22', '1.0.22~1'), ('^1.22.1', '1.23.1'))
# Registry component_hash (components.espressif.com API) of exactly those versions,
# recomputed with idf_component_tools hash_dir over the downloaded archives.
MANAGED_COMPONENT_HASHES = {
    'espressif__mdns': 'b5b30022c46302eab89c1d2e3b6fb5918a627563a7462f0ee284b22b9a934967',
    'espressif__libsodium': 'd4066b373fc6d4c5196db42aeea7faef4ac7f3080af96c2b74e2476c08b78dc1',
    'joltwallet__littlefs': 'e1a0f4a6adf821759cb34703a91107bbdce5d7ff6f4728f40ab012bb87842974',
}
# The NimBLE source policy is a closed singleton: exactly this patch and its inventory.
NIMBLE_POLICY_FILES = ('sdk.patch', 'source-policy.json')
# Upstream build_idf_libs.py refreshes cacrt_all.pem from curl.se at build time.
# The official bundle bytes equal gen_crt_bundle.py over this dated release.
CA_BUNDLE = 'lib-builder/cacert-2026-10-20.pem'
CA_BUNDLE_SHA256 = '796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe'
IDF_CA_BUNDLE = 'components/mbedtls/esp_crt_bundle/cacrt_all.pem'
IDF_CA_BUNDLE_PRISTINE_SHA256 = '50a6277ec69113f00c5fd45f09e8b97a4b3e32daa35d3a95ab30137a55386cef'
SDK_LABEL = 'ESP-IDF 5.5.6 / Arduino ESP32 3.3.12 source-review candidate'

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

STOCK_POLICY = 'stock'
TESLA_POLICY = 'tesla-psram-first-v1'
SINGLE_LINK_POLICY = 'tesla-psram-first-single-link-v1'
CLASSIC_RELEASE_POLICY = 'tesla-psram-first-single-link-classic-release-v1'
NIMBLE_POLICY = 'tesla-psram-first-nimble-single-link-init-v2'
NIMBLE_SETTINGS = parse_config((S / 'lib-builder/defconfig.warp4-tesla-nimble').read_text())
CLASSIC_CORE_SHA256 = 'fcb203dcb5a4b9f2fb86cd3c1a354b8cdcf641c5279375147695c88449598899'
SINGLE_LINK_SETTINGS = {'CONFIG_BT_ACL_CONNECTIONS': '1',
                        'CONFIG_BTDM_CTRL_BLE_MAX_CONN': '1',
                        'CONFIG_BTDM_CTRL_BLE_MAX_CONN_EFF': '1',
                        'CONFIG_BTDM_CONTROLLER_BLE_MAX_CONN': '1',
                        'CONFIG_BTDM_CONTROLLER_BLE_MAX_CONN_EFF': '1',
                        'CONFIG_BT_MULTI_CONNECTION_ENBALE': 'n',
                        'CONFIG_BT_BLE_SMP_ENABLE': 'y',
                        'CONFIG_BT_GATTC_ENABLE': 'y',
                        'CONFIG_BT_GATTS_ENABLE': 'n',
                        'CONFIG_BT_CLASSIC_ENABLED': 'n',
                        'CONFIG_BLE_MESH': 'n',
                        'CONFIG_BT_BTC_TASK_STACK_SIZE': '8192',
                        'CONFIG_BT_BTU_TASK_STACK_SIZE': '8192'}
MALLOC_THRESHOLD = 'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'

def validate_config(cfg, base, policy=STOCK_POLICY):
    if policy not in (STOCK_POLICY, TESLA_POLICY, SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        raise ValueError('Unknown allocation policy')
    psram_first = policy in (TESLA_POLICY, SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY, NIMBLE_POLICY)
    if psram_first:
        if base.get(MALLOC_THRESHOLD) != '2048' or cfg.get(MALLOC_THRESHOLD) != '0':
            raise ValueError('Tesla allocation policy requires exactly 2048 -> 0')
    if policy in (SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY):
        for key, value in SINGLE_LINK_SETTINGS.items():
            if cfg.get(key, 'n') != value:
                raise ValueError('Single-link candidate setting changed: ' + key)
    if policy == NIMBLE_POLICY:
        for key, value in NIMBLE_SETTINGS.items():
            if cfg.get(key, 'n') != value:
                raise ValueError('NimBLE candidate setting changed: ' + key)
    protected = ('CONFIG_SECURE_', 'CONFIG_BOOTLOADER_APP_', 'CONFIG_SPIRAM',
                 'CONFIG_PARTITION_TABLE', 'CONFIG_ESPTOOLPY_FLASH', 'CONFIG_ESP32_REV_MIN',
                 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM')
    reference = parse_config((S / 'lib-builder/defconfig.warp4-protected-resolved').read_text()) | base
    for key in base.keys() | cfg.keys():
        if key.startswith(protected):
            val = reference.get(key, 'n')
            if psram_first and key == MALLOC_THRESHOLD:
                continue
            if cfg.get(key, 'n') != val:
                raise ValueError('Upstream security/PSRAM setting changed: ' + key)
    required = {'CONFIG_PARTITION_TABLE_OFFSET': '0xe000', 'CONFIG_BT_ENABLED': 'y',
                'CONFIG_BT_BLUEDROID_ENABLED': 'y', 'CONFIG_BT_BLE_ENABLED': 'y',
                'CONFIG_BTDM_CTRL_MODE_BLE_ONLY': 'y',
                'CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST': 'n'}
    if policy == NIMBLE_POLICY:
        required.pop('CONFIG_BT_BLUEDROID_ENABLED')
        required.pop('CONFIG_BT_BLE_ENABLED')
    for key, val in required.items():
        if cfg.get(key, 'n') != val:
            raise ValueError('Required secure Bluetooth SDK setting: ' + key + '=' + val)

def profile_config(policy=STOCK_POLICY):
    base = parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
    cfg = base | parse_config((S / 'lib-builder/defconfig.warp4-tesla-ble').read_text())
    if policy in (TESLA_POLICY, SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        cfg |= parse_config((S / 'lib-builder/defconfig.warp4-tesla-psram-first').read_text())
    if policy in (SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY):
        cfg |= parse_config((S / 'lib-builder/defconfig.warp4-tesla-single-link').read_text())
    if policy == NIMBLE_POLICY:
        # Legacy Bluedroid aliases can override the requested host during IDF
        # resolution. Start the NimBLE host namespace solely from its policy.
        for key in list(cfg):
            if key.startswith(('CONFIG_BT_', 'CONFIG_BLUEDROID', 'CONFIG_NIMBLE', 'CONFIG_BLE_')):
                cfg.pop(key)
        cfg |= NIMBLE_SETTINGS | {'CONFIG_BT_ENABLED': 'y', 'CONFIG_BT_CONTROLLER_ENABLED': 'y'}
    validate_config(cfg, base, policy=policy)
    return cfg

def validate_targets(targets):
    if list(targets) != ['tesla-review']:
        raise ValueError('Source review only: use pio run -e warp4_tesla_ble -t tesla-review; no image/device targets')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify_classic_core(path):
    # Pinned output of the narrow patch against the actual core. Both the SDK
    # source/export and the separately compiled Arduino core must match.
    if sha(Path(path)) != CLASSIC_CORE_SHA256:
        raise ValueError('Classic release core differs from tested source patch')

def inputs():
    paths = [S / 'lib-builder/tesla_sdk.py', S / 'lib-builder/defconfig.esp32secure', S / 'lib-builder/defconfig.warp4-tesla-ble', S / 'lib-builder/defconfig.warp4-protected-resolved', S / 'lib-builder/defconfig.warp4-tesla-psram-first']
    paths += [S / 'lib-builder/defconfig.warp4-tesla-single-link']
    paths += [S / 'lib-builder/defconfig.warp4-tesla-nimble']
    paths += [S / CA_BUNDLE]
    paths += nimble_policy_paths(S)
    paths += sorted((S / 'patches/lib-builder').rglob('*.patch'))
    paths += sorted((S / 'patches/lib-builder').rglob('*.rawpatch'))
    paths += sorted((S / 'patches/lib-builder-pre').rglob('*.patch'))
    return {str(p.relative_to(S)): sha(p) for p in paths}

def nimble_policy_paths(project):
    # Reject any extra/missing policy file and bind the inventory to its patch.
    p = Path(project) / 'lib-builder/nimble-init-v2'
    entries = sorted(p.iterdir())
    if [e.name for e in entries] != sorted(NIMBLE_POLICY_FILES) or not all(e.is_file() and not e.is_symlink() for e in entries):
        raise ValueError('NimBLE source policy must contain exactly ' + ', '.join(NIMBLE_POLICY_FILES))
    inventory = json.loads((p / 'source-policy.json').read_text())
    if inventory.get('policy') != NIMBLE_POLICY or inventory.get('sdk_patch_sha256') != sha(p / 'sdk.patch'):
        raise ValueError('NimBLE source policy inventory is not bound to its sdk.patch')
    return entries

def strip_manager_metadata(component, expected):
    # idf-component-manager 2.x adds two root files that are not part of the
    # registry archive: .component_hash (the lock hash) and CHECKSUMS.json
    # (downloaded separately from the registry checksums_url). The manager's
    # own validate_hash_eq_hashdir excludes exactly these two. Accept them
    # only as consistent metadata of the pristine tree, then remove them so
    # hash_dir sees precisely the archive content.
    component = Path(component)
    marker = component / '.component_hash'
    if marker.is_symlink() or (marker.exists() and not marker.is_file()):
        raise ValueError('Managed component hash marker is not a regular file: ' + component.name)
    if marker.is_file() and marker.read_text().strip() != expected:
        raise ValueError('Managed component hash marker differs from pinned hash: ' + component.name)
    checksums = component / 'CHECKSUMS.json'
    if checksums.is_symlink() or (checksums.exists() and not checksums.is_file()):
        raise ValueError('Managed component CHECKSUMS.json is not a regular file: ' + component.name)
    if checksums.is_file():
        data = json.loads(checksums.read_text())
        listed = {}
        for entry in data.get('files', ()):
            path = entry.get('path')
            if not isinstance(path, str) or path in listed:
                raise ValueError('Managed component CHECKSUMS.json is malformed: ' + component.name)
            listed[path] = (entry.get('size'), entry.get('hash'))
        actual = {}
        for p in component.rglob('*'):
            if p.is_symlink():
                raise ValueError('Managed component contains a symlink: ' + component.name)
            rel = p.relative_to(component).as_posix()
            if p.is_file() and rel not in ('.component_hash', 'CHECKSUMS.json'):
                actual[rel] = (p.stat().st_size, sha(p))
        if data.get('version') != '1.0' or data.get('algorithm') != 'sha256' or listed != actual:
            raise ValueError('Managed component CHECKSUMS.json does not describe its content: ' + component.name)
    marker.unlink(missing_ok=True)
    checksums.unlink(missing_ok=True)

def verify_managed_components(builder, env):
    # Content hash of the downloaded registry components before WARP patches.
    # Already-patched trees are hashed from a scratch copy with the exact
    # pinned patch reverse-applied; anything else must match pristine bytes.
    import shutil
    import tempfile
    builder = Path(builder)
    patches = S / 'patches/lib-builder/managed_components'
    script = ('import sys\nfrom idf_component_tools.hash_tools.calculate import hash_dir\n'
              'print(hash_dir(sys.argv[1]))\n')
    for local, expected in MANAGED_COMPONENT_HASHES.items():
        source = builder / 'managed_components' / local
        if not source.is_dir() or source.is_symlink():
            raise ValueError('Managed component unavailable: ' + local)
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / local
            shutil.copytree(source, copy, symlinks=True)
            for patch in sorted((patches / local).glob('*')) if (patches / local).is_dir() else ():
                undo = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=copy, env=patch_env(copy),
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if undo.returncode == 0:
                    run('git', 'apply', '--reverse', patch, cwd=copy, env=patch_env(copy))
            strip_manager_metadata(copy, expected)
            out = subprocess.run(['bash', '-c', '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && python -c "$0" "$1"',
                                  script, str(copy)], env=env, text=True, stdout=subprocess.PIPE, check=True)
            actual = out.stdout.strip().splitlines()[-1] if out.stdout.strip() else ''
        if actual != expected:
            raise ValueError('Managed component content differs from pinned registry hash: ' + local)

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

def verify_package(package, policy=STOCK_POLICY):
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
    if (manifest.get('reference_package') != OFFICIAL_PACKAGE or manifest.get('sdk') != SDK_LABEL or
            manifest.get('previous_unresolved_upstream_revisions') != previous_unresolved_revisions()):
        raise ValueError('SDK reference package/version provenance differs')
    verify_idf_version(package)
    if manifest.get('allocation_policy') != policy:
        raise ValueError('SDK allocation policy mismatch')
    if policy in (CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        verify_classic_core(package / 'tesla-core/esp32-hal-bt.c')
    if policy == NIMBLE_POLICY:
        verify_candidate_export(package, policy)
        if manifest.get('sdk_source_policy') != source_policy_inventory(package / 'tesla-sdk-source', policy):
            raise ValueError('SDK source policy inventory differs from exact candidate hashes')
    files = manifest.get('files', {})
    if not files or 'esp32/lib/libbt.a' not in files:
        raise ValueError('Missing Bluetooth archives')
    actual = {str(p.relative_to(package)): sha(p) for p in package.rglob('*')
              if p.is_file() and p.name != 'tesla-sdk.json'}
    validate_files(files, actual)
    cfg = parse_config((package / 'esp32/sdkconfig').read_text())
    validate_config(cfg, parse_config((S / 'lib-builder/defconfig.esp32secure').read_text()), policy=policy)
    import sys
    sys.path.insert(0, str(S))
    from installer_sdk_verify import verify_config_header
    verify_config_header(cfg, (package / 'esp32/qio_qspi/include/sdkconfig.h').read_text())
    return manifest | {'disposition': 'noninstallable-fallback-review'}

def unresolved_revisions():
    return [{'component': key, 'revision': rev} for key, rev in UPSTREAM_PACKAGE_REVISIONS.items()]

def previous_unresolved_revisions():
    return [{'component': key, 'revision': rev} for key, rev in PREVIOUS_UPSTREAM_PACKAGE_REVISIONS.items()]

def verify_idf_version(package):
    # Version evidence from the exported build output, not from a label.
    header = (Path(package) / 'esp32/include/esp_common/include/esp_idf_version.h').read_text()
    found = []
    for part in ('MAJOR', 'MINOR', 'PATCH'):
        match = re.search(r'#define ESP_IDF_VERSION_' + part + r'\s+(\d+)\s*$', header, re.M)
        if match is None:
            raise ValueError('Exported ESP-IDF version header incomplete')
        found.append(int(match[1]))
    found = tuple(found)
    if found != IDF_VERSION:
        raise ValueError('Exported ESP-IDF version is not ' + '.'.join(map(str, IDF_VERSION)))
    # gen_pioarduino_manifest.py: tags/v5.5.6-1-g... -> 5.5.6, plus the IDF HEAD.
    version = json.loads((Path(package) / 'package.json').read_text()).get('version')
    if version != '.'.join(map(str, IDF_VERSION)) + '+sha.' + PINS['esp-idf-secure'][1][:10]:
        raise ValueError('SDK package.json version does not name the pinned ESP-IDF commit')

def freeze_managed_versions(manifest):
    # Every upstream range must be replaced by its exact official version.
    for old, new in MANAGED_VERSIONS:
        if manifest.count('version: "' + old + '"') != 1:
            raise ValueError('Managed component range changed: ' + old)
        manifest = manifest.replace('version: "' + old + '"', 'version: "' + new + '"')
    return manifest

def prepare_ca_bundle(idf):
    # Pinned replacement for upstream's unpinned curl.se refresh.
    source, target = S / CA_BUNDLE, Path(idf) / IDF_CA_BUNDLE
    if sha(source) != CA_BUNDLE_SHA256:
        raise ValueError('Pinned CA bundle differs')
    if sha(target) != CA_BUNDLE_SHA256:
        if sha(target) != IDF_CA_BUNDLE_PRISTINE_SHA256:
            raise ValueError('IDF CA bundle is neither pinned ESP-IDF source nor pinned refresh')
        target.write_bytes(source.read_bytes())

def ensure_idf_release_tag(idf, offline=False):
    # Package version evidence from the real annotated release tag, not a label.
    # build.sh derives the version from `git describe --all` while HEAD stays
    # detached (CONFIG_ARDUINO_IDF_BRANCH ""): tags/v5.5.6-1-g<pin> -> 5.5.6.
    rev = PINS['esp-idf-secure'][1]
    name, tag_object, tag_commit = IDF_RELEASE_TAG
    def get(*args):
        return subprocess.run(['git', *args], cwd=idf, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL).stdout.strip()
    if not get('rev-parse', '--verify', '-q', 'refs/tags/' + name):
        if offline:
            raise ValueError('Offline SDK requires the fetched ESP-IDF release tag ' + name)
        run('git', 'fetch', '--no-tags', '--depth=2', 'origin', rev,
            '+refs/tags/' + name + ':refs/tags/' + name, cwd=idf)
    if (get('rev-parse', 'refs/tags/' + name) != tag_object or
            get('rev-parse', name + '^{commit}') != tag_commit or
            get('rev-parse', 'HEAD^2') != tag_commit or
            get('rev-parse', 'HEAD^{tree}') != get('rev-parse', tag_commit + '^{tree}')):
        raise ValueError('Pinned ESP-IDF is not the verified ' + name + ' release tree')
    # copy-libs.sh records `rev-parse --short HEAD`; a shallow clone would
    # abbreviate to 7 digits. The header verifier expects exactly 10.
    run('git', 'config', 'core.abbrev', '10', cwd=idf)

def PINS_as_json():
    return {k: list(v) for k, v in PINS.items()}

def patch_env(target):
    # git apply run inside a subdirectory of an enclosing repository (e.g.
    # builder/managed_components/*, ignored by builder's .gitignore) resolves
    # patch paths from that repository's top level and silently skips them
    # ("Skipped patch"), so --reverse --check also succeeds. Stop repository
    # discovery above the target: real roots/submodules keep their own .git.
    return dict(os.environ, GIT_CEILING_DIRECTORIES=str(Path(target).resolve().parent))

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
        env = patch_env(target)
        reverse = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=target, env=env,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if reverse.returncode:
            run('git', 'apply', '--check', patch, cwd=target, env=env)
            run('git', 'apply', patch, cwd=target, env=env)

def nimble_source_policy():
    p = S / 'lib-builder/nimble-init-v2'
    inventory = json.loads((p / 'source-policy.json').read_text())
    if inventory['policy'] != NIMBLE_POLICY or sha(p / 'sdk.patch') != inventory['sdk_patch_sha256']:
        raise ValueError('SDK source policy patch mismatch')
    return inventory

def source_policy_inventory(idf, policy):
    if policy != NIMBLE_POLICY:
        return None
    inventory = nimble_source_policy()
    files = {name: sha(Path(idf) / name) for name in inventory['files']}
    if files != {name: entry['after_sha256'] for name, entry in inventory['files'].items()}:
        raise ValueError('SDK source policy differs from explicit patched-source allowlist')
    return {'policy': policy, 'sdk_patch_sha256': inventory['sdk_patch_sha256'], 'files': files}

def prepare_candidate_sources(builder, idf, policy):
    builder, idf = Path(builder), Path(idf)
    if policy in (CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        core = builder / 'components/arduino'
        source = core / 'cores/esp32/esp32-hal-bt.c'
        if sha(source) != CLASSIC_CORE_SHA256:
            patch = S / 'patches/lib-builder/components/arduino/0002-Track-Classic-controller-memory-not-host.patch'
            run('git', 'apply', '--check', patch, cwd=core)
            run('git', 'apply', patch, cwd=core)
        verify_classic_core(source)
    if policy == NIMBLE_POLICY:
        inventory = nimble_source_policy()
        actual = {name: sha(idf / name) for name in inventory['files']}
        if actual != {name: row['after_sha256'] for name, row in inventory['files'].items()}:
            if actual != {name: row['before_sha256'] for name, row in inventory['files'].items()}:
                raise ValueError('SDK source policy is neither pristine pinned source nor exact candidate')
            patch = S / 'lib-builder/nimble-init-v2/sdk.patch'
            run('git', 'apply', '--check', patch, cwd=idf)
            run('git', 'apply', patch, cwd=idf)
        source_policy_inventory(idf, policy)

def export_candidate_sources(package, builder, idf, policy):
    import shutil
    package, builder, idf = Path(package), Path(builder), Path(idf)
    prepare_candidate_sources(builder, idf, policy)
    if policy in (CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        (package / 'tesla-core').mkdir(exist_ok=True)
        shutil.copy2(builder / 'components/arduino/cores/esp32/esp32-hal-bt.c', package / 'tesla-core/esp32-hal-bt.c')
    if policy == NIMBLE_POLICY:
        for name in nimble_source_policy()['files']:
            dest = package / 'tesla-sdk-source' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(idf / name, dest)
        verify_candidate_export(package, policy)

def verify_candidate_export(package, policy):
    if policy in (CLASSIC_RELEASE_POLICY, NIMBLE_POLICY):
        verify_classic_core(Path(package) / 'tesla-core/esp32-hal-bt.c')
    if policy == NIMBLE_POLICY:
        source_policy_inventory(Path(package) / 'tesla-sdk-source', policy)

def offline_build_sdk(builder, idf, env):
    # Exact same SDK archive/bootloader export targets as build.sh's esp32
    # target inventory; only remote distribution-index generation is excluded.
    import shlex
    targets = json.loads((builder / 'configs/builds.json').read_text())['targets']
    target, = [row for row in targets if row['target'] == 'esp32']
    stages = [('idf-libs', target['idf_libs'])]
    stages += [('copy-bootloader', config) for config in target['bootloaders']]
    stages += [('mem-variant', config) for config in target['mem_variants']]
    for stage, configs in stages:
        command = '. "$IDF_PATH/export.sh" && ./build.sh -s -t esp32 -b ' + shlex.join([stage, *configs])
        run('bash', '-c', command, cwd=builder, env=env)
    revision = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=idf, text=True).strip()
    version = subprocess.check_output(['git', 'describe', '--all'], cwd=idf, text=True).strip()
    run(os.environ.get('IDF_PYTHON_ENV_PATH', '') + '/bin/python', builder / 'tools/gen_pioarduino_manifest.py',
        '-o', builder / 'out/tools/esp32-arduino-libs', '-s', version, '-c', revision, cwd=builder, env=env)

def prepare(build=False, policy=STOCK_POLICY, offline=False):
    ROOT.mkdir(parents=True, exist_ok=True)
    for relative, (url, rev) in PINS.items():
        target = ROOT / relative
        if not target.exists():
            if offline:
                raise ValueError('Offline SDK source unavailable: ' + relative)
            run('git', 'clone', '--no-checkout', '--filter=blob:none', url, target)
            run('git', 'checkout', '--detach', rev, cwd=target)
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=target, text=True).strip()
        if actual != rev:
            raise ValueError('Wrong source revision at ' + relative + '; use a fresh owned build directory')
    builder = ROOT / 'builder'
    idf = ROOT / 'esp-idf-secure'
    ensure_idf_release_tag(idf, offline=offline)
    apply_patches(builder, S / 'patches/lib-builder-pre')
    if not offline:
        run('git', 'submodule', 'update', '--init', '--recursive', '--depth=1', cwd=idf)
    else:
        status = subprocess.check_output(['git', 'submodule', 'status', '--recursive'], cwd=idf, text=True)
        if any(line[:1] in ('-', '+', 'U') for line in status.splitlines()):
            raise ValueError('Offline SDK requires all exact pinned submodules already present')
    prepare_candidate_sources(builder, idf, policy)
    # Download exact upstream managed versions before freezing override paths.
    arduino = builder / 'components/arduino'
    manifest = subprocess.check_output(['git', 'show', 'HEAD:idf_component.yml'], cwd=arduino, text=True)
    manifest = freeze_managed_versions(manifest)
    if offline:
        for component in ('espressif/mdns', 'espressif/libsodium', 'joltwallet/littlefs'):
            local = component.replace('/', '__')
            if not (builder / 'managed_components' / local).is_dir():
                raise ValueError('Offline SDK requires actual local component: ' + component)
            manifest = manifest.replace('  ' + component + ':\n',
                '  ' + component + ':\n    override_path: ../../managed_components/' + local + '\n')
    (arduino / 'idf_component.yml').write_text(manifest)
    base = parse_config((S / 'lib-builder/defconfig.esp32secure').read_text())
    cfg = profile_config(policy)
    (builder / 'configs/defconfig.esp32').write_text(''.join(k + '=' + v + '\n' for k, v in cfg.items()))
    (builder / 'sdkconfig').write_text(''.join(k + '=' + v + '\n' for k, v in cfg.items()))
    # A CMake build tree is bound to its IDF source path. Retain earlier failed
    # experiments, but never mix their generated objects into this build.
    cache = builder / 'build/CMakeCache.txt'
    if cache.exists() and str(idf) not in cache.read_text():
        import shutil
        shutil.move(builder / 'build', builder / 'build-plain-tag-rejected')
    env = dict(os.environ, IDF_PATH=str(idf), IDF_TOOLS_PATH=os.environ.get('IDF_TOOLS_PATH', str(ROOT / 'tools')), AR_SOURCE_BRANCH='tf-3-3-12')
    if not offline:
        run('bash', idf / 'install.sh', 'esp32', env=env, cwd=builder)
    command = '. "$IDF_PATH/export.sh" && ./build.sh -s -t esp32 -b reconfigure'
    run('bash', '-c', command, cwd=builder, env=env)
    for component in ('espressif/mdns', 'espressif/libsodium', 'joltwallet/littlefs'):
        local = component.replace('/', '__')
        if 'override_path: ../../managed_components/' + local not in manifest:
            manifest = manifest.replace('  ' + component + ':\n',
                '  ' + component + ':\n    override_path: ../../managed_components/' + local + '\n')
    (arduino / 'idf_component.yml').write_text(manifest)
    verify_managed_components(builder, env)
    apply_patches(builder / 'managed_components', S / 'patches/lib-builder/managed_components')
    # SDK is a sibling, not builder/esp-idf; apply its full patch set explicitly.
    apply_patches(idf, S / 'patches/lib-builder/esp-idf')
    prepare_ca_bundle(idf)
    if build:
        source_policy_inventory(idf, policy)
        if offline:
            offline_build_sdk(builder, idf, env)
        else:
            run('bash', '-c', '. "$IDF_PATH/export.sh" && ./build.sh -s -t esp32', cwd=builder, env=env)
        package = builder / 'out/tools/esp32-arduino-libs'
        config = parse_config((package / 'esp32/sdkconfig').read_text())
        validate_config(config, base, policy=policy)
        export_candidate_sources(package, builder, idf, policy)
        files = {str(p.relative_to(package)): sha(p) for p in package.rglob('*')
                 if p.is_file() and p.name != 'tesla-sdk.json'}
        manifest = {'allocation_policy': policy, 'schema': 1, 'pins': PINS_as_json(), 'inputs': inputs(), 'files': files,
                    'sdk_source_policy': source_policy_inventory(idf, policy),
                    'installable': False, 'exact_upstream_package': False,
                    'unresolved_upstream_revisions': unresolved_revisions(),
                    'previous_unresolved_upstream_revisions': previous_unresolved_revisions(),
                    'reference_package': OFFICIAL_PACKAGE, 'sdk': SDK_LABEL}
        (package / 'tesla-sdk.json').write_text(json.dumps(manifest, indent=2) + '\n')
        verify_package(package, policy=policy)
        print('Verified source-review SDK:', package)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'verify'])
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--root', type=Path, default=ROOT, help='Separate owned SDK source/build directory')
    parser.add_argument('--policy', choices=[STOCK_POLICY, TESLA_POLICY, SINGLE_LINK_POLICY, CLASSIC_RELEASE_POLICY, NIMBLE_POLICY], default=STOCK_POLICY)
    parser.add_argument('--offline', action='store_true', help='Require exact available sources, components and installed IDF tools')
    parser.add_argument('--package', type=Path)
    args = parser.parse_args()
    ROOT = args.root.resolve()
    if args.action == 'prepare':
        prepare(args.build, policy=args.policy, offline=args.offline)
    else:
        verify_package(args.package or ROOT / 'builder/out/tools/esp32-arduino-libs', policy=args.policy)
