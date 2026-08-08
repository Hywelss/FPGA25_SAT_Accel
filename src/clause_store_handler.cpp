#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <ap_utils.h>
#include "data_structures.h"

static bool validClausePageSize(const unsigned int pageSize){
    #pragma HLS inline
    return pageSize >= 4 && pageSize <= _FPGA_MAX_LITERAL_ELEMENTS &&
        pageSize%4 == 0;
}

static unsigned int safeClauseLength(
    const cls clauseID,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES]){
    #pragma HLS inline
    if(clauseID < 0 || (unsigned int)clauseID >= _FPGA_MAX_CLAUSES){
        return 0;
    }
    const unsigned int length = mCmd[clauseID].numElements;
    return length <= _FPGA_MAX_LITERAL_ELEMENTS ? length : 0;
}

void copyCls(ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4], ap_uint<128>* clauseStore, const unsigned int clauseElements){
    #pragma HLS inline off

    unsigned int clauseElements4 = clauseElements/4;
    if(clauseElements%4 != 0){
        clauseElements4++;
    }
    COPY_CLS_STORE: for(unsigned int i = 0; i < clauseElements4; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        mClsStore[i] = reg(reg(reg(clauseStore[i])));
    }
}

void copyCmd(clauseMetaData mCmd[_FPGA_MAX_CLAUSES], clauseMetaData* cmd, const unsigned int ORIGINAL_CLS_CNT){
    #pragma HLS inline off
    COPY_CMD: for(unsigned int i = 0; i < ORIGINAL_CLS_CNT; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        mCmd[i] = reg(reg(cmd[i]));
    }
}

void copy_cls_data(ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4], clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128>* clauseStore, clauseMetaData* cmd,
    const unsigned int clauseElements, const unsigned int ORIGINAL_CLS_CNT){
    #pragma HLS inline off
    #pragma HLS dataflow

    copyCmd(mCmd, cmd, ORIGINAL_CLS_CNT);
    copyCls(mClsStore, clauseStore, clauseElements);
}

void axiStreamBuffer_sendDelete(hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream, hls::stream<ap_uint<96>>& intermediateStream){
    #pragma HLS inline off

    BUFFER_LOOP_DELETE: while(true){
        #pragma HLS loop_tripcount min=16 max=16
        ap_axiu<96,0,0,0> read = clauseStoreInputStream.read();

        if((int)read.data.range(95,64) == csh::EXIT){
            break;
        }

        ap_uint<96> getData = read.data;
        intermediateStream.write(getData);  
    }
}

#ifdef FPGA_HW
void axiStreamBuffer_sendLength(hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream, hls::stream<ap_uint<32>>& intermediateStream){
#else
void axiStreamBuffer_sendLength(hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream, 
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES]){
#endif
    #pragma HLS inline off
    BUFFER_LOOP: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        ap_axiu<96,0,0,0> read = clauseStoreInputStream.read();

        cls getCls = read.data.range(31,0);

        #ifdef FPGA_HW
        ap_uint<32> send = getCls;
        intermediateStream.write(send);
        if(getCls == csh::EXIT){
            break;
        }
        #else

        if(getCls == csh::EXIT){
            break;
        }
        ap_axiu<32,0,0,0> sendData;
        sendData.data = safeClauseLength(getCls, mCmd);
        clauseStoreOutputStream.write(sendData);
        #endif
    }
}

void sendLength(hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream, hls::stream<ap_uint<32>>& intermediateStream, 
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES]){
    #pragma HLS inline off
    LOOP_SEND_LENGTH: while(true){

        ap_uint<32> read;
        if(intermediateStream.read_nb(read)){
            cls clsID = read.range(31,0);
            if(clsID == csh::EXIT){
                break;
            }

            ap_axiu<32,0,0,0> sendData;
            sendData.data = safeClauseLength(clsID, mCmd);
            clauseStoreOutputStream.write(sendData);
            
        }
    }
}

void sendLength_wrapper(hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream, 
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES]){
    #pragma HLS inline off

    hls::stream<ap_uint<32>> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=16

    #pragma HLS dataflow
    
    #ifdef FPGA_HW
    axiStreamBuffer_sendLength(clauseStoreInputStream1, intermediateStream);
    sendLength(clauseStoreOutputStream, intermediateStream, mCmd);
    #else
    axiStreamBuffer_sendLength(clauseStoreInputStream1, clauseStoreOutputStream, mCmd);
    #endif

}

void rejectLengthRequests(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off
    REJECT_LENGTH_REQUESTS: while(true){
        #pragma HLS loop_tripcount min=1 max=1024
        const ap_axiu<96,0,0,0> request = clauseStoreInputStream.read();
        if((int)request.data.range(31,0) == csh::EXIT){
            break;
        }
        ap_axiu<32,0,0,0> response;
        response.data = 0;
        clauseStoreOutputStream.write(response);
    }
}

