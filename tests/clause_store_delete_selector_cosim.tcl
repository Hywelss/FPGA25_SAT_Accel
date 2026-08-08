set source_root $::env(DELETE_SELECTOR_COSIM_SOURCE_ROOT)
set project_dir $::env(DELETE_SELECTOR_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top getDeletedClsID
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/clause_store_handler.cpp"
add_files -tb -cflags $include_flags \
    "$source_root/tests/clause_store_delete_selector_cosim_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 4.0
config_rtl -deadlock_detection sim
set_directive_interface -mode m_axi -offset direct -bundle gmem \
    -depth 1310720 \
    getDeletedClsID usedClsIDBuckets
set_directive_interface -mode ap_memory -depth 10 getDeletedClsID tracker
set_directive_interface -mode ap_memory -depth 10 getDeletedClsID LBDBucketCount

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
