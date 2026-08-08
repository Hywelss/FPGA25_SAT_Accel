set source_root $::env(LOCATION_COSIM_SOURCE_ROOT)
set project_dir $::env(LOCATION_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top location_handler
set include_flags "-I$source_root/src -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/location_handler.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/location_handler_protocol_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode m_axi -depth 1 location_handler clsToLitStorePos
set_directive_interface -mode m_axi -depth 1 location_handler litToClsStorePos

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
