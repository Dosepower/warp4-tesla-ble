"""Source/build invariants for the real secure WARP4 port (no device I/O)."""
from pathlib import Path
import configparser
import unittest

S = Path(__file__).resolve().parents[2]


class UpstreamPort(unittest.TestCase):
    def test_secure_build_inherits_upstream(self):
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read(S / 'warp4_tesla_ble.ini')
        e = cfg['env:warp4_tesla_ble']
        self.assertEqual(e['extends'], 'env:warp4')
        for key in ('board', 'board_build.partitions', 'custom_secure_boot',
                    'custom_provisioning', 'custom_signature_preset'):
            self.assertNotIn(key, e)
        self.assertIn('-fstack-usage', e['build_flags'])
        self.assertEqual(e['custom_build_cache_limit'], '-1')
        self.assertNotIn('bench', e['extra_scripts'])
        self.assertNotIn('merge_firmware', e['extra_scripts'])
        self.assertIn('pre:pio_hooks.py', e['extra_scripts'])
        self.assertIn('-DTESLA_BLE_MANUAL_VEHICLE_UI=1', e['build_flags'])
        self.assertNotIn('bench', (S / 'warp4_tesla_ble.ini').read_text().lower())

    def test_secure_sdk_overlay(self):
        text = (S / 'lib-builder/defconfig.warp4-tesla-ble').read_text()
        self.assertIn('CONFIG_BT_ENABLED=y', text)
        self.assertIn('CONFIG_BT_BLUEDROID_ENABLED=y', text)
        self.assertIn('CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y', text)
        self.assertIn('CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY=y', text)
        self.assertIn('CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=n', text)
        self.assertNotIn('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0', text)
        self.assertNotIn('CONFIG_PARTITION_TABLE_OFFSET=', text)
        self.assertNotIn('CONFIG_SECURE_', text)

    def test_ev_extension_keeps_upstream_rounding(self):
        text = (S / 'web/src/modules/ev/main.tsx').read_text()
        self.assertIn('registerVehicleExtension', text)
        self.assertIn('Math.round(props.ev.charging_efficiency * 100)', text)
        self.assertNotIn('../tesla_vehicle', text)

    def test_checked_string_preserves_const_ref_api(self):
        text = (S / 'src/config.h').read_text()
        self.assertIn('updateStringChecked', text)
        # This port must not replace the current upstream Config implementation.
        self.assertIn('const ConfVariant &', text)


if __name__ == '__main__':
    unittest.main()