void rejectClauseRequests(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2){
    #pragma HLS inline off
    bool done1 = false;
    bool done2 = false;
    REJECT_CLAUSE_REQUESTS: while(!done1 || !done2){
        #pragma HLS loop_tripcount min=1 max=1024
        #pragma HLS pipeline II=1
        if(!done1 && !clauseStoreInputStream1.empty() &&
                !clauseStoreOutputStream1.full()){
            const ap_axiu<96,0,0,0> request = clauseStoreInputStream1.read();
            if((int)request.data.range(95,64) == csh::EXIT){
                done1 = true;
            }else{
                ap_axiu<32,0,0,0> response;
                response.data = 0;
                clauseStoreOutputStream1.write(response);
            }
        }
        if(!done2 && !clauseStoreInputStream2.empty() &&
                !clauseStoreOutputStream2.full()){
            const ap_axiu<96,0,0,0> request = clauseStoreInputStream2.read();
            if((int)request.data.range(95,64) == csh::EXIT){
                done2 = true;
            }else{
                ap_axiu<32,0,0,0> response;
                response.data = 0;
                clauseStoreOutputStream2.write(response);
            }
        }
    }
}

void sendDataScheduler(const ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_uint<33>>& outputBuffer1,
    hls::stream<ap_uint<33>>& outputBuffer2){
    #pragma HLS inline off

    enum LaneState {
        LANE_IDLE,
        LANE_READ,
        LANE_FOLLOW_PAGE,
        LANE_SEND_END,
        LANE_SEND_EXIT,
        LANE_DONE
    };

    LaneState state1 = LANE_IDLE;
    LaneState state2 = LANE_IDLE;
    unsigned int address1 = 0;
    unsigned int address2 = 0;
    unsigned int subIndex1 = 0;
    unsigned int subIndex2 = 0;
    unsigned int processed1 = 0;
    unsigned int processed2 = 0;
    unsigned int clauseLength1 = 0;
    unsigned int clauseLength2 = 0;
    unsigned int pageSize1 = CLAUSE_PAGE_SIZE;
    unsigned int pageSize2 = CLAUSE_PAGE_SIZE;
    unsigned int nextPage1 = 0;
    unsigned int nextPage2 = 0;

    SEND_DATA_DUAL: while(state1 != LANE_DONE || state2 != LANE_DONE){
        #pragma HLS loop_tripcount min=16 max=1024
        #pragma HLS pipeline II=2

        if(state1 == LANE_IDLE && !clauseStoreInputStream1.empty()){
            const ap_axiu<96,0,0,0> command = clauseStoreInputStream1.read();
            if((int)command.data.range(95,64) == csh::EXIT){
                state1 = LANE_SEND_EXIT;
            }else{
                const cls clauseID = command.data.range(31,0);
                if(clauseID < 0 || (unsigned int)clauseID >= _FPGA_MAX_CLAUSES){
                    state1 = LANE_SEND_END;
                }else{
                    const clauseMetaData metadata = mCmd[clauseID];
                    address1 = metadata.addressStart;
                    clauseLength1 = metadata.numElements;
                    subIndex1 = 0;
                    processed1 = 0;
                    pageSize1 = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
                    const bool validMetadata = metadata.numElements > 0 &&
                        metadata.numElements <= _FPGA_MAX_LITERAL_ELEMENTS &&
                        metadata.addressStart%4 == 0 &&
                        validClausePageSize(pageSize1);
                    state1 = validMetadata ? LANE_READ : LANE_SEND_END;
                }
            }
        }else if(state1 == LANE_READ){
            if(address1 >= _FPGA_MAX_LITERAL_ELEMENTS){
                state1 = LANE_SEND_END;
                continue;
            }
            const ap_uint<128> line = mClsStore[address1/4];
            ap_uint<33> output = 0;
            output.range(31,0) = line.range(32*(subIndex1%4)+31,
                                               32*(subIndex1%4));
            const lit literal = (ap_int<32>)output.range(31,0);
            if(literal == 0 || literal > _FPGA_MAX_LITERALS ||
                    literal < -_FPGA_MAX_LITERALS){
                state1 = LANE_SEND_END;
            }else if(!outputBuffer1.full()){
                outputBuffer1.write(output);
                processed1++;
                subIndex1++;
                if(processed1 == clauseLength1){
                    state1 = LANE_SEND_END;
                }else if(subIndex1 == pageSize1-1){
                    nextPage1 = line.range(127,96);
                    subIndex1 = 0;
                    state1 = LANE_FOLLOW_PAGE;
                }else{
                    address1++;
                }
            }
        }else if(state1 == LANE_FOLLOW_PAGE){
            address1 = nextPage1;
            state1 = LANE_READ;
        }else if(state1 == LANE_SEND_END){
            const ap_uint<33> endOfClause = 0;
            if(!outputBuffer1.full()){
                outputBuffer1.write(endOfClause);
                state1 = LANE_IDLE;
            }
        }else if(state1 == LANE_SEND_EXIT){
            ap_uint<33> exitToken = 0;
            exitToken[32] = 1;
            if(!outputBuffer1.full()){
                outputBuffer1.write(exitToken);
                state1 = LANE_DONE;
            }
        }

        if(state2 == LANE_IDLE && !clauseStoreInputStream2.empty()){
            const ap_axiu<96,0,0,0> command = clauseStoreInputStream2.read();
            if((int)command.data.range(95,64) == csh::EXIT){
                state2 = LANE_SEND_EXIT;
            }else{
                const cls clauseID = command.data.range(31,0);
                if(clauseID < 0 || (unsigned int)clauseID >= _FPGA_MAX_CLAUSES){
                    state2 = LANE_SEND_END;
                }else{
                    const clauseMetaData metadata = mCmd[clauseID];
                    address2 = metadata.addressStart;
                    clauseLength2 = metadata.numElements;
                    subIndex2 = 0;
                    processed2 = 0;
                    pageSize2 = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
                    const bool validMetadata = metadata.numElements > 0 &&
                        metadata.numElements <= _FPGA_MAX_LITERAL_ELEMENTS &&
                        metadata.addressStart%4 == 0 &&
                        validClausePageSize(pageSize2);
                    state2 = validMetadata ? LANE_READ : LANE_SEND_END;
                }
            }
        }else if(state2 == LANE_READ){
            if(address2 >= _FPGA_MAX_LITERAL_ELEMENTS){
                state2 = LANE_SEND_END;
                continue;
            }
            const ap_uint<128> line = mClsStore[address2/4];
            ap_uint<33> output = 0;
            output.range(31,0) = line.range(32*(subIndex2%4)+31,
                                               32*(subIndex2%4));
            const lit literal = (ap_int<32>)output.range(31,0);
            if(literal == 0 || literal > _FPGA_MAX_LITERALS ||
                    literal < -_FPGA_MAX_LITERALS){
                state2 = LANE_SEND_END;
            }else if(!outputBuffer2.full()){
                outputBuffer2.write(output);
                processed2++;
                subIndex2++;
                if(processed2 == clauseLength2){
                    state2 = LANE_SEND_END;
                }else if(subIndex2 == pageSize2-1){
                    nextPage2 = line.range(127,96);
                    subIndex2 = 0;
                    state2 = LANE_FOLLOW_PAGE;
                }else{
                    address2++;
                }
            }
        }else if(state2 == LANE_FOLLOW_PAGE){
            address2 = nextPage2;
            state2 = LANE_READ;
        }else if(state2 == LANE_SEND_END){
            const ap_uint<33> endOfClause = 0;
            if(!outputBuffer2.full()){
                outputBuffer2.write(endOfClause);
                state2 = LANE_IDLE;
            }
        }else if(state2 == LANE_SEND_EXIT){
            ap_uint<33> exitToken = 0;
            exitToken[32] = 1;
            if(!outputBuffer2.full()){
                outputBuffer2.write(exitToken);
                state2 = LANE_DONE;
            }
        }
    }
}

