#include <cstdlib>
#include <iostream>

#include "minimize.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testMissingReasonIsKeptWithoutRequest(){
    static literalMinimizeMetaData lmmd[_FPGA_MAX_LITERALS] = {};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512] = {};
    static ap_uint<2> mergeScratchPad[_FPGA_MAX_LITERALS] = {};
    static cls unitByCls[_FPGA_MAX_LITERALS] = {};

    LMMD_MIN_KEEP(lmmd[0].compactlmmd) = 1;
    hls::stream<lit> literals;
    literals.write(1);
    literals.write(0);
    hls::stream<ap_axiu<96,0,0,0>> requests;
    hls::stream<ap_axiu<32,0,0,0>> responses;
    unsigned int nonRemovableCount = 0;
    bool didSimplify = false;
    ap_uint<64> stats[2] = {0, 0};
    unsigned int clearOverhead = 0;

    minimize_dataflow_wrapper_layer_1(literals, lmmd, validBit,
        mergeScratchPad, nonRemovableCount, didSimplify, unitByCls,
        false, 1, stats, clearOverhead, requests, responses);

    require(nonRemovableCount == 1,
        "a literal without a reason clause must be kept conservatively");
    require(requests.empty(),
        "a missing reason must not issue clause ID -1 to the store");
    require(responses.empty(),
        "a missing reason must not wait for a store response");
    require(!didSimplify,
        "missing evidence must not mark a literal removable");
}

void testExhaustedReasonFrontierTerminates(){
    static literalMinimizeMetaData lmmd[_FPGA_MAX_LITERALS] = {};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512] = {};
    static ap_uint<2> mergeScratchPad[_FPGA_MAX_LITERALS] = {};
    static cls unitByCls[_FPGA_MAX_LITERALS] = {};
    unitByCls[0] = 1;
    unitByCls[1] = 2;

    hls::stream<lit> literals;
    literals.write(1);
    literals.write(0);
    hls::stream<ap_axiu<96,0,0,0>> requests;
    hls::stream<ap_axiu<32,0,0,0>> responses;
    ap_axiu<32,0,0,0> response;
    response.data = 2;
    responses.write(response);
    response.data = 0;
    responses.write(response);
    response.data = 2;
    responses.write(response);
    response.data = 0;
    responses.write(response);

    unsigned int nonRemovableCount = 0;
    bool didSimplify = false;
    ap_uint<64> stats[2] = {0, 0};
    unsigned int clearOverhead = 0;

    minimize_dataflow_wrapper_layer_1(literals, lmmd, validBit,
        mergeScratchPad, nonRemovableCount, didSimplify, unitByCls,
        false, 1, stats, clearOverhead, requests, responses);

    require(nonRemovableCount == 1,
        "an exhausted reason frontier must keep the original literal");
    unsigned int requestCount = 0;
    while(!requests.empty()){
        requests.read();
        requestCount++;
    }
    require(requestCount == 2,
        "a repeated reason must stop after the frontier is exhausted");
    require(responses.empty(),
        "the exhausted-frontier path must consume each clause terminator");
    require(!didSimplify,
        "an exhausted reason frontier is not proof of removability");
}

void testInvalidReasonLiteralIsNotRemovable(){
    static literalMinimizeMetaData lmmd[_FPGA_MAX_LITERALS] = {};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512] = {};
    static ap_uint<2> mergeScratchPad[_FPGA_MAX_LITERALS] = {};
    hls::stream<ap_axiu<32,0,0,0>> responses;
    ap_axiu<32,0,0,0> response;
    response.data = 0x7fffffff;
    responses.write(response);
    response.data = 0;
    responses.write(response);

    myStream<lit,_FPGA_MAX_LEARN_ELE,_FPGA_MAX_LEARN_ELE_BITS> frontier{};
    unsigned int numElements = 0;
    unsigned int countMarked = 0;
    int exitCondition = 0;
    ap_uint<64> stats = 0;
    minimize_dataflow_wrapper_layer_2(frontier, mergeScratchPad, validBit,
        lmmd, numElements, countMarked, exitCondition, stats, responses);

    require(exitCondition == 2,
        "a malformed reason clause must keep the original literal");
    require(responses.empty(),
        "a malformed reason clause must still consume its terminator");
}

void testAbsoluteClauseConsumesLaneTerminator(){
    static literalMinimizeMetaData lmmd[_FPGA_MAX_LITERALS] = {};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512] = {};
    static ap_uint<2> mergeScratchPad[_FPGA_MAX_LITERALS] = {};
    static cls unitByCls[_FPGA_MAX_LITERALS] = {};

    hls::stream<lit> literals;
    literals.write(0);
    hls::stream<ap_axiu<96,0,0,0>> requests;
    hls::stream<ap_axiu<32,0,0,0>> responses;
    unsigned int nonRemovableCount = 0;
    bool didSimplify = false;
    ap_uint<64> stats[2] = {0, 0};
    unsigned int clearOverhead = 0;

    minimize_dataflow_wrapper_layer_1(literals, lmmd, validBit,
        mergeScratchPad, nonRemovableCount, didSimplify, unitByCls,
        true, 1, stats, clearOverhead, requests, responses);

    require(literals.empty(),
        "an absolute learned clause must consume its lane terminator");
    require(requests.empty() && responses.empty(),
        "an absolute learned clause must not issue a reason request");
}

} // namespace

int main(){
    testMissingReasonIsKeptWithoutRequest();
    testExhaustedReasonFrontierTerminates();
    testInvalidReasonLiteralIsNotRemovable();
    testAbsoluteClauseConsumesLaneTerminator();
    std::cout << "MINIMIZE_PROTOCOL_TEST_PASS\n";
    return 0;
}
