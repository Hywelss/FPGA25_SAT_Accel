#include "data_structures.h"

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

#include <cassert>
#include <iostream>
#include <vector>

#ifndef CLAUSE_STORE_READ_TOP
#define CLAUSE_STORE_READ_TOP sendData_dataflow
#endif

void CLAUSE_STORE_READ_TOP(
    const ap_uint<128>* clauseStore,
    const clauseMetaData* commands,
    const ap_uint<1>* compactClauseLayout,
    unsigned int clausePageSize,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input1,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input2,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output1,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output2);

void saveData(
    ap_uint<128>* clauseStore,
    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_>& freeClausePages,
    clauseMetaData command,
    unsigned int clausePageSize,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input,
    hls::stream<ap_axiu<64, 0, 0, 0>>& locationInput);

void sendLength_wrapper(
    hls::stream<ap_axiu<32, 0, 0, 0>>& output,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input,
    const clauseMetaData commands[_FPGA_MAX_CLAUSES]);

extern "C" void clause_store_handler(
    ap_uint<128>* clauseStore, clauseMetaData* commands,
    cls* usedClauseBuckets, unsigned int* trackLBD,
    unsigned int initialClauseElements, unsigned int maxClauseElements,
    unsigned int initialClauseCount, unsigned int clausePageSize,
    double prunePercentage, bool sessionReset,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input1,
    hls::stream<ap_axiu<96, 0, 0, 0>>& input2,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output1,
    hls::stream<ap_axiu<32, 0, 0, 0>>& output2,
    hls::stream<ap_axiu<64, 0, 0, 0>>& locationInput);

static ap_axiu<96, 0, 0, 0> command(unsigned int clauseID) {
    ap_axiu<96, 0, 0, 0> result;
    result.data = 0;
    result.data.range(31, 0) = clauseID;
    return result;
}

static ap_axiu<96, 0, 0, 0> exitCommand() {
    auto result = command(0);
    result.data.range(95, 64) = csh::EXIT;
    return result;
}

static ap_axiu<96, 0, 0, 0> lengthExitCommand() {
    auto result = command((unsigned int)csh::EXIT);
    result.data.range(95, 64) = csh::SEND_LEN;
    return result;
}

static std::vector<int> drain(hls::stream<ap_axiu<32, 0, 0, 0>>& stream) {
    std::vector<int> result;
    while(!stream.empty()){
        result.push_back(stream.read().data);
    }
    return result;
}

static void expect(hls::stream<ap_axiu<32, 0, 0, 0>>& stream,
                   const std::vector<int>& expected) {
    assert(drain(stream) == expected);
}

