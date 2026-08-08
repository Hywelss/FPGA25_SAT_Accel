#include "data_structures.h"

void deleteExplicitClauses_wrapper(
    mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    unsigned int removeTotal, unsigned int clausePageSize);

void clauseStoreDeleteCosim(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int clausePageSize) {
    static mmuStream<cls, _FPGA_MAX_CLAUSES> freeClsID;
    static mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>
        freeClsPageAddresses;
    freeClsID.reset(_FPGA_MAX_CLAUSES, _FPGA_MAX_CLAUSES, 1);
    freeClsPageAddresses.reset(_FPGA_MAX_LITERAL_ELEMENTS,
        _FPGA_MAX_LITERAL_ELEMENTS, clausePageSize);

    deleteExplicitClauses_wrapper(freeClsID, freeClsPageAddresses,
        clauseStoreInputStream1, clauseStoreInputStream2,
        clauseStoreOutputStream1, locationInputStream,
        mCmd, mClsStore, compactClauseLayout, removeTotal, clausePageSize);
}