void sendDataOutput(hls::stream<ap_uint<33>>& outputBuffer,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    SEND_DATA_OUTPUT: while(true){
        #pragma HLS loop_tripcount min=16 max=1024
        #pragma HLS pipeline II=2
        const ap_uint<33> buffered = outputBuffer.read();
        if(buffered[32]){
            break;
        }

        ap_axiu<32,0,0,0> output;
        output.data = buffered.range(31,0);
        clauseStoreOutputStream.write(output);
    }
}

#if !defined(FPGA_HW) && !defined(__SYNTHESIS__)
void sendClauseSequentialModel(
    const ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE, const cls clauseID,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    if(clauseID < 0 || (unsigned int)clauseID >= _FPGA_MAX_CLAUSES){
        ap_axiu<32,0,0,0> endOfClause;
        endOfClause.data = 0;
        clauseStoreOutputStream.write(endOfClause);
        return;
    }

    const clauseMetaData metadata = mCmd[clauseID];
    const unsigned int pageSize = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
    if(metadata.numElements == 0 ||
            metadata.numElements > _FPGA_MAX_LITERAL_ELEMENTS ||
            metadata.addressStart%4 != 0 || !validClausePageSize(pageSize)){
        ap_axiu<32,0,0,0> endOfClause;
        endOfClause.data = 0;
        clauseStoreOutputStream.write(endOfClause);
        return;
    }
    unsigned int address = metadata.addressStart;
    unsigned int subIndex = 0;

    SEND_CLAUSE_SEQUENTIAL_MODEL: for(unsigned int processed = 0;
            processed < metadata.numElements; processed++){
        if(address >= _FPGA_MAX_LITERAL_ELEMENTS){
            break;
        }
        const ap_uint<128> line = mClsStore[address/4];
        ap_axiu<32,0,0,0> output;
        output.data = line.range(32*(subIndex%4)+31, 32*(subIndex%4));
        const lit literal = output.data;
        if(literal == 0 || literal > _FPGA_MAX_LITERALS ||
                literal < -_FPGA_MAX_LITERALS){
            break;
        }
        clauseStoreOutputStream.write(output);

        subIndex++;
        address++;
        if(processed + 1 < metadata.numElements && subIndex == pageSize-1){
            address = line.range(127,96);
            subIndex = 0;
        }
    }

    ap_axiu<32,0,0,0> endOfClause;
    endOfClause.data = 0;
    clauseStoreOutputStream.write(endOfClause);
}

