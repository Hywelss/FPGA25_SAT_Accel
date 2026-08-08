#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
rtl_dir=${PQ_RTL_DIR:-/tmp/inductor-pq-handler-cosim/solution/syn/verilog}
if [[ -n "${PQ_RTL_SIM_DIR:-}" ]]; then
    sim_dir=$PQ_RTL_SIM_DIR
else
    sim_dir=$(mktemp -d "${TMPDIR:-/tmp}/inductor-pq-handler-rtl-closed-loop.XXXXXX")
fi
vivado_bin=${VIVADO_BIN:-/tools/Xilinx/Vivado/2022.2/bin}

if [[ ! -f "$rtl_dir/pqHandler.v" ]]; then
    echo "Missing synthesized priority-queue RTL; run tests/run_pq_handler_cosim.sh first" >&2
    exit 2
fi

mkdir -p "$sim_dir"
cd "$sim_dir"
cp "$rtl_dir"/*.dat .
"$vivado_bin/xvlog" "$rtl_dir"/*.v
"$vivado_bin/xvlog" "$rtl_dir"/../../sim/verilog/ip/xil_defaultlib/*.v
"$vivado_bin/xvlog" --sv "$repo_root/tests/pq_handler_rtl_closed_loop_tb.sv"
"$vivado_bin/xelab" pq_handler_rtl_closed_loop_tb \
    -L unisims_ver -L floating_point_v7_0_20 -L floating_point_v7_1_15 \
    -s pq_handler_rtl_closed_loop_sim
simulation_output=$("$vivado_bin/xsim" pq_handler_rtl_closed_loop_sim -runall)
printf '%s\n' "$simulation_output"
grep -q 'PQ_CLOSED_LOOP_RTL_PASS' <<<"$simulation_output"
