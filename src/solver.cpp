#include "fpga_solver.h"
#include "discover.h"
#include "learn.h"
#include "backtrack.h"
#include "copy_in.h"
#include "fixed_literal.h"
#include "manage.h"
#include "temporary_clause_tracker.h"
#include "unsat_core.h"

void sendTime(hls::stream<ap_axiu<64,0,0,0>>& timerValueStream, hls::stream<ap_axiu<1,0,0,0>>& conditionStream, 
    const unsigned int code, volatile uint64_t* store){
    #pragma HLS inline off
    IO_WAIT:{
        ap_axiu<1,0,0,0> pktTimer;
        ap_axiu<64,0,0,0> value;

        pktTimer.data = code;
        conditionStream.write(pktTimer);

        if(code == 1){
            value = timerValueStream.read();
            *store = value.data;
        }
    }
}

void copyStats(ap_uint<64>* learnedStats, ap_uint<64>* longestClause, ap_uint<64>* litStoreAccessStats, ap_uint<64>* cycleCounter, int* miscCounters){
    #pragma HLS inline off

    int copyMe[48];
    copyMe[0] = learnedStats[0].range(31,0);
    copyMe[1] = learnedStats[0].range(63,32);
    copyMe[2] = learnedStats[1].range(31,0);
    copyMe[3] = learnedStats[1].range(63,32);
    copyMe[4] = learnedStats[2].range(31,0);
    copyMe[5] = learnedStats[2].range(63,32);
    copyMe[6] = learnedStats[3].range(31,0);
    copyMe[7] = learnedStats[3].range(63,32);
    copyMe[8] = learnedStats[4].range(31,0);
    copyMe[9] = learnedStats[4].range(63,32);

    copyMe[10] = longestClause[0].range(31,0);
    copyMe[11] = longestClause[0].range(63,32);
    copyMe[12] = longestClause[1].range(31,0);
    copyMe[13] = longestClause[1].range(63,32);
    copyMe[14] = checkCnt;

    copyMe[15] = litStoreAccessStats[0].range(31,0);
    copyMe[16] = litStoreAccessStats[0].range(63,32);
    copyMe[17] = litStoreAccessStats[1].range(31,0);
    copyMe[18] = litStoreAccessStats[1].range(63,32);

    copyMe[19] = litStoreAccessStats[2].range(31,0);
    copyMe[20] = litStoreAccessStats[2].range(63,32);
    copyMe[21] = litStoreAccessStats[3].range(31,0);
    copyMe[22] = litStoreAccessStats[3].range(63,32);

    copyMe[23] = cycleCounter[0].range(31,0);
    copyMe[24] = cycleCounter[0].range(63,32);

    copyMe[25] = cycleCounter[1].range(31,0);
    copyMe[26] = cycleCounter[1].range(63,32);

    copyMe[27] = cycleCounter[2].range(31,0);
    copyMe[28] = cycleCounter[2].range(63,32);

    copyMe[29] = cycleCounter[3].range(31,0);
    copyMe[30] = cycleCounter[3].range(63,32);

    copyMe[31] = cycleCounter[4].range(31,0);
    copyMe[32] = cycleCounter[4].range(63,32);

    copyMe[33] = cycleCounter[5].range(31,0);
    copyMe[34] = cycleCounter[5].range(63,32);

    copyMe[35] = cycleCounter[6].range(31,0);
    copyMe[36] = cycleCounter[6].range(63,32);

    copyMe[37] = cycleCounter[7].range(31,0);
    copyMe[38] = cycleCounter[7].range(63,32);

    copyMe[39] = cycleCounter[8].range(31,0);
    copyMe[40] = cycleCounter[8].range(63,32);

    copyMe[41] = overhead;
    copyMe[42] = splitResidualCnt;

    memcpy(miscCounters+7, copyMe, sizeof(int) * 43);
}

