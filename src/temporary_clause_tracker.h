#ifndef TEMPORARY_CLAUSE_TRACKER_H
#define TEMPORARY_CLAUSE_TRACKER_H

#include <ap_axi_sdata.h>
#include <hls_stream.h>

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

inline bool emitTemporaryDeleteIDs(
    ap_uint<1> temporaryClauseMask[_FPGA_MAX_CLAUSES],
    const unsigned int declaredCount,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    unsigned int& remainingCount) {
    unsigned int emittedCount = 0;
    remainingCount = 0;

    EMIT_TEMPORARY_DELETE_IDS: for(unsigned int clauseID = 0;
            clauseID < _FPGA_MAX_CLAUSES; clauseID++) {
        #pragma HLS loop_tripcount min=0 max=131072
        #pragma HLS pipeline II=1
        if(temporaryClauseMask[clauseID] == 0) {
            continue;
        }
        if(emittedCount < declaredCount) {
            ap_axiu<96,0,0,0> idCommand;
            idCommand.data = 0;
            idCommand.data.range(31,0) = clauseID;
            clauseStoreInputStream.write(idCommand);
            temporaryClauseMask[clauseID] = 0;
            emittedCount++;
        } else {
            remainingCount++;
        }
    }

    const bool exactCount = emittedCount == declaredCount && remainingCount == 0;
    PAD_TEMPORARY_DELETE_IDS: while(emittedCount < declaredCount) {
        #pragma HLS loop_tripcount min=0 max=131072
        #pragma HLS pipeline II=1
        ap_axiu<96,0,0,0> invalidID;
        invalidID.data = 0;
        invalidID.data.range(31,0) = _FPGA_MAX_CLAUSES;
        clauseStoreInputStream.write(invalidID);
        emittedCount++;
    }
    return exactCount;
}

#endif
