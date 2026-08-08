#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
vitis_hls_root="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}"
project_dir="${BACKTRACK_CLOSED_LOOP_PROJECT:-/tmp/inductor-backtrack-closed-loop-cosim}"

export BACKTRACK_CLOSED_LOOP_SOURCE_ROOT="$repo_root"
export BACKTRACK_CLOSED_LOOP_PROJECT="$project_dir"
export AP_GCC_PATH="${AP_GCC_PATH:-/usr/bin}"
export LD_PRELOAD="$(g++ -print-file-name=libstdc++.so.6)"

"$vitis_hls_root/bin/vitis_hls" -f \
    "$repo_root/tests/backtrack_closed_loop_cosim.tcl"
