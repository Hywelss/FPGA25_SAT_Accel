#include "discover.h"

static void bufferPropagation(hls::stream<bcpPacket>& externalInput,
    hls::stream<bcpPacket>& internalInput){
    #pragma HLS inline off
    while(true){
        const bcpPacket packet = externalInput.read();
        internalInput.write(packet);
        if(packet.literalUnitted == 0){
            break;
        }
    }
}

static void forwardDuplicateCounts(hls::stream<int>& internalOutput,
    hls::stream<int>& externalOutput){
    #pragma HLS inline off
    while(true){
        const int count = internalOutput.read();
        externalOutput.write(count);
        if(count == -1){
            break;
        }
    }
}

void discoverCacheCosim(hls::stream<colorAssignment>& toCommitStream,
    hls::stream<bcpPacket>& toDecide,
    hls::stream<int>& duplicateCountStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream,
    const flippedLiteral litToCheck,
    lit answerStack[_FPGA_MAX_LITERALS],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    cls unitByCls[_FPGA_MAX_LITERALS], unsigned int& answerStackHeight,
    const unsigned int decisionLevel, unsigned int& fixedDecisionStackHeight,
    const lit topLiteral, const bool useFlipped, const bool forceLiteralPhase,
    const ap_uint<1> positiveLiteralPhase, bool& skipCPUOnce){
    hls::stream<bcpPacket> bufferedPropagation;
    hls::stream<int> bufferedDuplicateCounts;
    #pragma HLS stream variable=bufferedPropagation depth=2
    #pragma HLS stream variable=bufferedDuplicateCounts depth=2
    #pragma HLS dataflow

    bufferPropagation(toDecide, bufferedPropagation);
    discover(toCommitStream, bufferedPropagation, bufferedDuplicateCounts,
        clauseStoreOutputStream, litToCheck, answerStack, lmd, lmmd,
        unitByCls, answerStackHeight, decisionLevel, fixedDecisionStackHeight,
        topLiteral, useFlipped, forceLiteralPhase, positiveLiteralPhase,
        skipCPUOnce);
    forwardDuplicateCounts(bufferedDuplicateCounts, duplicateCountStream);
}
