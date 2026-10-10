#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../../.." && pwd)"
build_dir="${TMPDIR:-$repo_root/.tmp}/warp-tesla-host-tests"
mkdir -p "$build_dir"

for source in "$repo_root"/software/tests/tesla_vehicle/test_*.cpp; do
    name="$(basename "${source%.cpp}")"
    binary="$build_dir/$name"
    g++ -std=c++17 -Wall -Wextra -Werror "$source" -o "$binary"
    "$binary"
done

echo "Tesla BLE host tests passed."
