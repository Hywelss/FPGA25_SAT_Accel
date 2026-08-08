set root [file normalize [file join [file dirname [info script]] ..]]
set dcp [file join $root _x link vivado vpl prj prj.runs impl_1 top_wrapper_routed.dcp]
set out [file join $root timing-analysis critical-cluster]
file mkdir $out

if {![file exists $dcp]} {
    error "routed checkpoint not found: $dcp"
}

open_checkpoint $dcp

set fd [open [file join $out pblocks.tsv] w]
puts $fd "pblock\tranges\tcell_count"
foreach pblock [get_pblocks -quiet] {
    set cells [get_cells -quiet -of_objects $pblock]
    puts $fd "[get_property NAME $pblock]\t[get_property GRID_RANGES $pblock]\t[llength $cells]"
}
close $fd

set color_hierarchy *solver_1/inst/grp_bcp_discover_dataflow_wrapper_fu_2294/colorStream_10_U0/grp_colorStream_10_Pipeline_COLOR_STREAM_fu_122
set color_cells [get_cells -quiet -hierarchical -filter "IS_PRIMITIVE && NAME =~ $color_hierarchy/*"]
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

set fd [open [file join $out cells.tsv] w]
puts $fd "set\tcell\tprimitive\tlocation\tsite_type"
foreach cell $color_cells {
    set loc [get_property LOC $cell]
    set site_type ""
    if {$loc ne ""} {
        set site [get_sites -quiet $loc]
        if {[llength $site] == 1} {
            set site_type [get_property SITE_TYPE $site]
        }
    }
    set kind color
    if {[lsearch -exact $critical_cells $cell] >= 0} {
        set kind critical
    }
    puts $fd "$kind\t[get_property NAME $cell]\t[get_property REF_NAME $cell]\t$loc\t$site_type"
}
close $fd

set fd [open [file join $out summary.txt] w]
puts $fd "color_cells=[llength $color_cells]"
puts $fd "critical_cells=[llength $critical_cells]"
foreach pattern {pageWalkIndex numElementsRead readOne_numElements icmp_ln109} {
    set cells [get_cells -quiet -hierarchical -filter "IS_PRIMITIVE && NAME =~ *colorStream_10*/grp_colorStream_10_Pipeline_COLOR_STREAM*/*${pattern}*"]
    puts $fd "$pattern=[llength $cells]"
}
close $fd

puts "INDUCTOR_CRITICAL_CLUSTER_ANALYSIS_DONE"
close_design
