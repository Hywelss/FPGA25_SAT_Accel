#include "data_structures.h"

void sendLength_wrapper(
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    const clauseMetaData mCmd[_FPGA_MAX_CLAUSES]);

void clauseLengthProtocolCosim(
    const clauseMetaData commands[8],
    hls::stream<ap_axiu<96,0,0,0>>& input,
    hls::stream<ap_axiu<32,0,0,0>>& output){
    #pragma HLS inline off

    LENGTH_PROTOCOL: while(true){
        #pragma HLS loop_tripcount min=1 max=8
        const ap_axiu<96,0,0,0> command = input.read();
        const unsigned int code = command.data.range(95,64);
        if(code == csh::EXIT){
            break;
        }
        if(code == csh::SEND_LEN || code == csh::SEND_LEN_BCP){
            sendLength_wrapper(output, input, commands);
        }
    }
}
