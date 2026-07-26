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
#define _FPGA_MAX_CLAUSE_ELEMENTS (128*4096)
#define _HOST_MAX_CLAUSE_ELEMENTS (1024*4096)
#else
#define _FPGA_MAX_CLAUSE_ELEMENTS (256*4096)
#define _HOST_MAX_CLAUSE_ELEMENTS _FPGA_MAX_CLAUSE_ELEMENTS
#endif

// ---- Occurrence hot/cold tier sizing ---------------------------------------
// Two independent numbers, both counted in 512-bit page words:
//   _OCC_CACHE_WORDS_ : the on-chip cache (URAM footprint). Must be a power of
//                       two, since it indexes the direct-mapped cache.
//   _OCC_TOTAL_WORDS_ : total occurrence capacity, held in DDR.
// Keeping them separate is what lets capacity grow without spending URAM, and
// lets the cache be shrunk for testing without shrinking capacity.
#if defined(FPGA_VCK5000)
  #define _OCC_CACHE_WORDS_ 32768
  #define _OCC_TOTAL_WORDS_ 65536
#else
  #define _OCC_CACHE_WORDS_ 65536
  #define _OCC_TOTAL_WORDS_ 131072
#endif

// Define OCC_DDR_TIER for the tiered design; without it capacity collapses back
// onto the cache size, i.e. the original all-on-chip solver.
#define OCC_DDR_TIER
#if !defined(OCC_DDR_TIER)
  #undef _OCC_TOTAL_WORDS_
  #define _OCC_TOTAL_WORDS_ _OCC_CACHE_WORDS_
#endif

// Test-only: shrink the cache to a handful of lines so the small regression
// instances overflow it immediately and actually execute the tiered paths --
// misses, evictions, write-through, page walks that cross tiers and delete-time
// compaction of DDR-resident pages. Capacity is untouched, so the same 20 cases
// must still produce the same answers. Enable with -DOCC_CACHE_STRESS for
// software emulation only; it is far too small to be useful in hardware.
#if defined(OCC_CACHE_STRESS)
  #undef _OCC_CACHE_WORDS_
  #define _OCC_CACHE_WORDS_ 64
#endif

#define _FPGA_MAX_LITERAL_ELEMENTS (_OCC_CACHE_WORDS_ * LIT_SLOTS_PER_WORD)
#define _FPGA_OCC_TOTAL_ELEMENTS   (_OCC_TOTAL_WORDS_ * LIT_SLOTS_PER_WORD)

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

// This was briefly raised to _FPGA_MAX_LITERALS on the theory that the board
// hang came from a full propagation FIFO, after C simulation reported a stream
// reaching 10,450 entries against this depth. That reading was wrong: C
// simulation does not run dataflow processes concurrently, so a producer fills
// its whole output before the consumer starts, and the depth it reports says
// nothing about what the hardware needs. The real cause was elsewhere, and the
// four-fold increase cost about 12 MHz in routed frequency, so it is reverted.
const int MAX_STREAM_DEPTH=(_FPGA_MAX_LITERALS/4);
// URAM-resident occurrence pages.
const unsigned int _MAX_PAGES_LIT_STORE_=_FPGA_MAX_LITERAL_ELEMENTS/LIT_SLOTS_PER_WORD;
// URAM + DDR-overflow occurrence pages. The free-page allocator recycle buffer
// and page address space span this; == _MAX_PAGES_LIT_STORE_ when OCC_DDR_TIER
// is off.
const unsigned int _MAX_PAGES_LIT_STORE_TOTAL_=_FPGA_OCC_TOTAL_ELEMENTS/LIT_SLOTS_PER_WORD;
const unsigned int _MAX_PAGES_CLS_STORE_=_FPGA_MAX_CLAUSE_ELEMENTS/4;

