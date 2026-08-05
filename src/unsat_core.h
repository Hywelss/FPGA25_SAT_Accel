#ifndef UNSAT_CORE_H
#define UNSAT_CORE_H

#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "data_structures.h"

bool extractUnsatCore(
    lit coreOutput[_FPGA_MAX_LITERALS], unsigned int& coreCount,
    const lit assumptions[_FPGA_MAX_LITERALS], unsigned int numAssumptions,
    const lit answerStack[_FPGA_MAX_LITERALS], unsigned int answerStackHeight,
    const literalMetaData lmd[_FPGA_MAX_LITERALS],
    const literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    const cls unitByCls[_FPGA_MAX_LITERALS],
    const myStream<cls,64,7>& unsatClauses, int conflictingAssumptionIndex,
    unsigned int numLiterals,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1);

#endif
