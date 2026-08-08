#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${DELETE_SELECTOR_COSIM_PROJECT:-/tmp/inductor-delete-selector-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export DELETE_SELECTOR_COSIM_SOURCE_ROOT="$repo_root"
export DELETE_SELECTOR_COSIM_PROJECT="$project_dir"
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/clause_store_delete_selector_cosim.tcl"
