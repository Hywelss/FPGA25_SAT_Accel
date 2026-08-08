#include "data_structures.h"

#include <cassert>

extern "C" void clause_store_handler(
    ap_uint<128>* clauseStore,
    clauseMetaData* cmd,
    cls* usedClsIDBuckets,
    unsigned int* trackLBD,
    unsigned int initialClauseElements,
    unsigned int maxClauseElements,
    unsigned int initialClauseCount,
    unsigned int clausePageSize,
    double prunePctage,
    bool sessionReset,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream);

int main() {
    ap_uint<128> clauseStore[1] = {};
    clauseMetaData commands[1] = {};
    cls usedBuckets[1] = {};
    unsigned int trackLBD[2 * _FPGA_MAX_LBD_BUCKETS] = {};
    hls::stream<ap_axiu<96,0,0,0>> input1;
    hls::stream<ap_axiu<96,0,0,0>> input2;
    hls::stream<ap_axiu<32,0,0,0>> output1;
    hls::stream<ap_axiu<32,0,0,0>> output2;
    hls::stream<ap_axiu<64,0,0,0>> locations;

    ap_axiu<96,0,0,0> command;
    command.data = 0;
    command.data.range(31,0) = 3;
    command.data.range(95,64) = csh::DELETE_IDS;
    input1.write(command);

    command.data = 0;
    command.data.range(95,64) = csh::EXIT;
    input1.write(command);

    // clausePageSize=0 deliberately makes the configuration invalid.  The
    // producer has not sent IDs because it must receive the accepted count
    // first.
    clause_store_handler(clauseStore, commands, usedBuckets, trackLBD,
        0, 0, 0, 0, 0.0, false,
        input1, input2, output1, output2, locations);

    assert(output1.read().data == 0);
    assert(output1.empty());
    assert(input1.empty());
    assert(input2.empty());
    assert(output2.empty());
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.empty());
}
