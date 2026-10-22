"""Structural source-review map evidence, not installer authorization."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('resources',Path(__file__).resolve().parents[2]/'tools/tesla_resource_report.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)

class RetainedMapTests(unittest.TestCase):
    def parse(self,text):
        return r.retained_map_ranges(text,{'.flash.text':{'address':0x40001000,'size':0x100,'type':1}})
    def test_multiline_retained_and_discarded(self):
        text='Discarded input sections\n .text.fake 0x40001000 0x20 fake.o\nMemory Configuration\n\nLinker script and memory map\n.flash.text 0x40001000 0x100\n .text.real\n                0x40001020 0x20 libbt.a(real.o)\n                0x40001020 real\nCross Reference Table\n .text.fake 0x40001000 0x20 fake.o\n'
        self.assertEqual(self.parse(text),[{'section':'.text.real','address':0x40001020,'size':0x20,'input':'libbt.a(real.o)','output_section':'.flash.text'}])
    def test_reject_missing_structure(self):
        with self.assertRaises(ValueError):self.parse('.text.fake 0x40001000 0x20 fake.o')
    def test_reject_range_outside_elf(self):
        with self.assertRaises(ValueError):self.parse('Memory Configuration\nLinker script and memory map\n.flash.text 0x40001000 0x100\n .text.real 0x400010f0 0x20 real.o\n')
    def test_ignore_nonallocated_debug_and_zero_length(self):
        self.assertEqual(self.parse('Memory Configuration\nLinker script and memory map\n.debug_info 0x0 0x20\n .debug_info 0x0 0x20 debug.o\n.flash.text 0x40001000 0x100\n .text.empty 0x40001000 0x0 empty.o\n'),[])
if __name__=='__main__':unittest.main()
