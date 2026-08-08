#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
clause_rtl_dir=/tmp/inductor-deletion-clause-rtl/solution/syn/verilog
manage_rtl_dir=/tmp/inductor-deletion-manage-rtl/solution/syn/verilog
sim_dir=${DELETION_RTL_SIM_DIR:-/tmp/inductor-deletion-closed-loop-rtl-sim}
vivado_bin=${VIVADO_BIN:-/tools/Xilinx/Vivado/2022.2/bin}
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

cd "$repo_root"
if [[ ${DELETION_RTL_REUSE_SYNTHESIS:-0} != 1 ]]; then
    "$vitis_hls_root/bin/vitis_hls" -f tests/deletion_closed_loop_rtl.tcl
fi

mkdir -p "$sim_dir"
cd "$sim_dir"
cp "$clause_rtl_dir"/*.dat "$sim_dir"/ 2>/dev/null || true
cp "$manage_rtl_dir"/*.dat "$sim_dir"/ 2>/dev/null || true
"$vivado_bin/xvlog" "$clause_rtl_dir"/*.v "$manage_rtl_dir"/*.v
"$vivado_bin/xvlog" --sv "$repo_root/tests/deletion_closed_loop_rtl_tb.sv"
"$vivado_bin/xelab" deletion_closed_loop_rtl_tb \
    -L unisims_ver -L xpm -mt 8 -s deletion_closed_loop_rtl_sim
simulation_output=$("$vivado_bin/xsim" deletion_closed_loop_rtl_sim -runall)
printf '%s\n' "$simulation_output"
grep -q 'DELETION_CLOSED_LOOP_RTL_PASS' <<<"$simulation_output"
