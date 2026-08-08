#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
rtl_dir=/tmp/inductor-clause-store-read-cosim/solution/syn/verilog
sim_dir=/tmp/inductor-clause-store-rtl-closed-loop
vivado_bin=${VIVADO_BIN:-/tools/Xilinx/Vivado/2022.2/bin}

if [[ ! -f "$rtl_dir/clauseStoreReadCosim.v" ]]; then
    echo "Missing synthesized clause-store RTL; run tests/run_clause_store_read_cosim.sh first" >&2
    exit 2
fi

mkdir -p "$sim_dir"
cd "$sim_dir"
"$vivado_bin/xvlog" "$rtl_dir"/*.v
"$vivado_bin/xvlog" --sv "$repo_root/tests/clause_store_rtl_closed_loop_tb.sv"
"$vivado_bin/xelab" clause_store_rtl_closed_loop_tb -s clause_store_rtl_closed_loop_sim
simulation_output=$("$vivado_bin/xsim" clause_store_rtl_closed_loop_sim -runall)
printf '%s\n' "$simulation_output"
grep -q 'CLOSED_LOOP_RTL_PASS' <<<"$simulation_output"
