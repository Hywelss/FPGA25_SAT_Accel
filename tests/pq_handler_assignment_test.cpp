#include <cassert>

#include "data_structures.h"

extern "C" void pqHandler(
    const unsigned int* decision_domain, int num_literals,
    int num_domain_literals, double decay, bool session_reset,
    hls::stream<ap_axiu<32, 0, 0, 0>>& input,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output);

namespace {
constexpr unsigned int kDomainSize = 56;

void write(hls::stream<ap_axiu<32, 0, 0, 0>>& stream, int value) {
    ap_axiu<32, 0, 0, 0> packet;
    packet.data = value;
    stream.write(packet);
}

void hide(hls::stream<ap_axiu<32, 0, 0, 0>>& input, int variable) {
    write(input, pq::HIDE_ELE);
    write(input, variable);
    write(input, pq::EXIT);
}
}  // namespace

int main() {
    unsigned int domain[_FPGA_MAX_LITERALS] = {};
    for (unsigned int i = 0; i < kDomainSize; ++i) {
        domain[i] = i + 1;
    }
    hls::stream<ap_axiu<32, 0, 0, 0>> input;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;

    write(input, pq::HIDE_ELE);
    write(input, 1);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::HIDE_ELE);
    write(input, kDomainSize);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    for (unsigned int i = 0; i < kDomainSize - 3; ++i) {
        write(input, pq::GET_UNDECIDED);
    }
    write(input, pq::EXIT);
    write(input, pq::UNHIDE_ELE);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, kDomainSize, kDomainSize, 0.95, true, input, output);

    for (int variable = kDomainSize - 1; variable >= 2; --variable) {
        if (variable != 3) {
            assert(output.read().data == variable);
        }
    }
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.read().data == 3);
    assert(output.empty());

    write(input, pq::GET_UNDECIDED);
    for (unsigned int i = 0; i < kDomainSize; ++i) {
        write(input, pq::GET_UNDECIDED);
    }
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, kDomainSize, kDomainSize, 0.95, false, input, output);

    for (int variable = kDomainSize; variable >= 1; --variable) {
        assert(output.read().data == variable);
    }
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.empty());

    domain[0] = 1;
    write(input, pq::GET_UNDECIDED);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 3, 1, 0.95, true, input, output);

    assert(output.read().data == 1);
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.empty());

    for (unsigned int i = 0; i < 8; ++i) {
        domain[i] = i + 1;
    }
    hide(input, 8);
    write(input, pq::GET_UNDECIDED);
    for (unsigned int i = 0; i < 7; ++i) {
        write(input, pq::GET_UNDECIDED);
    }
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 8, 8, 0.95, true, input, output);

    for (int variable = 7; variable >= 1; --variable) {
        assert(output.read().data == variable);
    }
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.empty());

    hide(input, 8);
    hide(input, 3);
    hide(input, 5);
    hide(input, 6);
    hide(input, 7);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 8, 8, 0.95, true, input, output);

    assert(output.read().data == 4);
    assert(output.read().data == 2);
    assert(output.read().data == 1);
    assert(output.read().data == pq::DOMAIN_EXHAUSTED);
    assert(output.empty());
}
