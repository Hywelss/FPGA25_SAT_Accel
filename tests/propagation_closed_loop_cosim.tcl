set source_root $::env(PROPAGATION_COSIM_SOURCE_ROOT)
set project_dir $::env(PROPAGATION_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top propagationClosedLoopCosim
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/discover.cpp"
add_files -cflags $include_flags "$source_root/src/decide.cpp"
add_files -cflags $include_flags "$source_root/src/color.cpp"
add_files -cflags $include_flags "$source_root/tests/propagation_closed_loop_cosim_top.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/propagation_closed_loop_cosim_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode axis propagationClosedLoopCosim clauseLengths
set_directive_interface -mode axis propagationClosedLoopCosim clauseRequests

csynth_design
exit
