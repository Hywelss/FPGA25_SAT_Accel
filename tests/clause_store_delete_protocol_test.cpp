#include "data_structures.h"

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

#include <cassert>

void deleteClauses(
    mmuStream<cls, _FPGA_MAX_CLAUSES>& freeClsID,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClsPageAddresses,
    cls removeID,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    unsigned int clausePageSize);

static clauseMetaData metadata[_FPGA_MAX_CLAUSES];
static ap_uint<128> clauseStore[_FPGA_MAX_LITERAL_ELEMENTS / 4];
static ap_uint<1> compactLayout[_FPGA_MAX_CLAUSES];

static ap_axiu<96, 0, 0, 0> update(unsigned int clauseID,
                                   unsigned int swapAddress,
                                   unsigned int replaceAddress) {
    ap_axiu<96, 0, 0, 0> packet;
    packet.data = 0;
    packet.data.range(31, 0) = clauseID;
    packet.data.range(63, 32) = swapAddress;
    packet.data.range(95, 64) = replaceAddress;
    return packet;
}

static void expectRecord(hls::stream<ap_axiu<32, 0, 0, 0>>& output,
                         unsigned int id, unsigned int length) {
    assert(output.read().data == id);
    assert(output.read().data == length);
}

static void testMaximumLengthRecord() {
    constexpr unsigned int kClauseID = 11;
    constexpr unsigned int kPageSize = 8;
    constexpr unsigned int kPayloadPerPage = kPageSize - 1;

    mmuStream<cls, _FPGA_MAX_CLAUSES> freeIDs;
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_> freePages;
    freeIDs.reset(_FPGA_MAX_CLAUSES, _FPGA_MAX_CLAUSES, 1);
    freePages.reset(_FPGA_MAX_LITERAL_ELEMENTS, _FPGA_MAX_LITERAL_ELEMENTS,
                    kPageSize);

    hls::stream<ap_axiu<96, 0, 0, 0>> updates;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;
    hls::stream<ap_axiu<64, 0, 0, 0>> locations;

    metadata[kClauseID] = {.addressStart = 0,
                           .numElements = _FPGA_MAX_LEARN_ELE};
    compactLayout[kClauseID] = 0;
    for(unsigned int literalIndex = 0;
            literalIndex < _FPGA_MAX_LEARN_ELE; literalIndex++) {
        const unsigned int page = literalIndex / kPayloadPerPage;
        const unsigned int offset = literalIndex % kPayloadPerPage;
        const unsigned int address = page * kPageSize + offset;
        clauseStore[address / 4].range(32 * (address % 4) + 31,
                                              32 * (address % 4)) =
            literalIndex + 1;
        if(offset == kPayloadPerPage - 1 &&
                literalIndex + 1 < _FPGA_MAX_LEARN_ELE) {
            const unsigned int linkAddress = page * kPageSize + kPageSize - 1;
            clauseStore[linkAddress / 4].range(
                32 * (linkAddress % 4) + 31,
                32 * (linkAddress % 4)) = (page + 1) * kPageSize;
        }
        updates.write(update(kClauseID + 1, address, address));
    }
    deleteClauses(freeIDs, freePages, kClauseID, updates, output, locations,
                  metadata, clauseStore, compactLayout, kPageSize);

    expectRecord(output, kClauseID, _FPGA_MAX_LEARN_ELE);
    for(unsigned int literal = 1; literal <= _FPGA_MAX_LEARN_ELE; literal++) {
        assert(output.read().data == literal);
    }
    assert(output.empty());
    assert(updates.empty());
    assert(metadata[kClauseID].numElements == 0);

    assert(locations.read().data.range(31, 0) == lh::SEND);
    for(unsigned int literalIndex = 0;
            literalIndex < _FPGA_MAX_LEARN_ELE; literalIndex++) {
        const unsigned int page = literalIndex / kPayloadPerPage;
        const unsigned int offset = literalIndex % kPayloadPerPage;
        assert(locations.read().data == page * kPageSize + offset);
    }
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.read().data.range(31, 0) == lh::UPDATE);
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.empty());
}

