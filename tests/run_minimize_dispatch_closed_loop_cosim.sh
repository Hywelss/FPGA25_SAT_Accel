#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${MINIMIZE_DISPATCH_COSIM_PROJECT:-/tmp/inductor-minimize-dispatch-closed-loop-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export MINIMIZE_DISPATCH_SOURCE_ROOT="$repo_root"
export MINIMIZE_DISPATCH_COSIM_PROJECT="$project_dir"
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/minimize_dispatch_closed_loop_cosim.tcl"
