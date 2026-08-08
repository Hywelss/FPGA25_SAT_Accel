set source_root $::env(CLAUSE_LENGTH_SOURCE_ROOT)
set project_dir $::env(CLAUSE_LENGTH_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top clauseLengthProtocolCosim
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/clause_store_handler.cpp"
add_files -cflags $include_flags "$source_root/tests/clause_length_protocol_cosim_top.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode axis clauseLengthProtocolCosim input
set_directive_interface -mode axis clauseLengthProtocolCosim output
set_directive_interface -mode ap_memory -depth 8 clauseLengthProtocolCosim commands

csynth_design
exit
