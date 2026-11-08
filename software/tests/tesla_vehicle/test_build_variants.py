"""Compile-time profile contracts; no SDK, device or generated files required."""
import configparser
import subprocess
import importlib.util
import ast
from pathlib import Path

S = Path(__file__).resolve().parents[2]


def project():
    config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(';',))
    config.read([str(S / 'platformio.ini')] + sorted(str(p) for p in S.glob('*.ini') if p.name != 'platformio.ini'))
    return config


def test_three_explicit_profiles_preserve_existing_target():
    config = project()
    for name, tesla, remote in [('tesla_only', 1, 0), ('remote_only', 0, 1), ('combined', 1, 1)]:
        section = 'env:warp4_ble_' + name + '_review'
        assert config.has_section(section), 'missing actual compile-time profile: ' + section
        assert config[section]['extends'] == 'env:warp4_tesla_ble_nimble_review'
        flags = config[section]['build_flags']
        assert '-DWARP_FEATURE_TESLA=' + str(tesla) in flags
        assert '-DWARP_FEATURE_REMOTE=' + str(remote) in flags
    # Existing installed/review baseline keeps its original name and inheritance.
    assert config['env:warp4_tesla_ble_nimble_review']['extends'] == 'env:warp4_tesla_ble_classic_review'
    assert '-DWARP_FEATURE_' not in config['env:warp4_tesla_ble_nimble_review']['build_flags']


def test_compile_time_selection_is_validated(tmp_path):
    header = S / 'src/modules/tesla_vehicle/bluetooth_features.h'
    assert header.is_file(), 'missing shared compile-time selection boundary'
    for tesla, remote in [(1, 0), (0, 1), (1, 1)]:
        result = subprocess.run(['c++', '-x', 'c++', '-fsyntax-only', '-DTESLA_BLE_USE_NIMBLE=1',
            '-DWARP_FEATURE_TESLA=' + str(tesla), '-DWARP_FEATURE_REMOTE=' + str(remote),
            '-include', str(header), '-'], input='', text=True, capture_output=True)
        assert result.returncode == 0, result.stderr
    result = subprocess.run(['c++', '-x', 'c++', '-fsyntax-only', '-DTESLA_BLE_USE_NIMBLE=1',
        '-DWARP_FEATURE_TESLA=0', '-DWARP_FEATURE_REMOTE=0',
        '-include', str(header), '-'], input='', text=True, capture_output=True)
    assert result.returncode != 0
    assert 'requires at least one feature' in result.stderr


def test_remote_owner_has_no_vehicle_protocol_dependency():
    header = S / 'src/modules/bluetooth_core/bluetooth_core.h'
    assert header.exists(), 'missing shared lifecycle and remote owner extraction'
    text = header.read_text()
    assert 'class BluetoothCore' in text
    assert 'tesla_clean' not in text
    assert 'tesla_ble_transport.h' not in text
    remote = S / 'src/modules/warp_remote/warp_remote.h'
    assert remote.exists(), 'missing independently registered remote-only module'
    assert 'BluetoothCore' in remote.read_text()


def test_frontend_selection_and_translation_exclude_opposite_feature():
    path = S / 'bluetooth_variants.py'
    assert path.exists(), 'missing generated frontend feature selection'
    spec = importlib.util.spec_from_file_location('variants', path)
    assert spec is not None and spec.loader is not None
    variants = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(variants)
    for tesla, remote in [(True, False), (False, True), (True, True)]:
        assert variants.include_frontend('modules/tesla_vehicle/guided_vehicle.tsx', tesla, remote) == tesla
        assert variants.include_frontend('modules/tesla_vehicle/remote_settings.tsx', tesla, remote) == remote
        assert variants.include_frontend('modules/ev/main.tsx', tesla, remote)
        data = {'tesla_vehicle': {'remote': {'enable': 'remote'}, 'bluetooth': {'title': 'Bluetooth', 'tesla_title': 'Tesla', 'active_hint': 'Tesla'}, 'content': {'cancel': 'Cancel', 'vehicle': 'Vehicle'}, 'status': {}, 'script': {}, 'pair_response': {}}}
        variants.prune_translation(data, tesla, remote)
        assert ('remote' in data['tesla_vehicle']) == remote
        assert ('vehicle' in data['tesla_vehicle']['content']) == tesla
        assert ('tesla_title' in data['tesla_vehicle']['bluetooth']) == tesla


def test_backend_exclusion_does_not_remove_canonical_remote_access():
    tree = ast.parse((S / 'tesla_build_guard.py').read_text())
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'source_review')
    namespace = {'Path': Path, 'flags': '-DWARP_FEATURE_REMOTE=0'}
    exec(compile(ast.Module(body=[function], type_ignores=[]), '<source_review>', 'exec'), namespace)
    select = namespace['source_review']
    assert select(None, 'src/modules/remote_access/remote_access.cpp') is not None
    assert select(None, 'src/modules/tesla_vehicle/remote_crypto.cpp') is None
    assert select(None, 'src/modules/tesla_vehicle/tesla_vehicle.cpp') is not None


def test_bluetooth_navbar_has_remote_only_owner():
    text = (S / 'web/src/modules/tesla_vehicle/main.tsx').read_text()
    assert 'module="warp_remote"' in text, 'remote-only page must not hide behind absent Tesla backend'


def test_public_workflows_are_host_only_read_only_and_never_release():
    workflows = sorted((S.parent / '.github/workflows').glob('*.yml'))
    assert len(workflows) == 2
    for workflow in workflows:
        text = workflow.read_text()
        assert 'pio run' not in text, workflow.name + ': full firmware CI prerequisites are not supported'
        assert 'contents: read' in text
        assert 'gh cache delete' not in text
        assert 'upload-artifact' not in text
        assert 'schedule:' not in text
