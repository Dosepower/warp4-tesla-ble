import os
from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]
MBED=Path(os.environ.get('REMOTE_TEST_MBEDTLS_SOURCE', '.remote-test-deps/remote_test_mbedtls_source'))
CACHE=Path(os.environ.get('REMOTE_TEST_MBEDTLS_BUILD', '.remote-test-deps/remote_test_mbedtls_build'))


def test_real_crypto_mutual_physical_consent_and_transaction_gate(tmp_path):
 p=tmp_path/'enroll.cpp'
 p.write_text('''#include <cassert>
#include <cstring>
#include "remote_enrollment.h"
struct Store {bool prepared=false,active=false,fail=false;uint8_t key[32]{};};
bool save(void *p,const uint8_t*key,bool active){auto&s=*static_cast<Store*>(p);if(s.fail)return false;s.prepared=true;s.active=active;memcpy(s.key,key,32);return true;}
int main(){
 uint8_t a[32]={},b[32]={},an[32]={1},bn[32]={2};a[31]=1;b[31]=2;
 Store ws,ts;RemoteEnrollment warp(false,{&ws,save}),tag(true,{&ts,save});
 assert(warp.begin(a,an)&&tag.begin(b,bn));
 assert(warp.peer(tag.hello())&&tag.peer(warp.hello()));
 uint8_t proof[17];assert(!tag.next(proof));assert(!warp.next(proof)); // physical consent required
 assert(tag.consent());assert(tag.next(proof));
 uint8_t bad[17];memcpy(bad,proof,17);bad[0]^=1;assert(!warp.accept(bad));assert(!ws.prepared);
 // Wrong proof is fail-closed; start a fresh attempt rather than continue it.
 RemoteEnrollment w(false,{&ws,save});assert(w.begin(a,an)&&w.peer(tag.hello()));
 assert(w.accept(proof)&&ws.prepared&&!ws.active);assert(w.next(proof));
 Store no_consent;RemoteEnrollment unconsented(true,{&no_consent,save});
 assert(unconsented.begin(b,bn)&&unconsented.peer(w.hello()));
 assert(!unconsented.accept(proof)&&!no_consent.prepared);
 assert(tag.accept(proof)&&ts.prepared&&!ts.active);assert(tag.next(proof));
 assert(w.accept(proof)&&ws.active);assert(w.next(proof));
 assert(tag.accept(proof)&&ts.active);assert(tag.next(proof));
 assert(!w.complete()&&!tag.complete());assert(w.accept(proof));assert(w.next(proof));
 assert(!w.complete()&&!tag.complete());assert(tag.accept(proof));assert(tag.next(proof));
 assert(tag.complete()&&!w.complete());assert(w.accept(proof)&&w.complete());
 assert(!memcmp(ws.key,ts.key,32));assert(!w.accept(proof));
 Store failed;failed.fail=true;RemoteEnrollment deny(false,{&failed,save});
 assert(deny.begin(a,an)&&deny.peer(tag.hello()));
 // A completed old proof can neither prepare storage nor create success.
 assert(!deny.accept(proof)&&!failed.prepared&&!deny.complete());
 return 0;
}
''')
 exe=tmp_path/'enroll';libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-no-pie','-DWARP_REMOTE_CRYPTO_HOST=1',
                 '-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
