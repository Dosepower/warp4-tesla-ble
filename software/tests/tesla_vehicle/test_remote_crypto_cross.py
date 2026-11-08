import os
"""Execute real Nordic ARM Oberon archive under Unicorn, real host mbedTLS,
then compare both to independent OpenSSL-backed cryptography vectors. No radio.
"""
from pathlib import Path
import subprocess,hashlib,json
from test_remote_crypto import S,MBED,CACHE
N=Path(os.environ.get('REMOTE_TEST_NCS_ROOT', '.remote-test-deps/remote_test_ncs_root'))
TAG=S.parents[1]/'tag'
CC=N/'toolchains/8285d8ad56/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc'
OB=N/'v3.4.1/nrfxlib/crypto/nrf_oberon'

def test_real_oberon_arm_mbedtls_and_independent_vectors(tmp_path):
 from cryptography.hazmat.primitives.asymmetric import ec
 from cryptography.hazmat.primitives import hashes
 from cryptography.hazmat.primitives.kdf.hkdf import HKDF
 from cryptography.hazmat.primitives.ciphers.aead import AESCCM
 from elftools.elf.elffile import ELFFile
 from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
 from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0
 pk=lambda n:ec.derive_private_key(n,ec.SECP256R1())
 pub=lambda key:key.public_key().public_numbers().x.to_bytes(32,'big')+key.public_key().public_numbers().y.to_bytes(32,'big')
 public=pub(pk(1));peer=pub(pk(2));shared=pk(1).exchange(ec.ECDH(),pk(2).public_key())
 hkdf=HKDF(algorithm=hashes.SHA256(),length=42,salt=bytes(range(13)),info=bytes(range(0xf0,0xfa))).derive(b'\x0b'*22)
 plain=bytes(range(32));key=bytes(range(32));nonce=bytes(range(13));aad=b'warp-test'
 sealed=AESCCM(key,tag_length=16).encrypt(nonce,plain,aad)
 expected=public+shared+hkdf+sealed+plain+bytes(32)
 arr=lambda b:','.join(map(str,b))
 probe=tmp_path/'probe.c';probe.write_text('''#include "remote_crypto.h"
#include <string.h>
uint8_t result[250];volatile int status;
static uint8_t sk[32]={'''+arr((1).to_bytes(32,'big'))+'''},peer[64]={'''+arr(peer)+'''};
int main(void){
 uint8_t key[32],nonce[13],ikm[22],salt[13],info[10],tag[16];
 for(unsigned i=0;i<32;i++)key[i]=i;
 for(unsigned i=0;i<13;i++)nonce[i]=salt[i]=i;
 for(unsigned i=0;i<22;i++)ikm[i]=0x0b;
 for(unsigned i=0;i<10;i++)info[i]=0xf0+i;
 if(!wr_crypto_public(result,sk)||!wr_crypto_shared(result+64,sk,peer)||!wr_crypto_hkdf(result+96,42,ikm,22,salt,13,info,10)){status=-1;return 1;}
 if(!wr_crypto_seal(result+138,result+170,key,nonce,(const uint8_t*)"warp-test",9,key,32)){status=-2;return 2;}
 if(!wr_crypto_open(result+186,key,nonce,(const uint8_t*)"warp-test",9,result+138,32,result+170)){status=-3;return 3;}
 memcpy(tag,result+170,16);tag[0]^=1;memset(result+218,0xaa,32);
 if(wr_crypto_open(result+218,key,nonce,(const uint8_t*)"warp-test",9,result+138,32,tag)){status=-4;return 4;}
 status=1;return 0;
}
''')
 runtime=tmp_path/'runtime.c';runtime.write_text('''#include <stddef.h>
void *memcpy(void *a,const void*b,size_t n){unsigned char*p=a;const unsigned char*q=b;while(n--)*p++=*q++;return a;}
void *memset(void *a,int c,size_t n){unsigned char*p=a;while(n--)*p++=c;return a;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
''')
 archive=OB/'lib/cortex-m33/soft-float/liboberon_3.0.20.a'
 elf=tmp_path/'oberon.elf'
 linker=tmp_path/'probe.ld';linker.write_text('ENTRY(main)\nSECTIONS { . = 0x10000; .text : { *(.text*) *(.rodata*) } . = 0x20000000; .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) } }\n')
 subprocess.run([str(CC),'-Os','-mthumb','-mcpu=cortex-m33','-mfloat-abi=soft','-fno-builtin','-nostdlib','-I'+str(TAG/'firmware/src'),'-I'+str(OB/'include'),str(probe),str(runtime),str(TAG/'firmware/src/remote_crypto.c'),str(archive),'-Wl,-T,'+str(linker),'-lgcc','-o',str(elf)],check=True)
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);uc.mem_map(0x10000000,0x200000);uc.mem_map(0x20000000,0x200000)
 with elf.open('rb') as f:
  e=ELFFile(f)
  for seg in e.iter_segments():
   if seg['p_type']=='PT_LOAD':uc.mem_write(seg['p_vaddr'],seg.data())
  syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 stop=0x10100000
 uc.mem_write(stop,b'\x00\xbe');uc.reg_write(UC_ARM_REG_SP,0x201ff000);uc.reg_write(UC_ARM_REG_LR,stop|1)
 uc.emu_start(syms['main']|1,stop,count=100000000)
 status=int.from_bytes(uc.mem_read(syms['status'],4),'little',signed=True)
 assert status==1,('ARM provider status',status,uc.reg_read(UC_ARM_REG_R0))
 arm=bytes(uc.mem_read(syms['result'],250));assert arm==expected
 # Same C fixture through actual mbedTLS; a separate executable avoids symbol
 # replacement/mocks and compares all plaintext/ciphertext/tag bytes.
 host=tmp_path/'host.cpp';host.write_text('#define main probe_main\n#include "'+str(probe)+'"\n#undef main\n#include <cstdio>\nint main(){if(probe_main())return 1;for(auto b:result)printf("%02x",b);return 0;}\n')
 exe=tmp_path/'mbed';libs=[CACHE/n for n in ['library/libmbedcrypto.a','3rdparty/everest/libeverest.a','3rdparty/p256-m/libp256m.a']]
 subprocess.run(['g++','-std=c++17','-DWARP_REMOTE_CRYPTO_HOST=1','-I'+str(TAG/'firmware/src'),'-I'+str(MBED/'include'),str(host),str(S/'src/modules/tesla_vehicle/remote_crypto.cpp'),*map(str,libs),'-o',str(exe)],check=True)
 assert bytes.fromhex(subprocess.check_output([str(exe)]).decode())==expected
 print(json.dumps({'oberon_archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'vector_bytes':len(expected),'vector_sha256':hashlib.sha256(expected).hexdigest(),'providers':['Nordic Oberon 3.0.20 ARM real archive/Unicorn','ESP mbedTLS host build','independent Python cryptography/OpenSSL']}))
