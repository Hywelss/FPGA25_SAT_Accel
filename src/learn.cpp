#include "learn.h"

void touchLearnDataflowStore(volatile uint64_t store[2]){
    #pragma HLS inline off
    store[0]++;
    store[1]++;
}
#include <set>

std::set<lit> resolvedLit;

void clause_store_prefetch(hls::stream<cls>& prefetchClsStore, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    PREFETCH_DRAM: while(true){
        #pragma HLS loop_tripcount min=1024 max=1024

        ap_axiu<32,0,0,0> getData = clauseStoreOutputStream.read();
        cls getCls = getData.data;

        prefetchClsStore.write(getCls);
        if(getCls == 0){
            break;
        }
    }
}

void merge_resolution_sort(hls::stream<lit_resolve>& mrsp2Stream, hls::stream<lit>& fromClsStore,
    lit resolutionClause[_FPGA_MAX_LEARN_ELE], ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS], ap_uint<512> validBit[_FPGA_MAX_LITERALS/512],
    unsigned int& numElements, bool& clauseTooLong, ap_uint<64>& learnedStats){
    #pragma HLS inline off

    const int RESOLVE_DEP = _FPGA_RESOLVE_DEP_DIST;
    int resolvedHappened = 0;
    lit possibleResolved = 0;
    ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPadResolved = 0;
    ap_uint<512> getBitReg[_FPGA_RESOLVE_DEP_DIST];
    #pragma HLS array_partition variable=getBitReg complete
    unsigned int validBitAddrIndexReg[_FPGA_RESOLVE_DEP_DIST];
    #pragma HLS array_partition variable=validBitAddrIndexReg complete

    for(unsigned int i = 0; i < _FPGA_RESOLVE_DEP_DIST; i++){
        #pragma HLS unroll
        validBitAddrIndexReg[i] = _FPGA_MAX_LITERALS;
    }

    if(numElements > 0){
        possibleResolved = resolutionClause[numElements-1];
    }

    RESOLVE: while(true){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS pipeline 
        #pragma HLS dependence variable=mergeScratchPad inter false
        #pragma HLS dependence variable=validBit inter distance=RESOLVE_DEP true

        lit readFromClsStore = fromClsStore.read();
        if(readFromClsStore == 0){
            break;
        }
        if(clauseTooLong){
            continue;
        }
        if(!isValidLiteral(readFromClsStore)){
            clauseTooLong = true;
            continue;
        }

        ap_uint<2> mergeStatus = mergeScratchPad[abs(readFromClsStore)-1].range(_FPGA_MAX_LEARN_ELE_BITS+1,_FPGA_MAX_LEARN_ELE_BITS);
        ap_uint<_FPGA_MAX_LEARN_ELE_BITS> position = mergeScratchPad[abs(readFromClsStore)-1].range(_FPGA_MAX_LEARN_ELE_BITS-1,0);

        unsigned int validBitAddrIndex = (abs(readFromClsStore)-1)/512;
        unsigned int validBitAddrSubIndex = (abs(readFromClsStore)-1)%512;
        ap_uint<512> getBit = validBit[validBitAddrIndex];

        for(unsigned int j = 0; j < _FPGA_RESOLVE_DEP_DIST; j++){
            if(validBitAddrIndexReg[j] == validBitAddrIndex){
                getBit = getBitReg[j];
            }
        }

        if(getBit.range(validBitAddrSubIndex,validBitAddrSubIndex) == 0){
            mergeStatus = 0;
            position = 0;
            getBit.range(validBitAddrSubIndex,validBitAddrSubIndex) = 1;
        }
        validBit[validBitAddrIndex] = getBit;
        
        
        for(unsigned int j = 0; j < _FPGA_RESOLVE_DEP_DIST-1; j++){
            validBitAddrIndexReg[j] = validBitAddrIndexReg[j+1];
            getBitReg[j] = getBitReg[j+1];
        }
        validBitAddrIndexReg[_FPGA_RESOLVE_DEP_DIST-1] = validBitAddrIndex;
        getBitReg[_FPGA_RESOLVE_DEP_DIST-1] = getBit;
        
        bool insert = false;
        learnedStats++;

        if(readFromClsStore> 0){
            if(mergeStatus.range(0,0) == 0){
                mergeStatus.range(0,0) = 1;
                insert = true;
            }
        }else{
            if(mergeStatus.range(1,1) == 0){
                mergeStatus.range(1,1) = 1;
                insert = true;
            }
        }

        if(mergeStatus == 3){
            if(position != numElements-1){
                resolutionClause[position] = possibleResolved;
                mergeScratchPadResolved.range(_FPGA_MAX_LEARN_ELE_BITS-1,0) = position;
                resolvedHappened = 1;
            }else{
                resolvedHappened = 2;
            }
            numElements--;
            
            mergeScratchPad[abs(readFromClsStore)-1] = 0;  
            mrsp2Stream.write((lit_resolve){.literal=readFromClsStore,.resolved=true});
        }else if(insert){
            if(numElements == _FPGA_MAX_LEARN_ELE){
                clauseTooLong = true;
                continue;
            }
            resolutionClause[numElements] = readFromClsStore;
            position = numElements;
            numElements++;

            if(resolvedHappened == 0){
                possibleResolved = readFromClsStore;
            }
    
            mergeScratchPad[abs(readFromClsStore)-1].range(_FPGA_MAX_LEARN_ELE_BITS+1,_FPGA_MAX_LEARN_ELE_BITS) = mergeStatus;
            mergeScratchPad[abs(readFromClsStore)-1].range(_FPGA_MAX_LEARN_ELE_BITS-1,0) = position;

            mrsp2Stream.write((lit_resolve){.literal=readFromClsStore,.resolved=false});
        }
    }
    if(resolvedHappened == 1 && isValidLiteral(possibleResolved)){
        mergeScratchPad[abs(possibleResolved)-1].range(_FPGA_MAX_LEARN_ELE_BITS-1,0) = mergeScratchPadResolved;
    }
    

    mrsp2Stream.write((lit_resolve){.literal=0,.resolved=false});
}

