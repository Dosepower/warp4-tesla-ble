// SPDX-License-Identifier: Apache-2.0
// Real production POSIX storage path, synthetic identities only; never print material.
#include "../../../src/modules/tesla_vehicle/tesla_ble_storage.h"
#include <tesla_clean/identity.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

static bool tracking = false, refuse = false;
static void *scratch = nullptr;
static unsigned allocations = 0, releases = 0;
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_calloc(size_t n, size_t size)
{
    if (tracking && n * size == 4096) {
        ++allocations;
        if (refuse) { return nullptr; }
        assert(!scratch);
        scratch = __real_calloc(n, size);
        assert(scratch);
        return scratch;
    }
    return __real_calloc(n, size);
}
extern "C" void __wrap_free(void *p)
{
    if (p && p == scratch) {
        // Every byte, including unused capacity, must be wiped before release.
        const volatile uint8_t *bytes = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < 4096; ++i) { assert(bytes[i] == 0); }
        scratch = nullptr;
        ++releases;
    }
    __real_free(p);
}
static int entropy(void *p, uint8_t *out, size_t size)
{
    auto &n = *static_cast<uint32_t *>(p);
    for (size_t i = 0; i < size; ++i) { n = n * 1664525u + 1013904223u; out[i] = n >> 24; }
    return 0;
}
using Snapshot = std::map<std::string, std::string>;
static Snapshot snapshot()
{
    Snapshot result;
    for (const auto &entry : std::filesystem::directory_iterator(TESLA_CLEAN_STORAGE_PREFIX "/config")) {
        if (entry.is_regular_file()) {
            std::ifstream file(entry.path(), std::ios::binary);
            result.emplace(entry.path().filename().string(), std::string(std::istreambuf_iterator<char>(file), {}));
        }
    }
    return result;
}
static void check(TeslaBleStorageAdapter &store, TeslaBleStorageAdapter::KeyPresence expected, bool oom)
{
    const auto before = snapshot();
    refuse = oom;
    allocations = releases = 0;
    tracking = true;
    const auto actual = store.private_key_presence();
    tracking = false;
    assert(actual == (oom ? TeslaBleStorageAdapter::KeyPresence::Unknown : expected));
    assert(allocations == 1 && releases == (oom ? 0u : 1u) && scratch == nullptr);
    assert(snapshot() == before); // Presence checking/OOM must never mutate any profile.
}
int main()
{
    using Presence = TeslaBleStorageAdapter::KeyPresence;
    std::filesystem::create_directories(TESLA_CLEAN_STORAGE_PREFIX "/config");
    TeslaBleStorageAdapter a, b;
    assert(a.select_profile(0, 1) && b.select_profile(1, 2)); // Synthetic namespaces.
    assert(a.forget_material() && b.forget_material());
    check(a, Presence::Absent, false); check(a, Presence::Absent, true);
    uint32_t seed = 1;
    tesla_clean::IdentityStore identity;
    assert(a.initialize_identity(identity, {entropy, &seed}));
    check(a, Presence::Present, false); check(a, Presence::Present, true);
    check(b, Presence::Absent, false); check(b, Presence::Absent, true);
    assert(a.forget_material());
    const uint8_t invalid[] = {1, 2, 3};
    assert(tesla_clean::KeyStorage::committed(a.save_transaction("private_key", invalid, sizeof(invalid))));
    check(a, Presence::Unknown, false); check(a, Presence::Unknown, true);
    assert(a.forget_material());
    assert(tesla_clean::KeyStorage::committed(a.save_transaction("authorized", invalid, 1)));
    check(a, Presence::CleanupRequired, false); check(a, Presence::CleanupRequired, true);
    assert(a.forget_material() && b.forget_material());
    std::puts("PASS: 10 presence/checked-allocation cases; wipe-before-free, OOM=Unknown, profile isolation, no persistence mutations");
}
