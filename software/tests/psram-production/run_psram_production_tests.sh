#!/bin/bash
# Tesla PSRAM production host tests (port of tests/tesla_psram lab suite to production switch names).
# HOST ONLY: proves allocator/lifecycle/sample-table/executor/dispatcher LOGIC against the real production headers;
# it does NOT prove ESP32 runtime placement. No device/LAN/USB/SDK. Usage: run_psram_production_tests.sh <outdir>
set -uo pipefail
H="$(cd "$(dirname "$0")" && pwd)"; SW="$(cd "$H/../.." && pwd)"; SRC=$SW/src; MOD=$SRC/modules/tesla_vehicle; LIB=$SW/lib/TeslaBLEClean/include
O="${1:?outdir}"; mkdir -p "$O"; rc=0; : > "$O/summary.txt"
MB="${TESLA_PSRAM_TEST_MBEDTLS_INC:-$SW/../../sdk/variant-warp4-tesla-r3/esp32/include/mbedtls/mbedtls/include}"
[ -f "$MB/mbedtls/platform_util.h" ] || { echo "missing mbedtls include $MB"; exit 2; }
W=(-std=gnu++17 -Wall -Wextra -Werror -pthread)
HOOKS=(-DTESLA_WORKER_TEST_HOOKS=1)
modeflags() { case $1 in normal) echo "-O2";; asan) echo "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all";; tsan) echo "-O1 -g -fsanitize=thread";; esac; }
res() { echo "$1 $2" | tee -a "$O/summary.txt"; [ "$1" = PASS ] || rc=1; }
runbin() { # name bin [args]
  local n=$1 b=$2; shift 2; local PRE=(); [[ $b == *-tsan ]] && PRE=(setarch "$(uname -m)" -R)
  if ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 "${PRE[@]}" "$b" "$@" > "$O/$n.log" 2>&1
  then res PASS "$n :: $(tail -1 "$O/$n.log")"; else res FAIL "$n rc=$? :: $(tail -3 "$O/$n.log" | tr '\n' ' ')"; fi; }
single() { # name src extra-flags...
  local n=$1 s=$2; shift 2
  for m in normal asan tsan; do
    if g++ "${W[@]}" $(modeflags $m) "$@" "$s" -o "$O/$n-$m" > "$O/$n-$m.compile.log" 2>&1; then echo "$n-$m" >> "$O/binaries.txt"; else res FAIL "COMPILE $n-$m :: $(grep -m2 error "$O/$n-$m.compile.log" | tr '\n' ' ')"; continue; fi
    if [ "$n" = memory_samples ]; then for x in noinit fail ready race; do runbin "$n-$m-$x" "$O/$n-$m" $x; done; else runbin "$n-$m" "$O/$n-$m"; fi
  done; }
g++ --version | head -1 | tee "$O/toolchain.txt"; echo "kernel $(uname -r) mbedtls_inc=$MB" | tee -a "$O/toolchain.txt"
PS=(-I"$H/stub/psram" -I"$MOD" -I"$LIB")
single allocator_canary   "$H/src/test_tesla_psram.cpp"            "${HOOKS[@]}" "${PS[@]}"
single lifecycle          "$H/src/test_t1r9_lifecycle_psram.cpp"   "${HOOKS[@]}" "${PS[@]}"
single memory_samples     "$H/src/test_t1r10_memory_samples.cpp"   "${HOOKS[@]}" "${PS[@]}"
single dispatcher_core    "$H/src/test_t1r13_dispatch.cpp"         "${HOOKS[@]}" -I"$SRC"
# production hook configuration (hooks OFF): real failure branches + canary RED control + hooks-absent compile-negative
for m in normal asan tsan; do
  if g++ "${W[@]}" $(modeflags $m) "${PS[@]}" "$H/src/test_psram_production_negative.cpp" -o "$O/negative-$m" > "$O/negative-$m.compile.log" 2>&1; then
    runbin "negative-$m" "$O/negative-$m"
    PRE=(); [ $m = tsan ] && PRE=(setarch "$(uname -m)" -R)
    if "${PRE[@]}" "$O/negative-$m" canary_red > "$O/negative-$m-canary_red.log" 2>&1; then res FAIL "negative-$m-canary_red (RED not detected)"
    elif grep -q 'CORRUPT HEAP: tail canary' "$O/negative-$m-canary_red.log" && ! grep -q RED_NOT_DETECTED "$O/negative-$m-canary_red.log"; then res PASS "negative-$m-canary_red :: aborted as expected (CORRUPT HEAP)"
    else res FAIL "negative-$m-canary_red unexpected :: $(tail -2 "$O/negative-$m-canary_red.log" | tr '\n' ' ')"; fi
  else res FAIL "COMPILE negative-$m :: $(grep -m2 error "$O/negative-$m.compile.log" | tr '\n' ' ')"; fi
