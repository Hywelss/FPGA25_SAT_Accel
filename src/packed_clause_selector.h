#ifndef PACKED_CLAUSE_SELECTOR_H
#define PACKED_CLAUSE_SELECTOR_H

#include "data_structures.h"

template <bool includeNextPartition>
inline bool selectPackedClause(const ap_int<256>& packedClauseIDs,
        const unsigned int partitionID, cls& selectedClause,
        ap_uint<3>& selectedIndex){
    #pragma HLS inline

    bool matches[8];
    #pragma HLS array_partition variable=matches complete

    const ap_uint<3> firstCandidateLowBits =
        (ap_uint<3>)partitionID+1;
    const ap_uint<3> nextCandidateLowBits =
        firstCandidateLowBits+1;
    const bool hasNextPartition = includeNextPartition &&
        partitionID+1 < _FPGA_CLS_STATES_PARTITION;

    PACKED_CLAUSE_MATCHES: for(unsigned int i = 0; i < 8; i++){
        #pragma HLS unroll
        const cls candidate = packedClauseIDs.range(32*i+31, 32*i);
        const ap_uint<3> candidateLowBits = candidate;
        matches[i] = candidate != 0 &&
            (candidateLowBits == firstCandidateLowBits ||
             (hasNextPartition &&
              candidateLowBits == nextCandidateLowBits));
    }

    const bool any01 = matches[0] || matches[1];
    const bool any23 = matches[2] || matches[3];
    const bool any45 = matches[4] || matches[5];
    const bool any67 = matches[6] || matches[7];
    const bool anyLowHalf = any01 || any23;
    const bool anyHighHalf = any45 || any67;
    const bool useHighHalf = !anyLowHalf;
    const bool useSecondPair = useHighHalf ? !any45 : !any01;
    const bool selectedEven = useHighHalf
        ? (useSecondPair ? matches[6] : matches[4])
        : (useSecondPair ? matches[2] : matches[0]);

    selectedIndex = ((ap_uint<3>)useHighHalf << 2) |
        ((ap_uint<3>)useSecondPair << 1) | !selectedEven;
    const bool found = anyLowHalf || anyHighHalf;
    selectedClause = found
        ? (cls)packedClauseIDs.range(32*selectedIndex+31, 32*selectedIndex)
        : 0;
    return found;
}

#endif
