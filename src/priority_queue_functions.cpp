#include "priority_queue_functions.h"

void loadPositioning(pqPosition mPositioning[_FPGA_MAX_LITERALS], pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS){
    #pragma HLS inline off

    GET_POSITION_3: for(unsigned int i = 0; i < NUM_LITERALS; i++){
        #pragma HLS loop_tripcount min=1024 max=1024

        mPriorityQueue[0][i].literal = i+1;
        mPriorityQueue[1][i].literal = i+1;
        mPriorityQueue[0][i].score = 0;
        mPriorityQueue[1][i].score = 0;

        mPositioning[i].pos = i;
    }

    MOVE_DOMAIN_TO_FRONT: for(unsigned int i = 0; i < NUM_DOMAIN_LITERALS; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        const unsigned int variable = decisionDomain[i];
        const unsigned int oldPosition = mPositioning[variable-1].pos;
        const pqData displaced = mPriorityQueue[0][i];
        const pqData selected = mPriorityQueue[1][oldPosition];

        mPriorityQueue[0][i] = selected;
        mPriorityQueue[1][i] = selected;
        mPositioning[variable-1].pos = i;

        mPriorityQueue[0][oldPosition] = displaced;
        mPriorityQueue[1][oldPosition] = displaced;
        mPositioning[displaced.literal-1].pos = oldPosition;
    }
}

inline int comp_fp64(double op1, double op2){
    // Credit to Linghao Song
    // return +1; op1 > op2
    // return -1; op1 < op2
    // return 0;
    //#pragma HLS inline
    
    ap_uint<64> u_op1 = *((ap_uint<64>*)(&op1));
    ap_uint<64> u_op2 = *((ap_uint<64>*)(&op2));

    if(u_op1.range(63,0) > u_op2.range(63,0)){
        return 1;
    }else if(u_op1.range(63,0) < u_op2.range(63,0)){
        return -1;
    }

    //sign
    /*if ((u_op1[63] == 0) & (u_op2[63] == 1)) {
        // op1 > 0.0, and op2 < 0.0
        return 1;
    }
    else if ((u_op1[63] == 1) & (u_op2[63] == 0)) {
        // op1 < 0.0, and op2 > 0.0
        return -1;
    }
    
    // exponent
    else if (
        ((u_op1[63] == 0) & (u_op1(62, 52) > u_op2(62, 52))) // both sign = 0, only need to check one sign
        | ((u_op1[63] == 1) & (u_op1(62, 52) < u_op2(62, 52))) // both sign = 1
    ) {
        // op1 > op2
        return 1;
    }
    else if (
        ((u_op1[63] == 0) & (u_op1(62, 52) < u_op2(62, 52))) // both sign = 0, only need to check one sign
        | ((u_op1[63] == 1) & (u_op1(62, 52) > u_op2(62, 52))) // both sign = 1
    ) {
        // op1 < op2
        return -1;
    }

    // fraction
    else if (
        ((u_op1[63] == 0) & (u_op1(51, 0) > u_op2(51, 0))) // both sign = 0, only need to check one sign
        | ((u_op1[63] == 1) & (u_op1(51, 0) < u_op2(51, 0))) // both sign = 1
    ) {
        // op1 > op2
        return 1;
    }
    else if (
        ((u_op1[63] == 0) & (u_op1(51, 0) < u_op2(51, 0))) // both sign = 0, only need to check one sign
        | ((u_op1[63] == 1) & (u_op1(51, 0) > u_op2(51, 0))) // both sign = 1
    ) {
        // op1 < op2
        return -1;
    }*/
    return 0;

    
}

