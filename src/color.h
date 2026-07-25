#ifndef COLOR_H
#define COLOR_H

#include "fpga_solver.h"
#include "hls_burst_maxi.h"
#include "ap_utils.h"
#include "data_structures.h"

// The stream decoupling below is a hardware-scheduling fix and is only used in
// synthesis. Under C simulation the dataflow processes run sequentially, so a
// blocking response read would hit an empty stream before the reader ever runs;
// there colorStream just loads DDR directly (functionally identical).
#if defined(OCC_DDR_TIER) && defined(FPGA_HW)
#define OCC_DDR_STREAMED 1
#endif

#if defined(OCC_DDR_STREAMED)
// Request/response protocol decoupling the BCP walk from DDR occurrence pages.
// A direct m_axi load inside the II=1 COLOR_STREAM loop forces HLS to schedule
// every iteration for the worst-case DRAM latency (measured: II 1 -> 24, i.e. a
// 24x BCP slowdown even for instances that never touch DDR). An hls::stream
// access instead stalls dynamically, so URAM-only walks keep II=1 and only real
// DDR page crossings pay. occDdrPageReader runs as a sibling process in the
// same dataflow region and exits on OCC_DDR_REQ_SENTINEL.
#define OCC_DDR_REQ_SENTINEL 0xFFFFFFFFu
void occDdrPageReader(hls::stream<unsigned int>& occReq, hls::stream<ap_uint<512>>& occResp,
    const ap_int<512>* litStoreDDR);
#endif

void colorStream(hls::stream<colorValue>* toStateUpdater,
    hls::stream<colorAssignment>& toColorStream, hls::stream<bool>* stopSending,
    const ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD], const ap_int<512>* litStoreDDR,
#if defined(OCC_DDR_STREAMED)
    hls::stream<unsigned int>& occReq, hls::stream<ap_uint<512>>& occResp,
#endif
    const unsigned int LITERAL_PAGE_SIZE,
    lit* literalCommit, const unsigned int type, ap_uint<64>* litStoreAccessStats);

#endif