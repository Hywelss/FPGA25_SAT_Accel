#include <cstdlib>
#include <iostream>

#include "discover.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

clsStateControlPacket endPacket(){
    return (clsStateControlPacket){
        .eosCount=-1,
        .bcp=(bcpPacket){
            .literalUnitted=0,
            .unitByClause=0,
            .unitByLit=0,
            .depthCount=0,
        },
        .pktType=solverCode::EOS_CNT,
    };
}

clsStateControlPacket backtrackPacket(){
    return (clsStateControlPacket){
        .eosCount=0,
        .bcp=(bcpPacket){
            .literalUnitted=0,
            .unitByClause=1,
            .unitByLit=0,
            .depthCount=0,
        },
        .pktType=solverCode::BACKTRACK,
    };
}

clsStateControlPacket completionPacket(int count){
    return (clsStateControlPacket){
        .eosCount=count,
        .bcp=(bcpPacket){
            .literalUnitted=0,
            .unitByClause=0,
            .unitByLit=0,
            .depthCount=0,
        },
        .pktType=solverCode::EOS_CNT,
    };
}

clsStateControlPacket invalidUnitPacket(){
    return (clsStateControlPacket){
        .eosCount=0,
        .bcp=(bcpPacket){
            .literalUnitted=_FPGA_MAX_LITERALS+1,
            .unitByClause=1,
            .unitByLit=1,
            .depthCount=0,
        },
        .pktType=solverCode::UNIT,
    };
}

void appendUpdaterEnds(hls::stream<clsStateControlPacket>& control){
    for(unsigned int i = 0; i < _FPGA_CLS_STATES_PARTITION/2; i++){
        control.write(endPacket());
    }
}

void requireDecisionEnd(hls::stream<bcpPacket>& decisions){
    require(!decisions.empty(), "control sink must terminate the discover stream");
    const bcpPacket packet = decisions.read();
    require(packet.literalUnitted == 0, "discover terminator must be a zero literal");
    require(decisions.empty(), "discover stream must contain exactly one terminator");
}

void testBackToBackUpdaterEnds(){
    static bool inDomain[_FPGA_MAX_LITERALS]{};
    hls::stream<bcpPacket> decisions;
    hls::stream<clsStateControlPacket> control;
    hls::stream<int> duplicateCounts;
    hls::stream<bool> stop;
    hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
    myStream<cls,64,7> conflicts{};
    bool doBacktrack = false;
    bool flushSignal = false;

    appendUpdaterEnds(control);
    duplicateCounts.write(-1);
    controlSink(decisions, control, duplicateCounts, stop, clauseRequests,
        conflicts, doBacktrack, inDomain, 1, 0, true, flushSignal);

    requireDecisionEnd(decisions);
    require(!stop.empty() && !stop.read(),
        "normal completion must release the color stream with false");
    require(stop.empty(), "color stream must receive exactly one stop value");
    require(clauseRequests.empty(), "empty completion must not request a clause");
    require(duplicateCounts.empty(), "duplicate-count terminator must be consumed");
}

void testBacktrackStillClosesDiscover(){
    static bool inDomain[_FPGA_MAX_LITERALS]{};
    hls::stream<bcpPacket> decisions;
    hls::stream<clsStateControlPacket> control;
    hls::stream<int> duplicateCounts;
    hls::stream<bool> stop;
    hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
    myStream<cls,64,7> conflicts{};
    bool doBacktrack = false;
    bool flushSignal = false;

    control.write(backtrackPacket());
    appendUpdaterEnds(control);
    duplicateCounts.write(-1);
    controlSink(decisions, control, duplicateCounts, stop, clauseRequests,
        conflicts, doBacktrack, inDomain, 1, 0, true, flushSignal);

    require(doBacktrack, "backtrack packet must set the backtrack result");
    requireDecisionEnd(decisions);
    require(!stop.empty() && stop.read(),
        "backtrack completion must stop the color stream with true");
    require(stop.empty(), "backtrack path must send exactly one stop value");
    require(duplicateCounts.empty(), "backtrack path must drain count termination");
}

void testCompletionAccountingMatchesExecutionModel(){
    static bool inDomain[_FPGA_MAX_LITERALS]{};
    hls::stream<bcpPacket> decisions;
    hls::stream<clsStateControlPacket> control;
    hls::stream<int> duplicateCounts;
    hls::stream<bool> stop;
    hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
    myStream<cls,64,7> conflicts{};
    bool doBacktrack = false;
    bool flushSignal = false;

    control.write(completionPacket(_FPGA_CLS_STATES_PARTITION/2 + 1));
    appendUpdaterEnds(control);
    duplicateCounts.write(-1);
    controlSink(decisions, control, duplicateCounts, stop, clauseRequests,
        conflicts, doBacktrack, inDomain, 1, 0, true, flushSignal);

#ifdef FPGA_HW
    require(doBacktrack,
        "hardware completion underflow must become an explicit solver error");
#else
    require(!doBacktrack,
        "software propagation waves must not treat sequential completions as hardware underflow");
#endif
    requireDecisionEnd(decisions);
    require(!stop.empty(), "completion accounting must close the color stream");
#ifdef FPGA_HW
    require(stop.read(), "hardware completion underflow must stop propagation");
#else
    require(!stop.read(), "software propagation waves must finish normally");
#endif
}

void testEmptyRootBatchBecomesError(){
    static bool inDomain[_FPGA_MAX_LITERALS]{};
    hls::stream<bcpPacket> decisions;
    hls::stream<clsStateControlPacket> control;
    hls::stream<int> duplicateCounts;
    hls::stream<bool> stop;
    hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
    myStream<cls,64,7> conflicts{};
    bool doBacktrack = false;
    bool flushSignal = false;

    appendUpdaterEnds(control);
    duplicateCounts.write(-1);
    controlSink(decisions, control, duplicateCounts, stop, clauseRequests,
        conflicts, doBacktrack, inDomain, 0, 0, false, flushSignal);

    require(doBacktrack,
        "level-zero propagation without fixed literals must fail explicitly");
    requireDecisionEnd(decisions);
    require(!stop.empty() && stop.read(),
        "an empty root batch must stop propagation");
}

void testInvalidUnitBecomesError(){
    static bool inDomain[_FPGA_MAX_LITERALS]{};
    hls::stream<bcpPacket> decisions;
    hls::stream<clsStateControlPacket> control;
    hls::stream<int> duplicateCounts;
    hls::stream<bool> stop;
    hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
    myStream<cls,64,7> conflicts{};
    bool doBacktrack = false;
    bool flushSignal = false;

    control.write(invalidUnitPacket());
    appendUpdaterEnds(control);
    duplicateCounts.write(-1);
    controlSink(decisions, control, duplicateCounts, stop, clauseRequests,
        conflicts, doBacktrack, inDomain, 1, 0, true, flushSignal);

    require(doBacktrack,
        "an invalid propagated literal must become an explicit error");
    requireDecisionEnd(decisions);
    require(!stop.empty() && stop.read(),
        "an invalid propagated literal must stop coloring");
    require(clauseRequests.empty(),
        "an invalid propagated literal must not request clause metadata");
}

} // namespace

int main(){
    testBackToBackUpdaterEnds();
    testBacktrackStillClosesDiscover();
    testCompletionAccountingMatchesExecutionModel();
    testEmptyRootBatchBecomesError();
    testInvalidUnitBecomesError();
    std::cout << "CONTROL_SINK_PROTOCOL_TEST_PASS\n";
    return 0;
}
