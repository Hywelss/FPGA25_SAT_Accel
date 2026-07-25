#include "manage.h"

void allocatePage(hls::stream<lit>& litNewPage, mmuStream<unsigned int, _MAX_PAGES_LIT_STORE_TOTAL_>& freeLitPageAddresses,
    literalMetaData lmd[_FPGA_MAX_LITERALS], 
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD], ap_int<512>* litStoreDDR, occTagEntry* occCacheTag, int& error,
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
        ap_uint<1> select = 0;
        if(getLit < 0){
            select = 1;
        }
        if(freeLitPageAddresses.empty()){
            error = -5;
            break;
        }
        unsigned int freePageAddress = freeLitPageAddresses.read();

        literalMetaData getLmd = lmd[abs(getLit)-1];

        unsigned int reqAddrLit = LMD_LATEST_PAGE(getLmd.compactlmd,select)/LIT_SLOTS_PER_WORD;

        const unsigned int linkWord = reqAddrLit + LITERAL_PAGE_SIZE/LIT_SLOTS_PER_WORD - 1;
        ap_uint<512> fetchLine = occReadWord(litStore, occCacheTag, litStoreDDR, linkWord);
        LIT_NEXT_PTR(fetchLine) = freePageAddress;
        occWriteWord(litStore, occCacheTag, litStoreDDR, linkWord, fetchLine);

        LMD_LATEST_PAGE(getLmd.compactlmd,select) = freePageAddress;
        LMD_FREE_SPACE(getLmd.compactlmd,select) = LITERAL_PAGE_SIZE-2;

        ap_uint<512> clearLastSubPage = 0;
        LIT_BACK_PTR(clearLastSubPage) = reqAddrLit*LIT_SLOTS_PER_WORD;
        occWriteWord(litStore, occCacheTag, litStoreDDR,
            freePageAddress/LIT_SLOTS_PER_WORD + LITERAL_PAGE_SIZE/LIT_SLOTS_PER_WORD - 1, clearLastSubPage);

        lmd[abs(getLit)-1] = getLmd;
    }
}


