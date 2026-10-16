#pragma once

#include <tesla_clean/storage.hpp>
#include <string>
#include <vector>


class TeslaBleStorageAdapter final : public tesla_clean::KeyStorage
{
public:
    using LoadResult = tesla_clean::LoadResult;
    using SaveResult = tesla_clean::SaveResult;
    enum class KeyPresence { Unknown, Present, Absent, CleanupRequired };
    KeyPresence private_key_presence();
    LoadResult load_into(const char *key, uint8_t *buffer, size_t capacity, size_t &size);
    SaveResult save(tesla_clean::Bytes pem) override { return save_transaction("private_key", pem.data, pem.size); }
    SaveResult save_transaction(const char *key, const uint8_t *buffer, size_t size);
    bool remove_key(const char *key);
    bool initialize_identity(tesla_clean::IdentityStore &keys, tesla_clean::Random random, bool allow_generate = true);
    bool has_private_key();
    bool forget_material();
    bool restart_required() const;
    template<typename F> bool set_mutation_guard(F &&guard) { return mutation_guard.assign(std::forward<F>(guard)); }

private:
    static const char *path_for_key(std::string_view key);
    bool may_mutate() const { return !mutation_guard || mutation_guard(); }
    tesla_clean::CheckedCallback<bool()> mutation_guard;
};