void sendDataSequentialModel(
    const ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    SEND_DATA_SEQUENTIAL_MODEL: while(true){
        const ap_axiu<96,0,0,0> command = clauseStoreInputStream.read();
        if((int)command.data.range(95,64) == csh::EXIT){
            break;
        }
        sendClauseSequentialModel(mClsStore, mCmd, compactClauseLayout,
            CLAUSE_PAGE_SIZE, command.data.range(31,0), clauseStoreOutputStream);
    }
}
#endif

void sendData_dataflow(const ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2){
    #pragma HLS inline off

    #if defined(FPGA_HW) || defined(__SYNTHESIS__)
    hls::stream<ap_uint<33>> outputBuffer1;
    hls::stream<ap_uint<33>> outputBuffer2;
    #pragma HLS stream variable=outputBuffer1 depth=2
    #pragma HLS stream variable=outputBuffer2 depth=2
    #pragma HLS dataflow

    sendDataScheduler(mClsStore, mCmd, compactClauseLayout, CLAUSE_PAGE_SIZE,
        clauseStoreInputStream1, clauseStoreInputStream2,
        outputBuffer1, outputBuffer2);
    sendDataOutput(outputBuffer1, clauseStoreOutputStream1);
    sendDataOutput(outputBuffer2, clauseStoreOutputStream2);
    #else
    sendDataSequentialModel(mClsStore, mCmd, compactClauseLayout,
        CLAUSE_PAGE_SIZE, clauseStoreInputStream1, clauseStoreOutputStream1);
    sendDataSequentialModel(mClsStore, mCmd, compactClauseLayout,
        CLAUSE_PAGE_SIZE, clauseStoreInputStream2, clauseStoreOutputStream2);
    #endif
}

void saveData(ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    const clauseMetaData cmd, const unsigned int CLAUSE_PAGE_SIZE, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream){
    #pragma HLS inline off

    unsigned int reqAddrOffsetCls = 0;
    ap_uint<128> get = 0;
    unsigned int tmpAddr = cmd.addressStart;
    unsigned int processed = 0;
    unsigned int nextAddr = 0;

    ap_axiu<64,0,0,0> updateLitStorePos;
    updateLitStorePos.data = 0;
    updateLitStorePos.data.range(31,0) = lh::SAVE;
    locationInputStream.write(updateLitStorePos);

    for(unsigned int i = 0; i < cmd.numElements; i++){
        ap_axiu<96,0,0,0> getData = clauseStoreInputStream1.read();

        get.range(32*(reqAddrOffsetCls%4)+31,32*(reqAddrOffsetCls%4)) = getData.data.range(31,0);

        unsigned int clsToLitAddr = tmpAddr + (reqAddrOffsetCls%4);

        ap_axiu<64,0,0,0> updateLitStorePos;
        updateLitStorePos.data.range(31,0) = clsToLitAddr;
        updateLitStorePos.data.range(63,32) = getData.data.range(63,32);

        locationInputStream.write(updateLitStorePos);

        processed++;
        reqAddrOffsetCls++;

        bool useNewPage = false;
        if(reqAddrOffsetCls == CLAUSE_PAGE_SIZE-1){
            if(i + 1 < cmd.numElements){
                nextAddr = reg(freeClsPageAddresses.read());
                
                get.range(127,96) = nextAddr;
                useNewPage = true;
            }
            reqAddrOffsetCls = 0;
        }

        mClsStore[tmpAddr/4] = get; 

        if(reqAddrOffsetCls%4 == 0){
            if(useNewPage){
                tmpAddr = nextAddr;
            }else{
                tmpAddr += 4;
            }
            get = 0;
        }  
    }

    updateLitStorePos.data = lh::EXIT;
    locationInputStream.write(updateLitStorePos);
}

void getDeletedClsID(hls::stream<cls>& removeIDStream,
    cls* usedClsIDBuckets, minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS],
    cls lastInsertedID, unsigned int removeTotal,
    unsigned int LBDBucketCount[_FPGA_MAX_LBD_BUCKETS]){
    #pragma HLS inline off

    unsigned int removedCount = 0;
    GET_REMOVE_ID: for(int bucketIndex = _FPGA_MAX_LBD_BUCKETS-1;
            bucketIndex >= 0 && removedCount < removeTotal; bucketIndex--){
        #pragma HLS loop_tripcount min=1 max=10

        const unsigned int bucket = (unsigned int)bucketIndex;
        const unsigned int initialUsedCount = tracker[bucket].usedCount;
        unsigned int accessIdx = tracker[bucket].accessIdx;
        unsigned int removedFromBucket = 0;
        bool protectedFound = false;

        SCAN_REMOVE_BUCKET: for(unsigned int scanned = 0;
                scanned < initialUsedCount && removedCount < removeTotal;
                scanned++){
            #pragma HLS loop_tripcount min=1 max=128

            const cls getClsID =
                usedClsIDBuckets[_FPGA_MAX_CLAUSES*bucket+accessIdx];
            accessIdx = (accessIdx+1)%_FPGA_MAX_CLAUSES;

            if(getClsID == lastInsertedID){
                protectedFound = true;
                continue;
            }

            LBDBucketCount[bucket]++;
            removedCount++;
            removedFromBucket++;
            removeIDStream.write(getClsID);
        }

        tracker[bucket].accessIdx = accessIdx;
        tracker[bucket].usedCount = initialUsedCount-removedFromBucket;

        // Keep the remaining ring contiguous.  This write is deliberately
        // outside the external-memory scan pipeline: placing it in that loop
        // expands one AXI write response into a 93-cycle recurrence.
        if(protectedFound){
            const unsigned int insertIdx = tracker[bucket].insertIdx;
            usedClsIDBuckets[_FPGA_MAX_CLAUSES*bucket+insertIdx] =
                lastInsertedID;
            tracker[bucket].insertIdx = (insertIdx+1)%_FPGA_MAX_CLAUSES;
        }
    }
}

