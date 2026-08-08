#include "manage.h"

void manageDeleteCosim(
    hls::stream<ap_axiu<96,0,0,0>>& updates,
    hls::stream<ap_axiu<32,0,0,0>>& deletedClauses,
    hls::stream<ap_axiu<32,0,0,0>>& locations,
    ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData metadata[_FPGA_MAX_LITERALS],
    unsigned int& freePageCount){
    static mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_> freePages;
    freePages.reset(_FPGA_MAX_LITERAL_ELEMENTS,
        _FPGA_MAX_LITERAL_ELEMENTS, 16);
    deleteTransposedClauses(literalStore, metadata, freePages, 16,
        updates, deletedClauses, locations);
    freePageCount = freePages.size();
}
