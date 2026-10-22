#pragma once
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
#include <tesla_clean/storage.hpp>
#include <tesla_clean/session_wire.hpp>
#include <tesla_clean/vcsec.hpp>
#include <atomic>
#include <memory>
// Separate bench-only record. Production key/session files are never used.
class BenchPairStorage : public tesla_clean::KeyStorage {
public:
    virtual tesla_clean::LoadResult load(uint8_t *, size_t, size_t &) = 0;
    virtual bool exposure(bool mark, bool &value) = 0;
    virtual bool remove() = 0;
};
class BenchPairing final {
public:
    BenchPairing(BenchPairStorage &disk, tesla_clean::Random random) : disk(disk), random(random) {}
    static bool vehicle_name(const char *vin, char (&name)[19]);
    bool bind(const char *vin, const char *name, const uint8_t *address, uint8_t type, bool connectable);
    bool prepare();
    using Send = bool (*)(void *, tesla_clean::Bytes);
    bool send_pair(uint32_t now, Send, void *);
    bool verify(uint32_t now, Send, void *);
    bool rehearse_local(); // Generic byte shapes/local crypto only; no command encoding or transport.
    bool delete_local(bool acknowledge_unresolved_tesla_authorization);
    void confirm_tesla_removal() { revoked_.store(true); } // User assertion, NOT cryptographic revocation evidence.
    void receive_frame(tesla_clean::Bytes, uint32_t now);
    void expire(uint32_t now);
    void fail() { active_=false; state_.store("error"); }
    bool active() const { return active_; } // Worker-only; never read on HTTP task.
    bool authenticated() const { return !strcmp(state(),"authenticated"); }
    const char *state() const { return state_.load(); }
    const char *public_key_hex() const { return public_hex; } // Public point only, not a private-key export.
    bool key_present() const { return key_present_.load(); }
    bool exposed() const { return exposed_.load(); }
    bool wait_expired() const { return wait_expired_.load(); }
    uint32_t information() const { return information_.load(); }
    bool revoked_by_user() const { return revoked_.load(); }
private:
    class RecordWriter final : public tesla_clean::KeyStorage {
        BenchPairing &owner;
    public:
        explicit RecordWriter(BenchPairing &p) : owner(p) {}
        tesla_clean::SaveResult save(tesla_clean::Bytes) override;
    };
    BenchPairStorage &disk;
    tesla_clean::Random random;
    std::unique_ptr<tesla_clean::IdentityStore> keys;
    char vin[18]{}, public_hex[131]{};
    uint8_t address[6]{}, address_type=0;
    bool bound=false, prepared=false;
    bool pair_attempted=false, active_=false;
    bool verify_attempted=false, verifying=false;
    uint32_t started=0;
    std::unique_ptr<tesla_clean::DomainAuthentication> authentication;
    std::array<uint8_t,16> routing{};
    std::unique_ptr<tesla_clean::Frame> verification_frame;
    static bool challenge(void *, const tesla_clean::ChallengeView &);
    std::atomic<const char *> state_{"no_key"};
    std::atomic<bool> key_present_{false}, exposed_{false}, revoked_{false};
    std::atomic<bool> wait_expired_{false};
    std::atomic<uint32_t> information_{0};
};
#endif
