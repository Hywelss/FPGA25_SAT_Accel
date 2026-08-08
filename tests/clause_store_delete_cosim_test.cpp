#include "data_structures.h"

#include <cassert>

void clauseStoreDeleteCosim(
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream1,
    hls::stream<ap_axiu<96,0,0,0>>& clauseStoreInputStream2,
    hls::stream<ap_axiu<32,0,0,0>>& clauseStoreOutputStream1,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    clauseMetaData mCmd[_FPGA_MAX_CLAUSES],
    ap_uint<128> mClsStore[_FPGA_MAX_LITERAL_ELEMENTS/4],
    const ap_uint<1> compactClauseLayout[_FPGA_MAX_CLAUSES],
    unsigned int removeTotal, unsigned int clausePageSize);

namespace {
constexpr unsigned int kClauseID = 11;
constexpr unsigned int kBadStartID = 21;
constexpr unsigned int kBadPageID = 22;
constexpr unsigned int kBadLiteralID = 23;
constexpr unsigned int kPageSize = 8;
constexpr unsigned int kPayloadPerPage = kPageSize - 1;

static clauseMetaData metadata[_FPGA_MAX_CLAUSES];
static ap_uint<128> clauseStore[_FPGA_MAX_LITERAL_ELEMENTS/4];
static ap_uint<1> compactLayout[_FPGA_MAX_CLAUSES];

ap_axiu<96,0,0,0> update(unsigned int literalIndex) {
    const unsigned int page = literalIndex/kPayloadPerPage;
    const unsigned int offset = literalIndex%kPayloadPerPage;
    const unsigned int address = page*kPageSize+offset;
    ap_axiu<96,0,0,0> packet;
    packet.data = 0;
    packet.data.range(31,0) = kClauseID+1;
    packet.data.range(63,32) = address;
    packet.data.range(95,64) = address;
    return packet;
}
} // namespace

int main() {
    metadata[kBadStartID] = {.addressStart=_FPGA_MAX_LITERAL_ELEMENTS,
                             .numElements=2};
    metadata[kBadPageID] = {.addressStart=2048, .numElements=8};
    for(unsigned int i = 0; i < 7; i++) {
        const unsigned int address = 2048+i;
        clauseStore[address/4].range(32*(address%4)+31,
                                     32*(address%4)) = i+1;
    }
    clauseStore[(2048+kPageSize-1)/4].range(127,96) =
        _FPGA_MAX_LITERAL_ELEMENTS;
    metadata[kBadLiteralID] = {.addressStart=4096, .numElements=1};
    clauseStore[4096/4].range(31,0) = 0;

    metadata[kClauseID] = {.addressStart=0,
                           .numElements=_FPGA_MAX_LEARN_ELE};
    for(unsigned int literalIndex = 0;
            literalIndex < _FPGA_MAX_LEARN_ELE; literalIndex++) {
        const unsigned int page = literalIndex/kPayloadPerPage;
        const unsigned int offset = literalIndex%kPayloadPerPage;
        const unsigned int address = page*kPageSize+offset;
        clauseStore[address/4].range(32*(address%4)+31,
                                     32*(address%4)) = literalIndex+1;
        if(offset == kPayloadPerPage-1 &&
                literalIndex+1 < _FPGA_MAX_LEARN_ELE) {
            const unsigned int linkAddress = page*kPageSize+kPageSize-1;
            clauseStore[linkAddress/4].range(32*(linkAddress%4)+31,
                32*(linkAddress%4)) = (page+1)*kPageSize;
        }
    }

    hls::stream<ap_axiu<96,0,0,0>> input1;
    hls::stream<ap_axiu<96,0,0,0>> input2;
    hls::stream<ap_axiu<32,0,0,0>> output;
    hls::stream<ap_axiu<64,0,0,0>> locations;
    ap_axiu<96,0,0,0> id;
    id.data = 0;
    id.data.range(31,0) = kBadStartID;
    input2.write(id);
    id.data.range(31,0) = kBadPageID;
    input2.write(id);
    id.data.range(31,0) = kBadLiteralID;
    input2.write(id);
    id.data.range(31,0) = kClauseID;
    input2.write(id);

    ap_axiu<96,0,0,0> end;
    end.data = 0;
    end.data.range(95,64) = csh::EXIT;
    input1.write(end);
    input1.write(end);
    input1.write(end);
    for(unsigned int i = 0; i < _FPGA_MAX_LEARN_ELE; i++) {
        input1.write(update(i));
    }
    input1.write(end);

    clauseStoreDeleteCosim(input1, input2, output, locations,
        metadata, clauseStore, compactLayout, 4, kPageSize);

    assert(input1.empty() && input2.empty());
    assert(output.read().data == kBadStartID);
    assert(output.read().data == 0);
    assert(output.read().data == kBadPageID);
    assert(output.read().data == 0);
    assert(output.read().data == kBadLiteralID);
    assert(output.read().data == 0);
    assert(output.read().data == kClauseID);
    assert(output.read().data == _FPGA_MAX_LEARN_ELE);
    for(unsigned int literal = 1; literal <= _FPGA_MAX_LEARN_ELE; literal++) {
        assert(output.read().data == literal);
    }
    assert(output.empty());

    assert(locations.read().data.range(31,0) == lh::SEND);
    for(unsigned int literalIndex = 0;
            literalIndex < _FPGA_MAX_LEARN_ELE; literalIndex++) {
        const unsigned int page = literalIndex/kPayloadPerPage;
        const unsigned int offset = literalIndex%kPayloadPerPage;
        assert(locations.read().data == page*kPageSize+offset);
    }
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.read().data.range(31,0) == lh::UPDATE);
    assert(locations.read().data == (ap_uint<64>)lh::EXIT);
    assert(locations.empty());
    assert(metadata[kClauseID].numElements == 0);
}
