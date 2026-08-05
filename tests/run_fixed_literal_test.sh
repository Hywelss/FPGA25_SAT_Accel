#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/sat_accel_fixed_literal_test"
mkdir -p "$build_dir"

g++ -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas \
    -I"$repo_root/src" \
    "$repo_root/tests/fixed_literal_test.cpp" \
    -o "$build_dir/fixed_literal_test"

"$build_dir/fixed_literal_test"
