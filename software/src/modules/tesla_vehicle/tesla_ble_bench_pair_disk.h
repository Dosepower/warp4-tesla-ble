#pragma once
#include "tesla_ble_bench_pairing.h"
#if defined(TESLA_BLE_BENCH_ONLY) && TESLA_BLE_BENCH_ONLY && defined(TESLA_BLE_IRAM_DIAGNOSTICS) && TESLA_BLE_IRAM_DIAGNOSTICS
class BenchPairDisk final : public BenchPairStorage {
public:
    tesla_clean::LoadResult load(uint8_t *,size_t,size_t &) override;
    tesla_clean::SaveResult save(tesla_clean::Bytes) override;
    bool exposure(bool mark,bool &value) override;
    bool remove() override;
};
#endif
