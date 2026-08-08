#include <cstdlib>
#include <iostream>

#include "backtrack.h"

void backtrackPriorityClosedLoop(
    clsState clsStates[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const lit answerStack[_FPGA_MAX_LITERALS],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    const ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    const unsigned int decisionDomain[_FPGA_MAX_LITERALS],
    unsigned int& answerStackHeight, unsigned int& protocolStatus,
    ap_uint<64> accessStats[4]);

int main(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    static unsigned int decisionDomain[_FPGA_MAX_LITERALS]{};
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};

    answerStack[0] = 1;
    answerStack[1] = -2;
    answerStack[2] = 3;
    answerStack[3] = -4;
    for(unsigned int i = 0; i < 4; i++){
        decisionDomain[i] = i+1;
        LMD_IS_IN_STACK(metadata[i].compactlmd) = true;
    }

    unsigned int stackHeight = 4;
    unsigned int protocolStatus = 99;
    backtrackPriorityClosedLoop(states, answerStack, metadata,
        literalStore, decisionDomain, stackHeight, protocolStatus,
        accessStats);

    if(stackHeight != 1 || protocolStatus != 0){
        std::cerr << "FAIL: stack=" << stackHeight
                  << " protocol=" << protocolStatus << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "BACKTRACK_PRIORITY_CLOSED_LOOP_PASS\n";
    return EXIT_SUCCESS;
}
