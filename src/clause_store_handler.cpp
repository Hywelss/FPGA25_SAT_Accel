#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <ap_utils.h>
#include "data_structures.h"

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
        clauseMetaData cmd = mCmd[getCls];
        ap_axiu<32,0,0,0> sendData;
        sendData.data = cmd.numElements;
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

            clauseMetaData cmd = mCmd[clsID];
            ap_axiu<32,0,0,0> sendData;
            sendData.data = cmd.numElements;
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

void sendDataScheduler(const ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_uint<33>>& outputBuffer1,
    hls::stream<ap_uint<33>>& outputBuffer2,
    hls::stream<bool>& outputCredit1,
    hls::stream<bool>& outputCredit2){
    #pragma HLS inline off

    ap_uint<2> active = 0;
    bool exit1 = false;
    bool exit2 = false;
    bool exitPending1 = false;
    bool exitPending2 = false;
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
    bool followPage1 = false;
    bool followPage2 = false;
    #if defined(FPGA_HW) || defined(__SYNTHESIS__)
    ap_uint<2> availableOutputSlots1 = 2;
    ap_uint<2> availableOutputSlots2 = 2;
    #endif

    SEND_DATA_DUAL: while(!exit1 || !exit2){
        #pragma HLS loop_tripcount min=16 max=1024
        #pragma HLS pipeline II=1

        ap_uint<2> nextActive = active;
        ap_uint<2> outputKind1 = 0;
        ap_uint<33> output1 = 0;
        ap_uint<128> line1 = 0;
        if(active[0]){
            if(processed1 == clauseLength1){
                outputKind1 = 2;
            }else if(followPage1){
                address1 = nextPage1;
                followPage1 = false;
            }else{
                line1 = mClsStore[address1/4];
                output1.range(31,0) = line1.range(32*(subIndex1%4)+31,32*(subIndex1%4));
                outputKind1 = 1;
            }
        }else if(exitPending1){
            output1[32] = 1;
            outputKind1 = 3;
        }else if(!exit1 && !clauseStoreInputStream1.empty()){
            const ap_axiu<96,0,0,0> command = clauseStoreInputStream1.read();
            if((int)command.data.range(95,64) == csh::EXIT){
                exitPending1 = true;
            }else{
                const cls clauseID = command.data.range(31,0);
                const clauseMetaData metadata = mCmd[clauseID];
                address1 = metadata.addressStart;
                clauseLength1 = metadata.numElements;
                subIndex1 = 0;
                processed1 = 0;
                followPage1 = false;
                pageSize1 = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
                nextActive[0] = 1;
            }
        }

        bool canWriteOutput1 = true;
        #if defined(FPGA_HW) || defined(__SYNTHESIS__)
        if(!outputCredit1.empty()){
            outputCredit1.read();
            availableOutputSlots1++;
        }
        canWriteOutput1 = availableOutputSlots1 != 0;
        #endif
        if(outputKind1 != 0 && canWriteOutput1){
            outputBuffer1.write(output1);
            #if defined(FPGA_HW) || defined(__SYNTHESIS__)
            availableOutputSlots1--;
            #endif
            if(outputKind1 == 1){
                processed1++;
                subIndex1++;
                if(processed1 < clauseLength1 && subIndex1 == pageSize1-1){
                    nextPage1 = line1.range(127,96);
                    followPage1 = true;
                    subIndex1 = 0;
                }else{
                    address1++;
                }
            }else if(outputKind1 == 2){
                nextActive[0] = 0;
            }else{
                exitPending1 = false;
                exit1 = true;
            }
        }

        ap_uint<2> outputKind2 = 0;
        ap_uint<33> output2 = 0;
        ap_uint<128> line2 = 0;
        if(active[1]){
            if(processed2 == clauseLength2){
                outputKind2 = 2;
            }else if(followPage2){
                address2 = nextPage2;
                followPage2 = false;
            }else{
                line2 = mClsStore[address2/4];
                output2.range(31,0) = line2.range(32*(subIndex2%4)+31,32*(subIndex2%4));
                outputKind2 = 1;
            }
        }else if(exitPending2){
            output2[32] = 1;
            outputKind2 = 3;
        }else if(!exit2 && !clauseStoreInputStream2.empty()){
            const ap_axiu<96,0,0,0> command = clauseStoreInputStream2.read();
            if((int)command.data.range(95,64) == csh::EXIT){
                exitPending2 = true;
            }else{
                const cls clauseID = command.data.range(31,0);
                const clauseMetaData metadata = mCmd[clauseID];
                address2 = metadata.addressStart;
                clauseLength2 = metadata.numElements;
                subIndex2 = 0;
                processed2 = 0;
                followPage2 = false;
                pageSize2 = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
                nextActive[1] = 1;
            }
        }

        bool canWriteOutput2 = true;
        #if defined(FPGA_HW) || defined(__SYNTHESIS__)
        if(!outputCredit2.empty()){
            outputCredit2.read();
            availableOutputSlots2++;
        }
        canWriteOutput2 = availableOutputSlots2 != 0;
        #endif
        if(outputKind2 != 0 && canWriteOutput2){
            outputBuffer2.write(output2);
            #if defined(FPGA_HW) || defined(__SYNTHESIS__)
            availableOutputSlots2--;
            #endif
            if(outputKind2 == 1){
                processed2++;
                subIndex2++;
                if(processed2 < clauseLength2 && subIndex2 == pageSize2-1){
                    nextPage2 = line2.range(127,96);
                    followPage2 = true;
                    subIndex2 = 0;
                }else{
                    address2++;
                }
            }else if(outputKind2 == 2){
                nextActive[1] = 0;
            }else{
                exitPending2 = false;
                exit2 = true;
            }
        }
        active = nextActive;
    }
}

