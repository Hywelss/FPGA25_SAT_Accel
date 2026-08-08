set source_root $::env(PQ_SOURCE_ROOT)
set project_dir $::env(PQ_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top pqHandler
set include_flags "-I$source_root/src"
add_files -cflags $include_flags "$source_root/src/pq_handler.cpp"
add_files -cflags $include_flags "$source_root/src/priority_queue_functions.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/pq_handler_assignment_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_compile -pipeline_loops 64
config_interface -m_axi_latency 0
config_rtl -deadlock_detection sim

# The depth controls the co-simulation memory model only. The old source did
# not declare it, so set the same value for both sides of the A/B comparison.
set_directive_interface -mode m_axi -depth 8192 pqHandler decision_domain

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
