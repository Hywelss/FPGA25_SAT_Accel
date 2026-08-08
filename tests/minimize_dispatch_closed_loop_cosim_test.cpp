#include <cstdlib>
#include <iostream>

#include "minimize.h"

void minimizeDispatchClosedLoopCosim(const unsigned int inputCount,
    const bool foundAbsolute, unsigned int& lane0Count,
    unsigned int& lane1Count);

static void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main(){
    unsigned int lane0Count = 0;
    unsigned int lane1Count = 0;

    minimizeDispatchClosedLoopCosim(0, true,
        lane0Count, lane1Count);
    require(lane0Count == 0 && lane1Count == 0,
        "the absolute-clause path must close both empty lanes");

    minimizeDispatchClosedLoopCosim(130, false,
        lane0Count, lane1Count);
    require(lane0Count + lane1Count == 130,
        "the normal path must deliver every literal exactly once");
    require(lane0Count == 65 && lane1Count == 65,
        "the hardware dispatcher must balance both lanes");

    std::cout << "MINIMIZE_DISPATCH_CLOSED_LOOP_COSIM_PASS\n";
    return 0;
}
