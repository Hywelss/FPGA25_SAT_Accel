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
    unsigned int domain[] = {1, 2, 3};
    hls::stream<ap_axiu<32, 0, 0, 0>> input;
    hls::stream<ap_axiu<32, 0, 0, 0>> output;

    write(input, pq::HIDE_ELE);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::UNHIDE_ELE);
    write(input, 3);
    write(input, pq::EXIT);
    write(input, pq::GET_UNDECIDED);
    write(input, pq::EXIT);
    write(input, pq::EXIT);

    pqHandler(domain, 3, 3, 0.95, true, input, output);

    assert(output.read().data == 2);
    assert(output.read().data == 3);
    assert(output.empty());
}
