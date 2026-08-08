#include "manage.h"

void allocatePage(hls::stream<lit>& litNewPage, mmuStream<unsigned int, _MAX_PAGES_LIT_STORE_>& freeLitPageAddresses,
    literalMetaData lmd[_FPGA_MAX_LITERALS], 
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16], int& error,
    const unsigned int LITERAL_PAGE_SIZE){
    #pragma HLS inline off
    ALLOCATE_PAGE: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        #pragma HLS dependence variable=lmd inter false
        #pragma HLS dependence variable=litStore inter false

        if(litNewPage.empty()){
            break;
        }
        lit getLit = litNewPage.read();
        const bool validPageSize = LITERAL_PAGE_SIZE >= 16 &&
            LITERAL_PAGE_SIZE <= _FPGA_MAX_LITERAL_ELEMENTS &&
            LITERAL_PAGE_SIZE%16 == 0;
        if(!isValidLiteral(getLit) || !validPageSize){
            error = -5;
            continue;
        }
        ap_uint<1> select = 0;
        if(getLit < 0){
            select = 1;
        }
        if(freeLitPageAddresses.empty()){
            error = -5;
            continue;
        }
        literalMetaData getLmd = lmd[abs(getLit)-1];
        const unsigned int latestPage = LMD_LATEST_PAGE(getLmd.compactlmd,select);
        if(latestPage >= _FPGA_MAX_LITERAL_ELEMENTS || latestPage%16 != 0 ||
                latestPage/16 + LITERAL_PAGE_SIZE/16 >
                    _FPGA_MAX_LITERAL_ELEMENTS/16){
            error = -5;
            continue;
        }

        unsigned int freePageAddress = freeLitPageAddresses.read();
        if(freePageAddress >= _FPGA_MAX_LITERAL_ELEMENTS ||
                freePageAddress%16 != 0 ||
                freePageAddress/16 + LITERAL_PAGE_SIZE/16 >
                    _FPGA_MAX_LITERAL_ELEMENTS/16){
            error = -5;
            continue;
        }

        unsigned int reqAddrLit = latestPage/16;

        ap_uint<512> fetchLine = litStore[reqAddrLit + LITERAL_PAGE_SIZE/16 - 1];
        fetchLine.range(511,480) = freePageAddress;
        litStore[reqAddrLit + LITERAL_PAGE_SIZE/16 - 1] = fetchLine;

        LMD_LATEST_PAGE(getLmd.compactlmd,select) = freePageAddress;
        LMD_FREE_SPACE(getLmd.compactlmd,select) = LITERAL_PAGE_SIZE-2;

        ap_uint<512> clearLastSubPage = 0;
        clearLastSubPage.range(479,448) = reqAddrLit*16;
        litStore[freePageAddress/16 + LITERAL_PAGE_SIZE/16 - 1] = clearLastSubPage;

        lmd[abs(getLit)-1] = getLmd;
    }
}


