if {[llength $argv] != 2} {
    error "usage: try_placement.tcl <place_design directive> <output tag>"
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
place_design -directive $directive
report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out placed-timing-summary.rpt]
report_design_analysis -congestion \
    -file [file join $out placed-congestion.rpt]
write_checkpoint -force [file join $out top_wrapper_placed.dcp]
puts "INDUCTOR_PLACEMENT_DONE directive=$directive tag=$tag"
close_design
