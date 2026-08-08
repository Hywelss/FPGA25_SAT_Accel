#!/bin/bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source /tools/Xilinx/Vitis/2022.2/settings64.sh

g++ -std=c++17 -O2 -w \
    -I"$repo_root/src" -I/tools/Xilinx/Vitis_HLS/2022.2/include \
    "$repo_root/tests/propagation_lane_routing_test.cpp" \
    "$repo_root/src/discover.cpp" "$repo_root/src/decide.cpp" \
    "$repo_root/src/color.cpp" -o /tmp/propagation_lane_routing_test

/tmp/propagation_lane_routing_test