void swapLower(pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const pqData initialpqData, const unsigned int initialPosition, const unsigned int remainingLiterals){
    #pragma HLS inline

    unsigned int getPosition = initialPosition;
    pqData toMoveLower = initialpqData;
    unsigned int savePosition = getPosition;

    pqData rightChild;
    pqData leftChild;

    bool exit = false;

    SWAP_LOWER: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        #pragma HLS dependence variable=mPriorityQueue inter false
        #pragma HLS pipeline II=2

        if(exit){
            break;
        }
        
        bool childExists[2] = {false, false};
        #pragma HLS array_partition variable=childExists dim=0 complete

        int newPosition = 2*getPosition+1;
        leftChild = mPriorityQueue[0][newPosition];
        if(newPosition >= remainingLiterals){
            leftChild.score = 0;
        }
        
        newPosition++;
        rightChild = mPriorityQueue[1][newPosition];
        if(newPosition >= remainingLiterals){
            rightChild.score = 0;
        }

        ap_uint<64> u_op1 = *((ap_uint<64>*)(&(leftChild.score)));
        ap_uint<64> u_op2 = *((ap_uint<64>*)(&(toMoveLower.score)));
        ap_uint<64> u_op3 = *((ap_uint<64>*)(&(leftChild.score)));
        ap_uint<64> u_op4 = *((ap_uint<64>*)(&(rightChild.score)));
        ap_uint<64> u_op5 = *((ap_uint<64>*)(&(rightChild.score)));
        ap_uint<64> u_op6 = *((ap_uint<64>*)(&(toMoveLower.score)));

        bool expr1 = (u_op1.range(62,0) > u_op2.range(62,0));
        bool expr2 = (u_op3.range(62,0) >= u_op4.range(62,0));
        bool expr3 = (u_op5.range(62,0) > u_op6.range(62,0));

        pqData use = leftChild;
        savePosition = getPosition;
        getPosition = 2*getPosition+1;

        if(expr3 && !expr2){
            use = rightChild;
            getPosition++;
        }else if(!expr1 && !expr3){
            use = toMoveLower;
            exit = true;
        }

        mPriorityQueue[0][savePosition] = use;
        mPriorityQueue[1][savePosition] = use;
        mPositioning[use.literal-1].pos = savePosition;
    }

    mPositioning[toMoveLower.literal-1].pos = savePosition;
    mPriorityQueue[0][savePosition] = toMoveLower;
    mPriorityQueue[1][savePosition] = toMoveLower;
}

void swapHigher(pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const pqData initialpqData, const unsigned int initialPosition){
    #pragma HLS inline

    unsigned int getPosition = initialPosition;
    pqData toMoveHigher = initialpqData;

    int savePosition = getPosition;
    bool exit = false;

    SWAP_HIGHER: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        #pragma HLS dependence variable=mPriorityQueue inter false
        #pragma HLS pipeline II=1

        int newPosition = ((int)getPosition-1)/2;
        if(exit || getPosition == 0){
            break;
        }

        pqData parent = mPriorityQueue[0][newPosition];
        int cmp = comp_fp64(parent.score,toMoveHigher.score);
        
        if(cmp == -1){

            mPriorityQueue[0][getPosition] = reg(parent);
            mPriorityQueue[1][getPosition] = reg(parent);
            mPositioning[parent.literal-1].pos = reg(getPosition);

            savePosition = newPosition;
        }else{   
            exit = true;
        }
        getPosition = newPosition;
    }

    mPositioning[toMoveHigher.literal-1].pos = savePosition;
    mPriorityQueue[0][savePosition] = toMoveHigher;
    mPriorityQueue[1][savePosition] = toMoveHigher;
}

void hideElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], unsigned int& remainingLiterals){
    #pragma HLS inline off

    HIDE_LOOP: while(true){
        #pragma HLS loop_tripcount min=32 max=32

        lit getLit;
        if(input.read_nb(getLit)){
            if(getLit == pq::EXIT){
                break;
            }

            pqPosition swapPosition = mPositioning[getLit-1];
            if(swapPosition.pos >= remainingLiterals){
                continue;
            }
            unsigned int getRemaining = remainingLiterals-1;
        
            pqData swap = mPriorityQueue[0][swapPosition.pos];
            pqData last = mPriorityQueue[1][getRemaining];

            mPriorityQueue[0][swapPosition.pos] = last;
            mPriorityQueue[1][swapPosition.pos] = last;
            mPositioning[last.literal-1].pos = swapPosition.pos;

            mPriorityQueue[0][getRemaining] = swap;
            mPriorityQueue[1][getRemaining] = swap;
            mPositioning[getLit-1].pos = getRemaining;  
            remainingLiterals = getRemaining;
                
            swapLower(mPriorityQueue, mPositioning, last, swapPosition.pos, getRemaining);
        }
    }
}

void unhideElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], const unsigned int NUM_DOMAIN_LITERALS,
    unsigned int& remainingLiterals){
    #pragma HLS inline off
    
    UNHIDE_LOOP: while(true){
        #pragma HLS loop_tripcount min=32 max=32

        lit getLit;
        if(input.read_nb(getLit)){
        
            if(getLit == pq::EXIT){
                break;
            }

            const unsigned int position = mPositioning[getLit-1].pos;
            if(position < remainingLiterals || position >= NUM_DOMAIN_LITERALS){
                continue;
            }

            pqPosition swapPosition = mPositioning[getLit-1];
            unsigned int getRemaining = remainingLiterals;
        
            pqData swap = mPriorityQueue[0][swapPosition.pos];
            pqData last = mPriorityQueue[1][getRemaining];

            mPriorityQueue[0][swapPosition.pos] = mPriorityQueue[0][getRemaining];
            mPriorityQueue[1][swapPosition.pos] = mPriorityQueue[1][getRemaining];
            mPositioning[last.literal-1].pos = swapPosition.pos;

            mPriorityQueue[0][getRemaining] = swap;
            mPriorityQueue[1][getRemaining] = swap;
            mPositioning[getLit-1].pos = getRemaining; 

            swapHigher(mPriorityQueue, mPositioning, swap, getRemaining);
            getRemaining++;
            remainingLiterals = getRemaining;
        }
    }
}

void decayEveryElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const unsigned int remainingLiterals, double& multiplier, const double decayFactor, const unsigned int NUM_LITERALS){
    #pragma HLS inline off

    DECAY_LOOP: while(true){
        #pragma HLS loop_tripcount min=32 max=32

        lit getLit;
        if(input.read_nb(getLit)){
            if(getLit == pq::EXIT){
                break;
            }

            pqPosition position = mPositioning[getLit-1];
            pqData getPq = mPriorityQueue[0][position.pos];

            mPriorityQueue[0][position.pos].score += multiplier;
            mPriorityQueue[1][position.pos].score += multiplier;
            getPq.score += multiplier;
        
            if(getPq.score > 1e100){
                ADJUST: for(unsigned int i = 0; i < NUM_LITERALS; i++){
                    #pragma HLS loop_tripcount min=1024 max=1024
                    #pragma HLS dependence variable=mPriorityQueue inter false

                    mPriorityQueue[0][i].score *= 1e-100;
                    mPriorityQueue[1][i].score *= 1e-100;
                }
                multiplier *= 1e-100;
            }

            if(position.pos < remainingLiterals){
                swapHigher(mPriorityQueue, mPositioning, getPq, position.pos);
            }
        }
    }
    multiplier *= 1/decayFactor;
}


void axiStreamBuffer(hls::stream<ap_axiu<32,0,0,0>>& input, hls::stream<lit>& intermediateStream){
    #pragma HLS inline off
    BUFFER_LOOP: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        ap_axiu<32,0,0,0> read = input.read();

        lit getLiteral = read.data;
        intermediateStream.write(getLiteral);

        if(getLiteral == pq::EXIT){
            break;
        }
    }
}
void hide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], unsigned int& remainingLiterals){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=MAX_STREAM_DEPTH
    #pragma HLS bind_storage variable=intermediateStream type=FIFO impl=URAM

    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    hideElement(intermediateStream, mPriorityQueue, mPositioning, remainingLiterals);
}
void unhide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], const unsigned int NUM_DOMAIN_LITERALS,
    unsigned int& remainingLiterals){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=MAX_STREAM_DEPTH
    #pragma HLS bind_storage variable=intermediateStream type=FIFO impl=URAM

    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    unhideElement(intermediateStream, mPriorityQueue, mPositioning, NUM_DOMAIN_LITERALS, remainingLiterals);
}
void unhide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const unsigned int remainingLiterals, double& multiplier, const double decayFactor, const unsigned int NUM_LITERALS){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=_FPGA_MAX_LEARN_ELE

    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    decayEveryElement(intermediateStream, mPriorityQueue, mPositioning, remainingLiterals, multiplier, decayFactor, NUM_LITERALS);
}

