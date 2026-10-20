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
    // Worker-only selection after link retirement and release of all leases.
    // Default is legacy storage for old diagnostic builds, never profile UI.
    bool select_profile(int index, uint64_t mac);
    // Read-only boot gate: errors/legacy artifacts block. Sole exception is the
    // explicit six-field unbound inert tesla_vehicle singleton, all artifacts absent.
    // VIN may be empty or strictly valid; never auto-assigned to a new profile.
    // It is retained byte-for-byte and reclassified on every boot (no marker).
    static bool legacy_material_present();
    static bool profiles_config_present();
    static constexpr unsigned MAX_PROFILES = 16; // Checked against WARP's EV_MAX_EVS at integration boundary.
    template<typename F> bool set_mutation_guard(F &&guard) { return mutation_guard.assign(std::forward<F>(guard)); }

private:
    const char *path_for_key(std::string_view key) const;
    char profile_prefix[64]{};
    mutable char profile_path[96]{};
    bool may_mutate() const { return !mutation_guard || mutation_guard(); }
    tesla_clean::CheckedCallback<bool()> mutation_guard;
};
