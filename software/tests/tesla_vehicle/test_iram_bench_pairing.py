#!/usr/bin/env python3
"""Bench pairing vertical slices; native crypto, synthetic identities, no hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_iram_bench_smoke import MODULE
ROOT=MODULE.parents[3]
E=ROOT/'software/.pio/bench-evidence/pairing'
MBED=ROOT/'software/.pio/bench-idf/components/mbedtls/mbedtls'
LIB=ROOT/'software/.pio/bench-evidence/discovery-local-name/host-normal/mbedtls'
HARNESS=r'''
#include "tesla_ble_bench_pairing.h"
#include <tesla_clean/protobuf.hpp>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace tesla_clean;
struct Disk : BenchPairStorage {
 std::vector<uint8_t> record; bool exposed=false, fail_save=false, fail_expose=false, fail_remove=false; unsigned saves=0;
 LoadResult load(uint8_t *p,size_t capacity,size_t &n) override { n=record.size(); if(n>capacity) return LoadResult::Error; if(!n) return LoadResult::Absent; memcpy(p,record.data(),n); return LoadResult::Loaded; }
 SaveResult save(Bytes b) override { ++saves; if(fail_save) return SaveResult::NotCommitted; record.assign(b.data,b.data+b.size); return SaveResult::Committed; }
 bool exposure(bool mark,bool &value) override { if(mark && fail_expose) return false; if(mark) exposed=true; value=exposed; return true; }
 bool remove() override { if(fail_remove) return false; record.clear(); exposed=false; return true; }
};
struct Fixture {
 mbedtls_ctr_drbg_context drbg; Disk disk; char vin[18]{}; char name[19]{}; uint8_t addr[6]{};
 Fixture() { memset(vin,'A',17); for(unsigned i=0;i<6;++i) addr[i]=static_cast<uint8_t>(i+32); mbedtls_ctr_drbg_init(&drbg); auto entropy=[](void *,unsigned char *p,size_t n){memset(p,51,n);return 0;}; assert(!mbedtls_ctr_drbg_seed(&drbg,entropy,nullptr,nullptr,0)); uint8_t digest[20]; assert(!mbedtls_sha1(reinterpret_cast<uint8_t *>(vin),17,digest)); name[0]='S'; for(unsigned i=0;i<8;++i) snprintf(name+1+2*i,3,"%02x",digest[i]); name[17]='C'; }
 ~Fixture(){mbedtls_ctr_drbg_free(&drbg);}
 Random rng(){return {mbedtls_ctr_drbg_random,&drbg};}
};
'''
PREP=r'''
int main(int argc,char **argv) {
 Fixture f; BenchPairing p(f.disk,f.rng()); assert(!strcmp(p.state(),"no_key"));
 assert(!p.bind(f.vin,f.name,f.addr,1,false)); auto wrong=std::string(f.name); wrong[2]=wrong[2]=='0'?'1':'0'; assert(!p.bind(f.vin,wrong.c_str(),f.addr,1,true));
 assert(p.bind(f.vin,f.name,f.addr,1,true)); assert(!p.bind(f.vin,f.name,f.addr,1,true));
 if(argc>1 && !strcmp(argv[1],"save_fail")) f.disk.fail_save=true;
 if(f.disk.fail_save) { assert(!p.prepare() && !strcmp(p.state(),"error")); assert(!p.prepare()); return 0; }
 assert(p.prepare() && !strcmp(p.state(),"key_ready") && f.disk.saves==1 && !f.disk.record.empty());
 assert(!p.prepare()); auto public_id=std::string(p.public_key_hex()); assert(public_id.size()==130);
 BenchPairing reloaded(f.disk,f.rng()); assert(reloaded.bind(f.vin,f.name,f.addr,1,true) && reloaded.prepare()); assert(public_id==reloaded.public_key_hex() && f.disk.saves==1);
 Fixture g; g.vin[0]='B'; assert(BenchPairing::vehicle_name(g.vin,g.name)); BenchPairing mismatch(f.disk,f.rng()); assert(mismatch.bind(g.vin,g.name,g.addr,1,true)); assert(!mismatch.prepare() && !strcmp(mismatch.state(),"error") && f.disk.saves==1);
 assert(!mismatch.delete_local(true) && !f.disk.record.empty());
 puts("prepare: exact runtime VIN binding, durable real P256, no overwrite, reload/mismatch/failure passed");
}
'''
class BenchPairingTests(unittest.TestCase):
    def build_run(self, main, scenarios=('',), extra_sources=(), instrument_auth=False, link_flags=(), emit=False, forbidden_symbols=()):
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory); (t/'store/config').mkdir(parents=True); (t/'test.cpp').write_text('#include <string>\n'+HARNESS+main)
            sources=[MODULE/'tesla_ble_bench_pairing.cpp']+[ROOT/'software/lib/TeslaBLEClean/src'/n for n in ('key.cpp','identity.cpp','metadata.cpp','session_crypto.cpp','session_info.cpp','authentication.cpp','response.cpp','messages.cpp','session_wire.cpp','framing.cpp','vcsec.cpp')]
            if instrument_auth:
                original=ROOT/'software/lib/TeslaBLEClean/src/authentication.cpp'
                code=original.read_text().replace('namespace tesla_clean {','extern bool bench_test_challenge_inflight;\nnamespace tesla_clean {',1).replace('const bool accepted=transport.send(transport.context,view);','bench_test_challenge_inflight=true;\n    const bool accepted=transport.send(transport.context,view);\n    bench_test_challenge_inflight=false;')
                (t/'auth-instrumented.cpp').write_text(code)
                sources=[t/'auth-instrumented.cpp' if p==original else p for p in sources]
            lib=LIB if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')!='1' else LIB.parents[1]/'host-sanitize/mbedtls'
            cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-DTESLA_BLE_BENCH_ONLY=1','-DTESLA_BLE_IRAM_DIAGNOSTICS=1','-DTESLA_CLEAN_STORAGE_PREFIX="'+str(t/'store')+'"','-I'+str(MODULE),'-I'+str(ROOT/'software/lib/TeslaBLEClean/include'),'-I'+str(MBED/'include'),str(t/'test.cpp'),*[str(p) for p in sources+list(extra_sources)],str(lib/'library/libmbedcrypto.a'),str(lib/'3rdparty/everest/libeverest.a'),str(lib/'3rdparty/p256-m/libp256m.a'),'-o',str(t/'test')]
            if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1': cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
            if forbidden_symbols: cmd+=['-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
            cmd+=list(link_flags)
            p=subprocess.run(cmd,capture_output=True,text=True); self.assertEqual(p.returncode,0,p.stderr)
            if forbidden_symbols:
                symbols=subprocess.check_output(['nm','-C','--defined-only',str(t/'test')],text=True)
                self.assertIn('BenchPairing::rehearse_local()',symbols)
                for forbidden in forbidden_symbols:self.assertNotIn(forbidden,symbols)
            for scenario in scenarios:
                with self.subTest(scenario=scenario):
                    p=subprocess.run([str(t/'test'),scenario],capture_output=True,text=True); self.assertEqual(p.returncode,0,p.stderr)
                    if emit: return p.stdout
    def test_01_preparation(self): self.build_run(PREP,('', 'save_fail'))
    def test_11_operation_workspaces_do_not_overlap_fixed_paths_and_formatting(self):
        pair=(MODULE/'tesla_ble_bench_pairing.cpp').read_text()
        disk=(MODULE/'tesla_ble_bench_pair_disk.cpp').read_text()
        discovery=(MODULE/'tesla_ble_bench_discovery.cpp').read_text()
        self.assertNotIn('TeslaBleStorageIO::Paths',disk, 'Only two immutable bench namespaces: no runtime 3x192-byte path workspaces')
        self.assertNotIn('    Paths p;',disk)
        self.assertNotIn('uint8_t record[544]',pair)
        self.assertNotIn('uint8_t readback[544]',disk)
        prepare=pair.split('bool BenchPairing::prepare()',1)[1].split('bool BenchPairing::send_pair',1)[0]
        self.assertNotIn('snprintf',prepare)
        run=discovery.split('void TeslaBleBenchDiscovery::run()',1)[1].split('bool TeslaBleBenchDiscovery::pair_owns',1)[0]
        self.assertNotIn('snapshot()',run)
    def test_12_checked_workspaces_fail_closed_and_zeroize(self):
        self.build_run(r'''
#include "tesla_ble_bench_pair_disk.h"
#include <filesystem>
static unsigned allocations=0,fail_at=0,cleared=0;static void *records[16]{};
extern "C" void *__real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t &);
extern "C" void __real__ZdlPvm(void *,size_t);
extern "C" void *__wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t &tag){
 if(n==544&&++allocations==fail_at)return nullptr;
 void *p=__real__ZnwmRKSt9nothrow_t(n,tag);if(n==544&&p){for(auto &entry:records)if(!entry){entry=p;break;}}return p;
}
extern "C" void __wrap__ZdlPvm(void *p,size_t n){for(auto &entry:records)if(entry==p){assert(n==544);auto *bytes=static_cast<uint8_t *>(p);for(size_t i=0;i<n;++i)assert(bytes[i]==0);++cleared;entry=nullptr;}__real__ZdlPvm(p,n);}
int main(int argc,char **argv){
 for(const auto &e:std::filesystem::recursive_directory_iterator(TESLA_CLEAN_STORAGE_PREFIX))if(e.is_regular_file())std::filesystem::remove(e.path());
 assert(argc==2);fail_at=unsigned(argv[1][0]-'0');Fixture f;BenchPairDisk disk;BenchPairing p(disk,f.rng());assert(p.bind(f.vin,f.name,f.addr,1,true));
 bool ok=p.prepare();assert(ok==(fail_at==0));assert(!p.exposed());if(!ok)assert(!p.key_present()&&!strcmp(p.state(),"error"));
 assert(allocations==(fail_at?fail_at:4));assert(cleared==(fail_at?allocations-1:allocations));for(auto *ptr:records)assert(!ptr);
}
''',scenarios=('0','1','2','3','4'),extra_sources=(MODULE/'tesla_ble_bench_pair_disk.cpp',),link_flags=('-Wl,--wrap=_ZnwmRKSt9nothrow_t','-Wl,--wrap=_ZdlPvm'))
    def test_02_explicit_pair_state_and_wire(self):
        self.build_run(r'''
int main(int argc,char **argv) {
 Fixture f; BenchPairing p(f.disk,f.rng()); assert(p.bind(f.vin,f.name,f.addr,1,true) && p.prepare());
 struct Link { Disk *disk; unsigned sent=0; std::vector<uint8_t> payload; bool refuse=false; } link{&f.disk,0,{},argc>1&&!strcmp(argv[1],"write_fail")};
 auto send=[](void *ctx,Bytes b){auto &l=*static_cast<Link *>(ctx); assert(l.disk->exposed); ++l.sent; l.payload.assign(b.data,b.data+b.size); return !l.refuse;};
 if(argc>1&&!strcmp(argv[1],"marker_fail")) { f.disk.fail_expose=true; assert(!p.send_pair(100,send,&link) && !link.sent); return 0; }
 assert(p.send_pair(100,send,&link)==!link.refuse && link.sent==1); assert(!p.send_pair(101,send,&link) && link.sent==1);
 assert(p.exposed() && p.key_present()); if(link.refuse) { assert(!strcmp(p.state(),"error")); return 0; }
 assert(!strcmp(p.state(),"request_sent"));
 ProtoReader envelope({link.payload.data(),link.payload.size()}); ProtoField field; assert(envelope.next(field)&&field.number==1&&field.wire==2&&envelope.done());
 ProtoReader signed_msg(field.bytes); assert(signed_msg.next(field)&&field.number==2&&field.wire==2); auto unsigned_wire=field.bytes; assert(signed_msg.next(field)&&field.number==3&&field.integer==2&&signed_msg.done());
 ProtoReader command(unsigned_wire); assert(command.next(field)&&field.number==16&&command.done()); ProtoReader whitelist(field.bytes); assert(whitelist.next(field)&&field.number==5); ProtoReader permission(field.bytes); assert(permission.next(field)&&field.number==1); assert(permission.next(field)&&field.number==4&&field.integer==6&&permission.done()); assert(whitelist.next(field)&&field.number==6&&whitelist.done());
 const uint8_t wait[]={0x22,2,8,1},ok[]={0x22,2,0x1a,0};
 p.receive_frame({wait,sizeof(wait)},101); assert(!strcmp(p.state(),"waiting_for_card"));
 if(argc>1&&!strcmp(argv[1],"timeout")) { p.expire(60100); assert(!strcmp(p.state(),"authorization_unverified")); }
 else { p.receive_frame({ok,sizeof(ok)},102); assert(!strcmp(p.state(),"authorization_unverified")); }
 assert(!p.active() && !p.authenticated());
 BenchPairing reboot(f.disk,f.rng()); assert(reboot.bind(f.vin,f.name,f.addr,1,true)&&reboot.prepare()); assert(!strcmp(reboot.state(),"authorization_unverified") && !reboot.send_pair(200,send,&link) && link.sent==1);
 puts("pair: one explicit role6 PRESENT_KEY2; exposure persisted; WAIT != OK != authentication; timeout no retry");
}
''', ('', 'timeout', 'write_fail', 'marker_fail'))
    def test_03_fresh_crypto_verification_and_cleanup(self):
        self.build_run(r'''
int main(int argc,char **argv) {
 Fixture f; BenchPairing p(f.disk,f.rng()); assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
 std::vector<uint8_t> sent; auto send=[](void *ctx,Bytes b){auto &s=*static_cast<std::vector<uint8_t> *>(ctx);s.assign(b.data,b.data+b.size);return true;};
 assert(p.verify(100,send,&sent) && !p.verify(100,send,&sent));
 ProtoReader request({sent.data(),sent.size()}); ProtoField field; std::array<uint8_t,16> route{},uuid{}; std::array<uint8_t,65> client{};
 while(request.next(field)) {
  if(field.number==7) { ProtoReader r(field.bytes); assert(r.next(field)&&field.number==2&&field.bytes.size==16); memcpy(route.data(),field.bytes.data,16); }
  else if(field.number==14) { ProtoReader r(field.bytes); assert(r.next(field)&&field.number==1&&field.bytes.size==65); memcpy(client.data(),field.bytes.data,65); }
  else if(field.number==51) { assert(field.bytes.size==16);memcpy(uuid.data(),field.bytes.data,16); }
 }
 assert(request.done()); P256Key remote; assert(remote.generate(f.rng())); std::array<uint8_t,65> remote_public{}; std::array<uint8_t,16> secret{},epoch{};
 assert(remote.public_point(remote_public)&&remote.exchange({client.data(),65},f.rng(),secret)); epoch.fill(9);
 ProtoWriter<128> info; assert(info.block(2,{remote_public.data(),65})&&info.block(3,{epoch.data(),16}));
 std::string scenario=argc>1?argv[1]:""; if(scenario=="denied") assert(info.integer(5,1));
 SessionCrypto crypto; assert(crypto.set_key({secret.data(),16})); std::array<uint8_t,32> tag{}; auto personalization=std::string(f.vin); if(scenario=="wrong_vin") personalization[0]='B';
 assert(crypto.session_info_tag({reinterpret_cast<const uint8_t *>(personalization.data()),17},{uuid.data(),16},info.bytes(),tag));
 if(scenario=="bad_tag") { tag[0]^=1; }
 if(scenario=="wrong_uuid") { uuid[0]^=1; }
 ProtoWriter<24> to,from; ProtoWriter<40> hmac; ProtoWriter<48> signature; ProtoWriter<512> reply;
 assert(to.block(2,{route.data(),16})&&from.integer(1,2)&&hmac.block(1,{tag.data(),32})&&signature.block(6,hmac.bytes()));
 assert(reply.block(6,to.bytes())&&reply.block(7,from.bytes())&&reply.block(15,info.bytes())&&reply.block(13,signature.bytes())&&reply.block(50,{uuid.data(),16}));
 p.receive_frame(reply.bytes(),scenario=="timeout"?5100:101);
 if(scenario.empty()) { assert(p.authenticated()&&!p.active()); }
 else { assert(!p.authenticated()); p.expire(5100); assert(!p.active()); }
 assert(!p.delete_local(false)); p.confirm_tesla_removal(); assert(p.revoked_by_user()&&p.key_present());
 f.disk.fail_remove=true; assert(!p.delete_local(true)&&p.key_present()); f.disk.fail_remove=false;
 assert(p.delete_local(true)&&!p.key_present()&&!strcmp(p.state(),"no_key")); assert(!p.verify(5200,send,&sent));
 assert(p.revoked_by_user()); puts("verify/cleanup: fresh VIN+UUID/domain HMAC, no command/retry; manual Tesla claim separate from local deletion");
}
''', ('', 'bad_tag', 'wrong_uuid', 'wrong_vin', 'denied', 'timeout'))
    def test_04_real_bench_disk_transaction(self):
        self.build_run(r'''
#include "tesla_ble_bench_pair_disk.h"
int main() {
 Fixture f; BenchPairDisk disk; BenchPairing p(disk,f.rng()); assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
 auto public_key=std::string(p.public_key_hex()); BenchPairing restored(disk,f.rng()); assert(restored.bind(f.vin,f.name,f.addr,1,true)&&restored.prepare()); assert(public_key==restored.public_key_hex());
 bool exposure=false; assert(disk.exposure(true,exposure)&&exposure); BenchPairing again(disk,f.rng()); assert(again.bind(f.vin,f.name,f.addr,1,true)&&again.prepare()&&again.exposed());
 assert(again.delete_local(true)); uint8_t record[544]; size_t size=0; assert(disk.load(record,sizeof(record),size)==LoadResult::Absent); assert(disk.exposure(false,exposure)&&!exposure);
 puts("disk: synced, closed, reloaded exact record; persistent exposure; explicit verified deletion");
}
''', extra_sources=(MODULE/'tesla_ble_bench_pair_disk.cpp',))
    def test_05_authentication_callback_does_not_nest_gatt_send(self):
        self.build_run(r'''
bool bench_test_challenge_inflight=false;
int main() {
 Fixture f; BenchPairing p(f.disk,f.rng()); assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare()); unsigned sent=0;
 auto send=[](void *ctx,Bytes b){ assert(!bench_test_challenge_inflight && b.size); ++*static_cast<unsigned *>(ctx);return true; };
 assert(p.verify(10,send,&sent)&&sent==1); assert(!p.verify(11,send,&sent)&&sent==1);
}
''', instrument_auth=True)
    def test_08_worker_measurement_has_no_deep_formatting(self):
        src=(MODULE/'tesla_ble_bench_discovery.cpp').read_text()
        measure=src.split('bool TeslaBleBenchDiscovery::measure(',1)[1].split('\nbool TeslaBleBenchDiscovery::stage(',1)[0]
        for forbidden in ('tesla_ble_iram_snapshot(', 'printf(', 'snprintf(', 'logger.'):
            self.assertNotIn(forbidden,measure)
        transport=src.split('void TeslaBleBenchDiscovery::pair_transport()',1)[1]
        self.assertNotIn('snapshot()',transport) # No overlapping full-report copies.
        callback=src.split('void TeslaBleBenchDiscovery::pair_frame(',1)[1].split('\nvoid TeslaBleBenchDiscovery::drain_pair_rx()',1)[0]
        self.assertNotIn('receive_frame(',callback) # FrameReceiver must unwind before crypto.
    def test_09_bench_project_hook_disables_object_cache(self):
        ini=(ROOT/'software/warp4_tesla_ble.ini').read_text().split('[env:warp4_tesla_ble_iram_bench]',1)[1].split('[env:',1)[0]
        self.assertIn('custom_build_cache_limit = -1',ini)
    def test_06_independent_embedded_form_factor_golden_decode(self):
        text=self.build_run(r'''
int main() {
 Fixture f; BenchPairing p(f.disk,f.rng()); assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
 auto send=[](void *,Bytes b){for(size_t i=0;i<b.size;++i)printf("%02x",b.data[i]); puts("");return true;}; assert(p.send_pair(10,send,nullptr));
}
''', emit=True)
        # Independent decoder, not TeslaBLEClean's ProtoReader or shared enums.
        def fields(data):
            pos=0; result={}
            def varint():
                nonlocal pos
                value=0; shift=0
                while True:
                    b=data[pos]; pos+=1; value|=(b&127)<<shift
                    if b<128: return value
                    shift+=7; self.assertLess(shift,64)
            while pos<len(data):
                tag=varint(); number=tag>>3; wire=tag&7
                self.assertNotIn(number,result)
                if wire==0: value=varint()
                else:
                    self.assertEqual(wire,2); size=varint(); value=data[pos:pos+size]; pos+=size
                result[number]=value
            self.assertEqual(pos,len(data)); return result
        envelope=fields(bytes.fromhex(text.strip())); self.assertEqual(set(envelope),{1})
        signed=fields(envelope[1]); self.assertEqual(set(signed),{2,3}); self.assertEqual(signed[3],2)
        unsigned=fields(signed[2]); self.assertEqual(set(unsigned),{16})
        whitelist=fields(unsigned[16]); self.assertEqual(set(whitelist),{5,6})
        permission=fields(whitelist[5]); self.assertEqual(set(permission),{1,4}); self.assertEqual(permission[4],6)
        key=fields(permission[1]); self.assertEqual(set(key),{1}); self.assertEqual(len(key[1]),65); self.assertEqual(key[1][0],4)
        metadata=fields(whitelist[6]); self.assertEqual(metadata.get(1,0),0) # official UNKNOWN=0; Android=7; cloud=9
    def test_10_exposure_backup_reconstructs_fail_closed(self):
        self.build_run(r'''
#include "tesla_ble_bench_pair_disk.h"
#include "tesla_ble_storage_io.h"
#include <filesystem>
int main() {
 Fixture f;BenchPairDisk disk;BenchPairing p(disk,f.rng());assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
 auto send=[](void *,Bytes){return true;};assert(p.send_pair(10,send,nullptr));p.expire(60010);
 TeslaBleStorageIO::Paths marker;assert(marker.initialize("/tesla_ble_bench_pair_exposed"));std::filesystem::rename(marker.canonical,marker.backup);
 BenchPairing reboot(disk,f.rng());assert(reboot.bind(f.vin,f.name,f.addr,1,true));assert(!reboot.prepare()&&strcmp(reboot.state(),"key_ready"));
 assert(!disk.remove()); // Must not unlink retained key without durable marker.
}
''',extra_sources=(MODULE/'tesla_ble_bench_pair_disk.cpp',))
    def test_07_delete_faults_and_reboot_fail_closed(self):
        self.build_run(r'''
#include "tesla_ble_bench_pair_disk.h"
#include <sys/stat.h>
#include "tesla_ble_storage_io.h"
#include <filesystem>
#include <cerrno>
extern "C" int __real_unlink(const char *);
extern "C" int __real_stat(const char *,struct stat *);
static const char *fault_suffix=nullptr; static bool read_fault=false,unlinked=false;
static bool match(const char *p) {if(!fault_suffix)return false;std::string s=p,f=fault_suffix;return s.size()>=f.size()&&s.compare(s.size()-f.size(),f.size(),f)==0;}
extern "C" int __wrap_unlink(const char *p) {if(match(p)&&!read_fault){errno=EIO;return -1;}int rc=__real_unlink(p);if(match(p)&&rc==0)unlinked=true;return rc;}
extern "C" int __wrap_stat(const char *p,struct stat *s) {if(match(p)&&read_fault&&unlinked){errno=EIO;return -1;}return __real_stat(p,s);}
int main(int argc,char **argv) {
 assert(argc==2); std::string scenario=argv[1]; for(const auto &entry:std::filesystem::recursive_directory_iterator(TESLA_CLEAN_STORAGE_PREFIX))if(entry.is_regular_file())std::filesystem::remove(entry.path());
 Fixture f; BenchPairDisk disk; BenchPairing p(disk,f.rng());assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
 auto send=[](void *,Bytes){return true;};assert(p.send_pair(1,send,nullptr));p.expire(60001);
 TeslaBleStorageIO::Paths paths;assert(paths.initialize("/tesla_ble_bench_pair_record"));
 // Both crash-recovery variants hold the same record, not fabricated material.
 std::filesystem::copy_file(paths.canonical,paths.temporary); std::filesystem::copy_file(paths.canonical,paths.backup);
 const char *suffixes[]={"_record","_record.tmp","_record.bak","_exposed","_exposed.tmp","_exposed.bak"};
 unsigned index=unsigned(scenario.back()-'0');assert(index<6);read_fault=scenario[0]=='r';fault_suffix=suffixes[index];
 if(index>=4) { // A pending marker is itself fail-closed; failed cleanup must retain it.
   FILE *file=fopen((std::string(TESLA_CLEAN_STORAGE_PREFIX)+"/tesla_ble_bench_pair_exposed"+(index==4?".tmp":".bak")).c_str(),"wb");assert(file);fputc(1,file);fclose(file);
 }
 assert(!p.delete_local(true));assert(p.key_present()&&p.exposed()&&!strcmp(p.state(),"error"));
 fault_suffix=nullptr;BenchPairing reboot(disk,f.rng());assert(reboot.bind(f.vin,f.name,f.addr,1,true));bool prepared=reboot.prepare();assert(!prepared||strcmp(reboot.state(),"key_ready"));
 bool exposed=false;bool readable=disk.exposure(false,exposed);assert(!readable||exposed);
 // Explicit retry by a new operator-owned action succeeds only after key variants disappear.
 assert(disk.remove());for(const char *path:{paths.canonical,paths.temporary,paths.backup})assert(!std::filesystem::exists(path));
 BenchPairing clean(disk,f.rng());assert(clean.bind(f.vin,f.name,f.addr,1,true)&&clean.prepare()&&!strcmp(clean.state(),"key_ready"));
}
''', scenarios=tuple(f'{kind}{i}' for kind in ('u','r') for i in range(6)),extra_sources=(MODULE/'tesla_ble_bench_pair_disk.cpp',),link_flags=('-Wl,--wrap=unlink','-Wl,--wrap=stat'))
if __name__=='__main__': unittest.main()
