#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 "$here/make_vectors.py"
python3 "$here/wire_vectors.py"
python3 "$here/test_safety.py"
python3 "$here/test_production_binding.py"
count=0
for source in "$here"/test_*.cpp; do
    name="$(basename "${source%.cpp}")"
    printf 'CLEAN ENTRY %s\n' "$name"
    bash "$here/run_test.sh" "$name" "${1:-}"
    count=$((count+1))
done
printf 'Clean-core suite passed: %s distinct native entries (%s)\n' "$count" "${1:-normal}"
for mode in retire pressure generation overflow lifecycle teardown no-wake stale-soc simultaneous forget boundary-matrix text allocations composition chronology acceptance identity-busy storage-quarantine; do
    printf 'WARP MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