// ---- Occurrence hot/cold tier ----------------------------------------------
// DDR (the reused litStore buffer) holds the authoritative occurrence table;
// the on-chip array is a direct-mapped cache of 512-bit page words. Residency
// therefore follows actual access -- a page that BCP keeps walking stays hot in
// URAM, one that is never touched drifts out -- instead of being decided by the
// accident of allocation order. Element addresses never change, so lmd and the
// back-reference map are untouched by promotion/eviction.
//
// Cache line == one page word, which is exactly the granularity colorStream
// walks; the two 8-slot chunk reads of a 16-slot page make the second access a
// guaranteed hit.
// Same on-chip array size as before (power of two, so the index is a mask).
#define OCC_CACHE_LINES   _MAX_PAGES_LIT_STORE_
#define OCC_TAG_BITS      16
// [dirty][valid][tag]
typedef ap_uint<OCC_TAG_BITS+2> occTagEntry;
#define OCC_TAG_VALID     (OCC_TAG_BITS)
#define OCC_TAG_DIRTY     (OCC_TAG_BITS+1)

static inline unsigned int occLine(unsigned int w){
    #pragma HLS inline
    return w & (OCC_CACHE_LINES-1);
}
static inline occTagEntry occTagOf(unsigned int w){
    #pragma HLS inline
    occTagEntry t = w / OCC_CACHE_LINES;
    t.range(OCC_TAG_VALID,OCC_TAG_VALID) = 1;
    return t;
}
// Which word a resident line holds: tag * OCC_CACHE_LINES + line index, the
// inverse of the split above. Needed to know where to flush a dirty victim.
static inline unsigned int occWordOf(occTagEntry t, unsigned int L){
    #pragma HLS inline
    return ((unsigned int)t.range(OCC_TAG_BITS-1,0)) * OCC_CACHE_LINES + L;
}
// Valid and tag must match; the dirty bit is not part of the identity.
static inline bool occHit(occTagEntry cur, occTagEntry want){
    #pragma HLS inline
    return cur.range(OCC_TAG_VALID,0) == want.range(OCC_TAG_VALID,0);
}
static inline bool occNeedsFlush(occTagEntry cur){
    #pragma HLS inline
    return cur.range(OCC_TAG_VALID,OCC_TAG_VALID) == 1 &&
           cur.range(OCC_TAG_DIRTY,OCC_TAG_DIRTY) == 1;
}

// The cache is write-back. Write-through cost far more than expected: every
// occurrence write took a DDR round trip even when the page was resident, and
// the clause-save phase measured 150x the pre-tier cycle count on an instance
// whose occurrence image fits the cache several times over. Under write-back an
// instance that fits issues essentially no DDR writes at all.
//
// Writes always replace a whole 512-bit word -- every caller reads a word,
// edits one slot and writes the word back -- so a write miss can install the
// line without fetching it first.
static inline ap_uint<512> occReadWord(ap_uint<512>* cacheData, occTagEntry* cacheTag,
    ap_int<512>* ddr, unsigned int w){
    #pragma HLS inline
    const unsigned int L = occLine(w);
    const occTagEntry T = occTagOf(w);
    const occTagEntry cur = cacheTag[L];
    if(occHit(cur,T)){
        return cacheData[L];
    }
    if(occNeedsFlush(cur)){
        ddr[occWordOf(cur,L)] = (ap_int<512>)cacheData[L];
    }
    const ap_uint<512> v = (ap_uint<512>)ddr[w];
    cacheData[L] = v;
    cacheTag[L] = T;
    return v;
}
static inline void occWriteWord(ap_uint<512>* cacheData, occTagEntry* cacheTag,
    ap_int<512>* ddr, unsigned int w, ap_uint<512> v){
    #pragma HLS inline
    const unsigned int L = occLine(w);
    occTagEntry T = occTagOf(w);
    const occTagEntry cur = cacheTag[L];
    if(!occHit(cur,T) && occNeedsFlush(cur)){
        ddr[occWordOf(cur,L)] = (ap_int<512>)cacheData[L];
    }
    cacheData[L] = v;
    T.range(OCC_TAG_DIRTY,OCC_TAG_DIRTY) = 1;
    cacheTag[L] = T;
}

extern int spentRemoving;
extern int overhead;
extern int splitResidualCnt;
extern unsigned int checkCnt;

void sendTime(hls::stream<ap_axiu<64,0,0,0>>& timerValueStream, hls::stream<ap_axiu<1,0,0,0>>& conditionStream, 
    const unsigned int code, volatile uint64_t* store);
#endif
