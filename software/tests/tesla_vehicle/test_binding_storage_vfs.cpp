// Exact binary EV/SLAC MAC binding through the production TeslaBleStorageAdapter
// target POSIX branch and the extracted esp_littlefs 1.22.3 VFS over full lfs.c.
#include "tesla_ble_storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" unsigned vfs_backend_open_fds();
using namespace tesla_clean;
static std::vector<uint8_t> load(TeslaBleStorageAdapter &s, const char *key, size_t capacity, LoadResult &r)
{
    uint8_t b[64]{}; size_t n = 0; assert(capacity <= sizeof(b));
    r = s.load_into(key, b, capacity, n);
    return std::vector<uint8_t>(b, b + n);
}
int main()
{
    TeslaBleStorageAdapter a, other;
    assert(a.select_profile(2, 0x0123456789ABULL) && other.select_profile(3, 0x0123456789ACULL));
    const uint8_t other_mac[6] = {0xA0, 0x0B, 0x00, 0xFF, 0x80, 0x01};
    assert(KeyStorage::committed(other.save_transaction("warp_ev_mac", other_mac, 6)));
    const uint8_t base[6] = {0x00, 0x80, 0xFF, 0x01, 0x7F, 0xFE};
    unsigned cases = 0; LoadResult r;
    for (unsigned pos = 0; pos < 6; ++pos) {
        for (unsigned v = 0; v < 256; ++v) {
            uint8_t mac[6]; std::memcpy(mac, base, 6); mac[pos] = uint8_t(v);
            assert(a.save_transaction("warp_ev_mac", mac, 6) == SaveResult::Committed);
            const auto got = load(a, "warp_ev_mac", 6, r);
            assert(r == LoadResult::Loaded && got.size() == 6 && !std::memcmp(got.data(), mac, 6));
            ++cases;
        }
    }
    // Reused key: identical rewrite is a normal committed transaction.
    for (int i = 0; i < 3; ++i) { assert(a.save_transaction("warp_ev_mac", base, 6) == SaveResult::Committed); }
    // Wrong sizes are stored only as given and never accepted as a 6-byte binding.
    const uint8_t seven[7] = {1, 2, 3, 4, 5, 6, 7};
    assert(KeyStorage::committed(a.save_transaction("warp_ev_mac", seven, 7)));
    (void)load(a, "warp_ev_mac", 6, r); assert(r == LoadResult::Error);
    assert(KeyStorage::committed(a.save_transaction("warp_ev_mac", seven, 5)));
    auto got = load(a, "warp_ev_mac", 6, r); assert(r == LoadResult::Loaded && got.size() == 5);
    assert(a.save_transaction("warp_ev_mac", seven, 0) == SaveResult::NotCommitted);
    assert(a.save_transaction("warp_ev_mac", nullptr, 6) == SaveResult::NotCommitted);
    assert(a.save_transaction("not_a_key", base, 6) == SaveResult::NotCommitted);
    assert(a.save_transaction("warp_ev_mac", base, 6) == SaveResult::Committed);
    got = load(a, "warp_ev_mac", 6, r); assert(r == LoadResult::Loaded && got == std::vector<uint8_t>(base, base + 6));
    // Profile isolation and no leaked VFS descriptors.
    got = load(other, "warp_ev_mac", 6, r); assert(r == LoadResult::Loaded && got == std::vector<uint8_t>(other_mac, other_mac + 6));
    assert(vfs_backend_open_fds() == 0 && !a.restart_required());
    std::printf("BINDING_STORAGE_PASS exact 6-byte MAC binding for every byte value at every position (%u), reuse, wrong sizes, invalid key, isolation; real esp_littlefs VFS; no power-loss claim\n", cases);
}