void sendDataOutput(hls::stream<ap_uint<33>>& outputBuffer,
    hls::stream<bool>& outputCredit,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream){
    #pragma HLS inline off

    SEND_DATA_OUTPUT: while(true){
        #pragma HLS loop_tripcount min=16 max=1024
        #pragma HLS pipeline II=1
        const ap_uint<33> buffered = outputBuffer.read();
        if(buffered[32]){
            break;
        }

        #if defined(FPGA_HW) || defined(__SYNTHESIS__)
        outputCredit.write(true);
        #endif
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

    const clauseMetaData metadata = mCmd[clauseID];
    const unsigned int pageSize = compactClauseLayout[clauseID] ? 4 : CLAUSE_PAGE_SIZE;
    unsigned int address = metadata.addressStart;
    unsigned int subIndex = 0;

    SEND_CLAUSE_SEQUENTIAL_MODEL: for(unsigned int processed = 0;
            processed < metadata.numElements; processed++){
        const ap_uint<128> line = mClsStore[address/4];
        ap_axiu<32,0,0,0> output;
        output.data = line.range(32*(subIndex%4)+31, 32*(subIndex%4));
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
    hls::stream<bool> outputCredit1;
    hls::stream<bool> outputCredit2;
    #pragma HLS stream variable=outputBuffer1 depth=2
    #pragma HLS stream variable=outputBuffer2 depth=2
    #pragma HLS stream variable=outputCredit1 depth=2
    #pragma HLS stream variable=outputCredit2 depth=2
    #pragma HLS dataflow

    sendDataScheduler(mClsStore, mCmd, compactClauseLayout, CLAUSE_PAGE_SIZE,
        clauseStoreInputStream1, clauseStoreInputStream2,
        outputBuffer1, outputBuffer2, outputCredit1, outputCredit2);
    sendDataOutput(outputBuffer1, outputCredit1, clauseStoreOutputStream1);
    sendDataOutput(outputBuffer2, outputCredit2, clauseStoreOutputStream2);
    #else
    // Software emulation executes this wrapper sequentially. Write directly to
    // the external streams so consumers can respond with their EXIT commands.
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
    unsigned int startBucketIndex = _FPGA_MAX_LBD_BUCKETS-1;
    bool skipped = false;
    GET_REMOVE_ID: while(true){
        #pragma HLS loop_tripcount min=16 max=16

        if(removedCount == removeTotal){
            break;
        }

        if(tracker[startBucketIndex].usedCount == 0 || skipped){
            skipped = false;
            startBucketIndex--;
            continue;
        }

        unsigned int toEndCount = _FPGA_MAX_CLAUSES - tracker[startBucketIndex].accessIdx;
        if(tracker[startBucketIndex].accessIdx < tracker[startBucketIndex].insertIdx){
            toEndCount = tracker[startBucketIndex].insertIdx - tracker[startBucketIndex].accessIdx;
        }
        unsigned int offset = tracker[startBucketIndex].accessIdx;

        if(toEndCount >= removeTotal-removedCount){
            toEndCount = removeTotal-removedCount;
        }

        bool skip = false;
        GET_DEL_CLS: for(unsigned int i = 0; i < toEndCount; i++){
            #pragma HLS pipeline
            #pragma HLS loop_tripcount min=16 max=16

            cls getClsID = usedClsIDBuckets[_FPGA_MAX_CLAUSES*startBucketIndex+offset+i];

            if(getClsID != lastInsertedID){
                LBDBucketCount[startBucketIndex]++;
                removedCount++;
                removeIDStream.write(getClsID);

            }else{
                skip = true;
                skipped = true;
            }
        }

        tracker[startBucketIndex].accessIdx = (tracker[startBucketIndex].accessIdx + (toEndCount-skip))%_FPGA_MAX_CLAUSES;
        tracker[startBucketIndex].usedCount -= (toEndCount-skip);
    }
}

#ifdef FPGA_HW
void deleteClauses(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<cls>& removeIDStream, hls::stream<ap_uint<96>>& intermediateStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE){
#else
void deleteClauses(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<cls>& removeIDStream,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int CLAUSE_PAGE_SIZE){
#endif

    #pragma HLS inline off

    unsigned int removeID = removeIDStream.read();

    freeClsID.write(removeID);
    clauseMetaData getCmd = mCmd[removeID];

    ap_axiu<32,0,0,0> sendData;
    sendData.data = removeID;
    clauseStoreOutputStream1.write(sendData);
    sendData.data = getCmd.numElements;
    clauseStoreOutputStream1.write(sendData);

    unsigned int subIndex = 0;
    unsigned int reqAddrLit = getCmd.addressStart;
    unsigned int state = 0;
    unsigned int processed = 0;
    unsigned int tmpAddr = 0;
    unsigned int numElements = getCmd.numElements;
    const bool compactLayout = compactClauseLayout[removeID];
    const unsigned int usePageSize = compactLayout ? 4 : CLAUSE_PAGE_SIZE;

    ap_axiu<64,0,0,0> updateLitStorePos;
    updateLitStorePos.data.range(31,0) = lh::SEND;
    locationInputStream.write(updateLitStorePos);

    ap_uint<128> get = 0;
    SEND_DATA_DELETE_NO_WRAP: while(true){
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

void delete_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, 
    hls::stream<cls>& removeIDStream,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    hls::stream<ap_uint<96>> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=1024

    DELETE_LOOP: for(unsigned int i = 0; i < removeTotal; i++){
        #pragma HLS loop_tripcount min=16 max=16
        #pragma HLS dataflow

        axiStreamBuffer_sendDelete(clauseStoreInputStream1, intermediateStream);
        #ifdef FPGA_HW
        deleteClauses(freeClsID, freeClsPageAddresses, removeIDStream,
            intermediateStream, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
        #endif
    }

}

void deleteClauses_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID, 
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    cls* usedClsIDBuckets, minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS], cls lastInsertedID,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES], ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
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
        deleteClauses(freeClsID, freeClsPageAddresses, removeIDStream,
            clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
    }
    #endif

}

void readExplicitDeleteIDs(hls::stream<cls>& removeIDStream,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    const unsigned int removeTotal){
    #pragma HLS inline off
    READ_EXPLICIT_DELETE_IDS: for(unsigned int i = 0; i < removeTotal; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        removeIDStream.write(clauseStoreInputStream2.read().data.range(31,0));
    }
}

void bufferExplicitDeleteUpdates(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_uint<96>>& intermediateStream,
    const unsigned int removeTotal){
    #pragma HLS inline off

    BUFFER_EXPLICIT_DELETE_UPDATES: for(unsigned int i = 0; i < removeTotal; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        while(true){
            #pragma HLS loop_tripcount min=1 max=1025
            const ap_axiu<96,0,0,0> update = clauseStoreInputStream1.read();
            if((int)update.data.range(95,64) == csh::EXIT){
                break;
            }
            intermediateStream.write(update.data);
        }
    }
}

#ifdef FPGA_HW
void deleteExplicitClauses(
    mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<cls>& removeIDStream,
    hls::stream<ap_uint<96>>& intermediateStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    DELETE_EXPLICIT_CLAUSES: for(unsigned int i = 0; i < removeTotal; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        deleteClauses(freeClsID, freeClsPageAddresses, removeIDStream,
            intermediateStream, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
    }
}
#endif

void deleteExplicitClauses_wrapper(mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    const unsigned int removeTotal, const unsigned int CLAUSE_PAGE_SIZE){
    #pragma HLS inline off

    hls::stream<cls> removeIDStream;
    #pragma HLS stream variable=removeIDStream depth=64

    #ifdef FPGA_HW
    hls::stream<ap_uint<96>> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=1024
    #pragma HLS dataflow

    readExplicitDeleteIDs(removeIDStream, clauseStoreInputStream2, removeTotal);
    bufferExplicitDeleteUpdates(clauseStoreInputStream1, intermediateStream, removeTotal);
    deleteExplicitClauses(freeClsID, freeClsPageAddresses, removeIDStream,
        intermediateStream, clauseStoreOutputStream1, locationInputStream,
        mCmd, mClsStore, compactClauseLayout, removeTotal, CLAUSE_PAGE_SIZE);
    #else
    readExplicitDeleteIDs(removeIDStream, clauseStoreInputStream2, removeTotal);
    for(unsigned int i = 0; i < removeTotal; i++){
        deleteClauses(freeClsID, freeClsPageAddresses, removeIDStream,
            clauseStoreInputStream1, clauseStoreOutputStream1, locationInputStream,
            mCmd, mClsStore, compactClauseLayout, CLAUSE_PAGE_SIZE);
    }
    #endif
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
    if(sessionReset){
        originalClauseCount = initialClauseCount;
        usedTotalIDCount = 0;
        freeID = 0;
        lastInsertedID = 0;
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
            if((freeClsPageAddresses.size()*(CLAUSE_PAGE_SIZE-1) < cmd.numElements) || freeClsID.empty()){
                sendData.data = -4;
                clauseStoreOutputStream1.write(sendData);
            }else{
                cmd.addressStart = freeClsPageAddresses.read();
                
                freeID = freeClsID.read();
                lastInsertedID = freeID;
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
        }else if(code == csh::DELETE){
            unsigned int removeTotal = usedTotalIDCount * PRUNE_PERCENTAGE;
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
            const unsigned int removeTotal = getCommand.data.range(31,0);
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
