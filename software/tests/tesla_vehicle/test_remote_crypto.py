import os
from pathlib import Path
import subprocess
S=Path(__file__).resolve().parents[2]
MBED=Path(os.environ.get('REMOTE_TEST_MBEDTLS_SOURCE', '.remote-test-deps/remote_test_mbedtls_source'))
CACHE=Path(os.environ.get('REMOTE_TEST_MBEDTLS_BUILD', '.remote-test-deps/remote_test_mbedtls_build'))


def test_real_mbedtls_p256_hkdf_ccm_fail_closed(tmp_path):
 p=tmp_path/'crypto.cpp'
 p.write_text('''#include <cassert>
#include <cstring>
#include "remote_crypto.h"
int main(){
 uint8_t a[32]={},b[32]={},ap[64],bp[64],ab[32],ba[32];a[31]=1;b[31]=2;
 assert(wr_crypto_public(ap,a)&&wr_crypto_public(bp,b));
 assert(wr_crypto_shared(ab,a,bp)&&wr_crypto_shared(ba,b,ap));assert(!memcmp(ab,ba,32));
 uint8_t bad[64]={};assert(!wr_crypto_shared(ab,a,bad));for(auto v:ab)assert(v==0);
 uint8_t ikm[22];memset(ikm,0x0b,sizeof(ikm));uint8_t salt[13];for(unsigned i=0;i<13;i++)salt[i]=i;
 uint8_t info[10];for(unsigned i=0;i<10;i++)info[i]=0xf0+i;
 const uint8_t expected[42]={0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65};
 uint8_t out[42];assert(wr_crypto_hkdf(out,42,ikm,22,salt,13,info,10)&&!memcmp(out,expected,42));
 uint8_t key[32]={1},nonce[13]={2},aad[12]={3},plain[16]={4},cipher[16],tag[16],recovered[16];
 assert(wr_crypto_seal(cipher,tag,key,nonce,aad,sizeof(aad),plain,sizeof(plain)));
 assert(wr_crypto_open(recovered,key,nonce,aad,sizeof(aad),cipher,sizeof(cipher),tag));assert(!memcmp(recovered,plain,16));
 tag[0]^=1;memset(recovered,42,16);assert(!wr_crypto_open(recovered,key,nonce,aad,sizeof(aad),cipher,sizeof(cipher),tag));for(auto v:recovered)assert(v==0);
 tag[0]^=1;aad[0]^=1;assert(!wr_crypto_open(recovered,key,nonce,aad,sizeof(aad),cipher,sizeof(cipher),tag));
 aad[0]^=1;nonce[0]^=1;assert(!wr_crypto_open(recovered,key,nonce,aad,sizeof(aad),cipher,sizeof(cipher),tag));
 return 0;
}
''')
 exe=tmp_path/'crypto'
 libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',
                 '-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(MBED/'include'),'-I'+str(S/'src/modules/tesla_vehicle'),
                 str(p),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
