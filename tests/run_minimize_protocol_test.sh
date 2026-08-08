#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
test_bin=/tmp/inductor-minimize-protocol-test

g++ -std=c++17 -O2 -w -ffunction-sections -fdata-sections \
    -I"$vitis_hls_root/include" -I"$repo_root/src" \
    "$repo_root/tests/minimize_protocol_test.cpp" \
    "$repo_root/src/minimize.cpp" "$repo_root/src/learn.cpp" \
    -Wl,--gc-sections -o "$test_bin"

timeout 30 "$test_bin"
