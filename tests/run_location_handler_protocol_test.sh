#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
test_bin=/tmp/inductor-location-handler-protocol-test

g++ -std=c++17 -O1 -w -DFPGA_VCK5000 \
    -I"$repo_root/src" -I"$vitis_hls_root/include" \
    "$repo_root/tests/location_handler_protocol_test.cpp" \
    "$repo_root/src/location_handler.cpp" \
    -o "$test_bin"

"$test_bin"
