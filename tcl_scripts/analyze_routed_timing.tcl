set root [file normalize [file join [file dirname [info script]] ..]]
set dcp [file join $root _x link vivado vpl prj prj.runs impl_1 top_wrapper_routed.dcp]
set out [file join $root timing-analysis]
file mkdir $out

if {![file exists $dcp]} {
    error "routed checkpoint not found: $dcp"
}

open_checkpoint $dcp

report_timing_summary -delay_type max -max_paths 200 \
    -file [file join $out timing-summary.rpt]
report_timing -delay_type max -max_paths 200 -nworst 1 \
    -sort_by group -input_pins \
    -file [file join $out worst-200.rpt]
report_design_analysis -congestion \
    -file [file join $out congestion.rpt]
report_utilization -hierarchical -hierarchical_depth 6 \
    -file [file join $out hierarchical-utilization.rpt]

set hierarchy *solver_1/inst/grp_bcp_discover_dataflow_wrapper_fu_2294/colorStream_10_U0/grp_colorStream_10_Pipeline_COLOR_STREAM_fu_122
set primitives [get_cells -quiet -hierarchical -filter "IS_PRIMITIVE && NAME =~ $hierarchy/*"]
set fd [open [file join $out color-stream-placement.tsv] w]
puts $fd "cell\tprimitive\tlocation"
foreach cell $primitives {
    puts $fd "[get_property NAME $cell]\t[get_property REF_NAME $cell]\t[get_property LOC $cell]"
}
close $fd

puts "INDUCTOR_TIMING_ANALYSIS_DONE"
close_design