void deleteTransposedClauses(ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD], ap_int<512>* litStoreDDR, occTagEntry* occCacheTag,
    literalMetaData lmd[_FPGA_MAX_LITERALS],
    mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_TOTAL_>& freeLitPageAddresses, const unsigned int LITERAL_PAGE_SIZE, 
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1, hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1, hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream){

    #pragma HLS inline off
    
    ap_axiu<32,0,0,0> getData = clauseStoreOutputStream1.read();
    unsigned int removeTotal = getData.data;

    REMOVE_FROM_LITSTORE: for(unsigned int a = 0; a < removeTotal; a++){
        #pragma HLS loop_tripcount min=32 max=32
        ap_axiu<32,0,0,0> getData = clauseStoreOutputStream1.read();

        unsigned int removedID = getData.data+1;
        getData = clauseStoreOutputStream1.read();
        unsigned int numElementsCls = getData.data;

        REMOVE_FROM_LITSTORE_2: for(unsigned int i = 0; i < numElementsCls; i++){
            #pragma HLS loop_tripcount min=32 max=32
            #pragma HLS dependence variable=lmd inter false
            #pragma HLS dependence variable=litStore inter false

            ap_axiu<32,0,0,0> getData = clauseStoreOutputStream1.read();
            ap_axiu<32,0,0,0> getAddr = locationOutputStream.read();

            lit literalToRemove = getData.data;
            unsigned int address = getAddr.data;

            literalMetaData getLmd = lmd[abs(literalToRemove)-1];
            ap_uint<1> selectSide = 0;
            if(literalToRemove < 0){
                selectSide = 1;
            }

            unsigned int numElements = LMD_NUM_ELE(getLmd.compactlmd,selectSide);
            unsigned int latestPage = LMD_LATEST_PAGE(getLmd.compactlmd,selectSide)/LIT_SLOTS_PER_WORD;
            unsigned int freeSpace = LMD_FREE_SPACE(getLmd.compactlmd,selectSide);
            unsigned int reqAddrOffsetMove = LITERAL_PAGE_SIZE - freeSpace - 2 - 1;

            ap_uint<512> replaceFetch = occReadWord(litStore, occCacheTag, litStoreDDR, address/LIT_SLOTS_PER_WORD);
            ap_uint<512> moveFetch;

            unsigned int previousLatestPage = latestPage;

            bool specialCase = false;
            unsigned int previousPage = 0;
            if(freeSpace == LITERAL_PAGE_SIZE-2){
                freeLitPageAddresses.write(latestPage*LIT_SLOTS_PER_WORD);

                moveFetch = occReadWord(litStore, occCacheTag, litStoreDDR, latestPage+LITERAL_PAGE_SIZE/LIT_SLOTS_PER_WORD-1);
                LMD_LATEST_PAGE(getLmd.compactlmd,selectSide) = LIT_BACK_PTR(moveFetch);
                LMD_FREE_SPACE(getLmd.compactlmd,selectSide) = 1;
                reqAddrOffsetMove = LITERAL_PAGE_SIZE - 2 - 1;
                latestPage = LIT_BACK_PTR(moveFetch)/LIT_SLOTS_PER_WORD;
            }else{
                LMD_FREE_SPACE(getLmd.compactlmd,selectSide) = freeSpace + 1;
            }

            cls movedCls;
            unsigned int replaceAddr = address;
            unsigned int swapAddr = latestPage*LIT_SLOTS_PER_WORD + reqAddrOffsetMove;
            if(address/LIT_SLOTS_PER_WORD == latestPage + reqAddrOffsetMove/LIT_SLOTS_PER_WORD){
                movedCls = LIT_SLOT(replaceFetch, reqAddrOffsetMove%LIT_SLOTS_PER_WORD);
                LIT_SLOT(replaceFetch, address%LIT_SLOTS_PER_WORD) = movedCls;
                LIT_SLOT(replaceFetch, reqAddrOffsetMove%LIT_SLOTS_PER_WORD) = 0;

                occWriteWord(litStore, occCacheTag, litStoreDDR, address/LIT_SLOTS_PER_WORD, replaceFetch);
            }else{
                moveFetch = occReadWord(litStore, occCacheTag, litStoreDDR, latestPage + reqAddrOffsetMove/LIT_SLOTS_PER_WORD);
                movedCls = LIT_SLOT(moveFetch, reqAddrOffsetMove%LIT_SLOTS_PER_WORD);

                LIT_SLOT(replaceFetch, address%LIT_SLOTS_PER_WORD) = movedCls;
                LIT_SLOT(moveFetch, reqAddrOffsetMove%LIT_SLOTS_PER_WORD) = 0;

                occWriteWord(litStore, occCacheTag, litStoreDDR, address/LIT_SLOTS_PER_WORD, replaceFetch);
                occWriteWord(litStore, occCacheTag, litStoreDDR, latestPage + reqAddrOffsetMove/LIT_SLOTS_PER_WORD, moveFetch);
            }

            ap_axiu<96,0,0,0> sendData;
            sendData.data.range(31,0) = movedCls;
            sendData.data.range(63,32) = swapAddr;
            sendData.data.range(95,64) = replaceAddr;
            clauseStoreInputStream1.write(sendData);

            LMD_NUM_ELE(getLmd.compactlmd,selectSide) = numElements-1;
            lmd[abs(literalToRemove)-1] = getLmd;
        }
        #ifdef FPGA_HW
        ap_axiu<96,0,0,0> sendClauseInputCommand;
        sendClauseInputCommand.data.range(95,64) = csh::EXIT;
        clauseStoreInputStream1.write(sendClauseInputCommand);
        #endif
    }
}