done
if g++ "${W[@]}" -O0 "${PS[@]}" -fsyntax-only "$H/src/neg_hooks_absent.cpp" > "$O/neg_hooks_absent.compile.log" 2>&1; then res FAIL "neg_hooks_absent (hooks visible without TESLA_WORKER_TEST_HOOKS)"
elif grep -q "inject_failures" "$O/neg_hooks_absent.compile.log"; then res PASS "neg_hooks_absent :: inject_failures absent in production config (expected compile error)"
else res FAIL "neg_hooks_absent wrong error"; fi
# storage executor PSRAM Job bounce (real tesla_ble_storage.cpp + tesla_storage_executor.h, POSIX --wrap'ed)
WRAP=(); for f in open stat fstat read write close unlink rename fsync; do WRAP+=(-Wl,--wrap=$f); done
for m in normal asan tsan; do
  FS=$(mktemp -d "${TMPDIR:-/tmp}/psramfs.XXXXXX"); mkdir -p "$FS/config"
  CX=(g++ -std=c++17 -Wall -Wextra -Werror -pthread -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 "${HOOKS[@]}" -DTESLA_WORKER_PSRAM_STACK=1 -DTESLA_STORAGE_EXECUTOR=1
      -DTESLA_PSRAM_PLACEMENT=1 -DTESLA_PSRAM_HOST_TEST -DTESLA_STORAGE_EXECUTOR_HOST_TEST "-DTESLA_CLEAN_STORAGE_PREFIX=\"$FS\""
      -I"$H/stub/executor" -I"$H/stub/psram" -I"$SRC" -I"$MOD" -I"$LIB" -I"$MB")
  ok=1; for u in "$MOD/tesla_ble_storage.cpp:storage" "$H/src/test_t1r11_executor_psram.cpp:test" "$H/src/link_shims.cpp:shims"; do
    "${CX[@]}" $(modeflags $m) -c "${u%%:*}" -o "$O/executor-$m-${u##*:}.o" >> "$O/executor-$m.compile.log" 2>&1 || ok=0; done
  [ $ok = 1 ] && g++ $(modeflags $m) -pthread "$O/executor-$m-test.o" "$O/executor-$m-storage.o" "$O/executor-$m-shims.o" "${WRAP[@]}" -o "$O/executor-$m" >> "$O/executor-$m.compile.log" 2>&1 || ok=0
  if [ $ok = 0 ]; then res FAIL "COMPILE executor-$m :: $(grep -m2 error "$O/executor-$m.compile.log" | tr '\n' ' ')"; rm -rf "$FS"; continue; fi
  echo "executor-$m" >> "$O/binaries.txt"
  for i in 1 2; do rm -rf "$FS/config"/*; runbin "executor-$m-run$i" "$O/executor-$m"; done; rm -rf "$FS"
done
( cd "$H" && sha256sum run_psram_production_tests.sh src/* stub/*/* ) > "$O/sources.sha256"
( cd "$MOD" && sha256sum tesla_psram.h bluetooth_memory.h tesla_storage_executor.h tesla_ble_call_dispatch.h tesla_ble_storage.cpp tesla_ble_storage.h tesla_ble_storage_io.h tesla_worker_psram.h ) > "$O/production_headers.sha256"
( cd "$O" && sha256sum $(cat binaries.txt 2>/dev/null) negative-normal negative-asan negative-tsan 2>/dev/null ) > "$O/binaries.sha256"
echo "PASS=$(grep -c '^PASS' "$O/summary.txt") FAIL=$(grep -c '^FAIL' "$O/summary.txt") rc=$rc" | tee -a "$O/summary.txt"; echo $rc > "$O/rc"; exit $rc
