// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/session_wire.hpp>
#include <tesla_clean/vcsec.hpp>
#include <tesla_clean/charge_state.hpp>
#include <optional>
namespace tesla_clean {
enum class WakePolicy { NoWakeSkip, WakeIfNeeded };
enum class VehicleResultCode { Soc, Status, WakeConfirmed, PairRequestSent, SkippedAsleep, KeyDenied, Failed, Expired, Disconnected };
// Nonsecret terminal classification of one command (diagnostics only, never
// authority): which bound or reply ended it, at which stage.
enum class VehicleFailure : uint8_t { None, Deadline, AttemptCap, WakePollCap, ChallengeCap, ChallengeInvalid, KeyDenied,
    Disconnected, VehicleFault, StatusUnusable, ChargeUnusable, Internal };
enum class VehicleStage : uint8_t { VcsecAuth, WaitVcsecAuth, Status, WaitStatus, Wake, WaitWake, InfoAuth, WaitInfoAuth, Charge, WaitCharge, Pair };
const char *vehicle_failure_name(VehicleFailure failure);
const char *vehicle_stage_name(VehicleStage stage);
// Saturating per-command counts; no payload, key, VIN or address material.
struct VehicleDiagnostics {
    VehicleFailure failure=VehicleFailure::None;
    VehicleStage stage=VehicleStage::VcsecAuth; // stage when the command ended
    uint32_t elapsed_ms=0;      // command start to terminal result
    uint32_t fault=0;           // last authenticated vehicle fault (0: none)
    uint8_t attempts=0;         // generic session/command attempts (cap 8)
    uint8_t challenges=0;       // session challenges begun (new UUID)
    uint8_t response_timeouts=0;// response budgets spent without an admitted reply
    uint8_t frames=0;           // complete frames dispatched to this command
    uint8_t rejected_replies=0; // session replies for this command not admitted
    uint8_t ignored_responses=0;// encrypted responses decoded but not accepted
    // Domain session already established on this link when the command began.
    bool vcsec_reused=false,info_reused=false;
    // An authenticated reply (session established/updated or response accepted) per domain.
    bool vcsec_answered=false,info_answered=false;
};
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
    // True only if, after this command exposed a wake, an authenticated VCSEC
    // vehicle status reported the vehicle awake. Evidence that the car actually
    // woke up (for honest reporting); never authority for anything else.
    bool awake_after_wake=false;
    VehicleDiagnostics diagnostics;
};
using VehicleCompletion=CheckedCallback<void(const VehicleResult &)>;
// total_ms: absolute operation deadline (hard upper bound, never extended).
// response_ms: wait for an encrypted reply after full framed TX; session requests
// are additionally resent at the 1 s transport interval while fresh.
// phase_ms: own step budget of each authentication phase and of the post-wake
// awake wait (reference AUTH_RESPONSE_TIMEOUT 25 s); 0 = one phase = total_ms.
struct VehicleTiming { uint32_t total_ms=5000,response_ms=500,transmit_ms=500,phase_ms=0; };
// Production SoC poll (WARP tesla_vehicle). Explicit phases, each with its own step
// budget, inside one hard absolute bound that no phase can extend:
//   VCSEC session  <= 25 s (from command start; usually immediate),
//   status/wake request exchange (bounded by attempts/backoff),
//   awake wait     <= 25 s after the single wake (authenticated status every 1 s),
//   infotainment session <= 25 s, a NEW budget from authenticated awake
//     (fresh challenge every 4 s, identical copies every 1 s in between),
//   charge-state request (response 5 s, bounded by attempts/backoff).
// Absolute upper bound for the whole operation: 90 s. There is no shared 30/40 s
// limit cutting the infotainment phase short after a slow wake.
inline constexpr VehicleTiming production_soc_timing{90000,5000,10000,session_auth_phase_ms};
static_assert(production_soc_timing.total_ms>=3*session_auth_phase_ms+2*production_soc_timing.response_ms,
    "absolute bound must cover VCSEC, awake and infotainment phases plus two request exchanges");
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
    using Stage=VehicleStage;
    struct Command {
        Kind kind=Kind::Poll;
        Stage stage=Stage::VcsecAuth;
        Stage published=Stage::WaitStatus;
        WakePolicy policy=WakePolicy::NoWakeSkip;
        uint64_t context=0,generation=0,authentication_context=0;
        uint32_t started=0,transmitted=0,retry_at=0;
        // Start of the current step budget: command start (VCSEC session), wake
        // exposure (awake wait), authenticated awake / fault refresh (session).
        uint32_t phase_started=0;
        // Failure retries of encrypted requests (exponential backoff index).
        unsigned retries=0;
        VehicleTiming timing;
        unsigned attempts=0;
        // Post-wake VCSEC status probes: paced and separately bounded (see tick/encrypted).
        unsigned wake_polls=0;
        uint32_t retry_delay=100;
        bool delayed=false,wake_sent=false,has_request=false,refresh=false,awake_after_wake=false;
        // Authenticated reply (session establishment/update or accepted response) per domain.
        bool vcsec_answered=false,info_answered=false;
        VehicleDiagnostics diagnostics;
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
    static OperationTime operation_time(const Command &c) {
        return {c.authentication_context,c.generation,c.started,c.timing.total_ms,c.timing.response_ms,
            c.timing.phase_ms?c.phase_started:c.started,c.timing.phase_ms};
    }
    static bool in_auth(const Command &c) {
        return c.stage==Stage::VcsecAuth || c.stage==Stage::WaitVcsecAuth || c.stage==Stage::InfoAuth || c.stage==Stage::WaitInfoAuth;
    }
    // Waiting for authenticated awake evidence after this command exposed a wake.
    static bool in_wake(const Command &c) {
        return c.wake_sent && !c.awake_after_wake && (c.stage==Stage::WaitWake || c.stage==Stage::Status || c.stage==Stage::WaitStatus);
    }
    uint32_t backoff(Command &c);
    bool advance();
    bool enqueue(Kind kind,WakePolicy policy,uint64_t context,VehicleCompletion &&completion,VehicleTiming timing);
    void finish(VehicleResultCode code,uint8_t battery=0,SleepState sleep=SleepState::Unknown,bool authenticated=false,
        VehicleFailure failure=VehicleFailure::Internal);
    void fail(VehicleFailure failure) { finish(VehicleResultCode::Failed,0,SleepState::Unknown,false,failure); }
    void retry(Stage stage,uint32_t now,uint32_t delay=legacy_retry_delay_ms);
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
    // Timing model (pinned reference tesla-ble v6.2.0 include/vehicle.h:363-381):
    //   TRANSPORT_RETRY_INTERVAL 1 s  -> transport_retry_interval_ms (session request
    //     resend cadence and post-wake status pace),
    //   AUTH_RESPONSE_TIMEOUT 25 s     -> VehicleTiming::phase_ms per phase (production),
    //   CLOCK_SYNC_MAX_LATENCY 4 s     -> session_info_max_latency_ms (challenge renewal),
    //   MAX_AUTH_RESENDS 10            -> session_info_max_replays,
    //   MAX_RETRIES 5, backoff 250 ms x2 up to 8 s -> request retries (backoff()).
    // The generic cap max_attempts counts every encrypted request including the
    // first status/wake/charge sends: 3 + MAX_RETRIES(5) = 8. Session request copies
    // are bounded per phase instead (session_info_max_challenges, phase_ms).
    // After a (possibly) exposed wake, VCSEC reports asleep until the car has woken.
    // Probe its status at the transport pace and never with the request backoff.
    // A wake is never resent; the awake wait is bounded by its own phase_ms, by
    // max_wake_status_polls and by the absolute total deadline.
    static constexpr uint32_t transport_retry_interval_ms=1000;
    static constexpr uint32_t wake_status_interval_ms=transport_retry_interval_ms;
    static constexpr unsigned max_wake_status_polls=40;
    static constexpr unsigned max_retries=5;
    static constexpr unsigned max_attempts=3+max_retries;
    static constexpr uint32_t initial_retry_delay_ms=250,max_retry_delay_ms=8000;
    // Single-phase operations (VehicleTiming::phase_ms==0, e.g. pairing and the
    // approval verification polls): original fixed retry delay, challenge and
    // replay caps (legacy_session_info_max_*). Their pairing proof is unchanged.
    static constexpr uint32_t legacy_retry_delay_ms=100;
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
