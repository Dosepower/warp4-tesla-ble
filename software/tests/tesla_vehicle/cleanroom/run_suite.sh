#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 "$here/make_vectors.py"
python3 "$here/wire_vectors.py"
python3 "$here/test_charge_start_guard.py"
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
for mode in retire pressure generation overflow lifecycle teardown no-wake stale-soc simultaneous forget boundary-matrix text allocations composition chronology acceptance identity-busy storage-quarantine ui-contract ui-accept-key ui-accept-error ui-delete-queued ui-delete-deferred ui-disconnect-queued ui-disconnect-sent ui-slac-queued ui-slac-sent ui-silent-identity ui-silent-link ui-delete-storage ui-delete-write ui-status-deadline ui-disconnect-unconsumed ui-slac-unconsumed ui-retirement-handoff r2-approval-disconnect r2-generated-failure r2-generated-guard r2-contended-start-approval r2-contended-error-approval r2-contended-start-failure r2-contended-error-failure r2-deadline-successor r3-failure-disconnect r3-status-disconnect r3-close-disconnect r3-close-delayed-disconnect r3-reverse-soc r3-reverse-soc-contention r3-terminal-first-soc r3-sequence; do
    printf 'WARP MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
printf 'WARP MODE r3-disconnect-close\n'
"${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" r3-disconnect-close
for mode in posix-guard posix-promotion posix-promotion-remove-fail posix-guard-remove-fail posix-promotion-close-fail posix-session-remove-fail; do
    printf 'POSIX MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module_posix$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
for mode in r4-shutdown-stale r4-shutdown-fresh r4-session-contention r4-session-stale r4-session-bounded r4-queue-full r4-exhaust-0 r4-exhaust-1 r4-exhaust-2 r4-exhaust-3 r4-exhaust-4 r4-terminal-contention-shutdown r4-counter-boundaries r4-ev-exhaustion r4-operation-last r4-serial-last; do
    printf 'WARP MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
for mode in r5-delete-close-disconnect r5-delete-disconnect-close r5-delete-held-close r5-delete-held-disconnect r5-stop-outcome r5-stop-session-first r5-stop-main-first r5-stop-approval-locks r5-final-absent r5-final-consumed r5-final-cleanup r5-final-unknown r5-queue-recovery; do
    printf 'WARP MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
    printf 'TARGET SYNC MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_target_sync$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
for mode in r5-delete-close-disconnect r5-delete-disconnect-close r5-final-absent r5-final-consumed r5-final-cleanup r5-final-unknown r5-queue-recovery; do
    printf 'POSIX MODE %s\n' "$mode"
    "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/test_warp_module_posix$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
done
for mode in 0 1 2 3 4 5 6 7 8 9 10 11; do
    for entry in test_warp_r6 test_warp_r6_sync test_warp_r6_posix; do
        printf 'R6 MODE %s %s\n' "$entry" "$mode"
        "${TESLA_CLEAN_TEST_OUT:-${TMPDIR:?}/tesla-clean-native}/$entry$([[ "${1:-}" != sanitize ]] || printf '%s' '-sanitized')" "$mode"
    done
done