unsigned int gipsatBucketIndex(unsigned int position){
    #pragma HLS inline
    unsigned int bucket = 0;
    BUCKET_INDEX: while(position != 0){
        #pragma HLS loop_tripcount min=0 max=16
        bucket++;
        position >>= 1;
    }
    return bucket;
}

void gipsatBucketPush(unsigned int variable,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], gipsatLink activityHeapSize,
    gipsatBucket& bucketHead){
    #pragma HLS inline
    const unsigned int index = variable-1;
    if(bucketState[index][0] == 0 || bucketState[index][1] != 0 || bucketState[index][2] != 0){
        return;
    }

    const unsigned int position = mPositioning[index].pos;
    const bool inActivityHeap = position < activityHeapSize &&
        mActivityHeap[0][position].literal == (lit)variable;
    const unsigned int bucket = gipsatBucketIndex(
        inActivityHeap ? position : (unsigned int)activityHeapSize);

    bucketNext[index] = bucketHeads[bucket];
    bucketHeads[bucket] = variable;
    bucketState[index][1] = 1;
    if(bucketHead > bucket){
        bucketHead = bucket;
    }
}

void loadGipsatBuckets(pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    gipsatBucket& bucketHead, gipsatLink& activityHeapSize){
    #pragma HLS inline off

    INIT_BUCKET_VARIABLES: for(unsigned int i = 0; i < NUM_LITERALS; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        bucketState[i] = 0;
        bucketNext[i] = 0;
        mPositioning[i].pos = GIPSAT_POSITION_NONE;
    }
    INIT_BUCKET_HEADS: for(unsigned int i = 0; i < GIPSAT_NUM_BUCKETS; i++){
        #pragma HLS unroll
        bucketHeads[i] = 0;
    }

    bucketHead = 0;
    activityHeapSize = 0;
    LOAD_BUCKET_DOMAIN: for(unsigned int i = 0; i < NUM_DOMAIN_LITERALS; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        // Preserve the loop-carried bucket-head value across the generated RTL pipeline.
        #pragma HLS pipeline II=2
        const unsigned int variable = decisionDomain[i];
        bucketState[variable-1][0] = 1;
        bucketNext[variable-1] = bucketHeads[0];
        bucketHeads[0] = variable;
        bucketState[variable-1][1] = 1;
    }
}

void reloadGipsatBuckets(const pqData mActivityHeap[2][_FPGA_MAX_LITERALS],
    const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    gipsatLink activityHeapSize, gipsatBucket& bucketHead){
    #pragma HLS inline off

    RELOAD_BUCKET_VARIABLES: for(unsigned int i = 0; i < NUM_LITERALS; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        bucketState[i] = 0;
        bucketNext[i] = 0;
    }
    RELOAD_BUCKET_HEADS: for(unsigned int i = 0; i < GIPSAT_NUM_BUCKETS; i++){
        #pragma HLS unroll
        bucketHeads[i] = 0;
    }
    bucketHead = GIPSAT_NUM_BUCKETS;
    RELOAD_BUCKET_DOMAIN: for(unsigned int i = 0; i < NUM_DOMAIN_LITERALS; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        const unsigned int variable = decisionDomain[i];
        bucketState[variable-1][0] = 1;
        gipsatBucketPush(variable, mActivityHeap, mPositioning, bucketState,
            bucketNext, bucketHeads, activityHeapSize, bucketHead);
    }
}

