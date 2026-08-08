set source_root $::env(LEARN_SAVED_COUNT_SOURCE_ROOT)
set project_dir $::env(LEARN_SAVED_COUNT_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top learnSavedCountCosim
set include_flags "-I$source_root/src"
add_files -cflags $include_flags "$source_root/tests/learn_saved_count_cosim_top.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/learn_saved_count_cosim_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode ap_memory -depth 1024 learnSavedCountCosim resolutionClause
set_directive_interface -mode ap_memory -depth 65536 learnSavedCountCosim lmmd

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
