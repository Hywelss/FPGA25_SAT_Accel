#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
XRT_ROOT="${XRT_ROOT:-/opt/xilinx/xrt}"
VITIS_ROOT="${VITIS_ROOT:-/tools/Xilinx/Vitis/2022.2}"
BINARY="$ROOT/src/bin/test.sw_emu.out"
XCLBIN="$ROOT/src/bin/workload-sw_emu.xclbin"
CONFIG="$ROOT/src/configuration.json"
METRICS="${TMPDIR:-/tmp}/sat_accel_unsat_core_sw_emu_metrics.csv"

export XILINX_XRT="$XRT_ROOT"
export XILINX_VITIS="$VITIS_ROOT"
export PATH="$VITIS_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$XRT_ROOT/lib:$VITIS_ROOT/lib/lnx64.o${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

run_query() {
    local name="$1"
    local expected_core="$2"
    local log="${TMPDIR:-/tmp}/sat_accel_unsat_core_${name}.log"
    XCL_EMULATION_MODE=sw_emu "$BINARY" \
        "$XCLBIN" "$CONFIG" \
        "$ROOT/tests/unsat_core_queries/$name.cnf" "$METRICS" 0 \
        >"$log"
    rg -q '^s UNSATISFIABLE$' "$log"
    rg -q "^u${expected_core:+ $expected_core} 0$" "$log"
}

run_query 01_irrelevant_assumption '(1 3|3 1)'
run_query 02_direct_assumption_conflict '(-3 1|1 -3)'
run_query 03_root_conflict ''
run_query 04_opposite_assumptions '(-1 1|1 -1)'
