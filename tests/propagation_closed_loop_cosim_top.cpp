#include "discover.h"

void propagationClosedLoopCosim(
    hls::stream<ap_axiu<32,0,0,0>>& clauseLengths,
    hls::stream<ap_axiu<96,0,0,0>>& clauseRequests,
    unsigned int scenario, unsigned int& answerHeight,
    lit& firstAnswer, lit& secondAnswer, bool& doBacktrack,
    unsigned int& conflictCount){
    #pragma HLS inline off

    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION];
    #pragma HLS array_partition variable=states dim=1 complete
    static lit answerStack[_FPGA_MAX_LITERALS];
    static literalMetaData lmd[_FPGA_MAX_LITERALS];
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS];
    static cls unitByCls[_FPGA_MAX_LITERALS];
    static bool inDomain[_FPGA_MAX_LITERALS];
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16];

    const lit topLiteral = 1 + 2*scenario;
    const lit unitLiteral = topLiteral + 1;
    const lit chainedUnitLiteral = topLiteral + 2;
    const cls clauseID = 1 + 8*scenario;
    const unsigned int adjacencyAddress = 16*scenario;

    literalMetaData topMetadata;
    topMetadata.compactlmd = 0;
    topMetadata.decisionLevelStackEnd = 0;
    LMD_ADDR_START(topMetadata.compactlmd, 1) =
        scenario == 4 ? _FPGA_MAX_LITERAL_ELEMENTS : adjacencyAddress;
    LMD_NUM_ELE(topMetadata.compactlmd, 1) = scenario == 5 ? 0 : 1;
    LMD_PHASE(topMetadata.compactlmd) = 1;
    LMD_SHORTEST_CLS_LENGTH(topMetadata.compactlmd) = UINT_MAX;
    lmd[topLiteral-1] = topMetadata;

    literalMetaData unitMetadata;
    unitMetadata.compactlmd = 0;
    unitMetadata.decisionLevelStackEnd = 0;
    const bool chainedScenario = scenario >= 9 && scenario <= 12;
    if(chainedScenario){
        LMD_ADDR_START(unitMetadata.compactlmd, 1) = adjacencyAddress + 16;
        LMD_NUM_ELE(unitMetadata.compactlmd, 1) = 1;
    }
    LMD_SHORTEST_CLS_LENGTH(unitMetadata.compactlmd) = UINT_MAX;
    lmd[unitLiteral-1] = unitMetadata;
    literalMetaData chainedMetadata;
    chainedMetadata.compactlmd = 0;
    chainedMetadata.decisionLevelStackEnd = 0;
    LMD_SHORTEST_CLS_LENGTH(chainedMetadata.compactlmd) = UINT_MAX;
    lmd[chainedUnitLiteral-1] = chainedMetadata;
    for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
        lmmd[lane][topLiteral-1] = {};
        lmmd[lane][unitLiteral-1] = {};
        lmmd[lane][chainedUnitLiteral-1] = {};
    }
    unitByCls[topLiteral-1] = 0;
    unitByCls[unitLiteral-1] = 0;
    unitByCls[chainedUnitLiteral-1] = 0;
    inDomain[topLiteral-1] = true;
    inDomain[unitLiteral-1] = scenario != 3;
    inDomain[chainedUnitLiteral-1] = true;

    ap_uint<512> adjacency = 0;
    if(scenario == 7){
        adjacency.range(255,224) = clauseID;
    }else{
        adjacency.range(31,0) = clauseID;
    }
    literalStore[adjacencyAddress/16] = adjacency;
    if(chainedScenario){
        ap_uint<512> chainedAdjacency = 0;
        chainedAdjacency.range(31,0) = clauseID + 1;
        literalStore[adjacencyAddress/16 + 1] = chainedAdjacency;
    }

    clsState state = {};
    if(scenario == 0){
        state.remainingUnassigned = 3;
        state.compressedList = (-topLiteral) ^ unitLiteral ^ (unitLiteral+1);
    }else if(scenario == 1 || scenario == 3 || scenario == 8 || chainedScenario){
        state.remainingUnassigned = 2;
        state.compressedList = (-topLiteral) ^ unitLiteral;
    }else if(scenario == 2){
        state.remainingUnassigned = 1;
        state.compressedList = -topLiteral;
    }else if(scenario == 7){
        state.remainingUnassigned = 4;
        state.compressedList = (-topLiteral) ^ (topLiteral+1) ^
            (topLiteral+2) ^ (topLiteral+3);
    }
    states[(clauseID-1)%_FPGA_CLS_STATES_PARTITION]
        [(clauseID-1)/_FPGA_CLS_STATES_PARTITION] = state;
    if(chainedScenario){
        clsState chainedState = {};
        chainedState.remainingUnassigned = 2;
        chainedState.compressedList = (-unitLiteral) ^ chainedUnitLiteral;
        states[clauseID%_FPGA_CLS_STATES_PARTITION]
            [clauseID/_FPGA_CLS_STATES_PARTITION] = chainedState;
    }

    unsigned int localAnswerHeight = 0;
    unsigned int fixedHeight = 0;
    lit literalCommit = 0;
    bool localDoBacktrack = false;
    myStream<cls,64,7> conflicts = {};
    ap_uint<64> accessStats[4] = {0,0,0,0};
    volatile uint64_t store[2] = {0,0};
    hls::stream<ap_axiu<32,0,0,0>> pqCommands;
    flippedLiteral unused = {};

    const unsigned int decisionLevel = scenario == 5 ? 0 : 1;
    const bool firstIteration = scenario != 5;
    bcp_discover_dataflow_wrapper(states, answerStack, lmd, lmmd,
        unitByCls, inDomain, literalStore, localAnswerHeight, conflicts,
        fixedHeight, literalCommit, localDoBacktrack, topLiteral, unused, 0,
        decisionLevel, false, firstIteration, false, 16, 1,
        accessStats, store, pqCommands, clauseRequests, clauseLengths);

    if(scenario == 8 && !localDoBacktrack){
        const lit nextDecision = topLiteral + 2;
        literalMetaData nextMetadata;
        nextMetadata.compactlmd = 0;
        nextMetadata.decisionLevelStackEnd = 0;
        LMD_SHORTEST_CLS_LENGTH(nextMetadata.compactlmd) = UINT_MAX;
        LMD_PHASE(nextMetadata.compactlmd) = 1;
        lmd[nextDecision-1] = nextMetadata;
        for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
            lmmd[lane][nextDecision-1] = {};
        }
        unitByCls[nextDecision-1] = 0;
        inDomain[nextDecision-1] = true;
        bcp_discover_dataflow_wrapper(states, answerStack, lmd, lmmd,
            unitByCls, inDomain, literalStore, localAnswerHeight, conflicts,
            fixedHeight, literalCommit, localDoBacktrack, nextDecision, unused, 0,
            2, false, true, false, 16, 1,
            accessStats, store, pqCommands, clauseRequests, clauseLengths);
    }
    if(chainedScenario && !localDoBacktrack){
        const unsigned int emptyPropagationRounds = scenario - 8;
        REPEAT_EMPTY_PROPAGATION: for(unsigned int round = 0;
                round < emptyPropagationRounds; round++){
            #pragma HLS loop_tripcount min=1 max=4
            #pragma HLS pipeline off
            const lit nextDecision = topLiteral + 3 + round;
            literalMetaData nextMetadata;
            nextMetadata.compactlmd = 0;
            nextMetadata.decisionLevelStackEnd = 0;
            LMD_SHORTEST_CLS_LENGTH(nextMetadata.compactlmd) = UINT_MAX;
            LMD_PHASE(nextMetadata.compactlmd) = 1;
            lmd[nextDecision-1] = nextMetadata;
            for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
                lmmd[lane][nextDecision-1] = {};
            }
            unitByCls[nextDecision-1] = 0;
            inDomain[nextDecision-1] = true;
            bcp_discover_dataflow_wrapper(states, answerStack, lmd, lmmd,
                unitByCls, inDomain, literalStore, localAnswerHeight, conflicts,
                fixedHeight, literalCommit, localDoBacktrack, nextDecision, unused, 0,
                2 + round, false, true, false, 16, 1,
                accessStats, store, pqCommands, clauseRequests, clauseLengths);
        }
    }

    answerHeight = localAnswerHeight;
    doBacktrack = localDoBacktrack;
    firstAnswer = localAnswerHeight > 0 ? answerStack[0] : 0;
    secondAnswer = localAnswerHeight > 1 ? answerStack[localAnswerHeight-1] : 0;
    conflictCount = conflicts.head;
}
