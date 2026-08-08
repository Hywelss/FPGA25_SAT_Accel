set root [file normalize [file join [file dirname [info script]] ..]]
set input_dcp [file join $root _x link vivado vpl prj prj.runs impl_1 top_wrapper_opt.dcp]
set out [file join $root timing-experiments extra-net-delay]
file mkdir $out

if {![file exists $input_dcp]} {
    error "optimized checkpoint not found: $input_dcp"
}

open_checkpoint $input_dcp
place_design -directive ExtraNetDelay_high
report_timing_summary -delay_type min_max -max_paths 100 \
    -file [file join $out placed-timing-summary.rpt]
report_design_analysis -congestion \
    -file [file join $out placed-congestion.rpt]
write_checkpoint -force [file join $out top_wrapper_placed.dcp]
puts "INDUCTOR_EXTRA_NET_DELAY_PLACEMENT_DONE"
close_design
