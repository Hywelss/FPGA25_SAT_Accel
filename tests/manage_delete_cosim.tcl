set source_root $::env(MANAGE_DELETE_COSIM_SOURCE_ROOT)
set project_dir $::env(MANAGE_DELETE_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top manageDeleteCosim
set include_flags "-I$source_root/src -DFPGA_HW -DFPGA_VCK5000"
add_files -cflags $include_flags "$source_root/src/manage.cpp"
add_files -cflags $include_flags "$source_root/tests/manage_delete_cosim_top.cpp"
add_files -tb -cflags $include_flags "$source_root/tests/manage_delete_cosim_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode axis manageDeleteCosim updates
set_directive_interface -mode axis manageDeleteCosim deletedClauses
set_directive_interface -mode axis manageDeleteCosim locations
set_directive_interface -mode ap_memory -depth 64 manageDeleteCosim literalStore
set_directive_interface -mode ap_memory -depth 64 manageDeleteCosim metadata

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
