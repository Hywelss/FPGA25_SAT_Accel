#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
test_binary=/tmp/inductor-domain-queue-test

g++ -std=c++17 -O2 \
    -I"$vitis_hls_root/include" \
    -I"$repo_root/src" \
    "$repo_root/tests/domain_queue_test.cpp" \
    "$repo_root/src/priority_queue_functions.cpp" \
    -o "$test_binary"

"$test_binary"