bool installQueryClauses(const lit* queryClauseStore,
    const clauseMetaData* queryCmd, unsigned int firstClause, unsigned int endClause,
    unsigned int permanentClauseCount,
    clsState clsStates[_FPGA_CLS_STATES_PARTITION][_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    lit answerStack[_FPGA_MAX_LITERALS], unsigned int& fixedDecisionStackHeight,
    unsigned int& permanentRootCount, bool& permanentFormulaUnsat,
    mmuStream<unsigned int, _MAX_PAGES_LIT_STORE_>& freeLitPageAddresses,
    ap_uint<1> temporaryClauseMask[_FPGA_MAX_CLAUSES], unsigned int& temporaryClauseCount,
    unsigned int numLiterals, unsigned int literalPageSize, int& error,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1){
    #pragma HLS inline off

    bool queryUnsat = false;
    INSTALL_QUERY_CLAUSES: for(unsigned int sourceID = firstClause; sourceID < endClause; sourceID++){
        #pragma HLS loop_tripcount min=0 max=1024
        const clauseMetaData source = queryCmd[sourceID];
        if(source.numElements > _FPGA_MAX_LEARN_ELE){
            error = -2;
            return false;
        }

        lit sourceClause[_FPGA_MAX_LEARN_ELE];
        #pragma HLS bind_storage variable=sourceClause type=RAM_S2P impl=auto latency=1
        lit reducedClause[_FPGA_MAX_LEARN_ELE];
        #pragma HLS bind_storage variable=reducedClause type=RAM_S2P impl=auto latency=1
        READ_QUERY_CLAUSE: for(unsigned int i = 0; i < source.numElements; i++){
            #pragma HLS loop_tripcount min=1 max=1024
            #pragma HLS pipeline II=1
            sourceClause[i] = queryClauseStore[source.addressStart + i];
        }

        unsigned int clauseLength = 0;
        bool satisfiedAtRoot = false;
        REDUCE_QUERY_CLAUSE: for(unsigned int i = 0; i < source.numElements; i++){
            #pragma HLS loop_tripcount min=1 max=1024
            #pragma HLS pipeline II=1
            const lit literal = sourceClause[i];
            if(literal == 0 || literal > (lit)numLiterals ||
                    literal < -(lit)numLiterals){
                error = -6;
                return false;
            }
            const literalMetaData metadata = lmd[abs(literal)-1];
            if(LMD_IS_IN_STACK(metadata.compactlmd) && LMD_DEC_LVL(metadata.compactlmd) == 0){
                const lit assigned = answerStack[(unsigned int)LMD_INSERT_LVL(metadata.compactlmd)];
                if(assigned == literal){
                    satisfiedAtRoot = true;
                }
            }else{
                reducedClause[clauseLength++] = literal;
            }
        }
        if(satisfiedAtRoot){
            continue;
        }
        if(clauseLength == 0){
            queryUnsat = true;
            if(sourceID < permanentClauseCount){
                permanentFormulaUnsat = true;
            }
            continue;
        }

        if(clauseLength == 1){
            const FixedLiteralStatus fixedStatus = classifyFixedLiteral(
                answerStack, fixedDecisionStackHeight, numLiterals,
                reducedClause[0]);
            if(fixedStatus == FIXED_LITERAL_ALREADY_ASSIGNED){
                continue;
            }
            if(fixedStatus == FIXED_LITERAL_CONFLICT){
                queryUnsat = true;
                if(sourceID < permanentClauseCount){
                    permanentFormulaUnsat = true;
                }
                continue;
            }
            if(fixedStatus == FIXED_LITERAL_INVALID){
                error = -6;
                return false;
            }
        }

        const bool isTemporary = sourceID >= permanentClauseCount;
        if(isTemporary && temporaryClauseCount >= _FPGA_MAX_CLAUSES){
            error = -6;
            return false;
        }

        ap_axiu<96,0,0,0> saveCommand;
        saveCommand.data = 0;
        saveCommand.data.range(31,0) = clauseLength;
        saveCommand.data.range(95,64) = csh::SAVE;
        clauseStoreInputStream1.write(saveCommand);
        const int givenClauseID = clauseStoreOutputStream1.read().data;
        if(givenClauseID < 0){
            error = givenClauseID;
            return false;
        }

        hls::stream<lit> newPages;
        #pragma HLS stream variable=newPages depth=_FPGA_MAX_LEARN_ELE
        clsState newClauseState = {.compressedList=0, .remainingUnassigned=0};
        saveQueryClauseDataflow(newPages, litStore, lmd, newClauseState,
            reducedClause, clauseLength, givenClauseID, literalPageSize,
            clauseStoreInputStream1);
        newClauseState.remainingUnassigned = clauseLength;
        clsStates[givenClauseID%_FPGA_CLS_STATES_PARTITION]
            [givenClauseID/_FPGA_CLS_STATES_PARTITION] = newClauseState;
        if(!newPages.empty()){
            allocatePage(newPages, freeLitPageAddresses, lmd, litStore, error, literalPageSize);
            if(error < 0){
                return false;
            }
        }

        if(isTemporary){
            if(!trackTemporaryClause(temporaryClauseMask, temporaryClauseCount,
                    givenClauseID)){
                error = -6;
                return false;
            }
        }
        if(clauseLength == 1){
            if(fixedDecisionStackHeight >= numLiterals){
                error = -6;
                return false;
            }
            answerStack[fixedDecisionStackHeight++] = reducedClause[0];
            if(!isTemporary){
                permanentRootCount++;
            }
        }
    }
    return queryUnsat;
}

extern "C"{
void solver(const unsigned int* decision_domain, int num_domain_literals,
    const lit* assumptions, const lit* queryClauseStore,
    const clauseMetaData* queryCmd,
    clsStatePCIE* clsStates, ap_int<512>* litStore, lit* answerStack,
    literalMetaDataPCIE* lmd, int* miscCounters,
    hls::stream<ap_axiu<32,0,0,0>>& pqHandlerValue, hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2,
     hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream,
    hls::stream<ap_axiu<32,0,0,0>>& restartValueStream, hls::stream<ap_axiu<1,0,0,0>>& stopStream,
    hls::stream<ap_axiu<64,0,0,0>>& timerValueStream, hls::stream<ap_axiu<1,0,0,0>>& conditionStream,
    hls::stream<ap_axiu<96,0,0,0>>& messageStream){

    #pragma HLS INTERFACE m_axi port=decision_domain offset=slave bundle=gmem7 latency=40
    #pragma HLS INTERFACE m_axi port=assumptions offset=slave bundle=gmem7 latency=40
    #pragma HLS INTERFACE m_axi port=queryClauseStore offset=slave bundle=gmem7 latency=40
    #pragma HLS INTERFACE m_axi port=queryCmd offset=slave bundle=gmem7 latency=40
    #pragma HLS INTERFACE m_axi port=clsStates offset=slave bundle=gmem5 latency=40
    #pragma HLS INTERFACE m_axi port=litStore offset=slave bundle=gmemLitStore1 latency=40
    #pragma HLS INTERFACE m_axi port=answerStack offset=slave bundle=gmem7 latency=40
    #pragma HLS INTERFACE m_axi port=lmd offset=slave bundle=gmem10 latency=40
    #pragma HLS INTERFACE m_axi port=miscCounters offset=slave bundle=gmem7 latency=40

    #pragma HLS INTERFACE axis port=pqHandlerValue
    #pragma HLS INTERFACE axis port=pqHandlerInput
    #pragma HLS INTERFACE axis port=restartValueStream
    #pragma HLS INTERFACE axis port=stopStream
    #pragma HLS INTERFACE axis port=timerValueStream
    #pragma HLS INTERFACE axis port=conditionStream
    #pragma HLS INTERFACE axis port=messageStream

    #pragma HLS INTERFACE s_axilite port=decision_domain
    #pragma HLS INTERFACE s_axilite port=num_domain_literals
    #pragma HLS INTERFACE s_axilite port=assumptions
    #pragma HLS INTERFACE s_axilite port=queryClauseStore
    #pragma HLS INTERFACE s_axilite port=queryCmd
    #pragma HLS INTERFACE s_axilite port=clsStates
	#pragma HLS INTERFACE s_axilite port=litStore
    #pragma HLS INTERFACE s_axilite port=answerStack 
	#pragma HLS INTERFACE s_axilite port=lmd
	#pragma HLS INTERFACE s_axilite port=miscCounters

	#pragma HLS INTERFACE s_axilite port=return

    unsigned int literalElements = miscCounters[0];
    unsigned int numClauses = miscCounters[1];
    const unsigned int NUM_LITERALS = miscCounters[2];
    const ap_uint<1> POSITIVE_LIT_PHASE_VAL = miscCounters[4];
    const unsigned int MAX_LITERAL_ELEMENTS = miscCounters[5];
    const unsigned int LITERAL_PAGE_SIZE = miscCounters[6];
    const unsigned int RESET_MULTIPLIER = miscCounters[7];
    const unsigned int NUM_PERMANENT_CLAUSES = miscCounters[8];
    const unsigned int NUM_TEMPORARY_CLAUSES = miscCounters[9];
    const unsigned int NUM_ASSUMPTIONS = miscCounters[10];
    const bool SESSION_RESET = miscCounters[12] != 0;
    const bool INITIAL_ROOT_CONFLICT = miscCounters[15] != 0;
    miscCounters[50] = 0;

    static lit mAnswerStack[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=mAnswerStack type=RAM_S2P impl=BRAM latency=1

    static ap_uint<512> mLitStore[_FPGA_MAX_LITERAL_ELEMENTS/16];
    #pragma HLS bind_storage variable=mLitStore type=RAM_S2P impl=URAM latency=1

    static literalMetaData mlmd[_FPGA_MAX_LITERALS];
    #pragma HLS aggregate variable=mlmd compact=auto
    #pragma HLS bind_storage variable=mlmd type=RAM_S2P impl=URAM latency=1

    cls unitByCls[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=unitByCls type=RAM_T2P impl=auto latency=1

    static literalMinimizeMetaData mlmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS];
    #pragma HLS array_partition variable=mlmmd dim=1 complete

    static bool mInDomain[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=mInDomain type=RAM_S2P impl=BRAM latency=1

    static mmuStream<unsigned int, _MAX_PAGES_LIT_STORE_> freeLitPageAddresses;
    #pragma HLS bind_storage variable=freeLitPageAddresses.array type=RAM_S2P impl=URAM

    lit literalCommit;

    static clsState mClsStates[_FPGA_CLS_STATES_PARTITION][_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION];
    #pragma HLS aggregate variable=mClsStates compact=auto
    #pragma HLS array_partition variable=mClsStates dim=1 complete
    #pragma HLS bind_storage variable=mClsStates type=RAM_S2P impl=URAM latency=2

    ap_uint<64> cycleCounter[9] = {0,0,0,0,0,0,0,0,0};
    #pragma HLS array_partition variable=cycleCounter dim=0 complete
    volatile uint64_t store[2];

    static ap_uint<1> temporaryClauseMask[_FPGA_MAX_CLAUSES];
    #pragma HLS bind_storage variable=temporaryClauseMask type=RAM_S2P impl=BRAM latency=1
    static unsigned int temporaryClauseCount = 0;
    static unsigned int answerStackHeight = 0;
    static unsigned int fixedDecisionStackHeight = 0;
    static unsigned int permanentRootCount = 0;
    static bool permanentFormulaUnsat = false;
    static int decisionLevel = 1;
    static bool firstIteration = false;
    const bool validLiteralPageSize = LITERAL_PAGE_SIZE >= 16 &&
        LITERAL_PAGE_SIZE <= _FPGA_MAX_LITERAL_ELEMENTS &&
        LITERAL_PAGE_SIZE%16 == 0;
    const uint64_t declaredClauseCount =
        (uint64_t)NUM_PERMANENT_CLAUSES + NUM_TEMPORARY_CLAUSES;
    const bool validQueryRange = declaredClauseCount <= UINT_MAX &&
        miscCounters[13] <= declaredClauseCount;
    const bool validConfiguration = literalElements <= _FPGA_MAX_LITERAL_ELEMENTS &&
        numClauses <= _FPGA_MAX_CLAUSES && NUM_LITERALS <= _FPGA_MAX_LITERALS &&
        num_domain_literals >= 0 &&
        (unsigned int)num_domain_literals <= NUM_LITERALS &&
        MAX_LITERAL_ELEMENTS <= _FPGA_MAX_LITERAL_ELEMENTS &&
        literalElements <= MAX_LITERAL_ELEMENTS && validLiteralPageSize &&
        NUM_PERMANENT_CLAUSES <= _FPGA_MAX_CLAUSES &&
        NUM_TEMPORARY_CLAUSES <= _FPGA_MAX_CLAUSES-NUM_PERMANENT_CLAUSES &&
        declaredClauseCount <= numClauses &&
        NUM_ASSUMPTIONS <= NUM_LITERALS && miscCounters[3] <= NUM_LITERALS &&
        validQueryRange;
    bool initialTrackerError = !validConfiguration;

    sendTime(timerValueStream, conditionStream, 1, &store[0]);
    if(SESSION_RESET && validConfiguration){
        freeLitPageAddresses.reset(literalElements,_FPGA_MAX_LITERAL_ELEMENTS,LITERAL_PAGE_SIZE);
        copy_in_dataflow_wrapper(mClsStates, mLitStore, mlmd, mlmmd,
            mAnswerStack, clsStates, litStore, lmd, answerStack,
            literalElements, numClauses, NUM_LITERALS, store);
        copy_domain(mInDomain, decision_domain, NUM_LITERALS, num_domain_literals);
        answerStackHeight = 0;
        fixedDecisionStackHeight = miscCounters[3];
        permanentRootCount = miscCounters[3];
        permanentFormulaUnsat = INITIAL_ROOT_CONFLICT;
        decisionLevel = (fixedDecisionStackHeight == 0) ? 1 : 0;
        firstIteration = false;
        clearTemporaryClauseTracker(temporaryClauseMask, temporaryClauseCount);
        INITIAL_TEMPORARY_IDS: for(unsigned int i = 0;
                i < NUM_TEMPORARY_CLAUSES && !initialTrackerError; i++){
            #pragma HLS loop_tripcount min=0 max=1024
            if(!trackTemporaryClause(temporaryClauseMask, temporaryClauseCount,
                    NUM_PERMANENT_CLAUSES + i)){
                initialTrackerError = true;
            }
        }
    }else if(validConfiguration){
        copy_domain(mInDomain, decision_domain, NUM_LITERALS, num_domain_literals);
    }

    if(answerStackHeight > NUM_LITERALS ||
            fixedDecisionStackHeight > NUM_LITERALS ||
            permanentRootCount > NUM_LITERALS ||
            fixedDecisionStackHeight < permanentRootCount){
        initialTrackerError = true;
    }

    sendTime(timerValueStream, conditionStream, 1, &store[1]);
    cycleCounter[0] += store[1]-store[0];
    
    overhead = 0;
    splitResidualCnt = 0;
    checkCnt = 0;

    bool doBackTrack = false;
    
    myStream<cls,64,7> unsatClauses;
    unsatClauses.head = 0;
    unsatClauses.tail = 0;

    double multiplier = 1.0;

    flippedLiteral litToCheck;
    bool useFlipped = false;
    unsigned int totalCount = 0;
    unsigned int decideCount = 0;
    unsigned int retryCount = 0;
    unsigned int backtrackCount = 0;
    unsigned int resetCount = 0;

    unsigned int restartCount = 1;
    unsigned int limit = RESET_MULTIPLIER;
    unsigned int limitCount = 0;

    ap_uint<64> learnedStats[5] = {0,0,0,0,0};
    #pragma HLS array_partition variable=learnedStats dim=0 complete
    ap_uint<64> longestClause[2] = {0,0};
    #pragma HLS array_partition variable=longestClause dim=0 complete
    ap_uint<64> litStoreAccessStats[4] = {0,0,0,0};
    #pragma HLS array_partition variable=litStoreAccessStats dim=0 complete

    int setupStatus = initialTrackerError ? -6 : (permanentFormulaUnsat ? -7 : 0);
    if(!SESSION_RESET){
        if(answerStackHeight > permanentRootCount){
            ap_axiu<32,0,0,0> pqCommand;
            pqCommand.data = pq::UNHIDE_ELE;
            pqHandlerInput.write(pqCommand);
            const bool cleanupOk = cleanupQueryAssignments(pqHandlerInput,
                mClsStates, mAnswerStack, mlmd, mLitStore,
                permanentRootCount, answerStackHeight, LITERAL_PAGE_SIZE,
                POSITIVE_LIT_PHASE_VAL, litStoreAccessStats);
            if(!cleanupOk){
                setupStatus = -6;
            }
            pqCommand.data = pq::EXIT;
            pqHandlerInput.write(pqCommand);
        }
        fixedDecisionStackHeight = permanentRootCount;
        if(temporaryClauseCount > _FPGA_MAX_CLAUSES){
            setupStatus = -6;
        }else if(temporaryClauseCount > 0){
            const unsigned int declaredTemporaryCount = temporaryClauseCount;
            ap_axiu<96,0,0,0> deleteCommand;
            deleteCommand.data = 0;
            deleteCommand.data.range(31,0) = declaredTemporaryCount;
            deleteCommand.data.range(95,64) = csh::DELETE_IDS;
            clauseStoreInputStream1.write(deleteCommand);

            const unsigned int acceptedDeleteCount =
                clauseStoreOutputStream1.read().data;
            unsigned int emittedDeleteCount = 0;
            unsigned int remainingTemporaryCount = 0;
            EMIT_AND_DELETE_TEMPORARY_CLAUSES: for(unsigned int clauseID = 0;
                    clauseID < _FPGA_MAX_CLAUSES; clauseID++){
                #pragma HLS loop_tripcount min=0 max=131072
                if(temporaryClauseMask[clauseID] == 0){
                    continue;
                }
                if(emittedDeleteCount < acceptedDeleteCount){
                    ap_axiu<96,0,0,0> idCommand;
                    idCommand.data = 0;
                    idCommand.data.range(31,0) = clauseID;
                    clauseStoreInputStream2.write(idCommand);
                    deleteOneTransposedClause(mLitStore, mlmd,
                        freeLitPageAddresses, LITERAL_PAGE_SIZE,
                        clauseStoreInputStream1, clauseStoreOutputStream1,
                        locationOutputStream);
                    temporaryClauseMask[clauseID] = 0;
                    emittedDeleteCount++;
                }else{
                    remainingTemporaryCount++;
                }
            }
            PAD_AND_DELETE_TEMPORARY_CLAUSES: while(
                    emittedDeleteCount < acceptedDeleteCount){
                #pragma HLS loop_tripcount min=0 max=131072
                ap_axiu<96,0,0,0> invalidID;
                invalidID.data = 0;
                invalidID.data.range(31,0) = _FPGA_MAX_CLAUSES;
                clauseStoreInputStream2.write(invalidID);
                deleteOneTransposedClause(mLitStore, mlmd,
                    freeLitPageAddresses, LITERAL_PAGE_SIZE,
                    clauseStoreInputStream1, clauseStoreOutputStream1,
                    locationOutputStream);
                emittedDeleteCount++;
            }
            temporaryClauseCount = remainingTemporaryCount;
            if(acceptedDeleteCount != declaredTemporaryCount ||
                    emittedDeleteCount != declaredTemporaryCount ||
                    remainingTemporaryCount != 0){
                setupStatus = -6;
            }
        }
        if(setupStatus == 0){
            int setupError = 0;
            const bool queryUnsat = installQueryClauses(queryClauseStore, queryCmd,
                miscCounters[13], (unsigned int)declaredClauseCount,
                NUM_PERMANENT_CLAUSES,
                mClsStates, mLitStore, mlmd, mAnswerStack,
                fixedDecisionStackHeight, permanentRootCount, permanentFormulaUnsat,
                freeLitPageAddresses, temporaryClauseMask, temporaryClauseCount,
                NUM_LITERALS, LITERAL_PAGE_SIZE, setupError,
                clauseStoreInputStream1, clauseStoreOutputStream1);
            if(setupError < 0){
                setupStatus = setupError;
            }else if(queryUnsat){
                setupStatus = -7;
            }
        }
        const bool hasNewPermanentRoots = fixedDecisionStackHeight > answerStackHeight;
        decisionLevel = hasNewPermanentRoots ? 0 : 1;
        firstIteration = !hasNewPermanentRoots;
    }

    ap_axiu<96,0,0,0> messageValue;

    if(setupStatus != 0){
        miscCounters[0] = 0;
        miscCounters[1] = 0;
        miscCounters[2] = 0;
        miscCounters[3] = 0;
        miscCounters[4] = 0;
        miscCounters[5] = answerStackHeight;
        miscCounters[6] = 0;
        copyStats(learnedStats, longestClause, litStoreAccessStats, cycleCounter, miscCounters);

        ap_axiu<1,0,0,0> stopPacket;
        stopPacket.data = 1;
        stopStream.write(stopPacket);
        ap_axiu<32,0,0,0> pqCommand;
        pqCommand.data = pq::EXIT;
        pqHandlerInput.write(pqCommand);
        FLUSH_RESTART_SETUP: while(true){
            #pragma HLS loop_tripcount min=16 max=16
            if(restartValueStream.read().data == 0){
                break;
            }
        }
        sendTime(timerValueStream, conditionStream, 0, nullptr);
        messageValue.data.range(95,64) = 0;
        messageValue.data.range(63,32) = 0;
        messageValue.data.range(31,0) = setupStatus == -7 ? -1 : setupStatus;
        messageStream.write(messageValue);
        ap_axiu<96,0,0,0> clauseCommand;
        clauseCommand.data = 0;
        clauseCommand.data.range(95,64) = csh::EXIT;
        clauseStoreInputStream1.write(clauseCommand);
        return;
    }

    if(fixedDecisionStackHeight != 0){
        ap_axiu<32,0,0,0> pqCommand;
        pqCommand.data = pq::HIDE_ELE;
        pqHandlerInput.write(pqCommand);
        HIDE_FIXED_DECISIONS: for(unsigned int i = 0; i < fixedDecisionStackHeight; i++){
            #pragma HLS loop_tripcount min=1 max=1024
            pqCommand.data = abs(mAnswerStack[i]);
            pqHandlerInput.write(pqCommand);
        }
        pqCommand.data = pq::EXIT;
        pqHandlerInput.write(pqCommand);
    }

    SOLVE_ITERATION: while(true){
        ap_axiu<32,0,0,0> sendPQHandler;

        #pragma HLS loop_tripcount min=1024 max=1024
        totalCount++;

        volatile uint64_t store[2];

        messageValue.data.range(95,64) = 1;
        messageValue.data.range(63,32) = totalCount;
        messageValue.data.range(31,0) = 0;
        messageStream.write(messageValue);

        if(!doBackTrack){
            if(useFlipped){
                retryCount++;
            }else{
                decideCount++;
            }

            messageValue.data.range(95,64) = 2;
            messageValue.data.range(63,32) = decideCount+retryCount;
            messageValue.data.range(31,0) = 1;
            messageStream.write(messageValue);

            lit topLiteral = 0;
                bool domainExhausted = false;
                bool forceLiteralPhase = false;
                bool assumptionConflict = false;
                bool pqSearchStarted = false;

            if(!useFlipped && decisionLevel != 0){
                sendTime(timerValueStream, conditionStream, 1, &store[0]);

                
                literalMetaData getLmd;
                LMD_IS_IN_STACK(getLmd.compactlmd) = true;

                if((unsigned int)decisionLevel <= NUM_ASSUMPTIONS){
                    topLiteral = assumptions[decisionLevel-1];
                    const bool validAssumption = topLiteral != 0 &&
                        topLiteral <= (lit)NUM_LITERALS &&
                        topLiteral >= -(lit)NUM_LITERALS;
                    if(!validAssumption){
                        assumptionConflict = true;
                    }else{
                        getLmd = mlmd[abs(topLiteral)-1];
                    }
                    if(validAssumption && LMD_IS_IN_STACK(getLmd.compactlmd)){
                        const lit assigned = mAnswerStack[(unsigned int)LMD_INSERT_LVL(getLmd.compactlmd)];
                        if(assigned != topLiteral){
                            assumptionConflict = true;
                        }else{
                            if((unsigned int)decisionLevel < _FPGA_MAX_LITERALS){
                                mlmd[decisionLevel].decisionLevelStackEnd = answerStackHeight;
                            }
                            sendTime(timerValueStream, conditionStream, 1, &store[1]);
                            cycleCounter[1] += store[1]-store[0];
                            decisionLevel++;
                            continue;
                        }
                    }
                    if(!assumptionConflict){
                        forceLiteralPhase = true;
                        ap_axiu<32,0,0,0> assumptionCommand;
                        assumptionCommand.data = pq::HIDE_ELE;
                        pqHandlerInput.write(assumptionCommand);
                        assumptionCommand.data = abs(topLiteral);
                        pqHandlerInput.write(assumptionCommand);
                        assumptionCommand.data = pq::EXIT;
                        pqHandlerInput.write(assumptionCommand);
                    }
                }
                
                FIND_TOP: while(!forceLiteralPhase && !doBackTrack){
                    #pragma HLS loop_tripcount min=16 max=16
                    if(!LMD_IS_IN_STACK(getLmd.compactlmd)){
                        break;
                    }
                    sendPQHandler.data = pq::GET_UNDECIDED;
                    pqHandlerInput.write(sendPQHandler);
                    pqSearchStarted = true;
                                
                    ap_wait();
                    
                    ap_axiu<32,0,0,0> getPQHandler = pqHandlerValue.read();

                    topLiteral = getPQHandler.data;
                    if(topLiteral == pq::DOMAIN_EXHAUSTED){
                        domainExhausted = true;
                        break;
                    }
                    if(topLiteral <= 0 || (unsigned int)topLiteral > NUM_LITERALS){
                        // Enter the normal learning-error cleanup path without
                        // indexing metadata with an invalid queue result.
                        domainExhausted = true;
                        doBackTrack = true;
                        break;
                    }
                    getLmd = mlmd[topLiteral-1];     
                }
                if(pqSearchStarted){
                    sendPQHandler.data = pq::EXIT;
                    pqHandlerInput.write(sendPQHandler);
                }
                
                sendTime(timerValueStream, conditionStream, 1, &store[1]);
                cycleCounter[1] += store[1]-store[0];
            }

            if(assumptionConflict){
                unsigned int coreCount = 0;
                const bool coreExtracted = extractUnsatCore(answerStack, coreCount,
                    assumptions, NUM_ASSUMPTIONS, mAnswerStack,
                    answerStackHeight, mlmd, mlmmd, unitByCls, unsatClauses,
                    decisionLevel-1, NUM_LITERALS, clauseStoreInputStream1,
                    clauseStoreInputStream2, clauseStoreOutputStream1);
                miscCounters[0] = totalCount;
                miscCounters[1] = decideCount;
                miscCounters[2] = retryCount;
                miscCounters[3] = backtrackCount;
                miscCounters[4] = resetCount;
                miscCounters[5] = answerStackHeight;
                miscCounters[6] = 0;
                miscCounters[50] = coreCount;
                if(NUM_TEMPORARY_CLAUSES == 0 && coreExtracted && coreCount == 0){
                    permanentFormulaUnsat = true;
                }
                copyStats(learnedStats, longestClause, litStoreAccessStats, cycleCounter, miscCounters);

                ap_axiu<1,0,0,0> pkt;
                pkt.data = 1;
                stopStream.write(pkt);
                sendPQHandler.data = pq::EXIT;
                pqHandlerInput.write(sendPQHandler);
                FLUSH_RESTART_ASSUMPTION: while(true){
                    #pragma HLS loop_tripcount min=16 max=16
                    if(restartValueStream.read().data == 0){
                        break;
                    }
                }
                sendTime(timerValueStream, conditionStream, 0, nullptr);
                messageValue.data.range(95,64) = 0;
                messageValue.data.range(63,32) = 0;
                messageValue.data.range(31,0) = coreExtracted ? -1 : -8;
                messageStream.write(messageValue);
                ap_axiu<96,0,0,0> sendClauseInputCommand;
                sendClauseInputCommand.data = 0;
                sendClauseInputCommand.data.range(95,64) = csh::EXIT;
                clauseStoreInputStream1.write(sendClauseInputCommand);
                return;
            }

            if(!domainExhausted){
                sendTime(timerValueStream, conditionStream, 1, &store[0]);

                ap_axiu<96,0,0,0> sendClauseInputCommand;
                sendClauseInputCommand.data = 0;
                sendClauseInputCommand.data.range(31,0) = 0;
                sendClauseInputCommand.data.range(95,64) = csh::SEND_LEN_BCP;
                clauseStoreInputStream1.write(sendClauseInputCommand);

                const unsigned int fixedDecisionsToPropagate =
                    decisionLevel == 0
                        ? fixedDecisionStackHeight - answerStackHeight
                        : fixedDecisionStackHeight;
                bcp_discover_dataflow_wrapper(mClsStates,
                    mAnswerStack, mlmd, mlmmd, unitByCls, mInDomain,
                    mLitStore,
                    answerStackHeight, unsatClauses, fixedDecisionStackHeight, literalCommit, doBackTrack,
                    topLiteral, litToCheck, fixedDecisionsToPropagate, decisionLevel, useFlipped, firstIteration,
                    forceLiteralPhase,
                    LITERAL_PAGE_SIZE, POSITIVE_LIT_PHASE_VAL,
                    litStoreAccessStats, store, pqHandlerInput, clauseStoreInputStream1, clauseStoreOutputStream1);

                sendClauseInputCommand.data.range(31,0) = csh::EXIT;
                sendClauseInputCommand.data.range(95,64) = csh::SEND_LEN_BCP;
                clauseStoreInputStream1.write(sendClauseInputCommand);

                if((unsigned int)decisionLevel < _FPGA_MAX_LITERALS){
                    mlmd[decisionLevel].decisionLevelStackEnd = answerStackHeight;
                }

                decisionLevel++;
                if(doBackTrack){
                    decisionLevel--;
                }

                sendTime(timerValueStream, conditionStream, 1, &store[1]);
                cycleCounter[2] += store[1]-store[0];

                // A conflict in the fixed decision stack makes the query unsatisfiable.
                if(decisionLevel == 0 || (doBackTrack && (unsigned int)decisionLevel <= NUM_ASSUMPTIONS)){
                    unsigned int coreCount = 0;
                    bool coreExtracted = true;
                    if(decisionLevel != 0){
                        coreExtracted = extractUnsatCore(answerStack, coreCount,
                            assumptions, NUM_ASSUMPTIONS, mAnswerStack,
                            answerStackHeight, mlmd, mlmmd, unitByCls,
                            unsatClauses, -1, NUM_LITERALS,
                            clauseStoreInputStream1, clauseStoreInputStream2,
                            clauseStoreOutputStream1);
                    }
                    miscCounters[0] = totalCount;
                    miscCounters[1] = decideCount;
                    miscCounters[2] = retryCount;
                    miscCounters[3] = backtrackCount;
                    miscCounters[4] = resetCount;
                    miscCounters[5] = answerStackHeight;
                    miscCounters[6] = 0;
                    miscCounters[50] = coreCount;
                    if(NUM_TEMPORARY_CLAUSES == 0 && coreExtracted && coreCount == 0){
                        permanentFormulaUnsat = true;
                    }

                    copyStats(learnedStats, longestClause, litStoreAccessStats, cycleCounter, miscCounters);

                    ap_axiu<1,0,0,0> pkt;
                    pkt.data = 1;
                    stopStream.write(pkt);

                    sendPQHandler.data = pq::EXIT;
                    pqHandlerInput.write(sendPQHandler);

                    FLUSH_RESTART_1: while(true){
                        #pragma HLS loop_tripcount min=16 max=16
                        if(restartValueStream.read().data == 0){
                            break;
                        }
                    }

                    sendTime(timerValueStream, conditionStream, 0, nullptr);
                    messageValue.data.range(95,64) = 0;
                    messageValue.data.range(63,32) = 0;
                    messageValue.data.range(31,0) = coreExtracted ? -1 : -8;
                    messageStream.write(messageValue);

                    ap_axiu<96,0,0,0> sendClauseInputCommand;
                    sendClauseInputCommand.data = 0;
                    sendClauseInputCommand.data.range(95,64) = csh::EXIT;

                    clauseStoreInputStream1.write(sendClauseInputCommand);

                    return;
                }
            }

            const bool allAssumptionsProcessed =
                (unsigned int)decisionLevel > NUM_ASSUMPTIONS;
            if((domainExhausted || answerStackHeight == NUM_LITERALS) &&
               allAssumptionsProcessed && !doBackTrack){
                miscCounters[0] = totalCount;
                miscCounters[1] = decideCount;
                miscCounters[2] = retryCount;
                miscCounters[3] = backtrackCount;
                miscCounters[4] = resetCount;
                miscCounters[5] = answerStackHeight;
                miscCounters[6] = 1;

                copyStats(learnedStats, longestClause, litStoreAccessStats, cycleCounter, miscCounters);

                COPY_OUT: for(unsigned int i = 0; i < answerStackHeight; i++){
                    #pragma HLS loop_tripcount min=1024 max=1024
                    answerStack[i] = mAnswerStack[i];
                }

                ap_axiu<1,0,0,0> pkt;
                pkt.data = 1;
                stopStream.write(pkt);

                sendPQHandler.data = pq::EXIT;
                pqHandlerInput.write(sendPQHandler);

                FLUSH_RESTART_2: while(true){
                    #pragma HLS loop_tripcount min=16 max=16
                    if(restartValueStream.read().data == 0){
                        break;
                    }
                }

                sendTime(timerValueStream, conditionStream, 0, nullptr);
                messageValue.data.range(95,64) = 0;
                messageValue.data.range(63,32) = 0;
                messageValue.data.range(31,0) = -1;
                messageStream.write(messageValue);

                ap_axiu<96,0,0,0> sendClauseInputCommand;
                sendClauseInputCommand.data = 0;
                sendClauseInputCommand.data.range(95,64) = csh::EXIT;

                clauseStoreInputStream1.write(sendClauseInputCommand);

                return;
            }
            firstIteration = true;
            useFlipped = false;
        }else{
            backtrackCount++;
        
            bool resetAll = false;
            limitCount++;
            if(limit == limitCount){
                limitCount = 0;
                limit = RESET_MULTIPLIER * restartValueStream.read().data;
                resetAll = true;
                resetCount++;
            }

            messageValue.data.range(95,64) = 3;
            messageValue.data.range(63,32) = backtrackCount;
            messageValue.data.range(31,0) = 2;
            messageStream.write(messageValue);

            int error = 0;
            lit insertPropagate[2] = {0,0};
            int givenClsID = -1;
            bool learntIsTemporary = false;

            learnClause(mClsStates,
                mLitStore,
                mlmd, mlmmd, insertPropagate, freeLitPageAddresses,
                decisionLevel, givenClsID,
                mAnswerStack, unitByCls, literalCommit, answerStackHeight,
                POSITIVE_LIT_PHASE_VAL, resetAll, unsatClauses, 
                NUM_LITERALS, MAX_LITERAL_ELEMENTS, LITERAL_PAGE_SIZE,
                miscCounters[11], learntIsTemporary,
                learnedStats, litStoreAccessStats, longestClause, error,
                clauseStoreInputStream1, clauseStoreInputStream2, clauseStoreOutputStream1, clauseStoreOutputStream2, 
                pqHandlerInput, pqHandlerValue, timerValueStream, conditionStream, cycleCounter);

            if(givenClsID >= 0 && learntIsTemporary){
                if(!trackTemporaryClause(temporaryClauseMask,
                        temporaryClauseCount, givenClsID)){
                    error = -6;
                }
            }

            if(resetAll && resetCount == 11){
                sendPQHandler.data = pq::SWITCH_TO_HEAP;
                pqHandlerInput.write(sendPQHandler);
            }
            

            if(error < 0){
                miscCounters[0] = totalCount;
                miscCounters[1] = decideCount;
                miscCounters[2] = retryCount;
                miscCounters[3] = backtrackCount;
                miscCounters[4] = resetCount;
                miscCounters[5] = answerStackHeight;
                
                copyStats(learnedStats, longestClause, litStoreAccessStats, cycleCounter, miscCounters);

                messageValue.data.range(95,64) = 0;
                messageValue.data.range(63,32) = 0;
                messageValue.data.range(31,0) = error;
                messageStream.write(messageValue);

                ap_axiu<1,0,0,0> pkt;
                pkt.data = 1;
                stopStream.write(pkt);

                sendPQHandler.data = pq::EXIT;
                pqHandlerInput.write(sendPQHandler);

                FLUSH_RESTART_3: while(true){
                    #pragma HLS loop_tripcount min=16 max=16
                    if(restartValueStream.read().data == 0){
                        break;
                    }
                }
                sendTime(timerValueStream, conditionStream, 0, nullptr);

                ap_axiu<96,0,0,0> sendClauseInputCommand;
                sendClauseInputCommand.data = 0;
                sendClauseInputCommand.data.range(95,64) = csh::EXIT;

                clauseStoreInputStream1.write(sendClauseInputCommand);

                return;
            }

            messageValue.data.range(95,64) = 3;
            messageValue.data.range(63,32) = backtrackCount;
            messageValue.data.range(31,0) = 4;
            messageStream.write(messageValue);

            if(resetAll){
                sendTime(timerValueStream, conditionStream, 1, &store[0]);

                decisionLevel = 0;

                ap_axiu<96,0,0,0> sendClauseInputCommand;
                sendClauseInputCommand.data = 0;
                sendClauseInputCommand.data.range(95,64) = csh::DELETE;

                clauseStoreInputStream1.write(sendClauseInputCommand);

                deleteTransposedClauses(mLitStore,
                    mlmd, freeLitPageAddresses, LITERAL_PAGE_SIZE, 
                    clauseStoreInputStream1, clauseStoreOutputStream1, locationOutputStream);

                sendTime(timerValueStream, conditionStream, 1, &store[1]);
                cycleCounter[8] += store[1]-store[0];
            }

            messageValue.data.range(95,64) = 3;
            messageValue.data.range(63,32) = backtrackCount;
            messageValue.data.range(31,0) = 5;
            messageStream.write(messageValue);
            
            unsatClauses.head = 0;
            unsatClauses.tail = 0;
            
            doBackTrack = false;
            if(decisionLevel == 0){
                if(insertPropagate[0] != 0){
                    mAnswerStack[answerStackHeight] = insertPropagate[0];
                    fixedDecisionStackHeight++;
                }else{
                    decisionLevel = 1;
                }
                if(fixedDecisionStackHeight == 0){
                    decisionLevel = 1;
                }
                useFlipped = false;
            }else{
                useFlipped = true;

                litToCheck.literal = insertPropagate[1];
                if(!isValidLiteral(litToCheck.literal) ||
                        (unsigned int)abs(litToCheck.literal) > NUM_LITERALS ||
                        givenClsID < 0 ||
                        (unsigned int)givenClsID >= _FPGA_MAX_CLAUSES){
                    miscCounters[0] = totalCount;
                    miscCounters[1] = decideCount;
                    miscCounters[2] = retryCount;
                    miscCounters[3] = backtrackCount;
                    miscCounters[4] = resetCount;
                    miscCounters[5] = answerStackHeight;
                    copyStats(learnedStats, longestClause, litStoreAccessStats,
                        cycleCounter, miscCounters);
                    messageValue.data = 0;
                    messageValue.data.range(31,0) = -9;
                    messageStream.write(messageValue);
                    ap_axiu<1,0,0,0> stopPacket;
                    stopPacket.data = 1;
                    stopStream.write(stopPacket);
                    sendPQHandler.data = pq::EXIT;
                    pqHandlerInput.write(sendPQHandler);
                    FLUSH_RESTART_INVALID_PROPAGATE: while(true){
                        #pragma HLS loop_tripcount min=16 max=16
                        if(restartValueStream.read().data == 0){
                            break;
                        }
                    }
                    sendTime(timerValueStream, conditionStream, 0, nullptr);
                    ap_axiu<96,0,0,0> exitCommand;
                    exitCommand.data = 0;
                    exitCommand.data.range(95,64) = csh::EXIT;
                    clauseStoreInputStream1.write(exitCommand);
                    return;
                }
                litToCheck.lmd = mlmd[abs(litToCheck.literal)-1];
                litToCheck.lmmd = mlmmd[0][abs(litToCheck.literal)-1];

                LMD_INSERT_LVL(litToCheck.lmd.compactlmd) = answerStackHeight;
                LMD_DEC_LVL(litToCheck.lmd.compactlmd) = decisionLevel;
                LMD_IS_IN_STACK(litToCheck.lmd.compactlmd) = true;
                LMD_UNIT_BY_LIT(litToCheck.lmd.compactlmd) = 0;

                unitByCls[abs(litToCheck.literal)-1] = givenClsID+1;
                LMMD_IS_DECIDE(litToCheck.lmmd.compactlmmd) = false;

                if(litToCheck.literal > 0){
                    LMD_PHASE(litToCheck.lmd.compactlmd) = POSITIVE_LIT_PHASE_VAL;
                }else{
                    LMD_PHASE(litToCheck.lmd.compactlmd) = !POSITIVE_LIT_PHASE_VAL;
                }
            }
        }
    }

	
}
}