void merge_resolution_sort_part_2(hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput, hls::stream<lit_resolve>& mrsp2Stream, 
    const literalMetaData lmd[_FPGA_MAX_LITERALS], literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    unsigned int& streamSize, unsigned int& highestIL, unsigned int& fixedStackCount, const int decisionLevel){
    #pragma HLS inline off

    bool once = true;
    if(highestIL == 0){
        once = false;
    }

    MERGE_SORT_2: while(true){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS dependence variable=lmmd inter false
        #pragma HLS pipeline II=1

        lit_resolve get;
        if(mrsp2Stream.read_nb(get)){
            if(get.literal == 0){
                break;
            }
            if(!isValidLiteral(get.literal)){
                continue;
            }

            literalMetaData getLmd = reg(lmd[reg(abs(get.literal)-1)]);
            literalMinimizeMetaData getLmmd = lmmd[0][abs(get.literal)-1];

            if(get.resolved){
                if(LMMD_IS_IN_FIX_STACK(getLmmd.compactlmmd) == 1){
                    fixedStackCount--;
                }
                LMMD_MIN_KEEP(getLmmd.compactlmmd) = 0;
            }else{
                if((int)LMD_DEC_LVL(getLmd.compactlmd) > 0){
                    ap_axiu<32,0,0,0> sendPQHandler;
                    sendPQHandler.data = abs(get.literal);
                    pqHandlerInput.write(sendPQHandler);
                }
                if((int)LMD_DEC_LVL(getLmd.compactlmd) == decisionLevel){
                    if(highestIL < LMD_INSERT_LVL(getLmd.compactlmd) && !once){
                        highestIL = LMD_INSERT_LVL(getLmd.compactlmd);
                    }
                    streamSize++;
                }
                if(LMMD_IS_IN_FIX_STACK(getLmmd.compactlmmd) == 1){
                    fixedStackCount++;
                }
                LMMD_MIN_KEEP(getLmmd.compactlmmd) = 1;
            }

            for(unsigned int i = 0; i < _FPGA_PARALLEL_MINIMIZE; i++){
                lmmd[i][abs(get.literal)-1] = getLmmd;
            }
        }
    }
}

void resolution_dataflow_wrapper(hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput,
    ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS], ap_uint<512> validBit[_FPGA_MAX_LITERALS/512], 
    const literalMetaData lmd[_FPGA_MAX_LITERALS], literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    unsigned int& numElements, unsigned int& streamSize, unsigned int& highestIL, unsigned int& fixedStackCount,
    const int decisionLevel, bool& clauseTooLong, ap_uint<64>& learnedStats,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    hls::stream<lit> fromClsStore;
    #pragma HLS stream variable=fromClsStore depth=8

    hls::stream<lit_resolve> mrsp2Stream;
    #pragma HLS stream variable=mrsp2Stream depth=8

    #pragma HLS dataflow

    clause_store_prefetch(fromClsStore, clauseStoreOutputStream);

    merge_resolution_sort(mrsp2Stream, fromClsStore,
        resolutionClause, mergeScratchPad, validBit, numElements,
        clauseTooLong, learnedStats);
    merge_resolution_sort_part_2(pqHandlerInput, mrsp2Stream, 
        lmd, lmmd, streamSize, highestIL, fixedStackCount, decisionLevel);
}

