#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/sat_accel_host_validation_test"
mkdir -p "$build_dir"

g++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/src" \
    "$repo_root/tests/host_validation_test.cpp" \
    -o "$build_dir/host_validation_test"

"$build_dir/host_validation_test"
