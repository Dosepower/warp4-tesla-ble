// COMPILE-NEGATIVE: in production hook configuration the injection hooks must not exist. Runner expects compile FAILURE.
#define TESLA_PSRAM_PLACEMENT 1
#define TESLA_PSRAM_HOST_TEST 1
#include "tesla_psram.h"
int main() { return static_cast<int>(tesla_psram::inject_failures.load()); }
