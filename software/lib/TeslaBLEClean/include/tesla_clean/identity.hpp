// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <tesla_clean/key.hpp>
#include <tesla_clean/support.hpp>
namespace tesla_clean {
enum class SaveResult { NotCommitted, NotCommittedCleanupPending, Committed, CommittedCleanupPending, RestartRequired };
class KeyStorage {
public:
    virtual ~KeyStorage()=default;
    virtual SaveResult save(Bytes pem)=0;
    static bool committed(SaveResult r) { return r==SaveResult::Committed || r==SaveResult::CommittedCleanupPending; }
};
struct IdentityOutcome { bool committed=false,cleanup_pending=false,quarantined=false; SaveResult last=SaveResult::NotCommitted; };
class IdentityLease {
    struct Material;
    Material *material_=nullptr;
    bool create(Allocation allocation);
    friend class IdentityStore;
public:
    IdentityLease()=default;
    ~IdentityLease();
    IdentityLease(const IdentityLease &)=delete;
    IdentityLease &operator=(const IdentityLease &)=delete;
    IdentityLease(IdentityLease &&other) noexcept;
    IdentityLease &operator=(IdentityLease &&other) noexcept;
    void reset();
    bool public_point(std::array<uint8_t,65> &output) const;
    bool clone(IdentityLease &output) const;
    // Borrowed crypto handle; the caller must not mutate its private identity.
    P256Key *key() const;
    bool same(const IdentityLease &other) const { return material_ && material_==other.material_; }
    explicit operator bool() const { return material_!=nullptr; }
};
class IdentityStore {
    struct Transaction;
    Allocation allocation_;
    IdentityLease current_;
    Transaction *transaction_=nullptr;
    uint64_t revision_=0;
    bool quarantined_=false;
    bool preparing_=false;
    void publish(IdentityLease &&candidate);
    IdentityOutcome save(Transaction &transaction);
    IdentityOutcome start(KeyStorage &storage,IdentityLease &&candidate,bool rotation);
public:
    explicit IdentityStore(Allocation allocation={}) : allocation_(allocation) {}
    IdentityStore(const IdentityStore &)=delete;
    IdentityStore &operator=(const IdentityStore &)=delete;
    bool acquire(IdentityLease &output) const { return current_.clone(output); }
    bool import_pem(Bytes pem,Random random);
    bool generate(Random random);
    IdentityOutcome rotate(KeyStorage &storage,Random random);
    IdentityOutcome persist(KeyStorage &storage);
    uint64_t revision() const { return revision_; }
    bool quarantined() const { return quarantined_; }
};
}
