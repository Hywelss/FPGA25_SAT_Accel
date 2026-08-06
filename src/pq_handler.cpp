#include <hls_stream.h>
#include <ap_utils.h>
#include <ap_axi_sdata.h>
#include "data_structures.h"
#include "priority_queue_functions.h"

extern "C"{
void pqHandler(const unsigned int* decision_domain, int num_literals, int num_domain_literals,
    double decay, bool session_reset,
    hls::stream<ap_axiu<32,0,0,0>>& input, hls::stream<ap_axiu<32,0,0,0>>& output){

    #pragma HLS INTERFACE axis port=input
    #pragma HLS INTERFACE axis port=output

    #pragma HLS INTERFACE m_axi port=decision_domain offset=slave bundle=gmemDomain latency=40 depth=_FPGA_MAX_LITERALS
    #pragma HLS INTERFACE s_axilite port=decision_domain
    #pragma HLS INTERFACE s_axilite port=num_literals
    #pragma HLS INTERFACE s_axilite port=num_domain_literals
    #pragma HLS INTERFACE s_axilite port=decay
    #pragma HLS INTERFACE s_axilite port=session_reset

	#pragma HLS INTERFACE s_axilite port=return

    static pqData mPriorityQueue[2][_FPGA_MAX_LITERALS];
    #pragma HLS array_partition variable=mPriorityQueue dim=1 complete
    #pragma HLS aggregate variable=mPriorityQueue compact=auto
    #pragma HLS bind_storage variable=mPriorityQueue type=RAM_S2P impl=URAM latency=1

    static pqPosition mPositioning[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=mPositioning type=RAM_S2P impl=URAM latency=1

    static ap_uint<3> bucketState[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=bucketState type=RAM_S2P impl=BRAM latency=1

    static gipsatLink bucketNext[_FPGA_MAX_LITERALS];
    #pragma HLS bind_storage variable=bucketNext type=RAM_S2P impl=BRAM latency=1

    static gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS];
    #pragma HLS array_partition variable=bucketHeads complete

    unsigned int remainingLiterals = num_domain_literals;

    unsigned int NUM_LITERALS = num_literals;

    static double multiplier = 1.0;
    double decayFactor = decay;

    static gipsatBucket bucketHead = 0;
    static gipsatLink activityHeapSize = 0;
    static unsigned int previousNumLiterals = 0;
    bool useBucket = true;
    if(session_reset){
        multiplier = 1.0;
        loadGipsatBuckets(mPositioning, bucketState, bucketNext, bucketHeads,
            decision_domain, NUM_LITERALS, num_domain_literals, bucketHead, activityHeapSize);
    }else{
        INITIALIZE_NEW_POSITIONS: for(unsigned int i = previousNumLiterals; i < NUM_LITERALS; i++){
            #pragma HLS loop_tripcount min=0 max=1024
            mPositioning[i].pos = GIPSAT_POSITION_NONE;
        }
        reloadGipsatBuckets(mPriorityQueue, mPositioning, bucketState, bucketNext, bucketHeads,
            decision_domain, NUM_LITERALS, num_domain_literals, activityHeapSize, bucketHead);
    }
    previousNumLiterals = NUM_LITERALS;

    int keepState = -1;
    while(true){
        ap_axiu<32,0,0,0> read;
        unsigned int code;

        if(keepState == -1){
            read = input.read();
            code = read.data;
        }else{
            code = keepState;
        }
        

        if(code == pq::EXIT){
            break;
        }else if(code == pq::GET_UNDECIDED){
            if(useBucket){
                GET_BUCKET_UNDECIDED: while(true){
                    #pragma HLS loop_tripcount min=8 max=8
                    ap_axiu<32,0,0,0> send;
                    send.data = gipsatBucketPop(bucketState, bucketNext, bucketHeads, bucketHead);
                    output.write(send);
                    ap_wait();
                    read = input.read();
                    code = read.data;
                    if(code == pq::EXIT){
                        break;
                    }
                }
                continue;
            }
            hls::stream<lit> removeLiterals;
            unsigned int streamSize = 0;
            #pragma HLS stream variable=removeLiterals depth=1024
            unsigned int inc = 0;
            keepState = -1;

            SCAN_LINEAR_FIND: while(true){
                #pragma HLS loop trip_count min=8 max=8

                if(code == pq::EXIT || streamSize == 1024-1){
                    if(streamSize == 1024-1 && code != pq::EXIT){
                        keepState = pq::GET_UNDECIDED;
                    }
                    break;
                }

                if(inc >= remainingLiterals){
                    ap_axiu<32,0,0,0> send;
                    send.data = pq::DOMAIN_EXHAUSTED;
                    output.write(send);
                    ap_wait();
                    read = input.read();
                    code = read.data;
                    continue;
                }

                lit getUndecided = mPriorityQueue[0][inc].literal;

                removeLiterals.write(abs(getUndecided));
                streamSize++;
                inc++;

                ap_axiu<32,0,0,0> send;
                send.data = getUndecided;
                output.write(send);
                ap_wait();
                read = input.read();
                code = read.data;
            }

            removeLiterals.write(pq::EXIT);
            hideElement(removeLiterals, mPriorityQueue, mPositioning, remainingLiterals);
        }else if(code == pq::UPDATE){
            if(useBucket){
                gipsatBumpActivityWrapper(input, mPriorityQueue, mPositioning,
                    activityHeapSize, multiplier, decayFactor);
            }else{
                unhide_wrapper(input, mPriorityQueue, mPositioning, remainingLiterals, multiplier, decayFactor, NUM_LITERALS);
            }
        }else if(code == pq::UNHIDE_ELE){
            if(useBucket){
                gipsatBucketUnhideWrapper(input, mPriorityQueue, mPositioning,
                    bucketState, bucketNext, bucketHeads, activityHeapSize, bucketHead);
            }else{
                unhide_wrapper(input, mPriorityQueue, mPositioning, num_domain_literals, remainingLiterals);
            }
        }else if(code == pq::HIDE_ELE){
            if(useBucket){
                gipsatBucketHideWrapper(input, bucketState);
            }else{
                hide_wrapper(input, mPriorityQueue, mPositioning, remainingLiterals);
            }
        }else if(code == pq::SWITCH_TO_HEAP && useBucket){
            gipsatSwitchToHeap(decision_domain, mPriorityQueue, mPositioning,
                bucketState, bucketNext, NUM_LITERALS, num_domain_literals,
                activityHeapSize, remainingLiterals);
            useBucket = false;
        }
    }

}
}