#ifdef FPGA_HW
void deleteClauses(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    const cls removeID, hls::stream<ap_uint<96>>& intermediateStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE){
#else
void deleteClauses(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    const cls removeID,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE){
#endif

    #pragma HLS inline off

    ap_axiu<32,0,0,0> sendData;
    sendData.data = removeID;
    clauseStoreOutputStream1.write(sendData);

    // Preserve the fixed response framing even if an explicit-delete list is
    // stale or duplicated.  Returning a zero length lets the solver finish
    // this deletion record without indexing metadata or waiting for literals.
    if(removeID < 0 || (unsigned int)removeID >= _FPGA_MAX_CLAUSES ||
            mCmd[removeID].numElements == 0){
        sendData.data = 0;
        clauseStoreOutputStream1.write(sendData);
        return;
    }

    const clauseMetaData getCmd = mCmd[removeID];
    const bool compactLayout = compactClauseLayout[removeID];
    const unsigned int usePageSize = compactLayout ? 4 : CLAUSE_PAGE_SIZE;
    bool validClause = getCmd.numElements <= _FPGA_MAX_LEARN_ELE &&
        usePageSize >= 4 && usePageSize <= _FPGA_MAX_LITERAL_ELEMENTS &&
        usePageSize%4 == 0 && getCmd.addressStart%4 == 0;
    unsigned int validateAddress = getCmd.addressStart;
    unsigned int validateSubIndex = 0;

    VALIDATE_DELETE_CLAUSE: for(unsigned int processed = 0;
            processed < getCmd.numElements && validClause; processed++){
        #pragma HLS loop_tripcount min=1 max=1024
        if(validateAddress >= _FPGA_MAX_LITERAL_ELEMENTS){
            validClause = false;
            break;
        }

        const ap_uint<128> line = mClsStore[validateAddress/4];
        const lit literal = line.range(
            32*(validateSubIndex%4)+31, 32*(validateSubIndex%4));
        if(literal == 0 || literal > _FPGA_MAX_LITERALS ||
                literal < -_FPGA_MAX_LITERALS){
            validClause = false;
            break;
        }

        validateSubIndex++;
        validateAddress++;
        if(processed + 1 < getCmd.numElements &&
                validateSubIndex == usePageSize-1){
            validateAddress = line.range(127,96);
            validateSubIndex = 0;
            if(validateAddress >= _FPGA_MAX_LITERAL_ELEMENTS ||
                    validateAddress%4 != 0){
                validClause = false;
            }
        }
    }

    if(!validClause){
        sendData.data = 0;
        clauseStoreOutputStream1.write(sendData);
        // A zero-length response tells the producer not to emit updates for
        // this record. Waiting for the stale metadata length here would leave
        // both kernels permanently blocked on each other.
        return;
    }

    freeClsID.write(removeID);
    mCmd[removeID].numElements = 0;

    sendData.data = getCmd.numElements;
    clauseStoreOutputStream1.write(sendData);

    unsigned int subIndex = 0;
    unsigned int reqAddrLit = getCmd.addressStart;
    unsigned int state = 0;
    unsigned int processed = 0;
    unsigned int tmpAddr = 0;
    unsigned int numElements = getCmd.numElements;
    ap_axiu<64,0,0,0> updateLitStorePos;
    updateLitStorePos.data = 0;
    updateLitStorePos.data.range(31,0) = lh::SEND;
    locationInputStream.write(updateLitStorePos);

    ap_uint<128> get = 0;
    SEND_DATA_DELETE_NO_WRAP: while(numElements != 0){
        #pragma HLS loop_tripcount min=16 max=16

        if(state == 0){
            if(subIndex == 0 && !compactLayout){
                freeClsPageAddresses.write(reqAddrLit);
            }

            get = mClsStore[reqAddrLit/4];
            tmpAddr = get.range(127,96);

            ap_axiu<32,0,0,0> sendData;
            sendData.data = get.range(32*(subIndex%4)+31,32*(subIndex%4));
            clauseStoreOutputStream1.write(sendData);

            ap_axiu<64,0,0,0> updateLitStorePos;
            updateLitStorePos.data = 0;
            updateLitStorePos.data.range(31,0) = (reqAddrLit/4)*4+subIndex%4;
            locationInputStream.write(updateLitStorePos);
                
            subIndex++;
            reqAddrLit++;
            processed++;
                
        if(processed == numElements){
                state = 2;
            }else if(subIndex == usePageSize-1){
                subIndex = 0;
                state = 1;
            }
        }else if(state == 1){
            state = 0;
            reqAddrLit = tmpAddr;
        }else if(state == 2){
            break;
        }
    }

    updateLitStorePos.data = lh::EXIT;
    locationInputStream.write(updateLitStorePos);

    ap_wait();

    updateLitStorePos.data = 0;
    updateLitStorePos.data.range(31,0) = lh::UPDATE;
    locationInputStream.write(updateLitStorePos);

    UPDATE_LIT_STORE_POS: for(unsigned int j = 0; j < numElements; j++){
        #pragma HLS loop_tripcount min=16 max=16

        ap_uint<96> getClsToUpdate;
        #ifdef FPGA_HW
        getClsToUpdate = intermediateStream.read();
        #else
        getClsToUpdate = clauseStoreInputStream.read().data;
        #endif
        
        unsigned int clsIDUpdate = getClsToUpdate.range(31,0);
        unsigned int swapAddr = getClsToUpdate.range(63,32);
        unsigned int replaceAddr = getClsToUpdate.range(95,64);

        if(clsIDUpdate-1 == removeID){
            continue;
        }

        updateLitStorePos.data.range(31,0) = swapAddr;
        updateLitStorePos.data.range(63,32) = replaceAddr;
        locationInputStream.write(updateLitStorePos);
    }

    updateLitStorePos.data = lh::EXIT;
    locationInputStream.write(updateLitStorePos);
}

#ifdef FPGA_HW
void deleteClauseTransaction(
    mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    const cls removeID,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    hls::stream<ap_uint<96>> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=1024
    #pragma HLS dataflow

    axiStreamBuffer_sendDelete(clauseStoreInputStream1, intermediateStream);
    deleteClauses(freeClsID, freeClsPageAddresses, removeID,
        intermediateStream, clauseStoreOutputStream1, locationInputStream,
        mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
}
#endif

void delete_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, 
    hls::stream<cls>& removeIDStream,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    DELETE_LOOP: for(unsigned int i = 0; i < removeTotal; i++){
        #pragma HLS loop_tripcount min=16 max=16
        const cls removeID = removeIDStream.read();
        #ifdef FPGA_HW
        deleteClauseTransaction(freeClsID, freeClsPageAddresses,
            removeID, clauseStoreInputStream1, clauseStoreOutputStream1,
            locationInputStream, mCmd, mClsStore, compactClauseLayout,
            CLAUSE_PAGE_SIZE);
        #else
        deleteClauses(freeClsID, freeClsPageAddresses, removeID,
            clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
        #endif
    }

}

void deleteClauses_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, 
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    cls* usedClsIDBuckets, minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS], cls lastInsertedID,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE, unsigned int LBDBucketCount[_FPGA_MAX_LBD_BUCKETS]){
    #pragma HLS inline off

    
    hls::stream<cls> removeIDStream;
    #pragma HLS stream variable=removeIDStream depth=64

    #pragma HLS dataflow

    #ifdef FPGA_HW
    getDeletedClsID(removeIDStream, usedClsIDBuckets, tracker, lastInsertedID, removeTotal, LBDBucketCount);
    delete_wrapper(freeClsID, removeIDStream,
        freeClsPageAddresses,
        clauseStoreInputStream1,
        clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, removeTotal, CLAUSE_PAGE_SIZE);
    #else
    getDeletedClsID(removeIDStream, usedClsIDBuckets, tracker, lastInsertedID, removeTotal, LBDBucketCount);
    for(unsigned int i = 0; i < removeTotal; i++){
        const cls removeID = removeIDStream.read();
        deleteClauses(freeClsID, freeClsPageAddresses, removeID,
            clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
    }
    #endif

}

void deleteExplicitClauses_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    // Explicit deletion is a per-clause transaction.  The solver sends the
    // next ID only after both clause views and the location map for the
    // current ID have been updated.
    DELETE_EXPLICIT_CLAUSES_TRANSACTIONAL: for(unsigned int i = 0;
            i < removeTotal; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        const ap_axiu<96,0,0,0> idPacket = clauseStoreInputStream2.read();
        const cls removeID = idPacket.data.range(31,0);

        #ifdef FPGA_HW
        deleteClauseTransaction(freeClsID, freeClsPageAddresses,
            removeID, clauseStoreInputStream1, clauseStoreOutputStream1,
            locationInputStream, mCmd, mClsStore, compactClauseLayout,
            CLAUSE_PAGE_SIZE);
        #else
        deleteClauses(freeClsID, freeClsPageAddresses, removeID,
            clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
        #endif
    }
}


extern "C"{
void clause_store_handler(ap_uint<128>* clauseStore, clauseMetaData* cmd, 
    cls* usedClsIDBuckets, unsigned int* trackLBD,
    const unsigned int initialClauseElements, const unsigned int maxClauseElements, 
    const unsigned int initialClauseCount, const unsigned int clausePageSize,
    const double prunePctage, const bool sessionReset,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream2,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream){

    #pragma HLS INTERFACE m_axi port=clauseStore offset=slave bundle=gmem13 latency=40
    #pragma HLS INTERFACE m_axi port=cmd offset=slave bundle=gmem14 latency=40
    #pragma HLS INTERFACE m_axi port=usedClsIDBuckets offset=slave bundle=gmem14 latency=40
    #pragma HLS INTERFACE m_axi port=trackLBD offset=slave bundle=gmem14 latency=40

    #pragma HLS INTERFACE s_axilite port=initialClauseElements
    #pragma HLS INTERFACE s_axilite port=maxClauseElements
    #pragma HLS INTERFACE s_axilite port=initialClauseCount
    #pragma HLS INTERFACE s_axilite port=clausePageSize
    #pragma HLS INTERFACE s_axilite port=sessionReset
	#pragma HLS INTERFACE axis port=clauseStoreInputStream1
    #pragma HLS INTERFACE axis port=clauseStoreInputStream2
    #pragma HLS INTERFACE axis port=clauseStoreOutputStream1
    #pragma HLS INTERFACE axis port=clauseStoreOutputStream2
	#pragma HLS INTERFACE s_axilite port=return

    const unsigned int MAX_CLAUSE_ELEMENTS = maxClauseElements;
    const unsigned int CLAUSE_PAGE_SIZE = clausePageSize;
    unsigned int clauseElements = initialClauseElements;
    const double PRUNE_PERCENTAGE = prunePctage;
    const bool validConfiguration = validClausePageSize(CLAUSE_PAGE_SIZE) &&
        MAX_CLAUSE_ELEMENTS <= _FPGA_MAX_LITERAL_ELEMENTS &&
        clauseElements <= MAX_CLAUSE_ELEMENTS &&
        initialClauseCount <= _FPGA_MAX_CLAUSES;

    static unsigned int originalClauseCount = 0;
    static unsigned int usedTotalIDCount = 0;

    static mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_> freeClsPageAddresses;
    #pragma HLS bind_storage variable=freeClsPageAddresses.array type=RAM_S2P impl=URAM latency=1

    static mmuStream<cls, _FPGA_MAX_CLAUSES> freeClsID;
    #pragma HLS bind_storage variable=freeClsID.array type=RAM_S2P impl=URAM latency=2

    static ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4];
    #pragma HLS bind_storage variable=mClsStore type=RAM_T2P impl=URAM latency=2

    static clauseMetaData mCmd[_FPGA_MAX_CLAUSES];
    #pragma HLS aggregate variable=mCmd compact=auto
    #pragma HLS bind_storage variable=mCmd type=RAM_T2P impl=URAM latency=2

    static ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES];
    #pragma HLS bind_storage variable=compactClauseLayout type=RAM_T2P impl=BRAM latency=1

    static unsigned int LBDBucketCount[2][_FPGA_MAX_LBD_BUCKETS];
    static minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
    static cls freeID = 0;
    static cls lastInsertedID = 0;
    static bool lastInsertedIsBucketed = false;
    if(sessionReset && validConfiguration){
        originalClauseCount = initialClauseCount;
        usedTotalIDCount = 0;
        freeID = 0;
        lastInsertedID = 0;
        lastInsertedIsBucketed = false;
        freeClsPageAddresses.reset(clauseElements,_FPGA_MAX_LITERAL_ELEMENTS,CLAUSE_PAGE_SIZE);
        freeClsID.reset(originalClauseCount,_FPGA_MAX_CLAUSES,1);
        INITIALIZE_LBD_META: for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
            tracker[i].insertIdx = 0;
            tracker[i].accessIdx = 0;
            tracker[i].usedCount = 0;
            LBDBucketCount[0][i] = 0;
            LBDBucketCount[1][i] = 0;
        }
        INITIALIZE_CLAUSE_LAYOUT: for(unsigned int i = 0; i < _FPGA_MAX_CLAUSES; i++){
            #pragma HLS loop_tripcount min=1024 max=1024
            #pragma HLS pipeline II=1
            compactClauseLayout[i] = i < originalClauseCount;
        }

        copy_cls_data(mClsStore, mCmd,
            clauseStore, cmd,
            clauseElements, originalClauseCount);
    }
    while(true){
        ap_axiu<96,0,0,0> getCommand = clauseStoreInputStream1.read();
        unsigned int code = getCommand.data.range(95,64);

        if(code == csh::EXIT){
            break;
        }else if(!validConfiguration){
            if(code == csh::SEND_LEN || code == csh::SEND_LEN_BCP){
                rejectLengthRequests(clauseStoreInputStream1,
                    clauseStoreOutputStream1);
            }else if(code == csh::SEND_CLS){
                rejectClauseRequests(clauseStoreInputStream1,
                    clauseStoreInputStream2, clauseStoreOutputStream1,
                    clauseStoreOutputStream2);
            }else if(code == csh::SAVE){
                ap_axiu<32,0,0,0> response;
                response.data = -4;
                clauseStoreOutputStream1.write(response);
            }else if(code == csh::DELETE || code == csh::DELETE_IDS){
                // DELETE_IDS is negotiated before any IDs are sent.  A zero
                // response must therefore be produced immediately; waiting
                // for IDs here would deadlock with the solver waiting for the
                // accepted count.
                ap_axiu<32,0,0,0> response;
                response.data = 0;
                clauseStoreOutputStream1.write(response);
            }
            continue;
        }else if(code == csh::SEND_LEN || code == csh::SEND_LEN_BCP){
            sendLength_wrapper(clauseStoreOutputStream1, clauseStoreInputStream1, mCmd);
        }else if(code == csh::SEND_CLS){
            
            sendData_dataflow(mClsStore, mCmd, compactClauseLayout,
                CLAUSE_PAGE_SIZE,
                clauseStoreInputStream1, clauseStoreInputStream2,
                clauseStoreOutputStream1, clauseStoreOutputStream2);
            
        }else if(code == csh::SAVE){
            clauseMetaData cmd;
            cmd.numElements = getCommand.data.range(31,0);

            ap_axiu<32,0,0,0> sendData;
            const bool validSave = cmd.numElements > 0 &&
                cmd.numElements <= _FPGA_MAX_LEARN_ELE &&
                validClausePageSize(CLAUSE_PAGE_SIZE);
            const unsigned int requiredPages = validSave
                ? (cmd.numElements + CLAUSE_PAGE_SIZE-2)/(CLAUSE_PAGE_SIZE-1)
                : UINT_MAX;
            if(!validSave || freeClsPageAddresses.size() < requiredPages ||
                    freeClsID.empty()){
                sendData.data = -4;
                clauseStoreOutputStream1.write(sendData);
            }else{
                cmd.addressStart = freeClsPageAddresses.read();
                
                freeID = freeClsID.read();
                lastInsertedID = freeID;
                lastInsertedIsBucketed = false;
                mCmd[freeID] = cmd;
                compactClauseLayout[freeID] = 0;

                sendData.data = freeID;
                clauseStoreOutputStream1.write(sendData);
                saveData(mClsStore, freeClsPageAddresses,
                    cmd, CLAUSE_PAGE_SIZE, clauseStoreInputStream1, locationInputStream);
            }            
        }else if(code == csh::BUCKET){
            unsigned int lbdLevelIndex = getCommand.data.range(31,0)-2;
            if(lbdLevelIndex > 9){
                lbdLevelIndex = 9;
            }

            usedClsIDBuckets[_FPGA_MAX_CLAUSES*lbdLevelIndex+tracker[lbdLevelIndex].insertIdx] = freeID;
            tracker[lbdLevelIndex].insertIdx = (tracker[lbdLevelIndex].insertIdx+1) % _FPGA_MAX_CLAUSES;
            tracker[lbdLevelIndex].usedCount++;
            LBDBucketCount[0][lbdLevelIndex]++;
            usedTotalIDCount++;
            lastInsertedIsBucketed = true;
        }else if(code == csh::DELETE){
            unsigned int trackedTotal = 0;
            COUNT_BUCKETED_CLAUSES: for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
                #pragma HLS unroll
                trackedTotal += tracker[i].usedCount;
            }
            usedTotalIDCount = trackedTotal;

            const unsigned int protectedCount =
                lastInsertedIsBucketed && trackedTotal != 0 ? 1 : 0;
            const unsigned int deletableTotal = trackedTotal-protectedCount;
            unsigned int removeTotal = 0;
            if(PRUNE_PERCENTAGE > 0.0){
                removeTotal = PRUNE_PERCENTAGE >= 1.0 ? trackedTotal :
                    (unsigned int)(trackedTotal*PRUNE_PERCENTAGE);
                if(removeTotal > deletableTotal){
                    removeTotal = deletableTotal;
                }
            }
            usedTotalIDCount -= removeTotal;
            ap_axiu<32,0,0,0> sendData;
            sendData.data = removeTotal;
            clauseStoreOutputStream1.write(sendData);

            if(removeTotal > 0){
                deleteClauses_wrapper(freeClsID, freeClsPageAddresses,
                    clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
                    usedClsIDBuckets, tracker, lastInsertedID,
                    mCmd, mClsStore, compactClauseLayout,
                    removeTotal, CLAUSE_PAGE_SIZE, LBDBucketCount[1]);
            }
        }else if(code == csh::DELETE_IDS){
            const unsigned int declaredRemoveTotal = getCommand.data.range(31,0);
            const unsigned int removeTotal =
                declaredRemoveTotal <= _FPGA_MAX_CLAUSES
                    ? declaredRemoveTotal : 0;
            ap_axiu<32,0,0,0> sendData;
            sendData.data = removeTotal;
            clauseStoreOutputStream1.write(sendData);
            if(removeTotal > 0){
                deleteExplicitClauses_wrapper(freeClsID, freeClsPageAddresses,
                    clauseStoreInputStream1, clauseStoreInputStream2,
                    clauseStoreOutputStream1, locationInputStream,
                    mCmd, mClsStore, compactClauseLayout,
                    removeTotal, CLAUSE_PAGE_SIZE);
            }
        }
    }

    WRITE_OUT_LBD_STATS: for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
        trackLBD[i] = LBDBucketCount[0][i];
        trackLBD[i+_FPGA_MAX_LBD_BUCKETS] = LBDBucketCount[1][i];
    }

    ap_axiu<64,0,0,0> updateLitStorePos;
    updateLitStorePos.data = lh::EXIT;
    locationInputStream.write(updateLitStorePos);

}
}
