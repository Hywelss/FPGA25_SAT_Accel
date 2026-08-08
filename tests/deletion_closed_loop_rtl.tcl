set repo_root [pwd]
set include_flags "-I$repo_root/src -DFPGA_HW -DFPGA_VCK5000"

cd /tmp
if {![info exists ::env(DELETION_RTL_MANAGE_ONLY)] ||
        $::env(DELETION_RTL_MANAGE_ONLY) ne "1"} {
    open_project -reset inductor-deletion-clause-rtl
    set_top clauseStoreDeleteCosim
    add_files "$repo_root/src/clause_store_handler.cpp" -cflags $include_flags
    add_files "$repo_root/tests/clause_store_delete_cosim_top.cpp" -cflags $include_flags
    open_solution -reset solution
    set_part {xcvc1902-vsvd1760-2MP-e-S}
    create_clock -period 5
    config_rtl -deadlock_detection sim
    set_directive_interface -mode axis clauseStoreDeleteCosim clauseStoreInputStream1
    set_directive_interface -mode axis clauseStoreDeleteCosim clauseStoreInputStream2
    set_directive_interface -mode axis clauseStoreDeleteCosim clauseStoreOutputStream1
    set_directive_interface -mode axis clauseStoreDeleteCosim locationInputStream
    set_directive_interface -mode ap_memory -depth 16 clauseStoreDeleteCosim mCmd
    set_directive_interface -mode ap_memory -depth 512 clauseStoreDeleteCosim mClsStore
    set_directive_interface -mode ap_memory -depth 16 clauseStoreDeleteCosim compactClauseLayout
    csynth_design
    close_project
}

open_project -reset inductor-deletion-manage-rtl
set_top manageDeleteCosim
add_files "$repo_root/src/manage.cpp" -cflags $include_flags
add_files "$repo_root/tests/manage_delete_cosim_top.cpp" -cflags $include_flags
open_solution -reset solution
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 5
config_rtl -deadlock_detection sim
set_directive_interface -mode axis manageDeleteCosim updates
set_directive_interface -mode axis manageDeleteCosim deletedClauses
set_directive_interface -mode axis manageDeleteCosim locations
set_directive_interface -mode ap_memory -depth 64 manageDeleteCosim literalStore
set_directive_interface -mode ap_memory -depth 64 manageDeleteCosim metadata
csynth_design
exit
