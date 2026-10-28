// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/session_wire.hpp>
#include <tesla_clean/vcsec.hpp>
#include <tesla_clean/charge_state.hpp>
#include <optional>
namespace tesla_clean {
enum class WakePolicy { NoWakeSkip, WakeIfNeeded };
enum class VehicleResultCode { Soc, Status, WakeConfirmed, PairRequestSent, SkippedAsleep, KeyDenied, Failed, Expired, Disconnected };
struct VehicleResult {
    uint64_t context=0,generation=0;
    VehicleResultCode code=VehicleResultCode::Failed;
    uint8_t battery=0;
    SleepState sleep=SleepState::Unknown;
    bool authenticated=false;
    // True once this command handed an RKE wake frame to the transport, including a
    // partial, refused or unacknowledged write: the vehicle may have received it.
    // Callers use it to settle a wake budget; it is never authentication evidence.
    bool wake_exposed=false;
};
using VehicleCompletion=CheckedCallback<void(const VehicleResult &)>;
struct VehicleTiming { uint32_t total_ms=5000,response_ms=500,transmit_ms=500; };
struct VehiclePorts {
    Random random;
    uint32_t (*now)(void *)=nullptr;
    WriteChunk write=nullptr;
    OwnsLink owns=nullptr;
    void *context=nullptr;
    size_t chunk_size=20;
    // True only if the backend proves the peer framing stream is reset/clean.
    // Absence/refusal causes local link retirement, not an unsafe framed retry.
    bool (*recover_frame)(void *,uint64_t)=nullptr;
};
// Single-worker bounded controller. Ports/identity store must outlive it.
// Callbacks may close/open/enqueue/tick, but must not destroy this object.
class Vehicle {
    enum class Kind { Poll, Status, Wake, Pair };
    enum class Stage { VcsecAuth, WaitVcsecAuth, Status, WaitStatus, Wake, WaitWake, InfoAuth, WaitInfoAuth, Charge, WaitCharge, Pair };
    struct Command {
        Kind kind=Kind::Poll;
        Stage stage=Stage::VcsecAuth;
        Stage published=Stage::WaitStatus;
        WakePolicy policy=WakePolicy::NoWakeSkip;
        uint64_t context=0,generation=0,authentication_context=0;
        uint32_t started=0,transmitted=0,retry_at=0;
        VehicleTiming timing;
        unsigned attempts=0;
        // Post-wake VCSEC status probes: paced and separately bounded (see tick/encrypted).
        unsigned wake_polls=0;
        uint32_t retry_delay=100;
        bool delayed=false,wake_sent=false,has_request=false,refresh=false;
        VehicleCompletion completion;
    };
    struct Channel {
        DomainAuthentication authentication;
        std::optional<ResponseGuard> guard;
        uint32_t high_water=0,clock_anchor=0;
        Channel(Domain domain,Bytes vin) : authentication(domain,vin) {}
        bool exhausted() const { return authentication.exhausted(); }
        // A local reconnect is not proof that the vehicle forgot a possibly exposed counter.
        void reset() { guard.reset(); authentication.reset(); clock_anchor=0; }
    };
    struct Deferred { Frame frame; uint32_t now=0; };
    struct Transmission {
        Vehicle *vehicle;
        VehiclePorts ports;
        uint64_t serial,generation;
        uint32_t started,operation_started;
        VehicleTiming timing;
        bool accepted=false;
    };
    IdentityStore &keys_;
    VehiclePorts ports_;
    std::array<uint8_t,17> vin_{};
    std::array<uint8_t,16> address_{},last_uuid_{};
    bool valid_=false,connected_=false,ever_opened_=false,uuid_used_=false;
    uint64_t generation_=0,last_generation_=0,serial_=0,transmission_=0;
    uint64_t operation_sequence_=0;
    bool operation_retired_=false;
    Channel vcsec_,info_;
    FrameReceiver receiver_;
    std::array<Command,4> commands_{};
    size_t count_=0;
    std::array<Deferred,4> deferred_{};
    size_t deferred_count_=0;
    bool deferred_overflow_=false;
    uint32_t deferred_overflows_=0; // Nonsecret diagnostic count only.
    // Nonsecret saturating counts of complete received frames and of frames
    // decoded as pairing/session replies. Diagnostics only, never authority.
    uint32_t frames_seen_=0,pair_replies_seen_=0,session_replies_seen_=0;
    // PRESENT_KEY has no request UUID. Only one explicitly sent request on
    // this physical generation may observe its unauthenticated status. Never
    // use this observation as session/key authorization.
    uint64_t pair_context_=0;
    uint32_t pair_started_=0,pair_tx_started_=0;
    bool pair_reply_pending_=false,pair_reply_terminal_=false;
    VcsecStatus pair_reply_;
    Channel &channel(Domain d) { return d==Domain::Vcsec?vcsec_:info_; }
    static OperationTime operation_time(const Command &c) { return {c.authentication_context,c.generation,c.started,c.timing.total_ms,c.timing.response_ms}; }
    bool advance();
    bool enqueue(Kind kind,WakePolicy policy,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing);
    void finish(VehicleResultCode code,uint8_t battery=0,SleepState sleep=SleepState::Unknown,bool authenticated=false);
    void retry(Stage stage,uint32_t now,uint32_t delay=100);
    bool transmit(Bytes payload,Transmission &tx);
    void drain(Transmission &tx);
    static bool transmit_owns(void *,uint64_t);
    static uint32_t transmit_clock(void *);
    static bool transmit_write(void *,Bytes,uint64_t);
    static bool transmit_challenge(void *,const ChallengeView &);
    static void emitted(void *,const Frame &);
    void frame(const Frame &,uint32_t now);
    bool authenticate(Domain d,Stage waiting,Stage next,uint32_t now);
    void encrypted(Domain d,Bytes payload,Stage waiting,uint32_t now);
public:
    // After a (possibly) exposed wake, VCSEC reports asleep until the car has woken.
    // Probe its status at this pace (official/reference transport retry interval) and
    // never with the 100 ms backoff. Probes count against their own cap, bounded in
    // addition by the command's unchanged total deadline, not the generic 8-attempt
    // budget that protects session/command retries. A wake is still never resent.
    static constexpr uint32_t wake_status_interval_ms=1000;
    static constexpr unsigned max_wake_status_polls=40;
    bool exhausted() const { return operation_retired_ || serial_ == UINT64_MAX || receiver_.exhausted() || vcsec_.exhausted() || info_.exhausted(); }
    Vehicle(IdentityStore &keys,Bytes vin,Bytes routing_address,VehiclePorts ports);
    Vehicle(const Vehicle &)=delete;
    Vehicle &operator=(const Vehicle &)=delete;
    bool open(uint64_t generation);
    void close(uint64_t generation);
    bool connected() const { return connected_; }
    uint64_t generation() const { return generation_; }
    size_t queued() const { return count_; }
    // True only while one accepted frame transmission owns the link. Received
    // complete frames are then deferred (never dispatched) until drain(tx), so a
    // worker may feed receive() from inside its own WRITE wait without
    // re-entering protocol processing or starting nested GATT work.
    bool transmitting() const { return connected_ && transmission_!=0; }
    uint32_t deferred_overflows() const { return deferred_overflows_; }
    uint32_t frames_seen() const { return frames_seen_; }
    uint32_t pair_replies_seen() const { return pair_replies_seen_; }
    uint32_t session_replies_seen() const { return session_replies_seen_; }
    uint32_t high_water(Domain d) const { return d==Domain::Vcsec?vcsec_.high_water:info_.high_water; }
    // An intentionally empty completion is optional. Callers preparing a callback
    // must check assign's result separately; absence is not an allocation-error marker.
    bool charge_state_poll(WakePolicy policy,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing={});
    bool status(uint64_t context,VehicleCompletion &&completion,VehicleTiming timing={});
    bool wake(bool explicitly_authorized,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing={});
    // PRESENT_KEY transmission only; approval must be verified by a subsequent authenticated exchange.
    bool pair(bool explicitly_authorized,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing={});
    bool take_pairing_status(uint64_t context,uint64_t generation,VcsecStatus &status);
    void end_pairing_status(uint64_t context);
    void tick();
    bool receive(Bytes fragment,uint64_t generation);
};
}