void undo_states_and_minimize_task_parallel_wrapper(hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput, 
    hls::stream<lit>& toMinimizeStream,
    clsState clsStates[_FPGA_CLS_STATES_PARTITION][_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS], literalMetaData lmd[_FPGA_MAX_LITERALS], 
    ap_uint<512> validBitMinimize[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS/512],
    ap_uint<2> mergeScratchPadMinimize[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    unsigned int nonRemovableCount[_FPGA_PARALLEL_MINIMIZE], bool didSimplify[_FPGA_PARALLEL_MINIMIZE], const cls unitByCls[_FPGA_MAX_LITERALS],
    const ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    const lit answerStack[_FPGA_MAX_LITERALS],  
    const lit literalCommit, const unsigned int backtrackHeight, unsigned int& answerStackHeight, 
    const bool foundAbsolute, const unsigned int LITERAL_PAGE_SIZE, const ap_uint<1> POSITIVE_LIT_PHASE_VAL, const unsigned int clearIterations,
    ap_uint<64> litStoreAccessStats[4], ap_uint<64> minimizeStats[_FPGA_PARALLEL_MINIMIZE][2], unsigned int overheadClear[_FPGA_PARALLEL_MINIMIZE],
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2, volatile uint64_t store[2]){
    #pragma HLS inline off

    hls::stream<lit> splitMinimizeStream[2];
    #pragma HLS stream variable=splitMinimizeStream depth=2
    #pragma HLS array_partition variable=splitMinimizeStream dim=0 complete

    #pragma HLS dataflow

    touchLearnDataflowStore(store);

    undo_states_dataflow_wrapper(pqHandlerInput, clsStates,
        answerStack, lmd, litStore, literalCommit, backtrackHeight, answerStackHeight,
        LITERAL_PAGE_SIZE, POSITIVE_LIT_PHASE_VAL, litStoreAccessStats);

    minimize_dispatch(toMinimizeStream,
        splitMinimizeStream, foundAbsolute);

    minimize_dataflow_wrapper_layer_1(splitMinimizeStream[0],
        lmmd[0], 
        validBitMinimize[0],
        mergeScratchPadMinimize[0],
        nonRemovableCount[0], didSimplify[0], unitByCls,
        foundAbsolute,
        clearIterations, minimizeStats[0], overheadClear[0],
        clauseStoreInputStream1, clauseStoreOutputStream1);

    minimize_dataflow_wrapper_layer_1(splitMinimizeStream[1],
        lmmd[1], 
        validBitMinimize[1],
        mergeScratchPadMinimize[1],
        nonRemovableCount[1], didSimplify[1], unitByCls,
        foundAbsolute,
        clearIterations, minimizeStats[1], overheadClear[1],
        clauseStoreInputStream2, clauseStoreOutputStream2);
    
}

void findNextCls(hls::stream<cls>& nextClauseReg, hls::stream<bool>& stopSignal, 
    int& trailEndIndex, const ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS], const ap_uint<512> validBit[_FPGA_MAX_LITERALS/512],
    const lit answerStack[_FPGA_MAX_LITERALS], const cls unitByCls[_FPGA_MAX_LITERALS],
    const literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS], ap_uint<64>& learnedStats){
    #pragma HLS inline off

    int latestHeight = 0;
    bool once = false;
    int saveTrailEndIndex = trailEndIndex;
    bool didRead = false;

    FIND_NEXT_CLS: for(int i = trailEndIndex; i >= 0; i--){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS pipeline 

        #ifdef FPGA_HW
        bool stop;
        if(stopSignal.read_nb(stop)){
            didRead = true;
            break;
        }
        #endif

        lit trailLit = reg(answerStack[i]);
        if(!isValidLiteral(trailLit)){
            trailEndIndex = i-1;
            continue;
        }
        literalMinimizeMetaData getLmmd = reg(lmmd[0][abs(trailLit)-1]);
        ap_uint<2> mergeStatus = mergeScratchPad[abs(trailLit)-1].range(_FPGA_MAX_LEARN_ELE_BITS+1,_FPGA_MAX_LEARN_ELE_BITS);
        trailEndIndex = i-1;
        learnedStats++;

        unsigned int validBitAddrIndex = (abs(trailLit)-1)/512;
        unsigned int validBitAddrSubIndex = (abs(trailLit)-1)%512;
        ap_uint<512> getBit = validBit[validBitAddrIndex];      

        if(getBit.range(validBitAddrSubIndex,validBitAddrSubIndex) == 1 && (mergeStatus == 1 || mergeStatus == 2)){
            if(!once){
                saveTrailEndIndex = trailEndIndex;
                once = true;
            
                nextClauseReg.write(unitByCls[abs(trailLit)-1]);
            }

            #ifndef FPGA_HW
            break;
            #endif
        }
    }

    trailEndIndex = saveTrailEndIndex;
    nextClauseReg.write(0);
    #ifdef FPGA_HW
    if(!didRead){
        stopSignal.read();
    }
    #endif
}

void findNextClsCompare(hls::stream<cls>& nextClauseReg, hls::stream<bool>& stopSignal,
    cls& nextClauseMerge, bool& foundNextClause){
    #pragma HLS inline off

    IO_INSERT:{
    nextClauseMerge = nextClauseReg.read();
    foundNextClause = nextClauseMerge != 0;

    ap_wait();
    #ifdef FPGA_HW
    stopSignal.write(true);
    #endif
    }

    FLUSH_FIND_NEXT_CLS: while(foundNextClause){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS pipeline 

        cls get = nextClauseReg.read();
        if(get == 0){
            break;
        }
    }
}

void findNextClsDataflow(int& trailEndIndex, cls& nextClauseMerge, bool& foundNextClause,
    const ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS], const ap_uint<512> validBit[_FPGA_MAX_LITERALS/512],
    const lit answerStack[_FPGA_MAX_LITERALS], const cls unitByCls[_FPGA_MAX_LITERALS],
    const literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS], ap_uint<64>& learnedStats){
    #pragma HLS inline off

    hls::stream<cls> nextClauseReg;
    hls::stream<bool> stopSignal;

    #pragma HLS dataflow

    findNextCls(nextClauseReg, stopSignal, 
        trailEndIndex, mergeScratchPad, validBit, answerStack, unitByCls,
        lmmd, learnedStats);
    findNextClsCompare(nextClauseReg, stopSignal, nextClauseMerge, foundNextClause);
}

