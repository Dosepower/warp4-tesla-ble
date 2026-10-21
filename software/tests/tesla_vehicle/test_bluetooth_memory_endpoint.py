"""Compile and execute the exact read-only firmware HTTP callback, offline."""
from pathlib import Path
import os,subprocess,tempfile,json,sys
S=Path(__file__).resolve().parents[2];source=(S/'src/modules/tesla_vehicle/tesla_vehicle.cpp').read_text()
a=source.index('    server.on_HTTPThread("/bluetooth/memory"');b=source.index('\n#endif',a);callback=source[a:b]
code=r'''
#include "bluetooth_memory.h"
#include <string>
#include <functional>
#include <cstdio>
#include <cassert>
struct Response {std::string body;};
struct WebServerRequest {
 Response &r;
 void beginChunkedResponse_json(unsigned code){assert(code==200);}
 void sendChunk(const char *s){r.body+=s;}
 bool endChunkedResponse(){return true;}
};
constexpr int HTTP_GET=1;
struct Server {
 std::function<bool(WebServerRequest)> handler;
 template<class F>void on_HTTPThread(const char *path,int method,F f){assert(std::string(path)=="/bluetooth/memory"&&method==HTTP_GET);handler=f;}
} server;
void register_http(){
'''+callback+r'''
}
int main(){
 using namespace bluetooth_memory;
 sample(Stage::BtActive,true);Sample before,after;assert(read(Stage::BtActive,before));
 register_http();Response first,second;assert(server.handler({first}));assert(server.handler({second}));
 assert(read(Stage::BtActive,after)&&after.samples==before.samples&&after.ms==before.ms);
 assert(first.body==second.body);puts(first.body.c_str());
}
'''
with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'],prefix='memory-endpoint-') as td:
 d=Path(td);(d/'test.cpp').write_text(code)
 cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-DWARP_TESLA_MEMORY_PHASES=1','-DTESLA_MEMORY_HOST_TEST=1','-I'+str(S/'src/modules/tesla_vehicle'),'-I'+str(S/'tests/tesla_vehicle/host'),str(d/'test.cpp'),'-o',str(d/'test')]
 if 'sanitize' in sys.argv:cmd+=['-fsanitize=address,undefined','-g','-no-pie']
 subprocess.run(cmd,check=True);run=subprocess.run([str(d/'test')],text=True,stdout=subprocess.PIPE,check=True)
 doc=json.loads(run.stdout);assert doc['version']==1 and doc['stack_unit']=='bytes' and len(doc['samples'])==22
 rows={x['stage']:x for x in doc['samples']};assert rows['bt_active']['samples']==1 and rows['worker_begin']['samples']==0
 print('Actual GET callback PASS; 22 bounded rows; no sampling/counter mutation/task-handle lookup')
