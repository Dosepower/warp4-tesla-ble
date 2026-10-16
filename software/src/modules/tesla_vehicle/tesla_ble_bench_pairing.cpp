#include "tesla_ble_bench_pairing.h"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include "tesla_ble_validation.h"
#include "tesla_ble_bench_record.h"
#include <mbedtls/sha1.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <tesla_clean/protobuf.hpp>
using namespace tesla_clean;
namespace {
constexpr uint8_t magic[8]={'B','P','A','I','R',1,0,0}; constexpr size_t header=32;
// Size-only resource shapes: byte arrays, NOT protocol objects. No constructor,
// command encoder, ChallengeTransport, or Tesla transcript exists here.
struct LocalRehearsalWorkspace {
    std::array<uint8_t,sizeof(Frame)> large{};
    std::array<uint8_t,sizeof(VcsecPacket)> small{};
    std::array<uint8_t,sizeof(DomainAuthentication)> context{};
    std::array<uint8_t,16> shared{};
    std::array<uint8_t,32> tag{};
    ~LocalRehearsalWorkspace() { mbedtls_platform_zeroize(this,sizeof(*this)); }
};
}
bool BenchPairing::vehicle_name(const char *value, char (&name)[19])
{
    name[0]=0;
    if (!tesla_ble_valid_vin(value)) { return false; }
    uint8_t digest[20];
    if (mbedtls_sha1(reinterpret_cast<const uint8_t *>(value),17,digest)) { return false; }
    name[0]='S';
    for (unsigned i=0;i<8;++i) { snprintf(name+1+2*i,3,"%02x",digest[i]); }
    name[17]='C'; name[18]=0;
    return true;
}
bool BenchPairing::bind(const char *value,const char *name,const uint8_t *addr,uint8_t type,bool connectable)
{
    char expected[19];
    if (bound || !addr || !name || !connectable || type>1 || !vehicle_name(value,expected) || strcmp(name,expected)) { return false; }
    memcpy(vin,value,17); memcpy(address,addr,6); address_type=type; bound=true; return true;
}
SaveResult BenchPairing::RecordWriter::save(Bytes pem)
{
    if (!pem.valid() || pem.size>512 || !pem.size) { return SaveResult::NotCommitted; }
    std::unique_ptr<BenchPairRecord> workspace(new(std::nothrow) BenchPairRecord);
    if (!workspace) { return SaveResult::NotCommitted; }
    auto *record=workspace->bytes.data();
    memcpy(record,magic,8); memcpy(record+8,owner.vin,17); memcpy(record+25,owner.address,6); record[31]=owner.address_type;
    memcpy(record+header,pem.data,pem.size);
    return owner.disk.save({record,header+pem.size});
}
bool BenchPairing::prepare()
{
    if (!bound || prepared) { return false; }
    prepared=true;
    std::unique_ptr<BenchPairRecord> workspace(new(std::nothrow) BenchPairRecord);
    if (!workspace) { state_.store("error"); return false; }
    auto *record=workspace->bytes.data(); size_t length=0; bool exposure=false;
    keys.reset(new(std::nothrow) IdentityStore);
    const auto loaded=disk.load(record,workspace->bytes.size(),length);
    const bool exposure_ok=disk.exposure(false,exposure);
    exposed_.store(exposure || !exposure_ok);
    bool ok=keys && exposure_ok;
    if (ok && loaded==LoadResult::Loaded) {
        ok=length>header && !memcmp(record,magic,8) && !memcmp(record+8,vin,17) && !memcmp(record+25,address,6) && record[31]==address_type &&
            keys->import_pem({record+header,length-header},random);
    } else if (ok && loaded==LoadResult::Absent && !exposure) {
        workspace.reset(); // No loaded key: release/zeroize before generate+save.
        RecordWriter writer(*this); const auto outcome=keys->rotate(writer,random);
        ok=outcome.committed && !outcome.cleanup_pending && !outcome.quarantined;
    } else { ok=false; }
    workspace.reset();
    IdentityLease lease; std::array<uint8_t,65> point{};
    ok=ok && keys->acquire(lease) && lease.public_point(point);
    if (!ok) { keys.reset(); state_.store("error"); return false; }
    constexpr char hex[]="0123456789abcdef";
    for (unsigned i=0;i<point.size();++i) { public_hex[2*i]=hex[point[i]>>4]; public_hex[2*i+1]=hex[point[i]&15]; }
    public_hex[130]=0;
    key_present_.store(true); exposed_.store(exposure);
    state_.store(exposure?"authorization_unverified":"key_ready"); return true;
}
bool BenchPairing::send_pair(uint32_t now, Send send, void *context)
{
    if (!send || pair_attempted || active_ || strcmp(state(),"key_ready") || exposed()) { return false; }
    pair_attempted=true;
    IdentityLease lease; VcsecPacket packet; bool marked=false;
    if (!keys || !keys->acquire(lease) || !pair_charging_manager(lease,packet,KeyFormFactor::Unknown) || !disk.exposure(true,marked) || !marked) { fail(); return false; }
    exposed_.store(true); started=now; active_=true; state_.store("request_sent");
    if (!send(context,{packet.bytes.data(),packet.length})) { fail(); return false; }
    return true;
}
void BenchPairing::expire(uint32_t now)
{
    if (active_ && now-started>=(verifying?5000u:60000u)) { active_=false; wait_expired_.store(true); state_.store(verifying?"error":"authorization_unverified"); }
}
void BenchPairing::receive_frame(Bytes wire,uint32_t now)
{
    expire(now);
    if (!active_) { return; }
    if (verifying) {
        SessionWireReply reply;
        if (!decode_session_reply(wire,{routing.data(),routing.size()},reply)) { return; }
        const auto admission=authentication->receive(reply.view(1),now,random);
        if (admission==SessionAdmission::Established) { active_=false; state_.store("authenticated"); }
        else if (admission==SessionAdmission::KeyDenied) { fail(); }
        return;
    }
    VcsecStatus status;
    if (decode_vcsec_status(wire,status) && status.kind==VcsecKind::Pairing) {
        information_.store(status.information);
        if (status.operation==VcsecOperation::Waiting) { state_.store("waiting_for_card"); }
        else if (status.operation==VcsecOperation::Success) { active_=false; state_.store("authorization_unverified"); }
        else { fail(); }
        return;
    }
    // Official NFC pairing WAIT may have no whitelist submessage yet.
    ProtoReader outer(wire); ProtoField field;
    if (!outer.next(field) || field.number!=4 || field.wire!=2 || !outer.done()) { return; }
    ProtoReader command(field.bytes);
    if (command.next(field) && field.number==1 && field.wire==0 && field.integer==1 && command.done()) { state_.store("waiting_for_card"); }
}
bool BenchPairing::challenge(void *context,const ChallengeView &view)
{
    auto &s=*static_cast<BenchPairing *>(context);
    // Capture only. DomainAuthentication::begin must unwind before any GATT
    // write/wait/measurement. No external callback or response can run here.
    return s.verification_frame && encode_session_request(view,{s.routing.data(),s.routing.size()},*s.verification_frame);
}
bool BenchPairing::verify(uint32_t now,Send send,void *context)
{
    if (verify_attempted || active_ || !key_present() || !keys || !send) { return false; }
    verify_attempted=true; verifying=true; started=now; wait_expired_.store(false);
    authentication.reset(new(std::nothrow) DomainAuthentication(Domain::Vcsec,{reinterpret_cast<uint8_t *>(vin),17}));
    verification_frame.reset(new(std::nothrow) Frame);
    if (!authentication || !verification_frame || random.fill(random.context,routing.data(),routing.size())) { fail(); return false; }
    active_=true; state_.store("authorization_unverified");
    const auto outcome=authentication->begin(*keys,{1,1,now,5000,5000},now,random,{challenge,this});
    if (outcome!=ChallengeResult::Sent || !verification_frame->length) { verification_frame.reset(); fail(); return false; }
    // Deadline remains anchored before preparation/transmission, never restarted.
    const bool sent=send(context,{verification_frame->data.data(),verification_frame->length});
    verification_frame.reset();
    if (!sent) { fail(); return false; }
    return true;
}
bool BenchPairing::rehearse_local()
{
    if (!keys || !key_present() || exposed() || active_ || strcmp(state(),"key_ready")) { return false; }
    // Only size-matched byte workspaces and local crypto primitives. In
    // particular there is no protocol object, encoder, transport callback,
    // Tesla transcript, exposure marker or pairing-state transition.
    std::unique_ptr<LocalRehearsalWorkspace> workspace(new(std::nothrow) LocalRehearsalWorkspace);
    IdentityLease lease;
    if (!workspace || !keys->acquire(lease) || !random.fill ||
        random.fill(random.context,workspace->large.data(),32)) { return false; }
    memcpy(workspace->small.data(),workspace->large.data(),32);
    // Touch the full generic context shape without instantiating any state
    // machine. Checked allocation + zeroization, not an authorization trial.
    mbedtls_platform_zeroize(workspace->context.data(),workspace->context.size());
    std::array<uint8_t,65> point{};
    // Local public point as ECDH peer; generic SHA256 HMAC over random bytes.
    // Neither value is serialized or exposed, and no VIN/session metadata is
    // fed to a Tesla command/authentication encoder.
    const auto *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return md && lease.public_point(point) && lease.key() &&
        lease.key()->exchange({point.data(),point.size()},random,workspace->shared) &&
        mbedtls_md_hmac(md,workspace->shared.data(),workspace->shared.size(),
            workspace->small.data(),32,workspace->tag.data())==0;
}
bool BenchPairing::delete_local(bool acknowledged)
{
    // A mismatched/unreadable record is never deleted through this target's action.
    if (!acknowledged || active() || !prepared || !key_present()) { return false; }
    if (!disk.remove()) { fail(); return false; }
    authentication.reset(); keys.reset(); key_present_.store(false); public_hex[0]=0;
    state_.store("no_key"); return true;
}
#endif
