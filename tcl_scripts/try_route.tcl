if {[llength $argv] != 2} {
    error "usage: try_route.tcl <route_design directive> <output tag>"
}

set directive [lindex $argv 0]
set tag [lindex $argv 1]
if {![regexp {^[A-Za-z0-9_]+$} $directive] || ![regexp {^[A-Za-z0-9_-]+$} $tag]} {
    error "directive and output tag must be simple names"
}

set root [file normalize [file join [file dirname [info script]] ..]]
set input_dcp [file join $root _x link vivado vpl prj prj.runs impl_1 level0_wrapper_phys_opt_loop.dcp]
set out [file join $root timing-experiments $tag]
file mkdir $out

if {![file exists $input_dcp]} {
    error "physical-optimization checkpoint not found: $input_dcp"
}

open_checkpoint $input_dcp
report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out before-route-timing-summary.rpt]

route_design -directive $directive

report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out routed-timing-summary.rpt]
report_route_status -file [file join $out route-status.rpt]
write_checkpoint -force [file join $out top_wrapper_routed.dcp]
puts "INDUCTOR_ROUTE_DONE directive=$directive tag=$tag"
close_design
