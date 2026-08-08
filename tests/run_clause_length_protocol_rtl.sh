#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
vitis_hls_root=${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/2022.2}
project_dir=${CLAUSE_LENGTH_PROJECT:-/tmp/inductor-clause-length-protocol}
rtl_dir=$project_dir/solution/syn/verilog
sim_dir=${CLAUSE_LENGTH_SIM_DIR:-/tmp/inductor-clause-length-protocol-rtl}
vivado_bin=${VIVADO_BIN:-/tools/Xilinx/Vivado/2022.2/bin}
system_libstdcpp=$(g++ -print-file-name=libstdc++.so.6)

export CLAUSE_LENGTH_SOURCE_ROOT=$repo_root
export CLAUSE_LENGTH_PROJECT=$project_dir
export AP_GCC_PATH=${AP_GCC_PATH:-/usr/bin}
export LD_PRELOAD=$system_libstdcpp

if [[ ${CLAUSE_LENGTH_SKIP_SYNTH:-0} != 1 ||
      ! -f "$rtl_dir/clauseLengthProtocolCosim.v" ]]; then
    "$vitis_hls_root/bin/vitis_hls" -f \
        "$repo_root/tests/clause_length_protocol_cosim.tcl"
fi

mkdir -p "$sim_dir"
cd "$sim_dir"
"$vivado_bin/xvlog" "$rtl_dir"/*.v
"$vivado_bin/xvlog" --sv "$repo_root/tests/clause_length_protocol_rtl_tb.sv"
"$vivado_bin/xelab" clause_length_protocol_rtl_tb \
    -s clause_length_protocol_rtl_sim
simulation_output=$("$vivado_bin/xsim" clause_length_protocol_rtl_sim -runall)
printf '%s\n' "$simulation_output"
grep -q 'CLAUSE_LENGTH_PROTOCOL_RTL_PASS' <<<"$simulation_output"
