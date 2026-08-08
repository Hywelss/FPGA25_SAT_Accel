#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
source_root=${1:-$repo_root}
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${PQ_COSIM_PROJECT:-/tmp/inductor-pq-handler-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

if [[ ! -f "$source_root/src/pq_handler.cpp" ||
      ! -f "$source_root/tests/pq_handler_assignment_test.cpp" ]]; then
    echo "Invalid source root: $source_root" >&2
    exit 2
fi

export PQ_SOURCE_ROOT=$source_root
export PQ_COSIM_PROJECT=$project_dir
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f "$repo_root/tests/pq_handler_cosim.tcl"
