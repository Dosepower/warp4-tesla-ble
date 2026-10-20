"""The real SDK patch must not reserve Bluetooth ROM data as manual heap."""
from pathlib import Path
import subprocess
import unittest
S=Path(__file__).resolve().parents[2]
PATCH=S/'patches/lib-builder/esp-idf/0009-heap-Add-MALLOC_CAP_IRAM-reserve-regions-for-manual-.patch'
LOW='MANUALLY_RESERVED_6432B_AT_0x3FFAE6E0_DRAM'
OTHER=['MANUALLY_RESERVED_15072B_AT_0x3FFE0440_DRAM','MANUALLY_RESERVED_8192B_AT_0x4009E000_IRAM']

class HeapPolicy(unittest.TestCase):
    def macros(self, bt):
        text=PATCH.read_text().split('diff --git a/components/heap/port/esp32/memory_layout.c')[0]
        added='\n'.join(line[1:] for line in text.splitlines() if line.startswith('+') and not line.startswith('+++'))
        cmd=['gcc','-E','-dM','-x','c','-']+(['-DCONFIG_BT_ENABLED=1'] if bt else [])
        return subprocess.check_output(cmd,input=added,text=True)
    def test_bt_rom_region_not_manually_reserved(self):
        self.assertNotIn(LOW,self.macros(True))
    def test_stock_off_arenas_unchanged(self):
        for macro in [LOW,*OTHER]: self.assertIn(macro,self.macros(False))
    def test_other_bt_arenas_retained(self):
        for macro in OTHER: self.assertIn(macro,self.macros(True))
if __name__=='__main__': unittest.main()
