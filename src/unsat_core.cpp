#include "unsat_core.h"

static bool markReasonClause(
    ap_uint<1> seen[_FPGA_MAX_LITERALS],
    const literalMetaData lmd[_FPGA_MAX_LITERALS],
    const cls clauseID, const unsigned int skipVariable,
    const unsigned int numLiterals,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    ap_axiu<96,0,0,0> request;
    request.data = 0;
    request.data.range(31,0) = clauseID-1;
    clauseStoreInputStream.write(request);

    bool valid = true;
    MARK_REASON_LITERALS: while(true){
        #pragma HLS loop_tripcount min=1 max=1024

        // A blocking AXIS read must not be scheduled past the zero terminator.
        const lit reasonLiteral = clauseStoreOutputStream.read().data;
        if(reasonLiteral == 0){
            break;
        }
        const unsigned int variable = abs(reasonLiteral);
        if(variable == 0 || variable > numLiterals){
            valid = false;
            continue;
        }
        if(variable == skipVariable){
            continue;
        }
        const literalMetaData metadata = lmd[variable-1];
        if(LMD_IS_IN_STACK(metadata.compactlmd) &&
           (unsigned int)LMD_DEC_LVL(metadata.compactlmd) > 0){
            seen[variable-1] = 1;
        }
    }
    return valid;
}

static void finishClauseReads(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2){
    #pragma HLS inline

    ap_axiu<96,0,0,0> exitCommand;
    exitCommand.data = 0;
    exitCommand.data.range(95,64) = csh::EXIT;
    clauseStoreInputStream1.write(exitCommand);
    clauseStoreInputStream2.write(exitCommand);
}

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
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1){
    #pragma HLS inline off

    if(numLiterals > _FPGA_MAX_LITERALS || numAssumptions > numLiterals ||
       answerStackHeight > numLiterals){
        return false;
    }

    ap_uint<1> seen[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=seen type=RAM_S2P impl=BRAM latency=1

    coreCount = 0;
    CLEAR_SEEN: for(unsigned int i = 0; i < numLiterals; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        #pragma HLS pipeline II=1
        seen[i] = 0;
    }
    ap_axiu<96,0,0,0> sendClauseCommand;
    sendClauseCommand.data = 0;
    sendClauseCommand.data.range(95,64) = csh::SEND_CLS;
    clauseStoreInputStream1.write(sendClauseCommand);

    if(conflictingAssumptionIndex >= 0){
        if((unsigned int)conflictingAssumptionIndex >= numAssumptions){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
        const unsigned int conflictVariable =
            abs(assumptions[conflictingAssumptionIndex]);
        if(conflictVariable == 0 || conflictVariable > numLiterals){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
        if(coreCount >= numAssumptions){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
        coreOutput[coreCount++] = assumptions[conflictingAssumptionIndex];
        seen[conflictVariable-1] = 1;
    }else{
        if(unsatClauses.head == 0 || unsatClauses.array[0] <= 0){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
        if(!markReasonClause(seen, lmd, unsatClauses.array[0], 0,
                numLiterals, clauseStoreInputStream1,
                clauseStoreOutputStream1)){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
    }

    TRACE_UNSAT_CORE: for(unsigned int offset = 0; offset < answerStackHeight; offset++){
        #pragma HLS loop_tripcount min=1 max=1024

        const lit assigned = answerStack[answerStackHeight-1-offset];
        const unsigned int variable = abs(assigned);
        if(variable == 0 || variable > numLiterals){
            finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
            return false;
        }
        if(seen[variable-1] == 0){
            continue;
        }
        seen[variable-1] = 0;

        const literalMetaData metadata = lmd[variable-1];
        const literalMinimizeMetaData minimizeMetadata = lmmd[0][variable-1];
        const unsigned int decisionLevel = LMD_DEC_LVL(metadata.compactlmd);
        if(LMMD_IS_DECIDE(minimizeMetadata.compactlmmd)){
            if(decisionLevel == 0){
                continue;
            }
            if(decisionLevel > numAssumptions){
                finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
                return false;
            }
            const unsigned int assumptionIndex = decisionLevel-1;
            if(coreCount >= numAssumptions){
                finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
                return false;
            }
            coreOutput[coreCount++] = assumptions[assumptionIndex];
        }else{
            const cls reasonClause = unitByCls[variable-1];
            if(reasonClause <= 0){
                finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
                return false;
            }
            if(!markReasonClause(seen, lmd, reasonClause, variable,
                    numLiterals, clauseStoreInputStream1,
                    clauseStoreOutputStream1)){
                finishClauseReads(clauseStoreInputStream1,
                    clauseStoreInputStream2);
                return false;
            }
        }
    }

    finishClauseReads(clauseStoreInputStream1, clauseStoreInputStream2);
    return true;
}
