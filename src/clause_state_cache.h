#ifndef CLAUSE_STATE_CACHE_H
#define CLAUSE_STATE_CACHE_H

#include "data_structures.h"

inline clsState selectNewestClauseState(const unsigned int address,
        const clsState memoryState,
        const unsigned int cacheAddress[_FPGA_CLS_DEP_DIST],
        const clsState cacheState[_FPGA_CLS_DEP_DIST]){
    #pragma HLS inline

    static_assert(_FPGA_CLS_DEP_DIST == 5,
        "balanced clause-state cache selector requires five entries");

    const bool match0 = address == cacheAddress[0];
    const bool match1 = address == cacheAddress[1];
    const bool match2 = address == cacheAddress[2];
    const bool match3 = address == cacheAddress[3];
    const bool match4 = address == cacheAddress[4];

    const clsState pair01 = match1 ? cacheState[1] : cacheState[0];
    const clsState pair23 = match3 ? cacheState[3] : cacheState[2];
    const bool any01 = match0 || match1;
    const bool any23 = match2 || match3;

    const clsState lower = any01 ? pair01 : memoryState;
    const clsState upper = match4 ? cacheState[4] : pair23;
    return (match4 || any23) ? upper : lower;
}

#endif
