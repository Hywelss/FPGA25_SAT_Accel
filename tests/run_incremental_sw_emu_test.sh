#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
xrt_root="${XRT_ROOT:-/opt/xilinx/xrt}"
vitis_root="${VITIS_ROOT:-/tools/Xilinx/Vitis/2022.2}"
binary="$repo_root/src/bin/test.sw_emu.out"
xclbin="$repo_root/src/bin/workload-sw_emu.xclbin"
config="$repo_root/src/configuration.json"
manifest="$repo_root/tests/incremental_queries/manifest.tsv"
metrics="${TMPDIR:-/tmp}/sat_accel_incremental_sw_emu_metrics.csv"
log="${TMPDIR:-/tmp}/sat_accel_incremental_sw_emu.log"

export XILINX_XRT="$xrt_root"
export XILINX_VITIS="$vitis_root"
export PATH="$vitis_root/bin:$PATH"
export LD_LIBRARY_PATH="$xrt_root/lib:$vitis_root/lib/lnx64.o${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ ! -x "$binary" || ! -s "$xclbin" ]]; then
    echo "Build software emulation artifacts with ./runCompile.sh sw_emu first." >&2
    exit 2
fi

XCL_EMULATION_MODE=sw_emu "$binary" --batch \
    "$xclbin" "$config" "$manifest" "$metrics" | tee "$log"

rg -q "INCREMENTAL QUERY: RESET previous-permanent=0 new-permanent=2" "$log"
rg -q "INCREMENTAL QUERY: REUSE previous-permanent=2 new-permanent=0" "$log"
rg -q "INCREMENTAL QUERY: RESET previous-permanent=0 new-permanent=1.*reason=variable layout changed" "$log"
rg -q "INCREMENTAL QUERY: REUSE previous-permanent=1 new-permanent=0 temporary=1 assumptions=2" "$log"
rg -q '^u (1 3|3 1) 0$' "$log"
rg -q "INCREMENTAL QUERY: REUSE previous-permanent=2 new-permanent=1" "$log"
rg -q "INCREMENTAL QUERY: REUSE previous-permanent=3 new-permanent=0" "$log"
rg -q "INCREMENTAL QUERY: RESET previous-permanent=0 new-permanent=2.*reason=variable layout changed" "$log"
rg -q "INCREMENTAL QUERY: REUSE previous-permanent=2 new-permanent=0" "$log"
