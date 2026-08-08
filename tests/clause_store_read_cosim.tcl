set repo_root [pwd]
cd /tmp
open_project -reset inductor-clause-store-read-cosim
set_top clauseStoreReadCosim
add_files "$repo_root/src/clause_store_handler.cpp" -cflags "-I$repo_root/src"
add_files "$repo_root/tests/clause_store_read_cosim_top.cpp" -cflags "-I$repo_root/src"
add_files -tb "$repo_root/tests/clause_store_read_test.cpp" -cflags "-I$repo_root/src -DCLAUSE_STORE_READ_TOP=clauseStoreReadCosim"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 4.0
config_rtl -deadlock_detection sim
set_directive_interface -mode axis clauseStoreReadCosim input1
set_directive_interface -mode axis clauseStoreReadCosim input2
set_directive_interface -mode axis clauseStoreReadCosim output1
set_directive_interface -mode axis clauseStoreReadCosim output2
set_directive_interface -mode ap_memory -depth 32 clauseStoreReadCosim clauseStore
set_directive_interface -mode ap_memory -depth 9 clauseStoreReadCosim commands
set_directive_interface -mode ap_memory -depth 9 clauseStoreReadCosim compactClauseLayout

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -enable_dataflow_profiling -ldflags "-no-pie"
exit
