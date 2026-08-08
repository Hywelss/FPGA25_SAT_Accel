#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${INVALID_CONFIG_COSIM_PROJECT:-/tmp/inductor-clause-store-invalid-config-cosim}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export INVALID_CONFIG_COSIM_SOURCE_ROOT="$repo_root"
export INVALID_CONFIG_COSIM_PROJECT="$project_dir"
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/clause_store_invalid_config_cosim.tcl"
