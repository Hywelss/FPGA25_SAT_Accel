set source_root $::env(INVALID_CONFIG_COSIM_SOURCE_ROOT)
set project_dir $::env(INVALID_CONFIG_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top clause_store_handler
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/clause_store_handler.cpp"
add_files -tb -cflags $include_flags \
    "$source_root/tests/clause_store_invalid_config_protocol_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 4.0
config_rtl -deadlock_detection sim
set_directive_interface -mode axis clause_store_handler locationInputStream
set_directive_interface -mode m_axi -depth 1 clause_store_handler clauseStore
set_directive_interface -mode m_axi -depth 1 clause_store_handler cmd
set_directive_interface -mode m_axi -depth 1 clause_store_handler usedClsIDBuckets
set_directive_interface -mode m_axi -depth 20 clause_store_handler trackLBD

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -enable_dataflow_profiling \
    -ldflags "-no-pie"
exit
