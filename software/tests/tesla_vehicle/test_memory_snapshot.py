import unittest,subprocess,tempfile,json
from pathlib import Path
S=Path(__file__).resolve().parents[2]
class MemorySnapshotTest(unittest.TestCase):
 def test_bounded_secret_free_snapshot(self):
  text=r'''
#include <cstddef>
#include <cstring>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
size_t heap_caps_get_free_size(unsigned c){return c==3?12345:2345678;}
size_t heap_caps_get_minimum_free_size(unsigned c){return c==3?10000:0;}
size_t heap_caps_get_largest_free_block(unsigned c){return c==3?8192:1900000;}
#include "memory_snapshot.h"
#include <cstdio>
int main(){char tiny[8]="guard";if(memory_snapshot_json(tiny,sizeof tiny))return 1;char buf[512];if(!memory_snapshot_json(buf,sizeof buf))return 2;puts(buf);}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.cpp').write_text(text)
   subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror','-I',str(S/'src/tools'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   r=subprocess.run([str(p/'test')],check=True,capture_output=True,text=True)
  self.assertEqual(json.loads(r.stdout),{'free_dram':12345,'min_free_dram':10000,'largest_free_dram_block':8192,'free_psram':2345678,'min_free_psram':None,'largest_free_psram_block':1900000})
