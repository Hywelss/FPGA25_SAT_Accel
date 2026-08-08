#include <cassert>
#include <iostream>

#include "manage.h"

void manageDeleteCosim(
    hls::stream<ap_axiu<96,0,0,0>>& updates,
    hls::stream<ap_axiu<32,0,0,0>>& deletedClauses,
    hls::stream<ap_axiu<32,0,0,0>>& locations,
    ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    literalMetaData metadata[_FPGA_MAX_LITERALS],
    unsigned int& freePageCount);

static ap_axiu<32,0,0,0> word(unsigned int value){
    ap_axiu<32,0,0,0> packet;
    packet.data = value;
    return packet;
}

int main(){
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    hls::stream<ap_axiu<96,0,0,0>> updates;
    hls::stream<ap_axiu<32,0,0,0>> deletedClauses;
    hls::stream<ap_axiu<32,0,0,0>> locations;

    deletedClauses.write(word(1));
    deletedClauses.write(word(4));
    deletedClauses.write(word(2));
    deletedClauses.write(word(0));
    deletedClauses.write(word(1));
    locations.write(word(0));
    locations.write(word(UINT_MAX));

    unsigned int freePageCount = UINT_MAX;
    manageDeleteCosim(updates, deletedClauses, locations,
        literalStore, metadata, freePageCount);

    assert(deletedClauses.empty() && locations.empty());
    assert(updates.read().data.range(31,0) == 5);
    assert(updates.read().data.range(31,0) == 5);
    assert((int)updates.read().data.range(95,64) == csh::EXIT);
    assert(updates.empty());
    assert(freePageCount == 0);
    std::cout << "MANAGE_DELETE_COSIM_PASS\n";
    return 0;
}
