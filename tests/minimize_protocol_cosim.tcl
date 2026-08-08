set source_root $::env(MINIMIZE_SOURCE_ROOT)
set project_dir $::env(MINIMIZE_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top minimize_dataflow_wrapper_layer_1
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/minimize.cpp"
add_files -cflags $include_flags "$source_root/src/learn.cpp"
add_files -cflags $include_flags "$source_root/src/backtrack.cpp"
add_files -cflags $include_flags "$source_root/src/color.cpp"
add_files -cflags $include_flags "$source_root/src/copy_in.cpp"
add_files -cflags $include_flags "$source_root/src/decide.cpp"
add_files -cflags $include_flags "$source_root/src/discover.cpp"
add_files -cflags $include_flags "$source_root/src/manage.cpp"
add_files -cflags $include_flags "$source_root/src/unsat_core.cpp"
add_files -cflags $include_flags "$source_root/src/solver.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/minimize_protocol_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode axis minimize_dataflow_wrapper_layer_1 toSplitStream
set_directive_interface -mode axis minimize_dataflow_wrapper_layer_1 clauseStoreInputStream
set_directive_interface -mode axis minimize_dataflow_wrapper_layer_1 clauseStoreOutputStream
set_directive_interface -mode ap_memory -depth 32768 minimize_dataflow_wrapper_layer_1 lmmd
set_directive_interface -mode ap_memory -depth 64 minimize_dataflow_wrapper_layer_1 validBitMinimize
set_directive_interface -mode ap_memory -depth 32768 minimize_dataflow_wrapper_layer_1 mergeScratchPadMinimize
set_directive_interface -mode ap_memory -depth 32768 minimize_dataflow_wrapper_layer_1 unitByCls

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -enable_dataflow_profiling -ldflags "-no-pie"
exit
