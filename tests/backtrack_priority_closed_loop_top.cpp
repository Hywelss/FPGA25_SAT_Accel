#include "backtrack.h"
#include "priority_queue_functions.h"

namespace {

void cleanupProducer(hls::stream<ap_axiu<32,0,0,0>>& link,
    clsState clsStates[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const lit answerStack[_FPGA_MAX_LITERALS],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    const ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    unsigned int& answerStackHeight, ap_uint<64> accessStats[4]){
    #pragma HLS inline off

    ap_axiu<32,0,0,0> command;
    command.data = pq::UNHIDE_ELE;
    link.write(command);
    cleanupQueryAssignments(link, clsStates, answerStack, lmd,
        literalStore, 1, answerStackHeight, 32, 0, accessStats);
    command.data = pq::EXIT;
    link.write(command);
    link.write(command);
}

void priorityConsumer(hls::stream<ap_axiu<32,0,0,0>>& link,
    const unsigned int decisionDomain[_FPGA_MAX_LITERALS],
    unsigned int& protocolStatus){
    #pragma HLS inline off

    static pqData activityHeap[2][_FPGA_MAX_LITERALS];
    #pragma HLS array_partition variable=activityHeap dim=1 complete
    static pqPosition positioning[_FPGA_MAX_LITERALS];
    static ap_uint<3> bucketState[_FPGA_MAX_LITERALS];
    static gipsatLink bucketNext[_FPGA_MAX_LITERALS];
    static gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS];
    #pragma HLS array_partition variable=bucketHeads complete

    gipsatBucket bucketHead = 0;
    gipsatLink activityHeapSize = 0;
    loadGipsatBuckets(positioning, bucketState, bucketNext, bucketHeads,
        decisionDomain, 4, 4, bucketHead, activityHeapSize);
    const lit removed0 = gipsatBucketPop(bucketState, bucketNext,
        bucketHeads, bucketHead);
    const lit removed1 = gipsatBucketPop(bucketState, bucketNext,
        bucketHeads, bucketHead);
    const lit removed2 = gipsatBucketPop(bucketState, bucketNext,
        bucketHeads, bucketHead);

    const ap_axiu<32,0,0,0> command = link.read();
    if((unsigned int)command.data != (unsigned int)pq::UNHIDE_ELE){
        protocolStatus = 1;
        return;
    }
    gipsatBucketUnhideWrapper(link, activityHeap, positioning,
        bucketState, bucketNext, bucketHeads, activityHeapSize, bucketHead);
    const ap_axiu<32,0,0,0> finalCommand = link.read();

    unsigned int recoveredMask = 0;
    for(unsigned int i = 0; i < 4; i++){
        const lit variable = gipsatBucketPop(bucketState, bucketNext,
            bucketHeads, bucketHead);
        if(variable > 0 && variable <= 4){
            recoveredMask |= 1u << (variable-1);
        }
    }
    const bool initialPopsCorrect = removed0 == 4 && removed1 == 3 &&
        removed2 == 2;
    protocolStatus = initialPopsCorrect &&
        (int)finalCommand.data == pq::EXIT && recoveredMask == 15 ? 0 : 2;
}

} // namespace

void backtrackPriorityClosedLoop(
    clsState clsStates[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const lit answerStack[_FPGA_MAX_LITERALS],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    const ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    const unsigned int decisionDomain[_FPGA_MAX_LITERALS],
    unsigned int& answerStackHeight, unsigned int& protocolStatus,
    ap_uint<64> accessStats[4]){
    #pragma HLS dataflow

    hls::stream<ap_axiu<32,0,0,0>> link;
    #pragma HLS stream variable=link depth=2

    cleanupProducer(link, clsStates, answerStack, lmd, literalStore,
        answerStackHeight, accessStats);
    priorityConsumer(link, decisionDomain, protocolStatus);
}
