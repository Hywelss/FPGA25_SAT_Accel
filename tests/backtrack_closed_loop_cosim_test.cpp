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

void runEmptyTraversalCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    answerStack[0] = -3;
    answerStack[1] = 2;
    unsigned int stackHeight = 2;
    undo_states_dataflow_wrapper(priorityUpdates, states, answerStack,
        metadata, literalStore, 2, 2, stackHeight, 32, 0, accessStats);

    require(stackHeight == 0, "empty traversal must remove both assignments");
    require(priorityUpdates.read().data == 2,
        "first removed variable must be returned to variable selection");
    require(priorityUpdates.read().data == 3,
        "second removed variable must be returned to variable selection");
    require(priorityUpdates.empty(), "empty traversal emitted an extra update");
}

void runOneClauseCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    answerStack[0] = 2;
    unsigned int stackHeight = 1;
    LMD_NUM_ELE(metadata[1].compactlmd, 1) = 1;
    LMD_ADDR_START(metadata[1].compactlmd, 1) = 0;
    LMD_IS_IN_STACK(metadata[1].compactlmd) = true;
    literalStore[0].range(31, 0) = 1;
    states[0][0].remainingUnassigned = 0;
    states[0][0].compressedList = 7;

    undo_states_dataflow_wrapper(priorityUpdates, states, answerStack,
        metadata, literalStore, 2, 1, stackHeight, 32, 0, accessStats);

    require(stackHeight == 0, "one-clause traversal must remove the assignment");
    require(priorityUpdates.read().data == 2,
        "one-clause traversal must return the removed variable");
    require(priorityUpdates.empty(), "one-clause traversal emitted an extra update");
    require(states[0][0].remainingUnassigned == 1,
        "transposed clause state was not restored");
    require(states[0][0].compressedList == (7 ^ -2),
        "transposed clause compressed literal was not restored");
}

void runIncrementalCleanupCase(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    hls::stream<ap_axiu<32,0,0,0>> priorityUpdates;

    // Keep literal 1 as the permanent root and remove three search assignments.
    answerStack[0] = 1;
    answerStack[1] = -2;
    answerStack[2] = 3;
    answerStack[3] = -4;
    unsigned int stackHeight = 4;

    LMD_NUM_ELE(metadata[3].compactlmd, 0) = 8;
    LMD_ADDR_START(metadata[3].compactlmd, 0) = 0;
    LMD_IS_IN_STACK(metadata[3].compactlmd) = true;
    LMD_NUM_ELE(metadata[2].compactlmd, 1) = 8;
    LMD_ADDR_START(metadata[2].compactlmd, 1) = 32;
    LMD_IS_IN_STACK(metadata[2].compactlmd) = true;
    LMD_NUM_ELE(metadata[1].compactlmd, 0) = 8;
    LMD_ADDR_START(metadata[1].compactlmd, 0) = 64;
    LMD_IS_IN_STACK(metadata[1].compactlmd) = true;

    for(unsigned int page = 0; page < 3; page++){
        for(unsigned int lane = 0; lane < _FPGA_CLS_STATES_PARTITION; lane++){
            literalStore[page*2].range(32*lane+31, 32*lane) = lane+1;
        }
    }

    undo_states_dataflow_wrapper(priorityUpdates, states, answerStack,
        metadata, literalStore, -4, 3, stackHeight, 32, 0, accessStats);

    require(stackHeight == 1,
        "incremental cleanup must preserve only the permanent root");
    require(priorityUpdates.read().data == 4,
        "incremental cleanup must first return the newest variable");
    require(priorityUpdates.read().data == 3,
        "incremental cleanup must return the middle variable");
    require(priorityUpdates.read().data == 2,
        "incremental cleanup must return the oldest search variable");
    require(priorityUpdates.empty(),
        "incremental cleanup emitted an extra priority update");

    const int expectedCompressed = 4 ^ -3 ^ 2;
    for(unsigned int lane = 0; lane < _FPGA_CLS_STATES_PARTITION; lane++){
        require(states[lane][0].remainingUnassigned == 3,
            "incremental cleanup lost an eight-way clause-state update");
        require(states[lane][0].compressedList == expectedCompressed,
            "incremental cleanup corrupted a transposed clause state");
    }
}

} // namespace

int main(){
    runEmptyTraversalCase();
    runOneClauseCase();
    runIncrementalCleanupCase();
    std::cout << "BACKTRACK_CLOSED_LOOP_COSIM_PASS\n";
    return 0;
}
