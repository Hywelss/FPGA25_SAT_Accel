#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
test_bin=/tmp/inductor-packed-clause-selector-test

g++ -std=c++17 -O2 -w \
    -I"$vitis_hls_root/include" -I"$repo_root/src" \
    "$repo_root/tests/packed_clause_selector_test.cpp" -o "$test_bin"

"$test_bin"
