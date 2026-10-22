#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../../.." && pwd)"
build_dir="${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/module-helpers"
[[ -z "${1:-}" || "${1:-}" == sanitize ]] || { printf 'Unsafe test mode\n' >&2; exit 2; }
# Frontend generation may remove the repository's .pio/libdeps link. Select and
# validate a separately owned, read-only ArduinoJson tree before any test output
# mkdir/redirection; no symlink or copy is materialized in the worktree.
TESLA_HOST_ARDUINOJSON="$(python3 -B "$repo_root/software/tests/tesla_vehicle/r13_host_dependency.py")" || exit 2
[[ -n "$TESLA_HOST_ARDUINOJSON" ]] || { printf 'Empty host dependency\n' >&2; exit 2; }
export TESLA_HOST_ARDUINOJSON
mbed_build="${TESLA_CLEAN_MBEDTLS_BUILD:-${build_dir%/module-helpers}/mbedtls}"
TESLA_CLEAN_TEST_OUT="${build_dir%/module-helpers}" python3 -B "$repo_root/software/tests/tesla_vehicle/materialize_cmake_sources.py" "$mbed_build"
python3 "$repo_root/software/tools/bench_confinement.py" outputs "${build_dir%/module-helpers}" "$mbed_build"
mkdir -p "$build_dir"

# Explicit preserved module-only entries; excluded private-coupled sources were
# removed only after coverage and pre-removal replacement builds passed.
# The locked Phase-2 evidence distinguishes replacement execution from old tests.
helpers=(callback_gate config_policy disabled_boot generation key_command
         key_validation operation pairing_policy publication queues result
         runtime session shutdown_result soc_policy teardown validation watch_sampling)
flags=(-std=c++17 -pthread -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
suffix=""
if [[ "${1:-}" == sanitize ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -no-pie)
    suffix="-sanitized"
fi
for helper in "${helpers[@]}"; do
    name="test_tesla_ble_$helper"
    printf 'HOST ENTRY %s\n' "$name"
    if ! g++ -H "${flags[@]}" -I"$repo_root/software/lib/TeslaBLEClean/include" \
        "$repo_root/software/tests/tesla_vehicle/$name.cpp" -o "$build_dir/$name$suffix" 2> "$build_dir/$name$suffix.includes"; then
        python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).read_text())' "$build_dir/$name$suffix.includes"
        exit 1
    fi
    "$build_dir/$name$suffix"
done

node "$repo_root/software/tests/tesla_vehicle/test_web_actions.mjs"
node "$repo_root/software/tests/tesla_vehicle/test_web_integration.mjs"
bash "$repo_root/software/tests/tesla_vehicle/cleanroom/run_suite.sh" "${1:-}"
printf 'Replacement execution suite passed: %s preserved module helpers plus clean suite. Coverage acceptance is separate.\n' "${#helpers[@]}"
