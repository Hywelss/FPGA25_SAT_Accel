#include <cstdlib>
#include <iostream>

#include "discover.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

colorValue makeUpdate(const cls clauseID, const lit literal, const bool clauseEnd){
    colorValue value{};
    value.clsID = 0;
    value.clsID.range(31, 0) = clauseID;
    value.litID = literal;
    value.depthCount = 0;
    value.clsEos = clauseEnd;
    value.streamEos = false;
    return value;
}

colorValue makePackedUpdate(const cls clauseIDs[8], const lit literal,
    const bool clauseEnd){
    colorValue value{};
    for(unsigned int i = 0; i < 8; i++){
        value.clsID.range(32*i+31, 32*i) = clauseIDs[i];
    }
    value.litID = literal;
    value.depthCount = 0;
    value.clsEos = clauseEnd;
    value.streamEos = false;
    return value;
}

colorValue makeStreamEnd(){
    colorValue value{};
    value.clsID = 0;
    value.litID = 0;
    value.depthCount = 0;
    value.clsEos = false;
    value.streamEos = true;
    return value;
}

void testBackToBackEndFlush(){
    static clsState statesEven[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static clsState statesOdd[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    statesEven[0].remainingUnassigned = 2;
    statesEven[0].compressedList = -1 ^ -2;

    hls::stream<colorValue> input;
    hls::stream<clsStateControlPacket> output;

    input.write(makeUpdate(1, 1, true));
    input.write(makeUpdate(1, 2, true));
    input.write(makeStreamEnd());

    updateStatesForward(output, input, statesEven, statesOdd, 0);

    int completionCount = 0;
    int exitCount = 0;
    int unitCount = 0;
    int conflictCount = 0;
    while(!output.empty()){
        const clsStateControlPacket packet = output.read();
        if(packet.eosCount == -1){
            exitCount++;
        }else if(packet.eosCount > 0){
            completionCount += packet.eosCount;
        }else if(packet.pktType == solverCode::UNIT){
            unitCount++;
        }else if(packet.pktType == solverCode::BACKTRACK){
            conflictCount++;
        }
    }

    require(completionCount == 2,
        "back-to-back final updates must report both clause completions");
    require(exitCount == 1, "each updater must emit exactly one stream exit");
    require(unitCount == 1, "the first update must produce one unit clause");
    require(conflictCount == 1, "the second update must produce one conflict");
    require(statesEven[0].remainingUnassigned == 0,
        "both state updates must be committed before exit");
}

void testEmptyLaneStillCompletes(){
    static clsState statesEven[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static clsState statesOdd[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    hls::stream<colorValue> input;
    hls::stream<clsStateControlPacket> output;

    input.write(makeUpdate(3, 1, true));
    input.write(makeStreamEnd());

    updateStatesForward(output, input, statesEven, statesOdd, 0);

    int completionCount = 0;
    int exitCount = 0;
    while(!output.empty()){
        const clsStateControlPacket packet = output.read();
        if(packet.eosCount == -1){
            exitCount++;
        }else if(packet.eosCount > 0){
            completionCount += packet.eosCount;
        }
    }

    require(completionCount == 1,
        "a clause-end marker must be acknowledged even when this lane has no clause ID");
    require(exitCount == 1, "empty lane must still emit exactly one stream exit");
}

void testBalancedSelectionAndRangeGuard(){
    static clsState statesEven[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static clsState statesOdd[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    statesEven[0] = {.compressedList=(-7 ^ -8), .remainingUnassigned=2};
    statesOdd[0] = {.compressedList=-9, .remainingUnassigned=1};

    const cls clauseIDs[8] = {
        -7, (cls)(_FPGA_MAX_CLAUSES + 1), 3, 1, 0, 2, 4, 0
    };
    hls::stream<colorValue> input;
    hls::stream<clsStateControlPacket> output;
    input.write(makePackedUpdate(clauseIDs, 7, true));
    input.write(makeStreamEnd());

    updateStatesForward(output, input, statesEven, statesOdd, 0);

    int completionCount = 0;
    int exitCount = 0;
    int unitCount = 0;
    int conflictCount = 0;
    while(!output.empty()){
        const clsStateControlPacket packet = output.read();
        if(packet.eosCount == -1){
            exitCount++;
        }else if(packet.eosCount > 0){
            completionCount += packet.eosCount;
        }else if(packet.pktType == solverCode::UNIT){
            unitCount++;
        }else if(packet.pktType == solverCode::BACKTRACK){
            conflictCount++;
        }
    }

    require(statesEven[0].remainingUnassigned == 1,
        "the valid even-partition clause must be updated exactly once");
    require(statesOdd[0].remainingUnassigned == 0,
        "the valid odd-partition clause must be updated exactly once");
    require(unitCount == 1 && conflictCount == 1,
        "all matching packed IDs must be processed in index order");
    require(completionCount == 1 && exitCount == 1,
        "invalid packed IDs must not disturb completion framing");
}

void testFinishedClauseCannotUnderflowOrRepeatConflict(){
    static clsState statesEven[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static clsState statesOdd[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    statesEven[0] = {.compressedList=0, .remainingUnassigned=0};

    hls::stream<colorValue> input;
    hls::stream<clsStateControlPacket> output;
    input.write(makeUpdate(1, 1, true));
    input.write(makeStreamEnd());
    updateStatesForward(output, input, statesEven, statesOdd, 0);

    int conflictCount = 0;
    int completionCount = 0;
    while(!output.empty()){
        const clsStateControlPacket packet = output.read();
        if(packet.pktType == solverCode::BACKTRACK && packet.eosCount == 0){
            conflictCount++;
        }else if(packet.eosCount > 0){
            completionCount += packet.eosCount;
        }
    }
    require(conflictCount == 0,
        "an already reported conflict must not be emitted a second time");
    require(completionCount == 1,
        "the finished-clause error must retain completion framing");
    require(statesEven[0].remainingUnassigned == 0,
        "a finished clause count must not underflow");
}

void testInvalidDecisionRetiresScheduledWork(){
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData lmd[_FPGA_MAX_LITERALS]{};
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};
    static cls unitByCls[_FPGA_MAX_LITERALS]{};
    hls::stream<colorAssignment> commits;

    unsigned int stackHeight = 0;
    unsigned int duplicateCount = 0;
    checkUndecided(commits, answerStack, lmd, lmmd, unitByCls,
        stackHeight, duplicateCount, 0, 0, 1, false, 0);
    require(stackHeight == 0 && commits.empty(),
        "an invalid decision must not touch the trail or start coloring");
    require(duplicateCount == 1,
        "an invalid decision must retire its scheduled propagation");

    answerStack[0] = 0;
    stackHeight = 0;
    duplicateCount = 0;
    checkUndecided(commits, answerStack, lmd, lmmd, unitByCls,
        stackHeight, duplicateCount, 1, 1, 0, false, 0);
    require(stackHeight == 1 && commits.empty(),
        "an invalid fixed literal must be skipped without an array access");
    require(duplicateCount == 1,
        "an invalid fixed literal must retain completion accounting");
}

} // namespace

int main(){
    testBackToBackEndFlush();
    testEmptyLaneStillCompletes();
    testBalancedSelectionAndRangeGuard();
    testFinishedClauseCannotUnderflowOrRepeatConflict();
    testInvalidDecisionRetiresScheduledWork();
    std::cout << "DISCOVER_PROTOCOL_TEST_PASS\n";
    return 0;
}
