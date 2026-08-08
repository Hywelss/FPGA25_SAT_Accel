#include <cstdlib>
#include <iostream>

#include "discover.h"
#include "packed_clause_selector.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

colorValue streamEnd(){
    colorValue value{};
    value.clsID = 0;
    value.litID = 0;
    value.depthCount = 0;
    value.didSolve = false;
    value.clsEos = false;
    value.streamEos = true;
    return value;
}

} // namespace

int main(){
    static clsState states[_FPGA_CLS_STATES_PARTITION]
        [_FPGA_MAX_CLAUSES/_FPGA_CLS_STATES_PARTITION]{};
    const cls targetClause = 10169;
    const unsigned int targetAddress = (targetClause-1)/_FPGA_CLS_STATES_PARTITION;
    states[(targetClause-1)%_FPGA_CLS_STATES_PARTITION][targetAddress] = {
        .compressedList=2599 ^ 2606 ^ -105 ^ -2709,
        .remainingUnassigned=4};

    colorValue update{};
    update.clsID = 0;
    update.clsID.range(255,224) = targetClause;
    update.litID = -2599;
    update.depthCount = 0;
    update.didSolve = false;
    update.clsEos = true;
    update.streamEos = false;

    hls::stream<colorValue> laneInput[_FPGA_CLS_STATES_PARTITION/2];
    hls::stream<clsStateControlPacket> laneOutput[_FPGA_CLS_STATES_PARTITION/2];
    for(unsigned int lane = 0; lane < _FPGA_CLS_STATES_PARTITION/2; lane++){
        cls selected = 0;
        ap_uint<3> selectedIndex = 0;
        const bool found = selectPackedClause<true>(update.clsID, 2*lane,
            selected, selectedIndex);
        require(found == (lane == 0),
            "standalone packed selector must route the target to lane zero");
        laneInput[lane].write(update);
        laneInput[lane].write(streamEnd());
        updateStatesForward(laneOutput[lane], laneInput[lane],
            states[2*lane], states[2*lane+1], 2*lane);
    }

    require(states[(targetClause-1)%_FPGA_CLS_STATES_PARTITION][targetAddress]
            .remainingUnassigned == 3,
        "one packed clause ID broadcast to four lanes must update exactly once");

    unsigned int unitCount = 0;
    unsigned int conflictCount = 0;
    for(unsigned int lane = 0; lane < _FPGA_CLS_STATES_PARTITION/2; lane++){
        while(!laneOutput[lane].empty()){
            const clsStateControlPacket packet = laneOutput[lane].read();
            if(packet.eosCount == 0 && packet.pktType == solverCode::UNIT){
                unitCount++;
            }else if(packet.eosCount == 0 &&
                    packet.pktType == solverCode::BACKTRACK){
                conflictCount++;
            }
        }
    }
    require(unitCount == 0 && conflictCount == 0,
        "a four-literal clause must not produce a unit or conflict after one update");

    std::cout << "PROPAGATION_LANE_ROUTING_TEST_PASS\n";
    return 0;
}
