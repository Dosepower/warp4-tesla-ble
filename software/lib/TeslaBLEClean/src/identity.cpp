// SPDX-License-Identifier: Apache-2.0
// Independently authored lifetime/storage machinery; no Tesla protocol additions.
#include <tesla_clean/identity.hpp>
#include <mbedtls/platform_util.h>
namespace tesla_clean {
struct IdentityLease::Material {
    Allocation allocation;
    size_t references=1;
    P256Key key;
    explicit Material(Allocation a) : allocation(a) {}
};
IdentityLease::~IdentityLease() { reset(); }
IdentityLease::IdentityLease(IdentityLease &&other) noexcept : material_(std::exchange(other.material_,nullptr)) {}
IdentityLease &IdentityLease::operator=(IdentityLease &&other) noexcept
{
    if(this!=&other) { reset(); material_=std::exchange(other.material_,nullptr); }
    return *this;
}
void IdentityLease::reset()
{
    auto *old=std::exchange(material_,nullptr);
    if(old && --old->references==0) {
        const Allocation allocation=old->allocation;
        old->~Material(); allocation.release(allocation.context,old);
    }
}
bool IdentityLease::create(Allocation allocation)
{
    if(!allocation.allocate || !allocation.release) { return false; }
    void *memory=allocation.allocate(allocation.context,sizeof(Material));
    if(!memory) { return false; }
    auto *candidate=new(memory) Material(allocation);
    reset(); material_=candidate; return true;
}
bool IdentityLease::clone(IdentityLease &output) const
{
    if(!material_ || material_->references==std::numeric_limits<size_t>::max()) { return false; }
    if(this==&output) { return true; }
    ++material_->references;
    IdentityLease candidate; candidate.material_=material_; output=std::move(candidate); return true;
}
P256Key *IdentityLease::key() const { return material_?&material_->key:nullptr; }
bool IdentityLease::public_point(std::array<uint8_t,65> &output) const
{
    output.fill(0); return material_ && material_->key.public_point(output);
}
struct IdentityStore::Transaction {
    KeyStorage &storage;
    IdentityLease candidate,publication;
    std::array<uint8_t,512> pem{};
    size_t length=0;
    unsigned depth=0;
    bool rotation=false,published=false,committed=false,cleanup_pending=false;
    explicit Transaction(KeyStorage &s) : storage(s) {}
    ~Transaction() { mbedtls_platform_zeroize(pem.data(),pem.size()); }
};
void IdentityStore::publish(IdentityLease &&candidate)
{
    current_=std::move(candidate);
    // Saturating diagnostic only. Identity equality uses leases, not this counter.
    if(revision_!=UINT64_MAX) { ++revision_; }
}
bool IdentityStore::import_pem(Bytes pem,Random random)
{
    if(transaction_ || preparing_ || quarantined_) { return false; }
    preparing_=true;
    IdentityLease candidate;
    const bool ok=candidate.create(allocation_) && candidate.key()->import_pem(pem,random);
    preparing_=false;
    if(ok) { publish(std::move(candidate)); }
    return ok;
}
bool IdentityStore::generate(Random random)
{
    if(transaction_ || preparing_ || quarantined_) { return false; }
    preparing_=true;
    IdentityLease candidate;
    const bool ok=candidate.create(allocation_) && candidate.key()->generate(random);
    preparing_=false;
    if(ok) { publish(std::move(candidate)); }
    return ok;
}
IdentityOutcome IdentityStore::save(Transaction &transaction)
{
    if(transaction.depth==8 || quarantined_) { return {false,false,quarantined_,SaveResult::NotCommitted}; }
    ++transaction.depth;
    const SaveResult result=transaction.storage.save({transaction.pem.data(),transaction.length});
    --transaction.depth;
    const bool committed=KeyStorage::committed(result);
    if(result==SaveResult::RestartRequired) { quarantined_=true; }
    transaction.cleanup_pending|=result==SaveResult::NotCommittedCleanupPending || result==SaveResult::CommittedCleanupPending;
    transaction.committed|=committed;
    if(committed && transaction.rotation && !transaction.published) {
        transaction.published=true;
        // A publication reference and all bytes were prepared before any save callback.
        publish(std::move(transaction.publication));
    }
    return {transaction.committed,transaction.cleanup_pending,quarantined_,result};
}
IdentityOutcome IdentityStore::start(KeyStorage &storage,IdentityLease &&candidate,bool rotation)
{
    Transaction transaction(storage); transaction.rotation=rotation;
    transaction.candidate=std::move(candidate);
    if(!transaction.candidate || !transaction.candidate.key()->export_pem(transaction.pem.data(),transaction.pem.size(),transaction.length) ||
       (rotation && !transaction.candidate.clone(transaction.publication))) { return {}; }
    transaction_=&transaction;
    const auto outcome=save(transaction);
    transaction_=nullptr;
    return outcome;
}
IdentityOutcome IdentityStore::rotate(KeyStorage &storage,Random random)
{
    if(transaction_) {
        if(&transaction_->storage!=&storage || !transaction_->rotation) { return {}; }
        return save(*transaction_);
    }
    if(preparing_ || quarantined_) { return {false,false,quarantined_,SaveResult::NotCommitted}; }
    preparing_=true; IdentityLease candidate;
    const bool ok=candidate.create(allocation_) && candidate.key()->generate(random);
    preparing_=false;
    if(!ok) { return {}; }
    return start(storage,std::move(candidate),true);
}
IdentityOutcome IdentityStore::persist(KeyStorage &storage)
{
    if(transaction_ || preparing_ || quarantined_) { return {false,false,quarantined_,SaveResult::NotCommitted}; }
    IdentityLease candidate;
    if(!current_.clone(candidate)) { return {}; }
    return start(storage,std::move(candidate),false);
}
}
