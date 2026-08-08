#include <cstdlib>
#include <iostream>

#include "backtrack.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void setOccurrence(ap_uint<512>* literalStore, unsigned int address,
    cls clauseID){
    literalStore[address/16].range(32*(address%16)+31,
        32*(address%16)) = clauseID;
}

void runRepeatedCleanupCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    answerStack[0] = 1;
    answerStack[1] = -2;
    answerStack[2] = 3;
    answerStack[3] = -4;
    for(unsigned int variable = 1; variable < 4; variable++){
        LMD_IS_IN_STACK(metadata[variable].compactlmd) = true;
    }
    unsigned int stackHeight = 4;
    const bool firstOk = cleanupQueryAssignments(priorityUpdates, states,
        answerStack, metadata, literalStore, 1, stackHeight, 32, 0,
        accessStats);

    require(firstOk && stackHeight == 1,
        "first cleanup did not preserve exactly one permanent root");
    require(priorityUpdates.read().data == 4 &&
        priorityUpdates.read().data == 3 &&
        priorityUpdates.read().data == 2 && priorityUpdates.empty(),
        "first cleanup did not return every search variable in stack order");
    require(!LMD_IS_IN_STACK(metadata[1].compactlmd) &&
        !LMD_IS_IN_STACK(metadata[2].compactlmd) &&
        !LMD_IS_IN_STACK(metadata[3].compactlmd),
        "first cleanup left a removed variable assigned");

    answerStack[1] = 2;
    answerStack[2] = -3;
    LMD_IS_IN_STACK(metadata[1].compactlmd) = true;
    LMD_IS_IN_STACK(metadata[2].compactlmd) = true;
    stackHeight = 3;
    const bool secondOk = cleanupQueryAssignments(priorityUpdates, states,
        answerStack, metadata, literalStore, 1, stackHeight, 32, 0,
        accessStats);

    require(secondOk && stackHeight == 1,
        "second cleanup did not complete after session reuse");
    require(priorityUpdates.read().data == 3 &&
        priorityUpdates.read().data == 2 && priorityUpdates.empty(),
        "second cleanup broke the variable-queue protocol");
}

void runMultiPageCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    answerStack[0] = 2;
    unsigned int stackHeight = 1;
    LMD_IS_IN_STACK(metadata[1].compactlmd) = true;
    LMD_NUM_ELE(metadata[1].compactlmd, 1) = 15;
    LMD_ADDR_START(metadata[1].compactlmd, 1) = 0;
    for(unsigned int i = 0; i < 14; i++){
        setOccurrence(literalStore, i, i+1);
    }
    setOccurrence(literalStore, 15, 32);
    setOccurrence(literalStore, 32, 15);

    const bool ok = cleanupQueryAssignments(priorityUpdates, states,
        answerStack, metadata, literalStore, 0, stackHeight, 16, 1,
        accessStats);
    require(ok && stackHeight == 0,
        "multi-page literal traversal did not finish");
    require(priorityUpdates.read().data == 2 && priorityUpdates.empty(),
        "multi-page cleanup returned the wrong variable");
    for(unsigned int clauseID = 1; clauseID <= 15; clauseID++){
        const unsigned int zeroBased = clauseID-1;
        const clsState state = states[
            zeroBased%_FPGA_CLS_STATES_PARTITION]
            [zeroBased/_FPGA_CLS_STATES_PARTITION];
        require(state.remainingUnassigned == 1 &&
            state.compressedList == -2,
            "multi-page cleanup missed or duplicated a clause-state update");
    }
    require(accessStats[3] == 3 && accessStats[1] == 1,
        "multi-page cleanup access accounting is inconsistent");
}

void runInvalidMetadataCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    answerStack[0] = -5;
    unsigned int stackHeight = 1;
    LMD_IS_IN_STACK(metadata[4].compactlmd) = true;
    LMD_NUM_ELE(metadata[4].compactlmd, 0) = 1;
    LMD_ADDR_START(metadata[4].compactlmd, 0) = 7;

    const bool ok = cleanupQueryAssignments(priorityUpdates, states,
        answerStack, metadata, literalStore, 0, stackHeight, 16, 0,
        accessStats);
    require(!ok && stackHeight == 0,
        "invalid literal-list metadata was not rejected with bounded cleanup");
    require(priorityUpdates.read().data == 5 && priorityUpdates.empty(),
        "invalid metadata left the variable-queue protocol unbalanced");
    require(!LMD_IS_IN_STACK(metadata[4].compactlmd),
        "invalid metadata left the removed variable assigned");
}

} // namespace

int main(){
    runRepeatedCleanupCase();
    runMultiPageCase();
    runInvalidMetadataCase();
    std::cout << "QUERY_CLEANUP_COSIM_PASS\n";
    return 0;
}
