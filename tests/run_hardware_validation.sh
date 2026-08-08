#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
xrt_root="${XRT_ROOT:-/opt/xilinx/xrt}"
vitis_root="${VITIS_ROOT:-/tools/Xilinx/Vitis/2022.2}"
binary="$repo_root/src/bin/test.real.out"
xclbin="$repo_root/src/bin/workload-hw.xclbin"
config="${SAT_ACCEL_CONFIG:-$repo_root/src/configuration.json}"
timeout_seconds="${SAT_ACCEL_TEST_TIMEOUT:-1200}"
log_dir="${SAT_ACCEL_TEST_LOG_DIR:-${TMPDIR:-/tmp}/sat_accel_hardware_validation}"

export XILINX_XRT="$xrt_root"
export XILINX_VITIS="$vitis_root"
export PATH="$vitis_root/bin:$PATH"
export LD_LIBRARY_PATH="$xrt_root/lib:$vitis_root/lib/lnx64.o${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ ! -x "$binary" || ! -s "$xclbin" || ! -s "$config" ]]; then
    echo "Missing test.real.out, workload-hw.xclbin, or configuration file." >&2
    exit 2
fi

mkdir -p "$log_dir"

run_batch() {
    local name="$1"
    local manifest="$2"
    local log="$log_dir/$name.log"
    local metrics="$log_dir/$name.csv"

    echo "HARDWARE TEST: $name"
    timeout --signal=TERM --kill-after=10s "$timeout_seconds" \
        "$binary" --batch "$xclbin" "$config" "$manifest" "$metrics" \
        2>&1 | tee "$log"
    rg -q "BATCH COMPLETE:" "$log"
}

run_batch incremental_full \
    "$repo_root/tests/hardware_queries/manifest_incremental_full.tsv"
rg -q "BATCH COMPLETE: 15 queries" "$log_dir/incremental_full.log"
rg -q "INCREMENTAL QUERY: REUSE.*temporary=0" "$log_dir/incremental_full.log"
rg -q "INCREMENTAL QUERY: .*temporary=4" "$log_dir/incremental_full.log"

run_batch unsat_core \
    "$repo_root/tests/hardware_queries/manifest_unsat_core.tsv"
rg -q -e '^u (1 3|3 1) 0$' "$log_dir/unsat_core.log"
rg -q -e '^u (-3 1|1 -3) 0$' "$log_dir/unsat_core.log"
rg -q '^u 0$' "$log_dir/unsat_core.log"
rg -q -e '^u (-1 1|1 -1) 0$' "$log_dir/unsat_core.log"

run_batch standard_and_stress \
    "$repo_root/tests/hardware_queries/manifest_standard_and_stress.tsv"
rg -q "BATCH COMPLETE: 9 queries" "$log_dir/standard_and_stress.log"

echo "HARDWARE VALIDATION PASS"
