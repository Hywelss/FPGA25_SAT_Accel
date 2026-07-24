#ifndef FPGA_SOLVER_H
#define FPGA_SOLVER_H

#include <limits>

#include <iostream>
#include <string.h>

#include <ap_int.h>
#include <hls_stream.h>
#include <math.h>
#include <utils/x_hls_utils.h>
#include <ap_axi_sdata.h>

// litStore occurrence-page layout (LIT_SLOTS_PER_WORD etc). Included here,
// before _MAX_PAGES_LIT_STORE_ below, so the page-count derivation can use it.
#include "lit_store_format.h"


//REQUIRES FPGA BITSTREAM RECOMPILE
#define _FPGA_MAX_LITERALS (8192*4)
#define _FPGA_MAX_CLAUSES 131072
#define _FPGA_MAX_TREE_HEIGHT 32
#define _FPGA_MAX_LBD_BUCKETS 10
#define _FPGA_CLS_STATES_PARTITION 8
#define _FPGA_CLS_STATES_SELECT_BITS 4
#define _FPGA_DISC_LMD_DEP_DIST 5
#define _FPGA_CLS_DEP_DIST 5
#define _FPGA_RESOLVE_DEP_DIST 3
#define _FPGA_PARALLEL_MINIMIZE 2
#if defined(FPGA_VCK5000)
// ScaleSAT phase 1: VCK5000 has enough BRAM headroom to keep a larger
// conflict-analysis scratchpad.  The original 1024-entry limit rejected
// otherwise valid learned clauses before memory pressure management ran.
#define _FPGA_MAX_LEARN_ELE 2048
#define _FPGA_MAX_LEARN_ELE_BITS 11
#else
#define _FPGA_MAX_LEARN_ELE 1024
#define _FPGA_MAX_LEARN_ELE_BITS 10
#endif

// A pressure-triggered restart is requested while there is still enough room
// for one worst-case learned clause.  This makes garbage collection proactive
// instead of waiting for the page allocator to fail.
#define _FPGA_GC_HEADROOM_MULTIPLIER 2

//REQUIRES FPGA BITSTREAM RECOMPILE
// litStore capacity is expressed as (word_count * LIT_SLOTS_PER_WORD) so the
// number of 512-bit litStore words (=> URAM footprint) stays fixed while
// LIT_STORE_PACK raises the occupancy per word. Unpacked (16 slots) reproduces
// the legacy 524288/1048576 values exactly; packed (24 slots) gives +50%
// occurrence capacity in the same litStore URAM.
#if defined(FPGA_VCK5000)
// VCK5000 has 463 URAMs versus the 960 URAMs on the original U55C target.
// Halving the literal/clauses store capacity keeps the complete design within
// the VCK5000 URAM budget while preserving the solver architecture.
#define _FPGA_MAX_LITERAL_ELEMENTS (32768 * LIT_SLOTS_PER_WORD)
// This is the learned/hot-clause URAM tier. Original clauses remain in DDR and
// use the larger host/device capacity below.
#define _FPGA_MAX_CLAUSE_ELEMENTS (128*4096)
#define _HOST_MAX_CLAUSE_ELEMENTS (1024*4096)
#else
#define _FPGA_MAX_LITERAL_ELEMENTS (65536 * LIT_SLOTS_PER_WORD)
#define _FPGA_MAX_CLAUSE_ELEMENTS (256*4096)
#define _HOST_MAX_CLAUSE_ELEMENTS _FPGA_MAX_CLAUSE_ELEMENTS
#endif

// ---- Occurrence DDR-spill tier (OCC_DDR_TIER) ------------------------------
// The occurrence (litStore) page allocator hands out URAM page addresses first
// (element addr < _FPGA_MAX_LITERAL_ELEMENTS) and, once URAM is exhausted, DDR
// overflow page addresses. A page whose address is >= _FPGA_MAX_LITERAL_ELEMENTS
// lives in a device-DDR arena at offset (addr - _FPGA_MAX_LITERAL_ELEMENTS).
// Instances whose occurrence lists fit in URAM never touch DDR (zero
// regression); only the overflow spills. Define OCC_DDR_TIER to enable; with it
// off _FPGA_OCC_TOTAL_ELEMENTS == _FPGA_MAX_LITERAL_ELEMENTS, i.e. baseline.
//#define OCC_DDR_TIER
#if defined(OCC_DDR_TIER)
  #if defined(FPGA_VCK5000)
    #define _FPGA_OCC_DDR_ELEMENTS (32768 * LIT_SLOTS_PER_WORD)
  #else
    #define _FPGA_OCC_DDR_ELEMENTS (65536 * LIT_SLOTS_PER_WORD)
  #endif
#else
  #define _FPGA_OCC_DDR_ELEMENTS 0
#endif
#define _FPGA_OCC_TOTAL_ELEMENTS (_FPGA_MAX_LITERAL_ELEMENTS + _FPGA_OCC_DDR_ELEMENTS)

// litStore URAM array size stays _FPGA_MAX_LITERAL_ELEMENTS; the allocator and
// host image span the full URAM+DDR address space.
#define _HOST_MAX_LITERAL_ELEMENTS _FPGA_OCC_TOTAL_ELEMENTS

//FOR THE CONFIGURATION.JSON FILE:

//MUST START AT 16 FOR ALIGNMENT OF 512 BITS
//MUST BE MULTIPLE OF 16
//_HOST_LITERAL_PAGE_SIZE 16

//FOR STORING CLAUSES, MUST START AT 4 FOR ALIGNMENT OF 128 BYTES
//MUST BE MULTIPLE OF 4
// _HOST_CLAUSE_PAGE_SIZE 4

//DECIDE AS POSITIVE OR NEGATIVE
// _HOST_POSITIVE_LIT_PHASE_VAL false

//MULTIPLIER USED TO DECAY VSIDS
// _HOST_DECAY_FACTOR 0.95

//PRUNE PERCENTAGE AFTER RESET
// _HOST_PRUNE_PERCENTRAGE 0.1

//RESET MULTIPLIER
// _HOST_RESET_MULTIPLIER 100

const int MAX_STREAM_DEPTH=(_FPGA_MAX_LITERALS/4);
// URAM-resident occurrence pages.
const unsigned int _MAX_PAGES_LIT_STORE_=_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD;
// URAM + DDR-overflow occurrence pages. The free-page allocator recycle buffer
// and page address space span this; == _MAX_PAGES_LIT_STORE_ when OCC_DDR_TIER
// is off.
const unsigned int _MAX_PAGES_LIT_STORE_TOTAL_=_FPGA_OCC_TOTAL_ELEMENTS/LIT_SLOTS_PER_WORD;
const unsigned int _MAX_PAGES_CLS_STORE_=_FPGA_MAX_CLAUSE_ELEMENTS/4;

extern int spentRemoving;
extern int overhead;
extern int splitResidualCnt;
extern unsigned int checkCnt;

void sendTime(hls::stream<ap_axiu<64,0,0,0>>& timerValueStream, hls::stream<ap_axiu<1,0,0,0>>& conditionStream, 
    const unsigned int code, volatile uint64_t* store);
#endif
