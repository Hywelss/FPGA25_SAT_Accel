#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VITIS_HLS_ROOT="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}"

g++ -std=c++17 -O2 \
    -I"$ROOT/src" \
    -I"$VITIS_HLS_ROOT/include" \
    "$ROOT/tests/unsat_core_test.cpp" \
    "$ROOT/src/unsat_core.cpp" \
    -o /tmp/sat_accel_unsat_core_test

/tmp/sat_accel_unsat_core_test
