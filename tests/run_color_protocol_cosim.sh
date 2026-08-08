#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${COLOR_COSIM_PROJECT:-/tmp/inductor-color-protocol-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export COLOR_SOURCE_ROOT="$repo_root"
export COLOR_COSIM_PROJECT="$project_dir"
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/color_protocol_cosim.tcl"
