#ifndef LIT_STORE_FORMAT_H
#define LIT_STORE_FORMAT_H

// ============================================================================
// litStore occurrence-list physical format (A1: packed occurrence entries)
// ============================================================================
//
// litStore is the on-chip occurrence table read on every BCP step
// (color.cpp::colorStream) and written by clause learning / deletion. It is a
// paged singly/doubly-linked structure. Each page is exactly one 512-bit word:
//
//     litStore[wordIndex] : ap_uint<512>
//
// A page holds LIT_SLOTS_PER_WORD fixed-width slots. The two HIGHEST slots are
// reserved for the page's link pointers; the rest hold clause IDs (occurrence
// entries, in element/"address" units where one element == one slot):
//
//     slot 0 .. LIT_PAGE_USABLE-1   : occurrence clause IDs (0 == empty)
//     slot LIT_SLOTS_PER_WORD-2      : BACK  pointer (element addr of prev page)
//     slot LIT_SLOTS_PER_WORD-1      : NEXT  pointer (element addr of next page)
//
// All litStore addresses ("addressStart", "latestPage", page pointers, the
// freeLitPageAddresses arena) are ELEMENT addresses: element e lives in
// litStore[e / LIT_SLOTS_PER_WORD] at slot (e % LIT_SLOTS_PER_WORD). A page
// starts at an element address that is a multiple of LIT_SLOTS_PER_WORD, which
// is exactly the runtime LITERAL_PAGE_SIZE (host must configure
// LITERAL_PAGE_SIZE == LIT_SLOTS_PER_WORD).
//
// ---------------------------------------------------------------------------
// Two build modes, selected by LIT_STORE_PACK:
//
//   unpacked (default) : 32-bit slots, 16 slots/word, 14 usable/page.
//                        Bit-identical to the legacy hard-coded layout
//                        (next = bits[511:480]=slot15, back = [479:448]=slot14).
//                        Use this to prove the accessor refactor is a no-op.
//
//   packed  (-DLIT_STORE_PACK) : 21-bit slots, 24 slots/word, 22 usable/page.
//                        Same 512-bit word (same URAM), but 22 vs 14 usable
//                        occurrence entries per page => ~1.57x occurrence
//                        capacity for the same on-chip URAM footprint. 21 bits
//                        addresses up to 2^21 = 2,097,152 clause IDs (current
//                        _FPGA_MAX_CLAUSES is 131,072, so ample head-room).
//
// Invariants both modes MUST satisfy:
//   * LIT_SLOTS_PER_WORD is a multiple of the BCP read chunk
//     (READ_CHUNK_SIZE == _FPGA_CLS_STATES_PARTITION == 8) so colorStream's
//     8-slot chunk read never straddles a 512-bit word. 16 = 2*8, 24 = 3*8.
//   * LIT_SLOT_BITS * LIT_SLOTS_PER_WORD <= 512.
//   * Pointer slots are wide enough for the largest element address
//     (_FPGA_MAX_LITERAL_ELEMENTS). 21 bits covers up to 2,097,151.
// ============================================================================

#include "ap_int.h"

// ---- Packing toggle --------------------------------------------------------
// Define LIT_STORE_PACK to build the 21-bit / 24-slot packed occurrence layout
// (+50% occurrence entries per 512-bit page => same litStore URAM, higher
// capacity). Comment it out to build the legacy 32-bit / 16-slot layout, which
// is bit-identical to the pre-A1 solver. Every host and kernel translation unit
// includes this header, so the choice is applied uniformly across the design.
#define LIT_STORE_PACK
// ----------------------------------------------------------------------------

#if defined(LIT_STORE_PACK)
    #define LIT_SLOT_BITS       21
    #define LIT_SLOTS_PER_WORD   24
#else
    #define LIT_SLOT_BITS       32
    #define LIT_SLOTS_PER_WORD   16
#endif

// Number of link pointers reserved at the top of every page (NEXT + BACK).
#define LIT_PAGE_RESERVED   2
// Usable occurrence slots per page.
#define LIT_PAGE_USABLE     (LIT_SLOTS_PER_WORD - LIT_PAGE_RESERVED)
// Highest bit of the usable (non-pointer) region of a page word. Zeroing
// [LIT_USABLE_HI:0] clears every occurrence slot while preserving the two link
// pointers. Unpacked: 32*14-1 == 447 (legacy value).
#define LIT_USABLE_HI       (LIT_SLOT_BITS * LIT_PAGE_USABLE - 1)

// Slot bit range helpers (slot i occupies [LIT_SLOT_BITS*i +: LIT_SLOT_BITS]).
#define LIT_SLOT_LO(i)      (LIT_SLOT_BITS * (i))
#define LIT_SLOT_HI(i)      (LIT_SLOT_BITS * (i) + LIT_SLOT_BITS - 1)

// Read/write occurrence slot i (0-based) of a 512-bit page word. Returns/takes
// an unsigned field LIT_SLOT_BITS wide; callers zero-extend to 32-bit cls where
// the surrounding streams still use 32-bit lanes (e.g. colorValue.clsID).
#define LIT_SLOT(word, i)   ((word).range(LIT_SLOT_HI(i), LIT_SLOT_LO(i)))

// Link-pointer slots (the two highest slots of the page). In unpacked mode
// these resolve to bits[511:480] (NEXT) and bits[479:448] (BACK) exactly as the
// legacy code hard-coded them.
#define LIT_NEXT_SLOT       (LIT_SLOTS_PER_WORD - 1)
#define LIT_BACK_SLOT       (LIT_SLOTS_PER_WORD - 2)
#define LIT_NEXT_PTR(word)  LIT_SLOT((word), LIT_NEXT_SLOT)
#define LIT_BACK_PTR(word)  LIT_SLOT((word), LIT_BACK_SLOT)

#endif // LIT_STORE_FORMAT_H
