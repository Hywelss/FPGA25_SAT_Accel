#include <cstdlib>
#include <iostream>

#include "discover.h"

#ifndef DISCOVER_CACHE_TOP
#define DISCOVER_CACHE_TOP discover
#endif

void DISCOVER_CACHE_TOP(hls::stream<colorAssignment>& toCommitStream,
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
    const ap_uint<1> positiveLiteralPhase, bool& skipCPUOnce);

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

literalMetaData metadata(unsigned int positiveAddress,
    unsigned int positiveCount){
    literalMetaData result{};
    LMD_ADDR_START(result.compactlmd, 1) = positiveAddress;
    LMD_NUM_ELE(result.compactlmd, 1) = positiveCount;
    LMD_SHORTEST_CLS_LENGTH(result.compactlmd) = UINT_MAX;
    return result;
}

} // namespace

int main(){
    static lit answerStack[_FPGA_MAX_LITERALS] = {};
    static literalMetaData lmd[_FPGA_MAX_LITERALS] = {};
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS] = {};
    static cls unitByCls[_FPGA_MAX_LITERALS] = {};

    lmd[0] = metadata(100, 1);
    lmd[_FPGA_MAX_LITERALS-1] = metadata(200, 2);

    hls::stream<colorAssignment> commits;
    hls::stream<bcpPacket> propagation;
    hls::stream<int> duplicateCounts;
    hls::stream<ap_axiu<32,0,0,0>> clauseLengths;

    propagation.write((bcpPacket){
        .literalUnitted=_FPGA_MAX_LITERALS,
        .unitByClause=17,
        .unitByLit=-1,
        .depthCount=3});
    propagation.write((bcpPacket){
        .literalUnitted=0,
        .unitByClause=0,
        .unitByLit=0,
        .depthCount=0});
    ap_axiu<32,0,0,0> length;
    length.data = 7;
    clauseLengths.write(length);

    flippedLiteral unused{};
    unsigned int answerStackHeight = 0;
    unsigned int fixedDecisionStackHeight = 0;
    bool skipCPUOnce = false;
    DISCOVER_CACHE_TOP(commits, propagation, duplicateCounts, clauseLengths,
        unused, answerStack, lmd, lmmd, unitByCls,
        answerStackHeight, 1, fixedDecisionStackHeight,
        1, false, true, 1, skipCPUOnce);

    require(answerStackHeight == 2,
        "the decision and delayed maximum-variable propagation must both commit");
    require(answerStack[0] == 1 && answerStack[1] == _FPGA_MAX_LITERALS,
        "the maximum variable must not alias an idle-cycle cache entry");
    require(LMD_IS_IN_STACK(lmd[_FPGA_MAX_LITERALS-1].compactlmd),
        "maximum-variable metadata must be committed");
    require((unsigned int)LMD_INSERT_LVL(
        lmd[_FPGA_MAX_LITERALS-1].compactlmd) == 1,
        "maximum-variable insertion level must remain deterministic");
    require(unitByCls[_FPGA_MAX_LITERALS-1] == 17,
        "maximum-variable reason clause must be retained");

    const colorAssignment decision = commits.read();
    const colorAssignment propagated = commits.read();
    const colorAssignment end = commits.read();
    require(decision.literal == 1 && decision.addressStart == 100 &&
            decision.numElements == 1 && !decision.eos,
        "decision work item must be preserved");
    require(propagated.literal == _FPGA_MAX_LITERALS &&
            propagated.addressStart == 200 &&
            propagated.numElements == 2 && propagated.depthCount == 3 &&
            !propagated.eos,
        "delayed maximum-variable work item must use initialized metadata");
    require(end.eos && commits.empty(),
        "discover must emit exactly one final work marker");
    require(duplicateCounts.read() == -1 && duplicateCounts.empty(),
        "discover must emit exactly one final duplicate-count marker");
    require(clauseLengths.empty(),
        "each nonzero propagation must consume exactly one clause length");

    std::cout << "DISCOVER_CACHE_TEST_PASS\n";
    return 0;
}
