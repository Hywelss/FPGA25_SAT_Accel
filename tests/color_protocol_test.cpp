#include <cstdlib>
#include <iostream>

#include "color.h"

void colorStreamForward(
    hls::stream<colorValue> toStateUpdater[_FPGA_CLS_STATES_PARTITION/2],
    hls::stream<colorAssignment>& toColorStream,
    hls::stream<bool>& stopSending,
    const ap_uint<512> litStore[_FPGA_MAX_LITERAL_ELEMENTS/16],
    lit& literalCommit,
    ap_uint<64> litStoreAccessStats[4]);

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

colorAssignment assignment(unsigned int address, unsigned int count,
    lit literal){
    return (colorAssignment){.addressStart=address, .numElements=count,
        .depthCount=0, .literal=literal, .eos=false};
}

} // namespace

int main(){
    static ap_uint<512> literalStore[_FPGA_MAX_LITERAL_ELEMENTS/16]{};
    literalStore[0].range(31, 0) = 1;

    const unsigned int pageAddress = 16;
    for(unsigned int i = 0; i < 14; i++){
        literalStore[pageAddress/16].range(32*i+31, 32*i) = i+1;
    }
    literalStore[pageAddress/16].range(511, 480) =
        _FPGA_MAX_LITERAL_ELEMENTS;

    hls::stream<colorAssignment> input;
    hls::stream<bool> stop;
    hls::stream<colorValue> outputs[_FPGA_CLS_STATES_PARTITION/2];
    ap_uint<64> accessStats[4] = {0, 0, 0, 0};
    lit committed = 0;

    input.write(assignment(0, 1, 7));
    input.write(assignment(_FPGA_MAX_LITERAL_ELEMENTS, 8, 8));
    input.write(assignment(pageAddress, 15, 9));
    input.write(assignment(0, _FPGA_MAX_LITERAL_ELEMENTS + 1, 10));
    input.write(assignment(1, 1, 11));
    input.write((colorAssignment){.addressStart=0, .numElements=0,
        .depthCount=0, .literal=0, .eos=true});
    stop.write(false);

    colorStreamForward(outputs, input, stop, literalStore, committed,
        accessStats);

    for(unsigned int lane = 0; lane < _FPGA_CLS_STATES_PARTITION/2; lane++){
        colorValue packet = outputs[lane].read();
        require(packet.clsEos && packet.clsID.range(31, 0) == 1,
            "a valid one-word adjacency list must finish normally");

        packet = outputs[lane].read();
        require(packet.clsEos && packet.clsID == 0,
            "an invalid start address must become an empty completion");

        packet = outputs[lane].read();
        require(!packet.clsEos,
            "the first chunk before a bad page link must be retained");
        for(unsigned int i = 0; i < 8; i++){
            require(packet.clsID.range(32*i+31, 32*i) == i+1,
                "the lower half of a clause page must retain every ID");
        }
        packet = outputs[lane].read();
        require(!packet.clsEos,
            "the final valid chunk before a bad page link must be retained");
        for(unsigned int i = 0; i < 6; i++){
            require(packet.clsID.range(32*i+31, 32*i) == i+9,
                "the upper half of a clause page must retain every ID");
        }
        require(packet.clsID.range(255, 192) == 0,
            "the page-link slots must not be sent as clause IDs");
        packet = outputs[lane].read();
        require(packet.clsEos && packet.clsID == 0,
            "a bad page link must become an empty completion");

        packet = outputs[lane].read();
        require(packet.clsEos && packet.clsID == 0,
            "an oversized adjacency list must become an empty completion");

        packet = outputs[lane].read();
        require(packet.clsEos && packet.clsID == 0,
            "an unaligned page start must become an empty completion");

        packet = outputs[lane].read();
        require(packet.streamEos,
            "each updater lane must receive exactly one stream terminator");
        require(outputs[lane].empty(),
            "the color stream must not emit extra packets");
    }

    require(input.empty() && stop.empty(),
        "the input and stop streams must be fully balanced");
    std::cout << "COLOR_PROTOCOL_TEST_PASS\n";
    return 0;
}
