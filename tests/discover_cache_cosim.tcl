set source_root $::env(DISCOVER_CACHE_SOURCE_ROOT)
set project_dir $::env(DISCOVER_CACHE_COSIM_PROJECT)
set project_parent [file dirname $project_dir]
set project_name [file tail $project_dir]

file mkdir $project_parent
cd $project_parent
open_project -reset $project_name
set_top discoverCacheCosim
set include_flags "-I$source_root/src -DFPGA_HW"
add_files -cflags $include_flags "$source_root/src/discover.cpp"
add_files -cflags $include_flags "$source_root/src/decide.cpp"
add_files -cflags $include_flags "$source_root/src/color.cpp"
add_files -cflags $include_flags "$source_root/tests/discover_cache_cosim_top.cpp"
add_files -tb -cflags "$include_flags -DDISCOVER_CACHE_TOP=discoverCacheCosim" "$source_root/tests/discover_cache_test.cpp"

open_solution -reset solution
set_part {xcvc1902-vsva2197-2MP-e-S}
create_clock -period 4
config_rtl -deadlock_detection sim
set_directive_interface -mode axis discoverCacheCosim toCommitStream
set_directive_interface -mode axis discoverCacheCosim toDecide
set_directive_interface -mode axis discoverCacheCosim duplicateCountStream
set_directive_interface -mode axis discoverCacheCosim clauseStoreOutputStream
set_directive_interface -mode ap_memory -depth 32768 discoverCacheCosim answerStack
set_directive_interface -mode ap_memory -depth 32768 discoverCacheCosim lmd
set_directive_interface -mode ap_memory -depth 65536 discoverCacheCosim lmmd
set_directive_interface -mode ap_memory -depth 32768 discoverCacheCosim unitByCls

csim_design
csynth_design
cosim_design -rtl verilog -random_stall -ldflags "-no-pie"
exit
