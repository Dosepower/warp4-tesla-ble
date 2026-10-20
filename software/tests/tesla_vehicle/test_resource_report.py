from pathlib import Path
import importlib.util
import unittest
S=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('resources',S/'tools/tesla_resource_report.py')
module=importlib.util.module_from_spec(spec)
class ResourcePolicy(unittest.TestCase):
    @classmethod
    def setUpClass(cls): spec.loader.exec_module(module)
    def test_synthetic_overlaps_rejected(self):
        for regions in ([('manual',0x1000,0x2000),('BT',0x1000,0x1800)],
                        [('a',0x1000,0x2001),('b',0x2002,0x3000)],
                        [('reverse',0x2000,0x1000)]):
            with self.assertRaises(ValueError): module.validate_regions(regions)
    def test_synthetic_adjacency_and_empty(self):
        module.validate_regions([('b',0x2000,0x3000),('a',0x1000,0x2000),('empty',0x4000,0x4000)])
if __name__=='__main__': unittest.main()
