#include <cassert>

#include "data_structures.h"

extern "C" void pqHandler(
    const unsigned int* decision_domain, int num_literals,
    int num_domain_literals, double decay, bool session_reset,
    hls::stream<ap_axiu<32, 0, 0, 0>>& input,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output);

namespace {
void write(hls::stream<ap_axiu<32, 0, 0, 0>>& stream, int value) {
    ap_axiu<32, 0, 0, 0> packet;
    packet.data = value;
    stream.write(packet);
}
}  // namespace

int main() {
    unsigned int domain[_FPGA_MAX_LITERALS] = {};
    for (unsigned int i = 0; i < 8; ++i) {
        domain[i] = i + 1;
    }
    hls::stream<ap_axiu<32, 0, 0, 0>> input;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;

    write(input, pq::HIDE_ELE);
    write(input, 1);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::HIDE_ELE);
    write(input, 8);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    for (unsigned int i = 0; i < 5; ++i) {
        write(input, pq::GET_UNDECIDED);
    }
    write(input, pq::EXIT);
    write(input, pq::UNHIDE_ELE);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 8, 8, 0.95, true, input, output);

    assert(output.read().data == 7);
    assert(output.read().data == 6);
    assert(output.read().data == 5);
    assert(output.read().data == 4);
    assert(output.read().data == 2);
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.read().data == 3);
    assert(output.empty());

    write(input, pq::GET_UNDECIDED);
    for (unsigned int i = 0; i < 8; ++i) {
        write(input, pq::GET_UNDECIDED);
    }
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 8, 8, 0.95, false, input, output);

    for (int variable = 8; variable >= 1; --variable) {
        assert(output.read().data == variable);
    }
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.empty());
}
