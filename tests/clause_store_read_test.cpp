#include "data_structures.h"

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

#include <cassert>
#include <vector>

void sendData_dataflow(
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

static std::vector<int> drain(hls::stream<ap_axiu<32, 0, 0, 0>>& stream) {
    std::vector<int> result;
    while(!stream.empty()){
        result.push_back(stream.read().data);
    }
    return result;
}

int main() {
    std::vector<ap_uint<128>> clauseStore(16, 0);
    std::vector<clauseMetaData> commands(2);
    std::vector<ap_uint<1>> compactClauseLayout(2, 0);

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

    hls::stream<ap_axiu<96, 0, 0, 0>> input1;
    hls::stream<ap_axiu<96, 0, 0, 0>> input2;
    hls::stream<ap_axiu<32, 0, 0, 0>> output1;
    hls::stream<ap_axiu<32, 0, 0, 0>> output2;
    input1.write(command(0));
    input1.write(exitCommand());
    input2.write(command(1));
    input2.write(exitCommand());

    sendData_dataflow(
        clauseStore.data(), commands.data(), compactClauseLayout.data(), 8,
        input1, input2, output1, output2);

    assert((drain(output1) == std::vector<int>{11, 12, 13, 14, 15, 0}));
    assert((drain(output2) == std::vector<int>{21, 22, 23, 24, 25, 26, 27, 28, 29, 0}));

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
}
