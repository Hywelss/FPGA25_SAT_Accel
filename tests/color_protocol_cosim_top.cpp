#include "color.h"

void colorStreamForward(
    hls::stream<colorValue> toStateUpdater[_FPGA_CLS_STATES_PARTITION/2],
    hls::stream<colorAssignment>& toColorStream,
    hls::stream<bool>& stopSending,
    const ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    lit& literalCommit,
    ap_uint<64> litStoreAccessStats[4]){
    #pragma HLS inline off

    colorStream(toStateUpdater, toColorStream, &stopSending, litStore, 16,
        &literalCommit, 0, litStoreAccessStats);
}
