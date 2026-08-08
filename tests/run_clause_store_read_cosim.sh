#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
system_libstdcpp=${SYSTEM_LIBSTDCPP:-/usr/lib/x86_64-linux-gnu/libstdc++.so.6}
export LD_PRELOAD="$system_libstdcpp${LD_PRELOAD:+:$LD_PRELOAD}"

cd "$repo_root"
"$vitis_hls_root/bin/vitis_hls" -f tests/clause_store_read_cosim.tcl
