#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
vitis_hls_root="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}"
build_dir="${TMPDIR:-/tmp}/sat_accel_learned_clause_capacity_test"
mkdir -p "$build_dir"

g++ -std=c++17 -O1 -w -ffunction-sections -fdata-sections \
    -DFPGA_VCK5000 \
    -I"$vitis_hls_root/include" \
    -I"$repo_root/src" \
    "$repo_root/tests/learned_clause_capacity_test.cpp" \
    "$repo_root/src/learn.cpp" \
    -Wl,--gc-sections \
    -o "$build_dir/learned_clause_capacity_test"

"$build_dir/learned_clause_capacity_test"
