#ifndef COLOR_H
#define COLOR_H

#include "fpga_solver.h"
#include "hls_burst_maxi.h"
#include "ap_utils.h"
#include "data_structures.h"

// A cache miss is served by loading from DDR inline, in colorStream itself.
// An earlier version delegated the load to a sibling dataflow process over a
// request/response stream pair, which made colorStream and that process feed
// each other -- a cycle, and Vitis HLS dataflow regions must be feed-forward.
// It built, then deadlocked on the board with every CU stuck in START. It had
// also never been simulated, because it was compiled in only for synthesis, so
// software emulation exercised a different path than the hardware did. One path
// for both is the point here: what sw_emu runs is what the board runs.
void colorStream(hls::stream<colorValue>* toStateUpdater,
    hls::stream<colorAssignment>& toColorStream, hls::stream<bool>* stopSending,
    ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD], ap_int<512>* litStoreDDR, occTagEntry* occCacheTag,
    const unsigned int LITERAL_PAGE_SIZE,
    lit* literalCommit, const unsigned int type, ap_uint<64>* litStoreAccessStats);

#endif