lit gipsatBucketPop(ap_uint<3> bucketState[_FPGA_MAX_LITERALS],
    const gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], gipsatBucket& bucketHead){
    #pragma HLS inline
    POP_AVAILABLE_BUCKET_LITERAL: while(true){
        #pragma HLS loop_tripcount min=1 max=1024
        FIND_NONEMPTY_BUCKET: while(bucketHead < GIPSAT_NUM_BUCKETS && bucketHeads[bucketHead] == 0){
            #pragma HLS loop_tripcount min=0 max=17
            bucketHead++;
        }
        if(bucketHead == GIPSAT_NUM_BUCKETS){
            return pq::DOMAIN_EXHAUSTED;
        }

        const unsigned int variable = bucketHeads[bucketHead];
        bucketHeads[bucketHead] = bucketNext[variable-1];
        bucketState[variable-1][1] = 0;
        if(bucketState[variable-1][2] == 0){
            return variable;
        }
    }
}

void gipsatBumpActivity(hls::stream<lit>& input,
    pqData mActivityHeap[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    gipsatLink& activityHeapSize, double& multiplier, const double decayFactor){
    #pragma HLS inline off
    BUMP_ACTIVITY: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        lit variable;
        if(input.read_nb(variable)){
            if(variable == pq::EXIT){
                break;
            }
            variable = abs(variable);
            const unsigned int index = variable-1;
            const unsigned int position = mPositioning[index].pos;
            const bool present = position < activityHeapSize &&
                mActivityHeap[0][position].literal == variable;
            pqData bumped = present ? mActivityHeap[0][position] : (pqData){.score=0,.literal=variable};
            bumped.score += multiplier;
            unsigned int insertPosition = position;
            if(!present){
                insertPosition = activityHeapSize;
                activityHeapSize++;
            }
            mPositioning[index].pos = insertPosition;
            mActivityHeap[0][insertPosition] = bumped;
            mActivityHeap[1][insertPosition] = bumped;
            swapHigher(mActivityHeap, mPositioning, bumped, insertPosition);

            if(bumped.score > 1e100){
                RESCALE_ACTIVITY: for(unsigned int i = 0; i < activityHeapSize; i++){
                    #pragma HLS loop_tripcount min=1 max=1024
                    mActivityHeap[0][i].score *= 1e-100;
                    mActivityHeap[1][i].score *= 1e-100;
                }
                multiplier *= 1e-100;
            }
        }
    }
    multiplier *= 1/decayFactor;
}

void gipsatBumpActivityWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    pqData mActivityHeap[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    gipsatLink& activityHeapSize, double& multiplier, const double decayFactor){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=_FPGA_MAX_LEARN_ELE
    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    gipsatBumpActivity(intermediateStream, mActivityHeap, mPositioning,
        activityHeapSize, multiplier, decayFactor);
}

void gipsatBucketUnhide(hls::stream<lit>& input,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], gipsatLink activityHeapSize,
    gipsatBucket& bucketHead){
    #pragma HLS inline off
    UNHIDE_BUCKET: while(true){
        #pragma HLS loop_tripcount min=32 max=32
        lit variable;
        if(input.read_nb(variable)){
            if(variable == pq::EXIT){
                break;
            }
            variable = abs(variable);
            bucketState[variable-1][2] = 0;
            gipsatBucketPush(variable, mActivityHeap, mPositioning, bucketState,
                bucketNext, bucketHeads, activityHeapSize, bucketHead);
        }
    }
}

void gipsatBucketUnhideWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink bucketNext[_FPGA_MAX_LITERALS],
    gipsatLink bucketHeads[GIPSAT_NUM_BUCKETS], gipsatLink activityHeapSize,
    gipsatBucket& bucketHead){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=MAX_STREAM_DEPTH
    #pragma HLS bind_storage variable=intermediateStream type=FIFO impl=BRAM
    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    gipsatBucketUnhide(intermediateStream, mActivityHeap, mPositioning, bucketState,
        bucketNext, bucketHeads, activityHeapSize, bucketHead);
}

void gipsatBucketHide(hls::stream<lit>& input,
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS]){
    #pragma HLS inline off
    HIDE_BUCKET: while(true){
        #pragma HLS loop_tripcount min=1 max=1024
        lit variable;
        if(input.read_nb(variable)){
            if(variable == pq::EXIT){
                break;
            }
            variable = abs(variable);
            if(bucketState[variable-1][0] != 0){
                bucketState[variable-1][2] = 1;
            }
        }
    }
}

void gipsatBucketHideWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS]){
    #pragma HLS inline off
    hls::stream<lit> intermediateStream;
    #pragma HLS stream variable=intermediateStream depth=1024
    #pragma HLS dataflow
    axiStreamBuffer(input, intermediateStream);
    gipsatBucketHide(intermediateStream, bucketState);
}

static pqData gipsatActivity(const pqData mActivityHeap[2][_FPGA_MAX_LITERALS],
    const pqPosition mPositioning[_FPGA_MAX_LITERALS], unsigned int activityHeapSize,
    unsigned int variable){
    #pragma HLS inline
    const unsigned int position = mPositioning[variable-1].pos;
    if(position < activityHeapSize && mActivityHeap[0][position].literal == (lit)variable){
        return mActivityHeap[0][position];
    }
    return (pqData){.score=0,.literal=(lit)variable};
}

static void gipsatHeapPushSingle(pqData heap[_FPGA_MAX_LITERALS],
    gipsatLink newPosition[_FPGA_MAX_LITERALS], pqData value, unsigned int size){
    #pragma HLS inline
    unsigned int position = size;
    PUSH_SINGLE_HEAP: while(position != 0){
        #pragma HLS loop_tripcount min=0 max=32
        const unsigned int parentPosition = (position-1)>>1;
        const pqData parent = heap[parentPosition];
        if(parent.score >= value.score){
            break;
        }
        heap[position] = parent;
        newPosition[parent.literal-1] = position;
        position = parentPosition;
    }
    heap[position] = value;
    newPosition[value.literal-1] = position;
}

void gipsatSwitchToHeap(const unsigned int* decisionDomain,
    pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    const ap_uint<3> bucketState[_FPGA_MAX_LITERALS], gipsatLink newPosition[_FPGA_MAX_LITERALS],
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    gipsatLink activityHeapSize, unsigned int& remainingLiterals){
    #pragma HLS inline off

    CLEAR_NEW_POSITIONS: for(unsigned int i = 0; i < NUM_LITERALS; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        newPosition[i] = GIPSAT_LINK_NONE;
    }

    unsigned int active = 0;
    BUILD_ACTIVE_HEAP: for(unsigned int i = 0; i < NUM_DOMAIN_LITERALS; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        const unsigned int variable = decisionDomain[i];
        if(bucketState[variable-1][1] != 0 && bucketState[variable-1][2] == 0){
            const pqData value = gipsatActivity(mPriorityQueue, mPositioning, activityHeapSize, variable);
            gipsatHeapPushSingle(mPriorityQueue[1], newPosition, value, active++);
        }
    }
    remainingLiterals = active;

    APPEND_HIDDEN_DOMAIN: for(unsigned int i = 0; i < NUM_DOMAIN_LITERALS; i++){
        #pragma HLS loop_tripcount min=1 max=1024
        const unsigned int variable = decisionDomain[i];
        if(bucketState[variable-1][1] == 0 || bucketState[variable-1][2] != 0){
            const pqData value = gipsatActivity(mPriorityQueue, mPositioning, activityHeapSize, variable);
            mPriorityQueue[1][active] = value;
            newPosition[variable-1] = active++;
        }
    }

    APPEND_OUTSIDE_DOMAIN: for(unsigned int variable = 1; variable <= NUM_LITERALS; variable++){
        #pragma HLS loop_tripcount min=1024 max=1024
        if(newPosition[variable-1] == GIPSAT_LINK_NONE){
            const pqData value = gipsatActivity(mPriorityQueue, mPositioning, activityHeapSize, variable);
            mPriorityQueue[1][active] = value;
            newPosition[variable-1] = active++;
        }
    }

    COMMIT_DECISION_HEAP: for(unsigned int i = 0; i < NUM_LITERALS; i++){
        #pragma HLS loop_tripcount min=1024 max=1024
        const pqData value = mPriorityQueue[1][i];
        mPriorityQueue[0][i] = value;
        mPositioning[value.literal-1].pos = i;
    }
}
