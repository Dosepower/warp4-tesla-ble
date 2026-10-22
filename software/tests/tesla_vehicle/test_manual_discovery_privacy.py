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
  text=(R/'software/web/src_tfpp/modules/tesla_vehicle/guided_vehicle.tsx').read_text()
  self.assertEqual(text.count('API.call("tesla_vehicle/scan"'),1)
  self.assertNotIn('setInterval',text)
  # The sole timeout waits for cached worker ACK after explicit confirmation;
  # it does not initiate scans, polls or network requests.
  self.assertEqual(text.count('setTimeout(resolve, 50)'),1)
  self.assertNotIn('localStorage',text);self.assertNotIn('sessionStorage',text)
  parts=re.split(r'\n    (?:override )?componentWillUnmount\(',text.split('    constructor() {')[1])
  self.assertEqual(len(parts),2,'Constructor boundary must be the actual lifecycle override')
  constructor=parts[0]
  self.assertNotIn('this.search(',constructor);self.assertNotIn('API.call',constructor)
if __name__=='__main__':unittest.main(verbosity=2)
