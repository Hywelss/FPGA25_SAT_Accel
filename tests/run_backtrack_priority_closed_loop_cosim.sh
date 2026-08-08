#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
project_dir="${BACKTRACK_PRIORITY_PROJECT:-/tmp/inductor-backtrack-priority-cosim}"
vitis_hls_root="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}"

export BACKTRACK_SOURCE_ROOT="$repo_root"
export BACKTRACK_PRIORITY_PROJECT="$project_dir"
export AP_GCC_PATH="${AP_GCC_PATH:-/usr/bin}"
export LD_PRELOAD="$(g++ -print-file-name=libstdc++.so.6)"

cd "$repo_root"
"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/backtrack_priority_closed_loop_cosim.tcl"