void writeClauseStream(hls::stream<ap_int<96>>& toSaveClauseStream, hls::stream<lit>& litNewPage,
    literalMetaData lmd[_FPGA_MAX_LITERALS], literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    clsState& newClauseState,
    const lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    const unsigned int numElements, const unsigned int givenClsID, const int levelBefore,
    const unsigned int constraintActivation, bool& containsConstraintActivation,
    const bool registerInLearntBuckets,
    const unsigned int LITERAL_PAGE_SIZE,  
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream){

    #pragma HLS inline off

    int uniqueDecisionLevel[_FPGA_MAX_LBD_BUCKETS+1];
    #pragma HLS array_partition variable=uniqueDecisionLevel dim=0 complete
    unsigned int uniqueueDecisionLevelCount = 0;
    unsigned int UDLindex = 0;

    ZERO_UDL: for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS+1; i++){
        #pragma HLS unroll
        uniqueDecisionLevel[i] = 0;
    }

    INSERT_NEW_CLAUSE: for(unsigned int i = 0; i < numElements; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        #pragma HLS pipeline
        #pragma HLS dependence variable=lmd inter false
        #pragma HLS dependence variable=lmmd inter false
        
        bool add = false;
        if(!shouldSaveClauseLiteral(resolutionClause[i], lmmd, add)){
            continue;
        }

        const unsigned int variable = abs(resolutionClause[i])-1;
        literalMetaData newlmd = reg(lmd[variable]);
        literalMinimizeMetaData newLmmd = lmmd[0][variable];

        ap_uint<1> selectSide = 0;
        if(resolutionClause[i] < 0){
            selectSide = 1;
        }

        if(add){
            lit getLit = resolutionClause[i];

            if(isConstraintActivationLiteral(getLit, constraintActivation)){
                containsConstraintActivation = true;
            }

            newClauseState.compressedList ^= getLit;
            int getDL = (int)LMD_DEC_LVL(newlmd.compactlmd);

            bool foundUDL = false;
            for(unsigned int j = 0; j < _FPGA_MAX_LBD_BUCKETS+1; j++){
                if(uniqueDecisionLevel[j] == getDL){
                    foundUDL = true;
                }
            }
            if(!foundUDL && UDLindex < _FPGA_MAX_LBD_BUCKETS+1){
                uniqueDecisionLevel[UDLindex] = getDL;
                UDLindex++;
                uniqueueDecisionLevelCount++;
            }

            unsigned int reqAddrLit = LMD_LATEST_PAGE(newlmd.compactlmd,selectSide)/16;
            unsigned int reqAddrOffset = LMD_FREE_SPACE(newlmd.compactlmd,selectSide);
            cls saveCls = givenClsID+1;

            ap_int<96> packValue;
            packValue.range(95,64) = saveCls;
            packValue.range(63,32) = reqAddrOffset;
            packValue.range(31,0) = reqAddrLit;

            toSaveClauseStream.write(packValue);

            LMD_NUM_ELE(newlmd.compactlmd,selectSide) = LMD_NUM_ELE(newlmd.compactlmd,selectSide) + 1;
            LMD_FREE_SPACE(newlmd.compactlmd, selectSide) = LMD_FREE_SPACE(newlmd.compactlmd, selectSide)-1;
            if(LMD_FREE_SPACE(newlmd.compactlmd, selectSide) == 0){
                litNewPage.write(getLit);
            }

            ap_axiu<96,0,0,0> sendClauseInputCommand;
            sendClauseInputCommand.data.range(31,0) = getLit;
            sendClauseInputCommand.data.range(63,32) = LMD_LATEST_PAGE(newlmd.compactlmd,selectSide) +
                LITERAL_PAGE_SIZE - reqAddrOffset - 2;
            sendClauseInputCommand.data.range(95,64) = csh::SAVE;

            clauseStoreInputStream.write(sendClauseInputCommand);
        }
        LMMD_MIN_KEEP(newLmmd.compactlmmd) = 0;

        lmd[variable] = reg(newlmd);
        for(unsigned int j = 0; j < _FPGA_PARALLEL_MINIMIZE; j++){
            lmmd[j][variable] = newLmmd;
        }
    }
    if(registerInLearntBuckets && !containsConstraintActivation){
        ap_axiu<96,0,0,0> sendClauseInputCommand;
        sendClauseInputCommand.data.range(31,0) = uniqueueDecisionLevelCount;
        sendClauseInputCommand.data.range(63,32) = 0;
        sendClauseInputCommand.data.range(95,64) = csh::BUCKET;
        clauseStoreInputStream.write(sendClauseInputCommand);
    }

    toSaveClauseStream.write(-1);
}

void saveClause(hls::stream<ap_int<96>>& toSaveClauseStream, ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16], const unsigned int LITERAL_PAGE_SIZE){
    #pragma HLS inline off

    INSERT_NEW_CLAUSE: while(true){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS pipeline
        #pragma HLS dependence variable=litStore inter false

        ap_int<96> get = toSaveClauseStream.read();

        if(get == -1){
            break;
        }

        cls saveCls = get.range(95,64);
        unsigned int reqAddrLit = get.range(31,0);
        unsigned int reqAddrOffset = LITERAL_PAGE_SIZE - get.range(63,32) - 2;

        ap_int<512> fetchLine = litStore[reqAddrLit + reqAddrOffset/16];

        if(reqAddrOffset%16 == 0 && get.range(63,32) > 14){
            fetchLine = 0;
        }else if(reqAddrOffset%16 == 0 && get.range(63,32) <= 14){
            fetchLine.range(447,0) = 0;
        }
        fetchLine.range(32*(reqAddrOffset%16)+31,32*(reqAddrOffset%16)) = saveCls;

        litStore[reqAddrLit + reqAddrOffset/16] = fetchLine;
    }
}

