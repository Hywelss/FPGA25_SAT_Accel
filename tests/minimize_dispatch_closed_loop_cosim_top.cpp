#include "minimize.h"

static void countMinimizeLane(hls::stream<lit>& input,
    unsigned int& count){
    #pragma HLS inline off
    count = 0;
    COUNT_LANE: while(true){
        #pragma HLS loop_tripcount min=1 max=1025
        const lit literal = input.read();
        if(literal == 0){
            break;
        }
        count++;
    }
}

static void produceMinimizeInput(const unsigned int inputCount,
    hls::stream<lit>& output){
    #pragma HLS inline off
    PRODUCE_INPUT: for(unsigned int i = 0; i < inputCount; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        #pragma HLS pipeline II=1
        output.write((lit)(i+1));
    }
}

void minimizeDispatchClosedLoopCosim(const unsigned int inputCount,
    const bool foundAbsolute, unsigned int& lane0Count,
    unsigned int& lane1Count){
    hls::stream<lit> input;
    #pragma HLS stream variable=input depth=2
    hls::stream<lit> lanes[2];
    #pragma HLS stream variable=lanes depth=2
    #pragma HLS array_partition variable=lanes complete
    #pragma HLS dataflow

    produceMinimizeInput(inputCount, input);
    minimize_dispatch(input, lanes, foundAbsolute);
    countMinimizeLane(lanes[0], lane0Count);
    countMinimizeLane(lanes[1], lane1Count);
}
