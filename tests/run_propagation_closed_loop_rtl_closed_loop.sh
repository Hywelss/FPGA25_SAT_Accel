#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
rtl_dir=${PROPAGATION_RTL_DIR:-/tmp/inductor-propagation-closed-loop-cosim/solution/syn/verilog}
sim_dir=${PROPAGATION_RTL_SIM_DIR:-/tmp/inductor-propagation-closed-loop-rtl-sim}
vivado_bin=${VIVADO_BIN:-/tools/Xilinx/Vivado/2022.2/bin}

if [[ ! -f "$rtl_dir/propagationClosedLoopCosim.v" ]]; then
    echo "Missing synthesized propagation RTL" >&2
    exit 2
fi

mkdir -p "$sim_dir"
cd "$sim_dir"
cp "$rtl_dir"/*.dat "$sim_dir"/
"$vivado_bin/xvlog" "$rtl_dir"/*.v
"$vivado_bin/xvlog" --sv "$repo_root/tests/propagation_closed_loop_rtl_tb.sv"
"$vivado_bin/xelab" propagation_closed_loop_rtl_tb \
    -L unisims_ver -L xpm -s propagation_closed_loop_rtl_sim
simulation_output=$("$vivado_bin/xsim" propagation_closed_loop_rtl_sim -runall)
printf '%s\n' "$simulation_output"
grep -q 'PROPAGATION_CLOSED_LOOP_RTL_PASS' <<<"$simulation_output"
