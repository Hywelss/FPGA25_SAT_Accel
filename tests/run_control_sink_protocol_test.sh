#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_bin=${TMPDIR:-/tmp}/inductor-control-sink-protocol-test

g++ -std=c++17 -DFPGA_HW -I/tools/Xilinx/Vitis_HLS/2022.2/include \
    -I"$repo_root/src" \
    "$repo_root/tests/control_sink_protocol_test.cpp" \
    "$repo_root/src/discover.cpp" "$repo_root/src/decide.cpp" \
    "$repo_root/src/color.cpp" -o "$test_bin"

"$test_bin"
