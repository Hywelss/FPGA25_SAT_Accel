set source_root $::env(BACKTRACK_SOURCE_ROOT)
set project_dir $::env(BACKTRACK_PRIORITY_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top backtrackPriorityClosedLoop
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/tests/backtrack_priority_closed_loop_top.cpp"
add_files -cflags $include_flags "$source_root/src/backtrack.cpp"
add_files -cflags $include_flags "$source_root/src/color.cpp"
add_files -cflags $include_flags "$source_root/src/priority_queue_functions.cpp"
add_files -tb -cflags $include_flags \
    "$source_root/tests/backtrack_priority_closed_loop_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_array_partition -type complete -dim 1 \
    backtrackPriorityClosedLoop clsStates

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
