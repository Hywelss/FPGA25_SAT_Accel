#include "backtrack.h"
#include "clause_state_cache.h"
#include "packed_clause_selector.h"

namespace {

void restoreQueryClauseState(
    clsState clsStates[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const unsigned int partition, const unsigned int address,
    const lit removedLiteral){
    #pragma HLS inline

    clsState state;
    switch(partition){
        case 0: state = clsStates[0][address]; break;
        case 1: state = clsStates[1][address]; break;
        case 2: state = clsStates[2][address]; break;
        case 3: state = clsStates[3][address]; break;
        case 4: state = clsStates[4][address]; break;
        case 5: state = clsStates[5][address]; break;
        case 6: state = clsStates[6][address]; break;
        default: state = clsStates[7][address]; break;
    }

    state.remainingUnassigned++;
    state.compressedList ^= -removedLiteral;

    switch(partition){
        case 0: clsStates[0][address] = state; break;
        case 1: clsStates[1][address] = state; break;
        case 2: clsStates[2][address] = state; break;
        case 3: clsStates[3][address] = state; break;
        case 4: clsStates[4][address] = state; break;
        case 5: clsStates[5][address] = state; break;
        case 6: clsStates[6][address] = state; break;
        default: clsStates[7][address] = state; break;
    }
}

} // namespace

void undoStates(hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput, hls::stream<colorAssignment>& toCommitStream,
    const lit answerStack[_FPGA_MAX_LITERALS], literalMetaData lmd[_FPGA_MAX_LITERALS],
    const lit literalCommit, unsigned int& answerStackHeight, 
    const unsigned int backtrackHeight, const ap_uint<1> POSITIVE_LIT_PHASE_VAL){
    #pragma HLS inline off

    bool foundLastCommit = false;
    GET_UNDO_LITERALS: for(unsigned int i = 0;
            i < backtrackHeight && answerStackHeight != 0; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        #pragma HLS dependence variable=lmd inter false
        #pragma HLS pipeline II=1

        lit getLit = answerStack[--answerStackHeight];
        if(getLit == 0 || getLit > _FPGA_MAX_LITERALS ||
                getLit < -_FPGA_MAX_LITERALS){
            continue;
        }
        literalMetaData getLmd = lmd[abs(getLit)-1];

        if(literalCommit == getLit){
            foundLastCommit = true;
        }

        if(foundLastCommit){
            ap_uint<1> selectSide = 0;
            if(getLit > 0){
                selectSide = 1;   
            }

            
            unsigned int numElements = (unsigned int)LMD_NUM_ELE(getLmd.compactlmd,selectSide);
            if(numElements > 0){
                toCommitStream.write((colorAssignment){.addressStart=(unsigned int)LMD_ADDR_START(getLmd.compactlmd,selectSide),
                    .numElements=numElements,.depthCount=0,.literal=getLit,.eos=false});
            }     
            ap_axiu<32,0,0,0> sendPQHandler;
            sendPQHandler.data = abs(getLit);
            pqHandlerInput.write(sendPQHandler);
        }

        literalMetaData wipeMetalmd = getLmd;

        LMD_IS_IN_STACK(wipeMetalmd.compactlmd) = false;
        if(getLit > 0){
            LMD_PHASE(wipeMetalmd.compactlmd) = POSITIVE_LIT_PHASE_VAL;
        }else{
            LMD_PHASE(wipeMetalmd.compactlmd) = !POSITIVE_LIT_PHASE_VAL;
        }

        lmd[abs(getLit)-1] = wipeMetalmd;
        
    }

    toCommitStream.write((colorAssignment){.addressStart=0,.depthCount=0,.literal=0,.eos=true});
}

void updateStatesBackward(hls::stream<colorValue>& toStateUpdater, 
    clsState clsStates[_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION], const unsigned int id){
    #pragma HLS inline off

    const int DEPENDENCY = _FPGA_CLS_DEP_DIST-1;
    colorValue get;
    cls clsID = 0;
    get.clsID = 0;
    get.streamEos = false;
    get.clsEos = false;
    bool didGet = false;
    const unsigned int READ_SIZE_CHUNK = _FPGA_CLS_STATES_PARTITION;
    int currentIndex = READ_SIZE_CHUNK-1;

    unsigned int cacheAddr[_FPGA_CLS_DEP_DIST];
    #pragma HLS array_partition variable=cacheAddr complete
    clsState cacheState[_FPGA_CLS_DEP_DIST];
    #pragma HLS array_partition variable=cacheState complete

    INVALID_ADDR: for(unsigned int i = 0; i < _FPGA_CLS_DEP_DIST; i++){
        cacheAddr[i] = _FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION;
        cacheState[i] = {};
    }

    UPDATE_BACKWARDS: while(true){
        #pragma HLS loop_tripcount min=512 max=512
        #pragma HLS dependence variable=clsStates inter true distance=DEPENDENCY
        #pragma HLS pipeline II=1

        if(didGet){
            if(get.streamEos){
                break; 
            }else{
                if(clsID > 0 && (unsigned int)clsID <= _FPGA_MAX_CLAUSES){
                    unsigned int addr = (clsID-1)/_FPGA_CLS_STATES_PARTITION;
                    clsState getState = clsStates[addr];

                    getState = selectNewestClauseState(addr, getState,
                        cacheAddr, cacheState);
                    
                    getState.remainingUnassigned++;
                    getState.compressedList ^= (-get.litID);
                        
                    clsStates[reg(addr)] = reg(getState);

                    for(unsigned int i = 0; i < _FPGA_CLS_DEP_DIST-1; i++){
                        cacheState[i] = cacheState[i+1];
                        cacheAddr[i] = cacheAddr[i+1];
                    }
                    cacheState[_FPGA_CLS_DEP_DIST-1] = getState;
                    cacheAddr[_FPGA_CLS_DEP_DIST-1] = addr;
                }
            }

        }

        if(currentIndex == READ_SIZE_CHUNK-1){
            didGet = toStateUpdater.read_nb(get);
        }
        if(didGet){
            clsID = 0;
            currentIndex = READ_SIZE_CHUNK-1;
            ap_uint<3> selectedIndex = 0;
            const bool foundClause = selectPackedClause<false>(
                get.clsID, id, clsID, selectedIndex);
            if(foundClause){
                currentIndex = selectedIndex;
                get.clsID.range(32*currentIndex+31,32*currentIndex) = 0;
            }
        }
    }
}

void undo_states_dataflow_wrapper(hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput, 
    clsState clsStates[_FPGA_CLS_STATES_PARTITION][_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const lit answerStack[_FPGA_MAX_LITERALS], literalMetaData lmd[_FPGA_MAX_LITERALS],
    const ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    const lit literalCommit, const unsigned int backtrackHeight, unsigned int& answerStackHeight,
    const unsigned int LITERAL_PAGE_SIZE, const ap_uint<1> POSITIVE_LIT_PHASE_VAL, ap_uint<64> litStoreAccessStats[4]){

    #pragma HLS inline off

    hls::stream<colorAssignment> toColorStream;
    #pragma HLS stream variable=toColorStream depth=4

    hls::stream<colorValue> toStateUpdater[_FPGA_CLS_STATES_PARTITION];
    #pragma HLS stream variable=toStateUpdater depth=4
    #pragma HLS array_partition variable=toStateUpdater complete

    #pragma HLS dataflow

    undoStates(pqHandlerInput, toColorStream,
        answerStack, lmd, literalCommit, answerStackHeight, 
        backtrackHeight, POSITIVE_LIT_PHASE_VAL);

    colorStream(toStateUpdater, toColorStream, nullptr, literalStore,
        LITERAL_PAGE_SIZE, nullptr, 1, litStoreAccessStats);

    updateStatesBackward(toStateUpdater[0], clsStates[0], 0);
    updateStatesBackward(toStateUpdater[1], clsStates[1], 1);
    updateStatesBackward(toStateUpdater[2], clsStates[2], 2);
    updateStatesBackward(toStateUpdater[3], clsStates[3], 3);
    updateStatesBackward(toStateUpdater[4], clsStates[4], 4);
    updateStatesBackward(toStateUpdater[5], clsStates[5], 5);
    updateStatesBackward(toStateUpdater[6], clsStates[6], 6);
    updateStatesBackward(toStateUpdater[7], clsStates[7], 7);
}

bool cleanupQueryAssignments(
    hls::stream<ap_axiu<32,0,0,0>>& pqHandlerInput,
    clsState clsStates[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION],
    const lit answerStack[_FPGA_MAX_LITERALS],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    const ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    const unsigned int targetStackHeight, unsigned int& answerStackHeight,
    const unsigned int LITERAL_PAGE_SIZE,
    const ap_uint<1> POSITIVE_LIT_PHASE_VAL,
    ap_uint<64> litStoreAccessStats[4]){
    #pragma HLS inline off

    bool valid = targetStackHeight <= answerStackHeight &&
        answerStackHeight <= _FPGA_MAX_LITERALS &&
        LITERAL_PAGE_SIZE >= 16 &&
        LITERAL_PAGE_SIZE <= _FPGA_MAX_LITERAL_ELEMENTS &&
        LITERAL_PAGE_SIZE%16 == 0;
    const unsigned int safeTarget = targetStackHeight <= answerStackHeight
        ? targetStackHeight : answerStackHeight;

    CLEANUP_QUERY_ASSIGNMENTS: for(unsigned int assignmentIndex = 0;
            assignmentIndex < _FPGA_MAX_LITERALS &&
            answerStackHeight > safeTarget; assignmentIndex++){
        #pragma HLS loop_tripcount min=0 max=32768

        const lit removedLiteral = answerStack[--answerStackHeight];
        if(!isValidLiteral(removedLiteral)){
            valid = false;
            continue;
        }

        const unsigned int variable = abs(removedLiteral)-1;
        literalMetaData metadata = lmd[variable];
        const bool wasAssigned = LMD_IS_IN_STACK(metadata.compactlmd);
        if(!wasAssigned){
            valid = false;
        }

        const ap_uint<1> side = removedLiteral > 0 ? 1 : 0;
        const unsigned int numElements =
            LMD_NUM_ELE(metadata.compactlmd, side);
        unsigned int address = LMD_ADDR_START(metadata.compactlmd, side);

        bool validList = wasAssigned && numElements <=
            _FPGA_MAX_LITERAL_ELEMENTS;
        if(numElements != 0 &&
                (address >= _FPGA_MAX_LITERAL_ELEMENTS || address%16 != 0)){
            validList = false;
        }

        unsigned int elementsRead = 0;
        unsigned int pageOffset = 0;
        const unsigned int usablePerPage = LITERAL_PAGE_SIZE >= 2
            ? LITERAL_PAGE_SIZE-2 : 0;
        const unsigned int pageCount = usablePerPage == 0 ? 0 :
            (numElements + usablePerPage-1)/usablePerPage;
        const unsigned int traversalLimit =
            pageCount*(LITERAL_PAGE_SIZE/_FPGA_CLS_STATES_PARTITION);

        WALK_QUERY_LITERAL_LIST: for(unsigned int chunk = 0;
                chunk < _FPGA_MAX_LITERAL_ELEMENTS/
                    _FPGA_CLS_STATES_PARTITION &&
                chunk < traversalLimit && elementsRead < numElements;
                chunk++){
            #pragma HLS loop_tripcount min=0 max=65536

            if(!validList || address >= _FPGA_MAX_LITERAL_ELEMENTS){
                validList = false;
                break;
            }

            const ap_uint<512> packed = literalStore[address/16];
            const bool upperHalf =
                (pageOffset & _FPGA_CLS_STATES_PARTITION) != 0;
            const ap_uint<256> clauseIDs = upperHalf
                ? packed.range(511,256) : packed.range(255,0);
            const bool pageEnd = pageOffset +
                _FPGA_CLS_STATES_PARTITION == LITERAL_PAGE_SIZE;
            const unsigned int chunkCapacity = pageEnd
                ? _FPGA_CLS_STATES_PARTITION-2
                : _FPGA_CLS_STATES_PARTITION;
            const unsigned int remaining = numElements-elementsRead;
            const unsigned int validLanes = remaining < chunkCapacity
                ? remaining : chunkCapacity;

            RESTORE_QUERY_CLAUSES: for(unsigned int lane = 0;
                    lane < _FPGA_CLS_STATES_PARTITION; lane++){
                #pragma HLS loop_tripcount min=8 max=8
                if(lane >= validLanes){
                    continue;
                }
                const cls clauseID = clauseIDs.range(32*lane+31,32*lane);
                if(clauseID <= 0 ||
                        (unsigned int)clauseID > _FPGA_MAX_CLAUSES){
                    validList = false;
                    continue;
                }
                const unsigned int zeroBased = (unsigned int)clauseID-1;
                restoreQueryClauseState(clsStates,
                    zeroBased%_FPGA_CLS_STATES_PARTITION,
                    zeroBased/_FPGA_CLS_STATES_PARTITION,
                    removedLiteral);
            }

            elementsRead += validLanes;
            address += _FPGA_CLS_STATES_PARTITION;
            pageOffset += _FPGA_CLS_STATES_PARTITION;
            litStoreAccessStats[3]++;

            if(pageEnd){
                pageOffset = 0;
                if(elementsRead < numElements){
                    const unsigned int nextPage = packed.range(511,480);
                    if(nextPage >= _FPGA_MAX_LITERAL_ELEMENTS ||
                            nextPage%16 != 0){
                        validList = false;
                        break;
                    }
                    address = nextPage;
                }
            }
        }

        if(numElements != 0){
            litStoreAccessStats[1]++;
        }
        if(elementsRead != numElements || !validList){
            valid = false;
        }

        LMD_IS_IN_STACK(metadata.compactlmd) = false;
        if(removedLiteral > 0){
            LMD_PHASE(metadata.compactlmd) = POSITIVE_LIT_PHASE_VAL;
        }else{
            LMD_PHASE(metadata.compactlmd) = !POSITIVE_LIT_PHASE_VAL;
        }
        lmd[variable] = metadata;

        ap_axiu<32,0,0,0> queueUpdate;
        queueUpdate.data = abs(removedLiteral);
        pqHandlerInput.write(queueUpdate);
    }

    if(answerStackHeight != safeTarget){
        valid = false;
    }
    return valid;
}
