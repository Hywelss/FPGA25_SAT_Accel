set source_root $::env(MINIMIZE_DISPATCH_SOURCE_ROOT)
set project_dir $::env(MINIMIZE_DISPATCH_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top minimizeDispatchClosedLoopCosim
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/tests/minimize_dispatch_closed_loop_cosim_top.cpp"
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
add_files -tb -cflags $include_flags "$source_root/tests/minimize_dispatch_closed_loop_cosim_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
csim_design
csynth_design
cosim_design -rtl verilog -random_stall -enable_dataflow_profiling -ldflags "-no-pie"
exit
