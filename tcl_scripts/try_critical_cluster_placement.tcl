if {[llength $argv] != 2} {
    error "usage: try_critical_cluster_placement.tcl <place_design directive> <output tag>"
}

set directive [lindex $argv 0]
set tag [lindex $argv 1]
if {![regexp {^[A-Za-z0-9_]+$} $directive] || ![regexp {^[A-Za-z0-9_-]+$} $tag]} {
    error "directive and output tag must be simple names"
}

set root [file normalize [file join [file dirname [info script]] ..]]
set input_dcp [file join $root _x link vivado vpl prj prj.runs impl_1 top_wrapper_opt.dcp]
set out [file join $root timing-experiments $tag]
file mkdir $out

if {![file exists $input_dcp]} {
    error "optimized checkpoint not found: $input_dcp"
}

open_checkpoint $input_dcp

set critical_cells [get_cells -quiet -hierarchical -filter {
    IS_PRIMITIVE &&
    (NAME =~ *colorStream_10*/grp_colorStream_10_Pipeline_COLOR_STREAM*/*pageWalkIndex* ||
     NAME =~ *colorStream_10*/grp_colorStream_10_Pipeline_COLOR_STREAM*/*numElementsRead* ||
     NAME =~ *colorStream_10*/grp_colorStream_10_Pipeline_COLOR_STREAM*/*readOne_numElements* ||
     NAME =~ *colorStream_10*/grp_colorStream_10_Pipeline_COLOR_STREAM*/*icmp_ln109* ||
     NAME =~ *solver_1/inst/numElementsRead_fu_142_reg* ||
     NAME =~ *solver_1/inst/readOne_numElements_fu_150_reg* ||
     NAME =~ *solver_1/inst/pageWalkIndex_fu_146_reg* ||
     NAME =~ *solver_1/inst/icmp_ln109*)
}]
set critical_count [llength $critical_cells]
if {$critical_count < 100} {
    error "critical color-stream cell selection is unexpectedly small: $critical_count"
}

set pblock_name pblock_color_stream_critical
create_pblock $pblock_name
resize_pblock [get_pblocks $pblock_name] -add {SLICE_X120Y65:SLICE_X145Y115}
add_cells_to_pblock [get_pblocks $pblock_name] $critical_cells
set_property CONTAIN_ROUTING false [get_pblocks $pblock_name]

set fd [open [file join $out constraint-summary.txt] w]
puts $fd "directive=$directive"
puts $fd "critical_cells=$critical_count"
puts $fd "range=[get_property GRID_RANGES [get_pblocks $pblock_name]]"
close $fd

place_design -directive $directive
report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out placed-timing-summary.rpt]
report_design_analysis -congestion \
    -file [file join $out placed-congestion.rpt]
write_checkpoint -force [file join $out top_wrapper_placed.dcp]
puts "INDUCTOR_CRITICAL_CLUSTER_PLACEMENT_DONE directive=$directive tag=$tag cells=$critical_count"
close_design
