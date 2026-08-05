#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
test_binary=/tmp/inductor-temporary-clause-tracker-test

g++ -std=c++17 -O2 -w \
    -I"$vitis_hls_root/include" \
    -I"$repo_root/src" \
    "$repo_root/tests/temporary_clause_tracker_test.cpp" \
    -o "$test_binary"

"$test_binary"
