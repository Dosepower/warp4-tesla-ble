#!/usr/bin/env python3
"""Production route/visibility/privacy invariants; complements executable API tests."""
from pathlib import Path
import re,unittest
R=Path(__file__).resolve().parents[3]
class Privacy(unittest.TestCase):
 def test_discovery_not_retained_by_mqtt(self):
  text=(R/'software/src/modules/mqtt/mqtt.cpp').read_text()
  for name in ('pushStateUpdate','pushRawStateUpdate','wantsStateUpdate'):
   body=text.split('Mqtt::'+name+'(')[1].split('\n}')[0]
   self.assertIn('tesla_vehicle/discovery',body)
   if name=='wantsStateUpdate':self.assertIn('WantsStateUpdate::No',body)
 def test_discovery_debug_redaction(self):
  text=(R/'software/src/modules/tesla_vehicle/tesla_vehicle.cpp').read_text()
  self.assertRegex(text,r'addState\("tesla_vehicle/discovery",\s*&tesla->discovery_state,\s*\{\},\s*\{"targets"\}\)')
  self.assertNotRegex(text,r'addPersistentConfig\("tesla_vehicle/discovery"')
 def test_no_callback_followups(self):
  text=(R/'software/src/modules/tesla_vehicle/bluetooth_lifecycle.cpp').read_text()
  gap=text.split('void BluetoothLifecycle::gap(')[1].split('bool BluetoothLifecycle::discovery_tick')[0]
  self.assertNotRegex(gap,r'esp_ble_gattc_|esp_fill_random|request_soc|pair\(')
  self.assertEqual(text.count('esp_ble_gap_start_scanning('),1)
 def test_no_frontend_background_scan(self):
  text=(R/'software/web/src/modules/tesla_vehicle/main.tsx').read_text()
  self.assertEqual(text.count('API.call("tesla_vehicle/scan"'),1)
  self.assertNotIn('setInterval',text);self.assertNotIn('setTimeout',text)
  self.assertNotIn('localStorage',text);self.assertNotIn('sessionStorage',text)
  constructor=text.split('    constructor() {')[2].split('    private commandAllowed')[0]
  self.assertNotIn('this.search(',constructor);self.assertNotIn('API.call',constructor)
if __name__=='__main__':unittest.main(verbosity=2)
