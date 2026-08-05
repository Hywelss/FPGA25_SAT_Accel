#ifndef TEMPORARY_CLAUSE_TRACKER_H
#define TEMPORARY_CLAUSE_TRACKER_H

#include "data_structures.h"

inline void clearTemporaryClauseTracker(
    ap_uint<1> temporaryClauseMask[_FPGA_MAX_CLAUSES],
    unsigned int& temporaryClauseCount) {
    CLEAR_TEMPORARY_CLAUSE_MASK: for(unsigned int i = 0; i < _FPGA_MAX_CLAUSES; i++) {
        #pragma HLS pipeline II=1
        temporaryClauseMask[i] = 0;
    }
    temporaryClauseCount = 0;
}

inline bool trackTemporaryClause(
    ap_uint<1> temporaryClauseMask[_FPGA_MAX_CLAUSES],
    unsigned int& temporaryClauseCount, int clauseID) {
    if(clauseID < 0 || clauseID >= _FPGA_MAX_CLAUSES) {
        return false;
    }
    if(temporaryClauseMask[clauseID] != 0) {
        return true;
    }
    if(temporaryClauseCount >= _FPGA_MAX_CLAUSES) {
        return false;
    }
    temporaryClauseMask[clauseID] = 1;
    temporaryClauseCount++;
    return true;
}

#endif