static void testInvalidClauseRecordsStayFramed() {
    constexpr unsigned int kBadStartID = 21;
    constexpr unsigned int kBadPageID = 22;
    constexpr unsigned int kBadLiteralID = 23;

    mmuStream<cls, _FPGA_MAX_CLAUSES> freeIDs;
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_> freePages;
    freeIDs.reset(_FPGA_MAX_CLAUSES, _FPGA_MAX_CLAUSES, 1);
    freePages.reset(_FPGA_MAX_LITERAL_ELEMENTS, _FPGA_MAX_LITERAL_ELEMENTS, 8);

    hls::stream<ap_axiu<96, 0, 0, 0>> updates;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;
    hls::stream<ap_axiu<64, 0, 0, 0>> locations;

    metadata[kBadStartID] = {.addressStart = _FPGA_MAX_LITERAL_ELEMENTS,
                             .numElements = 2};
    deleteClauses(freeIDs, freePages, kBadStartID, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);
    expectRecord(output, kBadStartID, 0);

    metadata[kBadPageID] = {.addressStart = 0, .numElements = 8};
    compactLayout[kBadPageID] = 0;
    for(unsigned int i = 0; i < 7; i++) {
        clauseStore[i / 4].range(32 * (i % 4) + 31, 32 * (i % 4)) = i + 1;
    }
    clauseStore[1].range(127, 96) = _FPGA_MAX_LITERAL_ELEMENTS;
    deleteClauses(freeIDs, freePages, kBadPageID, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);
    expectRecord(output, kBadPageID, 0);

    metadata[kBadLiteralID] = {.addressStart = 32, .numElements = 1};
    clauseStore[8].range(31, 0) = 0;
    deleteClauses(freeIDs, freePages, kBadLiteralID, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);
    expectRecord(output, kBadLiteralID, 0);

    assert(updates.empty());
    assert(output.empty());
    assert(locations.empty());
    assert(freeIDs.empty());
    assert(freePages.empty());
    assert(metadata[kBadStartID].numElements == 2);
    assert(metadata[kBadPageID].numElements == 8);
    assert(metadata[kBadLiteralID].numElements == 1);
}

int main() {
    mmuStream<cls, _FPGA_MAX_CLAUSES> freeIDs;
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_> freePages;
    freeIDs.reset(_FPGA_MAX_CLAUSES, _FPGA_MAX_CLAUSES, 1);
    freePages.reset(_FPGA_MAX_LITERAL_ELEMENTS, _FPGA_MAX_LITERAL_ELEMENTS, 8);

    hls::stream<ap_axiu<96, 0, 0, 0>> updates;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;
    hls::stream<ap_axiu<64, 0, 0, 0>> locations;

    metadata[7] = {.addressStart = 16, .numElements = 2};
    clauseStore[4].range(31, 0) = 3;
    clauseStore[4].range(63, 32) = -4;
    updates.write(update(8, 40, 16));
    updates.write(update(8, 41, 17));
    deleteClauses(freeIDs, freePages, 7, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);

    expectRecord(output, 7, 2);
    assert((int)output.read().data == 3);
    assert((int)output.read().data == -4);
    assert(output.empty());
    assert(updates.empty());
    assert(metadata[7].numElements == 0);
    assert(freeIDs.read() == 7);
    assert(freePages.read() == 16);

    assert(locations.read().data.range(31, 0) == lh::SEND);
    assert(locations.read().data == 16);
    assert(locations.read().data == 17);
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.read().data.range(31, 0) == lh::UPDATE);
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.empty());

    // A repeated ID and an out-of-range ID each retain the two-word response
    // frame but must not consume updates, touch metadata, or recycle storage.
    deleteClauses(freeIDs, freePages, 7, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);
    expectRecord(output, 7, 0);

    deleteClauses(freeIDs, freePages, _FPGA_MAX_CLAUSES, updates, output, locations,
                  metadata, clauseStore, compactLayout, 8);
    expectRecord(output, _FPGA_MAX_CLAUSES, 0);

    assert(output.empty());
    assert(updates.empty());
    assert(locations.empty());
    assert(freeIDs.empty());
    assert(freePages.empty());

    testMaximumLengthRecord();
    testInvalidClauseRecordsStayFramed();
}
