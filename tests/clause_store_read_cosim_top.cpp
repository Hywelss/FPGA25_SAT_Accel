#include "data_structures.h"

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

void sendData_dataflow(
    const ap_uint<128>* clauseStore,
    const clauseMetaData* commands,
    const ap_uint<1>* compactClauseLayout,
    unsigned int clausePageSize,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input1,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input2,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output1,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output2);

void clauseStoreReadCosim(
    const ap_uint<128> clauseStore[32],
    const clauseMetaData commands[9],
    const ap_uint<1> compactClauseLayout[9],
    unsigned int clausePageSize,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input1,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input2,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output1,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output2) {
    sendData_dataflow(clauseStore, commands, compactClauseLayout, clausePageSize,
                      input1, input2, output1, output2);
}
