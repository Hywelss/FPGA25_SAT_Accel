#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${PROPAGATION_COSIM_PROJECT:-/tmp/inductor-propagation-closed-loop-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export PROPAGATION_COSIM_SOURCE_ROOT=$repo_root
export PROPAGATION_COSIM_PROJECT=$project_dir
export PROPAGATION_RTL_DIR=${PROPAGATION_RTL_DIR:-$project_dir/solution/syn/verilog}
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/propagation_closed_loop_cosim.tcl"

bash "$repo_root/tests/run_propagation_closed_loop_rtl_closed_loop.sh"
