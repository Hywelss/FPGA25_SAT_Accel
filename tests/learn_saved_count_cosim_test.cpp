#include <cstdlib>
#include <iostream>
#include "learn.h"

void learnSavedCountCosim(
    const lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    const literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    const unsigned int numElements, const unsigned int numLiterals,
    unsigned int& savedCount, bool& valid);

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void clearInputs(
    lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]){
    for(unsigned int i = 0; i < _FPGA_MAX_LEARN_ELE; i++){
        resolutionClause[i] = 0;
    }
    for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
        for(unsigned int i = 0; i < _FPGA_MAX_LITERALS; i++){
            lmmd[lane][i].compactlmmd = 0;
        }
    }
}

void setKeep(literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
        unsigned int variable, bool keep){
    for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
        LMMD_MIN_KEEP(lmmd[lane][variable].compactlmmd) = keep ? 1 : 0;
    }
}

} // namespace

int main(){
    static lit resolutionClause[_FPGA_MAX_LEARN_ELE];
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS];
    unsigned int savedCount = 0;
    bool valid = false;

    clearInputs(resolutionClause, lmmd);
    resolutionClause[0] = 1;
    resolutionClause[1] = -2;
    resolutionClause[2] = 3;
    setKeep(lmmd, 0, true);
    setKeep(lmmd, 1, true);
    setKeep(lmmd, 2, true);
    LMMD_IS_IN_FIX_STACK(lmmd[0][1].compactlmmd) = 1;
    LMMD_MIN_KEEP(lmmd[1][2].compactlmmd) = 2;
    learnSavedCountCosim(resolutionClause, lmmd, 3, 3, savedCount, valid);
    require(valid && savedCount == 1,
        "mixed keep, root and lane decisions must count one literal");

    clearInputs(resolutionClause, lmmd);
    for(unsigned int i = 0; i < _FPGA_MAX_LEARN_ELE; i++){
        resolutionClause[i] = i+1;
        setKeep(lmmd, i, true);
    }
    learnSavedCountCosim(resolutionClause, lmmd, _FPGA_MAX_LEARN_ELE,
        _FPGA_MAX_LEARN_ELE, savedCount, valid);
    require(valid && savedCount == _FPGA_MAX_LEARN_ELE,
        "the hardware path must preserve the 1024-literal limit");

    resolutionClause[0] = 0;
    learnSavedCountCosim(resolutionClause, lmmd, 1,
        _FPGA_MAX_LEARN_ELE, savedCount, valid);
    require(!valid, "a zero literal must be rejected in hardware");

    resolutionClause[0] = _FPGA_MAX_LEARN_ELE+1;
    setKeep(lmmd, _FPGA_MAX_LEARN_ELE, true);
    learnSavedCountCosim(resolutionClause, lmmd, 1,
        _FPGA_MAX_LEARN_ELE, savedCount, valid);
    require(!valid, "a literal outside the active formula must be rejected");

    std::cout << "LEARN_SAVED_COUNT_COSIM_PASS\n";
    return 0;
}