void deleteOneTransposedClause(ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_>& freeLitPageAddresses, const unsigned int LITERAL_PAGE_SIZE, 
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream){

    #pragma HLS inline off

        ap_axiu<32,0,0,0> getData = clauseStoreOutputStream1.read();

        unsigned int removedID = getData.data+1;
        getData = clauseStoreOutputStream1.read();
        unsigned int numElementsCls = getData.data;

        if(numElementsCls == 0){
            #ifdef FPGA_HW
            ap_axiu<96,0,0,0> emptyRecordEnd;
            emptyRecordEnd.data = 0;
            emptyRecordEnd.data.range(95,64) = csh::EXIT;
            clauseStoreInputStream1.write(emptyRecordEnd);
            #endif
            return;
        }

        REMOVE_FROM_LITSTORE_2: for(unsigned int i = 0; i < numElementsCls; i++){
            #pragma HLS loop_tripcount min=32 max=32
            #pragma HLS dependence variable=lmd inter false
            #pragma HLS dependence variable=litStore inter false

            ap_axiu<32,0,0,0> getData = clauseStoreOutputStream1.read();
            ap_axiu<32,0,0,0> getAddr = locationOutputStream.read();

            lit literalToRemove = getData.data;
            unsigned int address = getAddr.data;

            const bool validLiteral = literalToRemove != 0 &&
                literalToRemove <= _FPGA_MAX_LITERALS &&
                literalToRemove >= -_FPGA_MAX_LITERALS;
            bool validRecord = validLiteral &&
                address < _FPGA_MAX_LITERAL_ELEMENTS &&
                LITERAL_PAGE_SIZE >= 16 &&
                LITERAL_PAGE_SIZE <= _FPGA_MAX_LITERAL_ELEMENTS &&
                LITERAL_PAGE_SIZE%16 == 0;

            literalMetaData getLmd = {};
            ap_uint<1> selectSide = 0;
            unsigned int numElements = 0;
            unsigned int latestPage = 0;
            unsigned int freeSpace = 0;
            unsigned int reqAddrOffsetMove = 0;
            if(validRecord){
                getLmd = lmd[abs(literalToRemove)-1];
                if(literalToRemove < 0){
                    selectSide = 1;
                }
                numElements = LMD_NUM_ELE(getLmd.compactlmd,selectSide);
                latestPage = LMD_LATEST_PAGE(getLmd.compactlmd,selectSide)/16;
                freeSpace = LMD_FREE_SPACE(getLmd.compactlmd,selectSide);
                validRecord = numElements != 0 &&
                    freeSpace <= LITERAL_PAGE_SIZE-2 &&
                    latestPage < _FPGA_MAX_LITERAL_ELEMENTS/16 &&
                    latestPage + LITERAL_PAGE_SIZE/16 <=
                        _FPGA_MAX_LITERAL_ELEMENTS/16;
                if(validRecord){
                    if(freeSpace == LITERAL_PAGE_SIZE-2){
                        reqAddrOffsetMove = 0;
                    }else{
                        reqAddrOffsetMove = LITERAL_PAGE_SIZE-freeSpace-3;
                        validRecord = latestPage + reqAddrOffsetMove/16 <
                            _FPGA_MAX_LITERAL_ELEMENTS/16;
                    }
                }
            }

            if(!validRecord){
                ap_axiu<96,0,0,0> skippedUpdate;
                skippedUpdate.data = 0;
                skippedUpdate.data.range(31,0) = removedID;
                clauseStoreInputStream1.write(skippedUpdate);
                continue;
            }

            ap_uint<512> replaceFetch = litStore[address/16];
            bool releaseLatestPage = false;
            unsigned int previousPageAddress = 0;
            if(freeSpace == LITERAL_PAGE_SIZE-2){
                const ap_uint<512> pageTrailer =
                    litStore[latestPage+LITERAL_PAGE_SIZE/16-1];
                previousPageAddress = pageTrailer.range(479,448);
                if(previousPageAddress >= _FPGA_MAX_LITERAL_ELEMENTS ||
                        previousPageAddress%16 != 0 ||
                        previousPageAddress/16 + LITERAL_PAGE_SIZE/16 >
                            _FPGA_MAX_LITERAL_ELEMENTS/16){
                    ap_axiu<96,0,0,0> skippedUpdate;
                    skippedUpdate.data = 0;
                    skippedUpdate.data.range(31,0) = removedID;
                    clauseStoreInputStream1.write(skippedUpdate);
                    continue;
                }
                reqAddrOffsetMove = LITERAL_PAGE_SIZE - 2 - 1;
                latestPage = previousPageAddress/16;
                releaseLatestPage = true;
            }

            const unsigned int moveLineAddress =
                latestPage + reqAddrOffsetMove/16;
            ap_uint<512> moveFetch = litStore[moveLineAddress];
            const cls movedCls = moveFetch.range(
                32*(reqAddrOffsetMove%16)+31,
                32*(reqAddrOffsetMove%16));
            if(movedCls <= 0 || (unsigned int)movedCls > _FPGA_MAX_CLAUSES){
                ap_axiu<96,0,0,0> skippedUpdate;
                skippedUpdate.data = 0;
                skippedUpdate.data.range(31,0) = removedID;
                clauseStoreInputStream1.write(skippedUpdate);
                continue;
            }

            if(releaseLatestPage){
                freeLitPageAddresses.write(
                    LMD_LATEST_PAGE(getLmd.compactlmd,selectSide));
                LMD_LATEST_PAGE(getLmd.compactlmd,selectSide) =
                    previousPageAddress;
                LMD_FREE_SPACE(getLmd.compactlmd,selectSide) = 1;
            }else{
                LMD_FREE_SPACE(getLmd.compactlmd,selectSide) = freeSpace + 1;
            }

            unsigned int replaceAddr = address;
            unsigned int swapAddr = latestPage*16 + reqAddrOffsetMove;
            if(address/16 == moveLineAddress){
                replaceFetch.range(32*(address%16)+31,32*(address%16)) = movedCls;
                replaceFetch.range(32*(reqAddrOffsetMove%16)+31,32*(reqAddrOffsetMove%16)) = 0;

                litStore[address/16] = replaceFetch;
            }else{
                replaceFetch.range(32*(address%16)+31,32*(address%16)) = movedCls;
                moveFetch.range(32*(reqAddrOffsetMove%16)+31,32*(reqAddrOffsetMove%16)) = 0;

                litStore[address/16] = replaceFetch;
                litStore[moveLineAddress] = moveFetch;
            }

            ap_axiu<96,0,0,0> sendData;
            sendData.data = 0;
            sendData.data.range(31,0) = movedCls;
            sendData.data.range(63,32) = swapAddr;
            sendData.data.range(95,64) = replaceAddr;
            clauseStoreInputStream1.write(sendData);

            LMD_NUM_ELE(getLmd.compactlmd,selectSide) = numElements-1;
            lmd[abs(literalToRemove)-1] = getLmd;
        }
        #ifdef FPGA_HW
        ap_axiu<96,0,0,0> sendClauseInputCommand;
        sendClauseInputCommand.data = 0;
        sendClauseInputCommand.data.range(95,64) = csh::EXIT;
        clauseStoreInputStream1.write(sendClauseInputCommand);
        #endif
}

void deleteTransposedClauses(ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_>& freeLitPageAddresses, const unsigned int LITERAL_PAGE_SIZE,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream){

    #pragma HLS inline off

    const unsigned int removeTotal = clauseStoreOutputStream1.read().data;
    REMOVE_FROM_LITSTORE: for(unsigned int a = 0; a < removeTotal; a++){
        #pragma HLS loop_tripcount min=32 max=32
        deleteOneTransposedClause(litStore, lmd, freeLitPageAddresses,
            LITERAL_PAGE_SIZE, clauseStoreInputStream1,
            clauseStoreOutputStream1, locationOutputStream);
    }
}
