#include <cstdlib>
#include <iostream>

#include "manage.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ap_axiu<32,0,0,0> word(unsigned int value){
    ap_axiu<32,0,0,0> packet;
    packet.data = value;
    return packet;
}

} // namespace

int main(){
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    static literalMetaData metadata[_FPGA_MAX_LITERALS]{};
    mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_> freePages;
    freePages.reset(_FPGA_MAX_LITERAL_ELEMENTS,
        _FPGA_MAX_LITERAL_ELEMENTS, 16);

    hls::stream<ap_axiu<96,0,0,0>> updates;
    hls::stream<ap_axiu<32,0,0,0>> deletedClauses;
    hls::stream<ap_axiu<32,0,0,0>> locations;

    deletedClauses.write(word(1));
    deletedClauses.write(word(4));
    deletedClauses.write(word(0));
    deleteTransposedClauses(literalStore, metadata, freePages, 16,
        updates, deletedClauses, locations);
#ifdef FPGA_HW
    require((int)updates.read().data.range(95,64) == csh::EXIT,
        "a zero-length deletion record must close explicitly");
#endif
    require(updates.empty() && deletedClauses.empty(),
        "a zero-length deletion record must not wait for literal updates");

    deletedClauses.write(word(1));
    deletedClauses.write(word(4));
    deletedClauses.write(word(2));
    deletedClauses.write(word(0));
    deletedClauses.write(word(1));
    locations.write(word(0));
    locations.write(word(UINT_MAX));

    deleteTransposedClauses(literalStore, metadata, freePages, 16,
        updates, deletedClauses, locations);

    require(deletedClauses.empty() && locations.empty(),
        "invalid deletion records must still be fully consumed");
    require(updates.read().data.range(31,0) == 5,
        "a bad literal must produce a same-clause placeholder update");
    require(updates.read().data.range(31,0) == 5,
        "a bad mapped address must produce a placeholder update");
#ifdef FPGA_HW
    require((int)updates.read().data.range(95,64) == csh::EXIT,
        "each hardware deletion record must retain its end marker");
#endif
    require(updates.empty(),
        "invalid deletion records must produce exactly one update per literal");
    require(freePages.empty(),
        "invalid deletion records must not recycle unverified pages");

    LMD_NUM_ELE(metadata[0].compactlmd, 0) = 1;
    LMD_LATEST_PAGE(metadata[0].compactlmd, 0) = 0;
    LMD_FREE_SPACE(metadata[0].compactlmd, 0) = 13;
    literalStore[0] = 0;
    deletedClauses.write(word(1));
    deletedClauses.write(word(4));
    deletedClauses.write(word(1));
    deletedClauses.write(word(1));
    locations.write(word(0));
    deleteTransposedClauses(literalStore, metadata, freePages, 16,
        updates, deletedClauses, locations);
    require(updates.read().data.range(31,0) == 5,
        "an invalid moved clause ID must produce a same-clause placeholder");
#ifdef FPGA_HW
    require((int)updates.read().data.range(95,64) == csh::EXIT,
        "a rejected move must still close its deletion record");
#endif
    require(LMD_NUM_ELE(metadata[0].compactlmd, 0) == 1,
        "a rejected move must not change literal metadata");
    require(freePages.empty(),
        "a rejected move must not recycle its latest page");

    mmuStream<unsigned int,_MAX_PAGES_LIT_STORE_> allocationPages;
    allocationPages.reset(0, 32, 16);
    hls::stream<lit> newPages;
    newPages.write(0);
    const unsigned int pagesBefore = allocationPages.size();
    int allocationError = 0;
    allocatePage(newPages, allocationPages, metadata, literalStore,
        allocationError, 16);
    require(allocationError == -5 && newPages.empty(),
        "an invalid literal page request must be drained and rejected");
    require(allocationPages.size() == pagesBefore,
        "an invalid literal must not consume a free page");

    std::cout << "MANAGE_DELETE_PROTOCOL_TEST_PASS\n";
    return 0;
}