int main() {
    std::vector<ap_uint<128>> clauseStore(32, 0);
    std::vector<clauseMetaData> commands(_FPGA_MAX_CLAUSES);
    std::vector<ap_uint<1>> compactClauseLayout(_FPGA_MAX_CLAUSES, 0);

    commands[0] = {.addressStart = 0, .numElements = 5};
    compactClauseLayout[0] = 1;
    clauseStore[0].range(31, 0) = 11;
    clauseStore[0].range(63, 32) = 12;
    clauseStore[0].range(95, 64) = 13;
    clauseStore[0].range(127, 96) = 4;
    clauseStore[1].range(31, 0) = 14;
    clauseStore[1].range(63, 32) = 15;

    commands[1] = {.addressStart = 16, .numElements = 9};
    for(unsigned int i = 0; i < 7; i++){
        clauseStore[4 + i / 4].range(32 * (i % 4) + 31, 32 * (i % 4)) = 21 + i;
    }
    clauseStore[5].range(127, 96) = 24;
    clauseStore[6].range(31, 0) = 28;
    clauseStore[6].range(63, 32) = 29;

    commands[2] = {.addressStart = 32, .numElements = 1};
    compactClauseLayout[2] = 1;
    clauseStore[8].range(31, 0) = 31;

    commands[3] = {.addressStart = 40, .numElements = 7};
    for(unsigned int i = 0; i < 7; i++){
        clauseStore[10 + i / 4].range(32 * (i % 4) + 31, 32 * (i % 4)) = 41 + i;
    }

    commands[4] = {.addressStart = 48, .numElements = 8};
    for(unsigned int i = 0; i < 7; i++){
        clauseStore[12 + i / 4].range(32 * (i % 4) + 31, 32 * (i % 4)) = 51 + i;
    }
    clauseStore[13].range(127, 96) = 64;
    clauseStore[16].range(31, 0) = 58;

    commands[5] = {.addressStart = 72, .numElements = 6};
    compactClauseLayout[5] = 1;
    clauseStore[18].range(31, 0) = 61;
    clauseStore[18].range(63, 32) = 62;
    clauseStore[18].range(95, 64) = 63;
    clauseStore[18].range(127, 96) = 80;
    clauseStore[20].range(31, 0) = 64;
    clauseStore[20].range(63, 32) = 65;
    clauseStore[20].range(95, 64) = 66;

    commands[6] = {.addressStart = _FPGA_MAX_LITERAL_ELEMENTS,
                   .numElements = 1};
    commands[7] = {.addressStart = 88, .numElements = 4};
    compactClauseLayout[7] = 1;
    clauseStore[22].range(31, 0) = 71;
    clauseStore[22].range(63, 32) = 72;
    clauseStore[22].range(95, 64) = 73;
    clauseStore[22].range(127, 96) = _FPGA_MAX_LITERAL_ELEMENTS;

    commands[8] = {.addressStart = 96, .numElements = 3};
    compactClauseLayout[8] = 1;
    clauseStore[24].range(31, 0) = 81;
    clauseStore[24].range(63, 32) = 0;
    clauseStore[24].range(95, 64) = 82;

    hls::stream<ap_axiu<96, 0, 0, 0>> input1;
    hls::stream<ap_axiu<96, 0, 0, 0>> input2;
    hls::stream<ap_axiu<32, 0, 0, 0>> output1;
    hls::stream<ap_axiu<32, 0, 0, 0>> output2;
    input1.write(command(0));
    input1.write(command(2));
    input1.write(command(4));
    input1.write(command(-1));
    input1.write(exitCommand());
    input2.write(command(1));
    input2.write(command(3));
    input2.write(command(5));
    input2.write(exitCommand());

    CLAUSE_STORE_READ_TOP(
        clauseStore.data(), commands.data(), compactClauseLayout.data(), 8,
        input1, input2, output1, output2);

    expect(output1, {11, 12, 13, 14, 15, 0, 31, 0,
                     51, 52, 53, 54, 55, 56, 57, 58, 0, 0});
    expect(output2, {21, 22, 23, 24, 25, 26, 27, 28, 29, 0,
                     41, 42, 43, 44, 45, 46, 47, 0,
                     61, 62, 63, 64, 65, 66, 0});

    input1.write(exitCommand());
    input2.write(command(2));
    input2.write(exitCommand());
    CLAUSE_STORE_READ_TOP(
        clauseStore.data(), commands.data(), compactClauseLayout.data(), 8,
        input1, input2, output1, output2);
    expect(output1, {});
    expect(output2, {31, 0});

    input1.write(command(6));
    input1.write(command(7));
    input1.write(command(8));
    input1.write(command(2));
    input1.write(exitCommand());
    input2.write(exitCommand());
    CLAUSE_STORE_READ_TOP(
        clauseStore.data(), commands.data(), compactClauseLayout.data(), 8,
        input1, input2, output1, output2);
    expect(output1, {0, 71, 72, 73, 0, 81, 0, 31, 0});
    expect(output2, {});

    commands[6].numElements = UINT_MAX;
    input1.write(command(6));
    input1.write(command(_FPGA_MAX_CLAUSES));
    input1.write(lengthExitCommand());
    sendLength_wrapper(output1, input1, commands.data());
    expect(output1, {0, 0});

    commands[6] = {.addressStart = 0,
                   .numElements = _FPGA_MAX_LITERAL_ELEMENTS + 1};
    input1.write(command(6));
    input1.write(command(2));
    input1.write(exitCommand());
    input2.write(exitCommand());
    CLAUSE_STORE_READ_TOP(
        clauseStore.data(), commands.data(), compactClauseLayout.data(), 8,
        input1, input2, output1, output2);
    expect(output1, {0, 31, 0});
    expect(output2, {});

    hls::stream<ap_axiu<64, 0, 0, 0>> rejectedLocations;
    unsigned int trackLBD[2 * _FPGA_MAX_LBD_BUCKETS] = {};
    cls unusedBuckets[1] = {};
    ap_axiu<96, 0, 0, 0> start;
    start.data = 0;
    start.data.range(95, 64) = csh::SEND_LEN;
    input1.write(start);
    input1.write(command(2));
    input1.write(lengthExitCommand());
    start.data.range(95, 64) = csh::SEND_CLS;
    input1.write(start);
    input1.write(command(2));
    input1.write(exitCommand());
    input2.write(command(2));
    input2.write(exitCommand());
    start.data = 0;
    start.data.range(31, 0) = 1;
    start.data.range(95, 64) = csh::SAVE;
    input1.write(start);
    start.data = 0;
    start.data.range(31, 0) = 1;
    start.data.range(95, 64) = csh::DELETE_IDS;
    input1.write(start);
    input1.write(exitCommand());

    clause_store_handler(clauseStore.data(), commands.data(), unusedBuckets,
        trackLBD, 0, _FPGA_MAX_LITERAL_ELEMENTS, 0, 0, 0.0, true,
        input1, input2, output1, output2, rejectedLocations);
    expect(output1, {0, 0, -4, 0});
    expect(output2, {0});
    assert(rejectedLocations.read().data == (ap_uint<64>)lh::EXIT);
    assert(rejectedLocations.empty());

    mmuStream<unsigned int, _MAX_PAGES_CLS_STORE_> freeClausePages(8, 64, 8);
    hls::stream<ap_axiu<96, 0, 0, 0>> saveInput;
    hls::stream<ap_axiu<64, 0, 0, 0>> locationInput;
    for(unsigned int i = 0; i < 7; i++){
        ap_axiu<96, 0, 0, 0> value;
        value.data = 0;
        value.data.range(31, 0) = 31 + i;
        saveInput.write(value);
    }
    const unsigned int pagesBefore = freeClausePages.size();
    saveData(clauseStore.data(), freeClausePages,
        {.addressStart = 0, .numElements = 7}, 8, saveInput, locationInput);
    assert(freeClausePages.size() == pagesBefore);
    assert(clauseStore[1].range(127, 96) == 0);
    while(!locationInput.empty()){
        locationInput.read();
    }
    assert(input1.empty() && input2.empty());
    assert(output1.empty() && output2.empty());
    assert(saveInput.empty() && locationInput.empty());
    std::cout << "CLAUSE_STORE_READ_TEST_PASS\n";
}
