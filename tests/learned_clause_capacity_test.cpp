#include <cassert>

#include "learn.h"

int main(){
    static lit resolutionClause[_FPGA_MAX_LEARN_ELE];
    static ap_uint<_FPGA_MAX_LEARN_ELE_BITS + 2>
        mergeScratchPad[_FPGA_MAX_LITERALS];
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS / 512];

    hls::stream<lit> clauseInput;
    hls::stream<lit_resolve> updates;
    for(unsigned int variable = 1;
            variable <= _FPGA_MAX_LEARN_ELE + 1; variable++){
        clauseInput.write(variable);
    }
    clauseInput.write(0);

    unsigned int numElements = 0;
    bool clauseTooLong = false;
    ap_uint<64> learnedStats = 0;
    merge_resolution_sort(updates, clauseInput, resolutionClause,
        mergeScratchPad, validBit, numElements, clauseTooLong, learnedStats);

    assert(clauseTooLong);
    assert(numElements == _FPGA_MAX_LEARN_ELE);
    assert(resolutionClause[0] == 1);
    assert(resolutionClause[_FPGA_MAX_LEARN_ELE - 1] ==
        static_cast<int>(_FPGA_MAX_LEARN_ELE));
    assert(clauseInput.empty());

    unsigned int updateCount = 0;
    while(!updates.empty()){
        const lit_resolve update = updates.read();
        if(update.literal != 0){
            updateCount++;
        }
    }
    assert(updateCount == _FPGA_MAX_LEARN_ELE);
}
