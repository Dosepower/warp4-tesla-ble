#pragma once
#include <cstddef>
namespace r13_psram_resource {
constexpr size_t internal8_free_floor = 8192;
constexpr size_t internal8_largest_floor = 4096;
constexpr size_t internal8_minimum_floor = 8192;
constexpr size_t worker_watermark_floor = 2048;
constexpr bool admit(size_t free, size_t largest, size_t minimum, size_t worker, bool worker_exists) {
    return free >= internal8_free_floor && largest >= internal8_largest_floor && minimum >= internal8_minimum_floor &&
           (!worker_exists || worker >= worker_watermark_floor);
}
}
