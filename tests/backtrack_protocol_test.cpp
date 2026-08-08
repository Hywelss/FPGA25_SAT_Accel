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

colorValue makePackedUpdate(const cls clauseIDs[8], const lit literal){
    colorValue value{};
    for(unsigned int i = 0; i < 8; i++){
        value.clsID.range(32*i+31, 32*i) = clauseIDs[i];
    }
    value.litID = literal;
    value.streamEos = false;
    return value;
}

colorValue makeStreamEnd(){
    colorValue value{};
    value.streamEos = true;
    return value;
}

} // namespace

int main(){
    static clsState states[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    states[0] = {.compressedList=10, .remainingUnassigned=0};
    states[1] = {.compressedList=20, .remainingUnassigned=1};

    const cls firstIDs[8] = {
        -1, (cls)(_FPGA_MAX_CLAUSES + 1), 2, 1, 0, 4, 9, 0
    };
    const cls secondIDs[8] = {1, 0, 0, 0, 0, 0, 0, 0};

    hls::stream<colorValue> input;
    input.write(makePackedUpdate(firstIDs, 3));
    input.write(makePackedUpdate(secondIDs, -5));
    input.write(makeStreamEnd());

    updateStatesBackward(input, states, 0);

    require(input.empty(), "the backward updater must consume its terminator");
    require(states[0].remainingUnassigned == 2,
        "two updates to clause 1 must both be committed");
    require(states[0].compressedList == (10 ^ -3 ^ 5),
        "same-address bypass must preserve both compressed-list updates");
    require(states[1].remainingUnassigned == 2,
        "the second valid partition-0 clause must be updated once");
    require(states[1].compressedList == (20 ^ -3),
        "packed IDs must be processed without losing later matches");

    std::cout << "BACKTRACK_PROTOCOL_TEST_PASS\n";
    return 0;
}