void saveClauseDataflow(hls::stream<lit>& litNewPage, ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS], literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    clsState& newClauseState,
    const lit resolutionClause[_FPGA_MAX_LEARN_ELE],
    const unsigned int numElements, const unsigned int givenClsID, const int levelBefore,
    const unsigned int constraintActivation, bool& containsConstraintActivation,
    const bool registerInLearntBuckets,
    const unsigned int LITERAL_PAGE_SIZE, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream){

    #pragma HLS inline off
    hls::stream<ap_int<96>> toSaveClauseStream;

    #pragma HLS dataflow

    writeClauseStream(toSaveClauseStream, litNewPage,
        lmd, lmmd, newClauseState,
        resolutionClause,
        numElements, givenClsID, levelBefore,
        constraintActivation, containsConstraintActivation, registerInLearntBuckets,
        LITERAL_PAGE_SIZE,
        clauseStoreInputStream);
    saveClause(toSaveClauseStream, litStore, LITERAL_PAGE_SIZE);
}

void writeQueryClauseStream(hls::stream<ap_int<96>>& toSaveClauseStream,
    hls::stream<lit>& litNewPage,
    literalMetaData lmd[_FPGA_MAX_LITERALS], clsState& newClauseState,
    const lit queryClause[_FPGA_MAX_LEARN_ELE],
    const unsigned int numElements, const unsigned int givenClsID,
    const unsigned int LITERAL_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream) {
    #pragma HLS inline off

    WRITE_QUERY_CLAUSE: for(unsigned int i = 0; i < numElements; i++) {
        #pragma HLS loop_tripcount min=1 max=1024
        #pragma HLS pipeline II=1
        #pragma HLS dependence variable=lmd inter false

        const lit getLit = queryClause[i];
        const unsigned int variable = abs(getLit) - 1;
        literalMetaData metadata = reg(lmd[variable]);
        const ap_uint<1> selectSide = getLit < 0;
        const unsigned int freeSpace = LMD_FREE_SPACE(metadata.compactlmd, selectSide);
        const unsigned int latestPage = LMD_LATEST_PAGE(metadata.compactlmd, selectSide);

        newClauseState.compressedList ^= getLit;

        ap_int<96> packedClause;
        packedClause.range(95,64) = givenClsID + 1;
        packedClause.range(63,32) = freeSpace;
        packedClause.range(31,0) = latestPage / 16;
        toSaveClauseStream.write(packedClause);

        LMD_NUM_ELE(metadata.compactlmd, selectSide) =
            LMD_NUM_ELE(metadata.compactlmd, selectSide) + 1;
        LMD_FREE_SPACE(metadata.compactlmd, selectSide) = freeSpace - 1;
        if(freeSpace == 1) {
            litNewPage.write(getLit);
        }
        lmd[variable] = reg(metadata);

        ap_axiu<96,0,0,0> saveCommand;
        saveCommand.data.range(31,0) = getLit;
        saveCommand.data.range(63,32) = latestPage + LITERAL_PAGE_SIZE - freeSpace - 2;
        saveCommand.data.range(95,64) = csh::SAVE;
        clauseStoreInputStream.write(saveCommand);
    }
    toSaveClauseStream.write(-1);
}

void saveQueryClauseDataflow(hls::stream<lit>& litNewPage,
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS], clsState& newClauseState,
    const lit queryClause[_FPGA_MAX_LEARN_ELE],
    const unsigned int numElements, const unsigned int givenClsID,
    const unsigned int LITERAL_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream) {
    #pragma HLS inline off

    hls::stream<ap_int<96>> toSaveClauseStream;
    #pragma HLS dataflow

    writeQueryClauseStream(toSaveClauseStream, litNewPage, lmd, newClauseState,
        queryClause, numElements, givenClsID, LITERAL_PAGE_SIZE,
        clauseStoreInputStream);
    saveClause(toSaveClauseStream, litStore, LITERAL_PAGE_SIZE);
}

