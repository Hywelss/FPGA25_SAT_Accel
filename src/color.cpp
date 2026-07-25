#include "color.h"

int splitResidualCnt = 0;
unsigned int checkCnt = 0;
void colorStream(hls::stream<colorValue>* toStateUpdater,
    hls::stream<colorAssignment>& toColorStream, hls::stream<bool>* stopSending,
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD], const ap_int<512>* litStoreDDR, occTagEntry* occCacheTag,
    const unsigned int LITERAL_PAGE_SIZE,
    lit* literalCommit, const unsigned int type, ap_uint<64>* litStoreAccessStats){
    #pragma HLS inline off

    int state = 0;
    colorAssignment readOne;
    unsigned int numElementsRead = 0;
    unsigned int pageWalkIndex = 0;
    bool once = false;

    ap_int<512> val = 0;

    readOne.eos = false;
    readOne.literal = 0;
    readOne.addressStart = 0;

    unsigned int address = 0;
    unsigned int tmpAddr = 0;

    ap_uint<64> localAccessStats[4] = {0,0,0,0};
    #pragma HLS array_partition variable=localAccessStats complete

    bool stopSendingRead = false;
    bool didRead = false;

    const unsigned int READ_CHUNK_SIZE=_FPGA_CLS_STATES_PARTITION;

    // A 16-slot page word serves READ_CHUNK_SIZE=8 slots per iteration, so the
    // same word is consumed by two consecutive iterations. Going through the
    // cache arrays for the second one creates a distance-1 read-after-write on a
    // miss and costs an initiation interval (measured II 1 -> 2). Keep the last
    // few (word, value) pairs in registers and answer from them instead, the
    // same bypass idiom the lmd/clsStates walks already use. Line reuse beyond
    // this window needs a fresh address that collides in the direct-mapped
    // index, which a sequential page walk does not produce, so the arrays can be
    // declared free of cross-iteration dependence.
    const int OCC_BYPASS = _FPGA_DISC_LMD_DEP_DIST;
    unsigned int bypassWord[_FPGA_DISC_LMD_DEP_DIST];
    #pragma HLS array_partition variable=bypassWord complete
    ap_uint<512> bypassVal[_FPGA_DISC_LMD_DEP_DIST];
    #pragma HLS array_partition variable=bypassVal complete
    INVALID_OCC_BYPASS: for(unsigned int i = 0; i < OCC_BYPASS; i++){
        #pragma HLS unroll
        bypassWord[i] = 0xFFFFFFFFu;
    }

    COLOR_STREAM: while(true){
        #pragma HLS loop_tripcount min=1024 max=1024
        #pragma HLS pipeline II=1
        // No inter-iteration dependence override here. The array is a
        // direct-mapped cache, so occLine() aliases many words onto one line and
        // consecutive iterations working on different pages can legitimately
        // collide; asserting independence would let a later iteration read a tag
        // the previous one has not written yet and hit on the wrong page. The
        // register bypass below only covers repeats of the same word.

        if(type == 0){
            if(stopSending->read_nb(stopSendingRead)){
                didRead = true;
            }
        }

        const unsigned int occWord = address/LIT_SLOTS_PER_WORD;
        // Probe the register bypass, then the direct-mapped page cache, then
        // DDR. A hit is a plain URAM read, so an instance whose working set
        // stays resident runs at the speed of the all-on-chip design.
        {
            bool bypassed = false;
            for(unsigned int i = 0; i < OCC_BYPASS; i++){
                #pragma HLS unroll
                if(bypassWord[i] == occWord){
                    val = bypassVal[i];
                    bypassed = true;
                }
            }

            if(!bypassed){
                val = occReadWord(litStore, occCacheTag, litStoreDDR, occWord);
            }

            for(unsigned int i = 0; i < OCC_BYPASS-1; i++){
                #pragma HLS unroll
                bypassWord[i] = bypassWord[i+1];
                bypassVal[i] = bypassVal[i+1];
            }
            bypassWord[OCC_BYPASS-1] = occWord;
            bypassVal[OCC_BYPASS-1] = val;
        }

        if(state == 0){
            if(readOne.eos || stopSendingRead){
                break;
            }else{
                if(toColorStream.read_nb(readOne)){
                    if(!readOne.eos){
                        state = 0;
                        if(readOne.numElements != 0){
                            state = 1;
                        }
                    
                        if(type == 0){
			    checkCnt++;
                            *literalCommit = readOne.literal;
                        }

                        once = false;
                        address = readOne.addressStart;
                    }
                }
            }
        }else if(state == 1){
            colorValue put;
#if defined(LIT_STORE_PACK)
            // Packed slots are LIT_SLOT_BITS wide; zero-extend each of the
            // READ_CHUNK_SIZE occurrence entries back to a 32-bit lane so the
            // downstream colorValue.clsID / updateStatesForward stay 32-bit.
            for(unsigned int q = 0; q < READ_CHUNK_SIZE; q++){
                #pragma HLS unroll
                put.clsID.range(32*q+31,32*q) =
                    (ap_uint<32>)LIT_SLOT(val, (pageWalkIndex%LIT_SLOTS_PER_WORD)+q);
            }
#else
            put.clsID = val.range(32*(pageWalkIndex%LIT_SLOTS_PER_WORD)+32*READ_CHUNK_SIZE-1,32*(pageWalkIndex%LIT_SLOTS_PER_WORD));
#endif
            put.litID = readOne.literal;
            put.depthCount = readOne.depthCount;

            put.clsEos = false;
            put.didSolve = false;
            put.streamEos = false;

            address += READ_CHUNK_SIZE;
            pageWalkIndex += READ_CHUNK_SIZE;
            numElementsRead += READ_CHUNK_SIZE;
            if(pageWalkIndex == LITERAL_PAGE_SIZE - READ_CHUNK_SIZE){
                tmpAddr = reg((unsigned int)LIT_NEXT_PTR(val));
            }else if(pageWalkIndex == LITERAL_PAGE_SIZE){
                pageWalkIndex = 0;
                put.clsID.range(32*READ_CHUNK_SIZE-1,32*(READ_CHUNK_SIZE-2)) = 0;

                address = tmpAddr;
                numElementsRead -= 2;
            }
            if(numElementsRead >= readOne.numElements){
                state = 0;
                numElementsRead = 0;
                pageWalkIndex = 0;

                put.clsEos = true;
            }

            if(type == 0){
                for(unsigned int i = 0; i < _FPGA_CLS_STATES_PARTITION/2; i++){
                    toStateUpdater[i].write(put);
                }
            }else{
                for(unsigned int i = 0; i < _FPGA_CLS_STATES_PARTITION; i++){
                    toStateUpdater[i].write(put);
                }
            }

            if(type == 0){
                localAccessStats[2]++;
                if(pageWalkIndex == 0 && !once){
                    localAccessStats[0]++;
                    once = true;
                }
            }else if(type == 1){
                localAccessStats[3]++;
                if(pageWalkIndex == 0 && !once){
                    localAccessStats[1]++;
                    once = true;
                }
            }
        }
    }

    if(type == 0){
        litStoreAccessStats[2] += localAccessStats[2];
        litStoreAccessStats[0] += localAccessStats[0];
    }else if(type == 1){
        litStoreAccessStats[3] += localAccessStats[3];
        litStoreAccessStats[1] += localAccessStats[1];           
    }

    if(type == 0){
        SEND_END_COLOR_FWD: for(unsigned int i = 0; i < _FPGA_CLS_STATES_PARTITION/2; i++){
            toStateUpdater[i].write((colorValue){.clsID=0,.litID=0,.depthCount=0,.didSolve=false,.clsEos=false,.streamEos=true});
        }
    }else{
        SEND_END_COLOR_BCK: for(unsigned int i = 0; i < _FPGA_CLS_STATES_PARTITION; i++){
            toStateUpdater[i].write((colorValue){.clsID=0,.litID=0,.depthCount=0,.didSolve=false,.clsEos=false,.streamEos=true});
        }
    }

    if(type == 0){
        FLUSH_SPLIT_ACCESS: while(!readOne.eos){
            #pragma HLS loop_tripcount min=16 max=16

            readOne = toColorStream.read();
            splitResidualCnt++;
        }

        if(!didRead){
            stopSendingRead = stopSending->read();
        }
    }
    
}
