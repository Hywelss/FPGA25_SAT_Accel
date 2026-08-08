#include <cstdlib>
#include <iostream>

#include "data_structures.h"

extern "C" void location_handler(
    const unsigned int* clsToLitStorePos,
    const unsigned int* litToClsStorePos,
    unsigned int clauseElements, unsigned int literalElements,
    bool sessionReset,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream,
    hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream);

namespace {

ap_axiu<64,0,0,0> command(ap_uint<64> value){
    ap_axiu<64,0,0,0> packet;
    packet.data = value;
    return packet;
}

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main(){
    const unsigned int clsToLiteral[1] = {7};
    const unsigned int literalToCls[1] = {0};
    hls::stream<ap_axiu<64,0,0,0>> input;
    hls::stream<ap_axiu<32,0,0,0>> output;

    input.write(command(lh::SEND));
    input.write(command(0));
    input.write(command(_FPGA_MAX_LITERAL_ELEMENTS));
    input.write(command((ap_uint<64>)lh::EXIT));

    input.write(command(lh::SAVE));
    ap_uint<64> invalidSave = 0;
    invalidSave.range(31,0) = _FPGA_MAX_LITERAL_ELEMENTS;
    invalidSave.range(63,32) = _FPGA_MAX_LITERAL_ELEMENTS;
    input.write(command(invalidSave));
    input.write(command((ap_uint<64>)lh::EXIT));

    input.write(command(lh::UPDATE));
    input.write(command(invalidSave));
    input.write(command((ap_uint<64>)lh::EXIT));
    input.write(command((ap_uint<64>)lh::EXIT));

    location_handler(clsToLiteral, literalToCls, 1, 1, true,
        input, output);

    require(output.read().data == 7,
        "a valid mapping lookup must be preserved");
    require(output.read().data == UINT_MAX,
        "an invalid mapping lookup must return one explicit sentinel");
    require(output.empty() && input.empty(),
        "invalid mapping commands must retain stream framing");

    std::cout << "LOCATION_HANDLER_PROTOCOL_TEST_PASS\n";
    return 0;
}
