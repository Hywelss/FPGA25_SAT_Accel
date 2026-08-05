#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/sat_accel_incremental_session_test"
mkdir -p "$build_dir"

g++ -std=c++17 -Wall -Wextra -Werror \
    -I"$repo_root/src" \
    "$repo_root/tests/incremental_session_test.cpp" \
    -o "$build_dir/incremental_session_test"
"$build_dir/incremental_session_test"
