#include "learn.h"

void learnSavedCountCosim(
    const lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    const literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    const unsigned int numElements, const unsigned int numLiterals,
    unsigned int& savedCount, bool& valid){
    valid = countSavedClauseLiterals(resolutionClause, numElements,
        numLiterals, lmmd, savedCount);
}