int overhead = 0;
void learnClause(clsState clsStates[_FPGA_CLS_STATES_PARTITION][_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS], literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS],
    lit insertPropagate[2], mmuStream<unsigned int, _MAX_PAGES_LIT_STORE_>& freeLitPageAddresses,
    int& decisionLevel, int& givenClsID,
    const lit answerStack[_FPGA_MAX_LITERALS], const cls unitByCls[_FPGA_MAX_LITERALS], const lit literalCommit, unsigned int& answerStackHeight,
    const ap_uint<1> POSITIVE_LIT_PHASE_VAL, const bool resetAll,
    const myStream<cls,64,7>& unsatClauses, 
    const unsigned int NUM_LITERALS, const unsigned int MAX_LITERAL_ELEMENTS, 
    const unsigned int LITERAL_PAGE_SIZE, const unsigned int constraintActivation,
    bool& learntIsTemporary,
    ap_uint<64> learnedStats[5], ap_uint<64>* litStoreAccessStats, ap_uint<64>* longestClause, int& error,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput, hls::stream<ap_axiu<32,0,0,0>>& pqHandlerValue,
    hls::stream<ap_axiu<64,0,0,0>>& timerValueStream, hls::stream<ap_axiu<1,0,0,0>>& conditionStream, ap_uint<64>* cycleCounter){

    #pragma HLS inline off

    learntIsTemporary = false;

    lit resolutionClause[_FPGA_MAX_LEARN_ELE];
    #pragma HLS bind_storage variable=resolutionClause type=RAM_S2P impl=auto latency=1

    ap_uint<512> validBitLearn[_FPGA_MAX_LITERALS/512];
    #pragma HLS bind_storage variable=validBitLearn type=RAM_S2P impl=auto latency=1
    ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPadLearn[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=mergeScratchPadLearn type=RAM_S2P impl=auto latency=2

    ap_uint<512> validBitMinimize[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS/512];
    #pragma HLS array_partition variable=validBitMinimize dim=1 complete
    #pragma HLS bind_storage variable=validBitMinimize type=RAM_S2P impl=auto latency=1
    ap_uint<2> mergeScratchPadMinimize[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS];
    #pragma HLS array_partition variable=mergeScratchPadMinimize dim=1 complete
    #pragma HLS bind_storage variable=mergeScratchPadMinimize type=RAM_S2P impl=auto latency=2

    static bool zeroOnce = false;

    bool addOne = false;
    if(NUM_LITERALS%512 != 0){
        addOne = true;
    }
    const unsigned int clearIterations = NUM_LITERALS/512+addOne;

    #ifndef FPGA_HW
    ZERO_SCRATCH_PAD_ASSERTION:for(unsigned int i = 0; i < _FPGA_MAX_LITERALS/512; i++){
        validBitLearn[i] = 0;
        for(unsigned int j = 0; j < _FPGA_PARALLEL_MINIMIZE; j++){
            validBitMinimize[j][i] = 0;
        }
    }
    #else
    if(!zeroOnce){
        ZERO_MEM_BIT: for(unsigned int i = 0; i < _FPGA_MAX_LITERALS/512; i++){
            validBitLearn[i] = 0;
            for(unsigned int j = 0; j < _FPGA_PARALLEL_MINIMIZE; j++){
                validBitMinimize[j][i] = 0;
            }
        }
        zeroOnce = true;
    }
    #endif

    ap_axiu<96,0,0,0> sendClauseInputCommand;
    sendClauseInputCommand.data = 0;
    ap_axiu<32,0,0,0> sendPQHandler;
    sendPQHandler.data = pq::UPDATE;
    pqHandlerInput.write(sendPQHandler);
    
    unsigned int numElements = 0;
    cls nextClauseMerge = 0;

    volatile uint64_t store[2];
    sendTime(timerValueStream, conditionStream, 1, &store[0]);

    unsigned int checkLengthLearn = UINT_MAX-1;

    sendClauseInputCommand.data.range(31,0) = 0;
    sendClauseInputCommand.data.range(95,64) = csh::SEND_LEN;
    clauseStoreInputStream1.write(sendClauseInputCommand);

    unsigned int numElementsMyStream = unsatClauses.head;
    if(numElementsMyStream == 0){
        error = -9;
    }
    GET_SHORTEST_START: for(unsigned int i = 0; i < numElementsMyStream; i++){
        #pragma HLS loop_tripcount min=16 max=16
        cls possibleStart = unsatClauses.array[i];

        if(possibleStart <= 0 ||
                (unsigned int)possibleStart > _FPGA_MAX_CLAUSES){
            error = -9;
            continue;
        }

        sendClauseInputCommand.data.range(31,0) = possibleStart-1;
        sendClauseInputCommand.data.range(63,32) = 0;
        sendClauseInputCommand.data.range(95,64) = 0;

        clauseStoreInputStream1.write(sendClauseInputCommand);
        ap_wait();
        unsigned int getLearnLength = clauseStoreOutputStream1.read().data;

        if(getLearnLength < checkLengthLearn){
            nextClauseMerge = possibleStart;
            checkLengthLearn = getLearnLength;
        }
    }

    sendClauseInputCommand.data.range(31,0) = csh::EXIT;
    sendClauseInputCommand.data.range(95,64) = csh::SEND_LEN;
    clauseStoreInputStream1.write(sendClauseInputCommand);

    bool foundAbsolute = false;
    bool isUIP = false;
    bool clauseTooLong = false;

    unsigned int streamSize = 0;
    int trailEndIndex = 0;
    unsigned int highestIL = 0;
    unsigned int fixedStackCount = 0;
    unsigned int backtrackHeight = 0;
    bool setTrailIndexOnce = false;
    lit possibleUIPLit = 0;

    sendClauseInputCommand.data.range(31,0) = 0;
    sendClauseInputCommand.data.range(63,32) = 0;
    sendClauseInputCommand.data.range(95,64) = csh::SEND_CLS;

    clauseStoreInputStream1.write(sendClauseInputCommand);

    RESOLUTION: while(true){
        #pragma HLS loop_tripcount min=1024 max=1024

        if(isUIP || foundAbsolute){
            break;
        }

        if(error < 0){
            break;
        }
        if(nextClauseMerge <= 0 ||
                (unsigned int)nextClauseMerge > _FPGA_MAX_CLAUSES){
            error = -9;
            break;
        }

        sendClauseInputCommand.data.range(31,0) = nextClauseMerge-1;
        sendClauseInputCommand.data.range(63,32) = 0;
        sendClauseInputCommand.data.range(95,64) = csh::SEND_CLS;

        clauseStoreInputStream1.write(sendClauseInputCommand);

        learnedStats[0]++;
        resolution_dataflow_wrapper(pqHandlerInput,
            mergeScratchPadLearn, validBitLearn, lmd, lmmd, resolutionClause,
            numElements, streamSize, highestIL, fixedStackCount, decisionLevel,
            clauseTooLong, learnedStats[1], clauseStoreOutputStream1);
        if(!setTrailIndexOnce){
            trailEndIndex = highestIL;
            setTrailIndexOnce = true;
        }

        if(longestClause[0] < numElements){
            longestClause[0] = numElements;
        }

        if(clauseTooLong){
            error = -2;
            break;
        }
        
        if(numElements == 1 || (numElements-fixedStackCount == 1)){
            foundAbsolute = true;
        }

        if(streamSize == 1){
            isUIP = true;
        }else{
            bool foundNextClause = false;
            findNextClsDataflow(trailEndIndex, nextClauseMerge, foundNextClause,
                mergeScratchPadLearn, validBitLearn, answerStack, unitByCls,
                lmmd, learnedStats[1]);
            if(!foundNextClause || nextClauseMerge <= 0 ||
                    (unsigned int)nextClauseMerge > _FPGA_MAX_CLAUSES){
                error = -9;
                break;
            }
            streamSize--;
        }
    }

    sendPQHandler.data = pq::EXIT;
    pqHandlerInput.write(sendPQHandler);

    ZERO_SEQ: for(unsigned int j = 0; j < clearIterations; j++){
        #pragma HLS loop_tripcount min=64 max=64
        #pragma HLS pipeline 
        validBitLearn[j] = 0;
    }
    overhead += clearIterations;

    if(error < 0){
        sendClauseInputCommand.data.range(31,0) = 0;
        sendClauseInputCommand.data.range(63,32) = 0;
        sendClauseInputCommand.data.range(95,64) = csh::EXIT;

        clauseStoreInputStream1.write(sendClauseInputCommand);
        clauseStoreInputStream2.write(sendClauseInputCommand);
        return;
    }

    unsigned int nonRemovableCount = 0;
    hls::stream<lit> toMinimizeStream;
    #pragma HLS stream variable=toMinimizeStream depth=_FPGA_MAX_LEARN_ELE

    // GipSAT backtracks a unit learnt clause to level zero.  Starting from
    // zero also keeps a missing second-highest level from becoming index -1.
    int levelBefore = 0;
    if(!resetAll && !foundAbsolute){
        GET_BT_LEVEL: for(unsigned int i = 0; i < numElements; i++){
            #pragma HLS loop_tripcount min=1024 max=1024
            #pragma HLS pipeline
        
            const lit getLit = resolutionClause[i];
            if(!isValidLiteral(getLit)){
                error = -9;
                continue;
            }
            const literalMetaData getLmd = reg(lmd[abs(getLit)-1]);
            const literalMinimizeMetaData getlmmd = lmmd[0][abs(getLit)-1];

            if(LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 0 && (int)LMD_DEC_LVL(getLmd.compactlmd) != 0 &&
                levelBefore < (int)LMD_DEC_LVL(getLmd.compactlmd) && (int)LMD_DEC_LVL(getLmd.compactlmd) != decisionLevel){
                levelBefore = LMD_DEC_LVL(getLmd.compactlmd);
            }
            if((int)LMD_DEC_LVL(getLmd.compactlmd) == decisionLevel){
                possibleUIPLit = getLit;
            }

            if(LMMD_IS_DECIDE(getlmmd.compactlmmd) == 1 && LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 0){
                nonRemovableCount++;
            }

            if(!(LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 1 || LMMD_IS_DECIDE(getlmmd.compactlmmd) == 1)){
                toMinimizeStream.write(getLit);
            }
        }
        if(!computeBacktrackHeight(levelBefore, answerStackHeight, lmd,
                backtrackHeight)){
            error = -9;
        }
    }else{
        if(foundAbsolute){
            FIND_ABSOLUTE: for(unsigned int i = 0; i < numElements; i++){
                #pragma HLS loop_tripcount min=1024 max=1024
                #pragma HLS pipeline
        
                if(!isValidLiteral(resolutionClause[i])){
                    error = -9;
                    continue;
                }
                const literalMinimizeMetaData getlmmd = lmmd[0][abs(resolutionClause[i])-1];

                if(LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 0){
                    insertPropagate[0] = resolutionClause[i];
                }
            }
        }else{
            GET_MINIMIZE: for(unsigned int i = 0; i < numElements; i++){
                #pragma HLS loop_tripcount min=1024 max=1024
                #pragma HLS pipeline
        
                const lit getLit = resolutionClause[i];
                if(!isValidLiteral(getLit)){
                    error = -9;
                    continue;
                }
                const literalMetaData getLmd = reg(lmd[abs(getLit)-1]);
                const literalMinimizeMetaData getlmmd = lmmd[0][abs(getLit)-1];

                if(LMMD_IS_DECIDE(getlmmd.compactlmmd) == 1 && LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 0){
                    nonRemovableCount++;
                }

                if(!(LMMD_IS_IN_FIX_STACK(getlmmd.compactlmmd) == 1 || LMMD_IS_DECIDE(getlmmd.compactlmmd) == 1)){
                    toMinimizeStream.write(getLit);
                }
            }
        }
        if(!computeBacktrackHeight(0, answerStackHeight, lmd,
                backtrackHeight)){
            error = -9;
        }
    }

    sendTime(timerValueStream, conditionStream, 1, &store[1]);
    cycleCounter[3] += store[1]-store[0];

    if(error < 0){
        sendClauseInputCommand.data = 0;
        sendClauseInputCommand.data.range(95,64) = csh::EXIT;
        clauseStoreInputStream1.write(sendClauseInputCommand);
        clauseStoreInputStream2.write(sendClauseInputCommand);
        return;
    }

    sendTime(timerValueStream, conditionStream, 1, &store[0]);

    sendPQHandler.data = pq::UNHIDE_ELE;
    pqHandlerInput.write(sendPQHandler);

    ap_uint<64> minimizeStats[_FPGA_PARALLEL_MINIMIZE][2] = {{0,0},{0,0}};
    #pragma HLS array_partition variable=minimizeStats dim=0 complete
    unsigned int nonRemovableCountMinimize[_FPGA_PARALLEL_MINIMIZE] = {0,0};
    #pragma HLS array_partition variable=nonRemovableCountMinimize dim=0 complete
    bool didSimplify[_FPGA_PARALLEL_MINIMIZE] = {false,false};
    #pragma HLS array_partition variable=didSimplify dim=0 complete
    unsigned int overheadClear[_FPGA_PARALLEL_MINIMIZE] = {0,0};
    #pragma HLS array_partition variable=overheadClear dim=0 complete

    undo_states_and_minimize_task_parallel_wrapper(pqHandlerInput, toMinimizeStream,
        clsStates,
        lmmd, lmd, validBitMinimize, mergeScratchPadMinimize,
        nonRemovableCountMinimize, didSimplify, unitByCls,
        litStore, answerStack,  
        literalCommit, backtrackHeight, answerStackHeight, 
        foundAbsolute, LITERAL_PAGE_SIZE, POSITIVE_LIT_PHASE_VAL, clearIterations,
        litStoreAccessStats, minimizeStats, overheadClear,
        clauseStoreInputStream1, clauseStoreInputStream2,
        clauseStoreOutputStream1, clauseStoreOutputStream2, store);

    ACC_LEARNED: for(unsigned int k = 0; k < _FPGA_PARALLEL_MINIMIZE; k++){
        nonRemovableCount += nonRemovableCountMinimize[k];
        overhead += overheadClear[k];
        learnedStats[2] += minimizeStats[k][0];
        learnedStats[3] += minimizeStats[k][1];
    }

    sendClauseInputCommand.data.range(31,0) = 0;
    sendClauseInputCommand.data.range(63,32) = 0;
    sendClauseInputCommand.data.range(95,64) = csh::EXIT;

    clauseStoreInputStream1.write(sendClauseInputCommand);
    clauseStoreInputStream2.write(sendClauseInputCommand);
    

    sendPQHandler.data = pq::EXIT;
    pqHandlerInput.write(sendPQHandler);
    
    sendTime(timerValueStream, conditionStream, 1, &store[1]);
    cycleCounter[4] += store[1]-store[0];

    unsigned int savedLiteralCount = 0;
    if(foundAbsolute &&
            (!isValidLiteral(insertPropagate[0]) ||
             (unsigned int)abs(insertPropagate[0]) > NUM_LITERALS ||
             answerStackHeight >= NUM_LITERALS)){
        error = -9;
        return;
    }
    if(!foundAbsolute &&
            !countSavedClauseLiterals(resolutionClause, numElements,
                NUM_LITERALS, lmmd, savedLiteralCount)){
        error = -9;
        return;
    }
    nonRemovableCount = savedLiteralCount;

    unsigned int removeCount = 0;
    lit possibleFixedLiteral = 0;

    bool preemptResize[2] = {false,false};
    #pragma HLS array_partition variable=preemptResize complete
    
    sendTime(timerValueStream, conditionStream, 1, &store[0]);

    hls::stream<lit> litNewPage;
    #pragma HLS stream variable=litNewPage depth=_FPGA_MAX_LEARN_ELE

    clsState newClauseState;

    if(foundAbsolute){
        decisionLevel = 0;
    }else{
        newClauseState.compressedList = 0;
        newClauseState.remainingUnassigned = 0;

        ap_axiu<96,0,0,0> sendClauseInputCommand;
        sendClauseInputCommand.data = 0;
        sendClauseInputCommand.data.range(31,0) = nonRemovableCount;
        sendClauseInputCommand.data.range(95,64) = csh::SAVE;

        CHECK_CLS_SPACE:{
            #pragma HLS protocol fixed
            clauseStoreInputStream1.write(sendClauseInputCommand);

            ap_wait();

            error = clauseStoreOutputStream1.read().data;
            if(error == -4){
                return;
            }else{
                givenClsID = error;
            }
        }

        saveClauseDataflow(litNewPage, litStore,
            lmd, lmmd, newClauseState,
            resolutionClause, 
            numElements, givenClsID,
            levelBefore, constraintActivation, learntIsTemporary,
            true,
            LITERAL_PAGE_SIZE, clauseStoreInputStream1);
        newClauseState.remainingUnassigned = nonRemovableCount;

        if(didSimplify[0] || didSimplify[1]){
            if(longestClause[1] < nonRemovableCount){
                longestClause[1] = nonRemovableCount;
            }
            learnedStats[4]++;
        }

        insertPropagate[1] = possibleUIPLit;
    
        decisionLevel = levelBefore;
        if(!resetAll){
            if(!isValidLiteral(possibleUIPLit) ||
                    (unsigned int)abs(possibleUIPLit) > NUM_LITERALS){
                error = -9;
                return;
            }
            newClauseState.remainingUnassigned = 1;
            newClauseState.compressedList = insertPropagate[1];
        }

        clsStates[givenClsID%_FPGA_CLS_STATES_PARTITION][givenClsID/_FPGA_CLS_STATES_PARTITION] = newClauseState;
    }

    sendTime(timerValueStream, conditionStream, 1, &store[1]);
    cycleCounter[5] += store[1]-store[0];

    sendTime(timerValueStream, conditionStream, 1, &store[0]);

    if(!litNewPage.empty()){
        allocatePage(litNewPage, freeLitPageAddresses, lmd, 
            litStore, error,
            LITERAL_PAGE_SIZE);
    }

    sendTime(timerValueStream, conditionStream, 1, &store[1]);
    cycleCounter[6] += store[1]-store[0];
}
