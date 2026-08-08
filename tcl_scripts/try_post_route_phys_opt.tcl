if {[llength $argv] != 2} {
    error "usage: try_post_route_phys_opt.tcl <phys_opt_design directive> <output tag>"
}

set directive [lindex $argv 0]
set tag [lindex $argv 1]
if {![regexp {^[A-Za-z0-9_]+$} $directive] || ![regexp {^[A-Za-z0-9_-]+$} $tag]} {
    error "directive and output tag must be simple names"
}

set root [file normalize [file join [file dirname [info script]] ..]]
set input_dcp [file join $root _x link vivado vpl prj prj.runs impl_1 top_wrapper_routed.dcp]
set out [file join $root timing-experiments $tag]
file mkdir $out

if {![file exists $input_dcp]} {
    error "routed checkpoint not found: $input_dcp"
}

open_checkpoint $input_dcp
report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out before-timing-summary.rpt]

phys_opt_design -directive $directive

report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out after-timing-summary.rpt]
report_route_status -file [file join $out route-status.rpt]
write_checkpoint -force [file join $out top_wrapper_post_route_physopt.dcp]
puts "INDUCTOR_POST_ROUTE_PHYS_OPT_DONE directive=$directive tag=$tag"
close_design
