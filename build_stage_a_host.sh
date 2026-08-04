#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
xrt_root="${XRT_ROOT:-/opt/xilinx/xrt}"
vitis_hls_root="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}"
output_dir="${STAGE_A_BUILD_DIR:-$repo_root/build/stage-a}"
output="$output_dir/inductor-sat-host"

if [[ ! -d "$xrt_root/include" || ! -d "$xrt_root/lib" ]]; then
    echo "XRT headers or libraries not found under $xrt_root" >&2
    exit 1
fi
if [[ ! -d "$vitis_hls_root/include" ]]; then
    echo "Vitis HLS headers not found under $vitis_hls_root" >&2
    exit 1
fi

mkdir -p "$output_dir"
g++ -std=c++17 -O3 -Wall -Wno-unknown-pragmas \
    -DFPGA_DEVICE -DC_KERNEL -DFPGA_VCK5000 \
    -I"$repo_root/src" \
    -I"$xrt_root/include" \
    -I"$vitis_hls_root/include" \
    "$repo_root/src/host.cpp" \
    "$repo_root/src/xcl2.cpp" \
    -L"$xrt_root/lib" \
    -Wl,-rpath,"$xrt_root/lib" \
    -lxilinxopencl -lxrt_core -lpthread -lrt -lstdc++ -luuid \
    -o "$output"

echo "$output"
