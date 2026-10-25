// SPDX-License-Identifier: Apache-2.0
#include <tesla_clean/vcsec.hpp>
#include <tesla_clean/key.hpp>
#include <mbedtls/ctr_drbg.h>
#include "wire_vectors.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tesla_clean;
int main()
{
    mbedtls_ctr_drbg_context drbg; mbedtls_ctr_drbg_init(&drbg);
    auto entropy=[](void *,unsigned char *p,size_t n) { std::memset(p,31,n); return 0; };
    assert(mbedtls_ctr_drbg_seed(&drbg,entropy,nullptr,nullptr,0)==0); Random rng{mbedtls_ctr_drbg_random,&drbg};
    IdentityStore keys; P256Key key; std::array<uint8_t,32> scalar{}; scalar[31]=1;
    assert(key.import_scalar({scalar.data(),32},rng)); std::array<uint8_t,512> pem{}; size_t n=0;
    assert(key.export_pem(pem.data(),pem.size(),n) && keys.import_pem({pem.data(),n},rng));
    IdentityLease lease; assert(keys.acquire(lease)); VcsecPacket packet;
    assert(pair_charging_manager(lease,packet));
    assert(packet.length==sizeof(wire_vectors::pair) && !std::memcmp(packet.bytes.data(),wire_vectors::pair,packet.length));
    IdentityLease absent; assert(!pair_charging_manager(absent,packet) && packet.length==0);
    auto status=status_payload(); const uint8_t expected[]={0x0a,0}; assert(status.size==2 && !std::memcmp(status.data,expected,2));
    assert(!wake_payload(false,packet) && packet.length==0);
    assert(wake_payload(true,packet) && packet.length==2 && packet.bytes[0]==0x10 && packet.bytes[1]==30);
    VcsecStatus parsed; const uint8_t awake[]={0x0a,2,0x18,1},asleep[]={0x0a,2,0x18,2},unknown[]={0x0a,0};
    assert(decode_vcsec_status({awake,sizeof(awake)},parsed) && parsed.kind==VcsecKind::Vehicle && parsed.sleep==SleepState::Awake);
    assert(decode_vcsec_status({asleep,sizeof(asleep)},parsed) && parsed.sleep==SleepState::Asleep);
    assert(decode_vcsec_status({unknown,sizeof(unknown)},parsed) && parsed.sleep==SleepState::Unknown);
    const uint8_t ok[]={0x22,2,0x1a,0},wait[]={0x22,4,8,1,0x1a,0},failure[]={0x22,6,8,2,0x1a,2,8,24};
    assert(decode_vcsec_status({ok,sizeof(ok)},parsed) && parsed.kind==VcsecKind::Pairing && parsed.operation==VcsecOperation::Success);
    // Populated whitelist result has precedence over the legacy outer status.
    assert(decode_vcsec_status({wait,sizeof(wait)},parsed) && parsed.operation==VcsecOperation::Success);
    const uint8_t legacy_ok[]={0x22,4,8,2,0x1a,0},unknown_code[]={0x22,5,0x1a,3,8,0x80,1},waiting[]={0x22,2,8,1};
    assert(decode_vcsec_status({legacy_ok,sizeof(legacy_ok)},parsed) && parsed.operation==VcsecOperation::Success);
    assert(decode_vcsec_status({unknown_code,sizeof(unknown_code)},parsed) && parsed.operation==VcsecOperation::Error && parsed.information==128);
    assert(decode_vcsec_status({waiting,sizeof(waiting)},parsed) && parsed.operation==VcsecOperation::Waiting);
    assert(decode_vcsec_status({failure,sizeof(failure)},parsed) && parsed.operation==VcsecOperation::Error && parsed.information==24);
    const uint8_t bad_sleep[]={0x0a,2,0x18,3},duplicate[]={0x0a,4,0x18,1,0x18,2},conflicting[]={0x0a,0,0x22,2,0x1a,0};
    assert(!decode_vcsec_status({bad_sleep,sizeof(bad_sleep)},parsed));
    assert(!decode_vcsec_status({duplicate,sizeof(duplicate)},parsed));
    assert(!decode_vcsec_status({conflicting,sizeof(conflicting)},parsed));
    for(size_t i=0;i<sizeof(failure);++i) { assert(!decode_vcsec_status({failure,i},parsed)); }
    mbedtls_ctr_drbg_free(&drbg);
    std::puts("Clean VCSEC subset: charging-manager/PRESENT_KEY independent KAT, no implicit wake, status sleep and pairing OK/WAIT/error, duplicate/conflict/truncation refusal passed");
}
