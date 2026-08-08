#include <cassert>
#include <iostream>

#include "data_structures.h"

void propagationClosedLoopCosim(
    hls::stream<ap_axiu<32,0,0,0>>& clauseLengths,
    hls::stream<ap_axiu<96,0,0,0>>& clauseRequests,
    unsigned int scenario, unsigned int& answerHeight,
    lit& firstAnswer, lit& secondAnswer, bool& doBacktrack,
    unsigned int& conflictCount);

int main(){
    for(unsigned int scenario = 0; scenario < 13; scenario++){
        std::cerr << "propagation scenario " << scenario << '\n';
        hls::stream<ap_axiu<32,0,0,0>> clauseLengths;
        hls::stream<ap_axiu<96,0,0,0>> clauseRequests;
        if(scenario == 1 || scenario == 8 ||
                (scenario >= 9 && scenario <= 12)){
            ap_axiu<32,0,0,0> length;
            length.data = 2;
            clauseLengths.write(length);
            if(scenario >= 9 && scenario <= 12){
                clauseLengths.write(length);
            }
        }

        unsigned int answerHeight = 0;
        lit firstAnswer = 0;
        lit secondAnswer = 0;
        bool doBacktrack = false;
        unsigned int conflictCount = 0;
        propagationClosedLoopCosim(clauseLengths, clauseRequests, scenario,
            answerHeight, firstAnswer, secondAnswer, doBacktrack,
            conflictCount);

        const lit topLiteral = 1 + 2*scenario;
        if(scenario == 0){
            assert(!doBacktrack && answerHeight == 1);
            assert(firstAnswer == topLiteral);
        }else if(scenario == 1){
            assert(!doBacktrack && answerHeight == 2);
            assert(firstAnswer == topLiteral && secondAnswer == topLiteral+1);
            assert(clauseRequests.read().data.range(31,0) == 8);
        }else if(scenario == 2){
            assert(doBacktrack && conflictCount == 1);
        }else if(scenario == 3){
            assert(!doBacktrack && answerHeight == 1);
            assert(firstAnswer == topLiteral);
        }else if(scenario == 4){
            assert(!doBacktrack && answerHeight == 1);
            assert(firstAnswer == topLiteral);
        }else if(scenario == 5){
            assert(doBacktrack && answerHeight == 0);
        }else if(scenario == 8){
            assert(!doBacktrack && answerHeight == 3);
            assert(firstAnswer == topLiteral && secondAnswer == topLiteral+2);
            assert(clauseRequests.read().data.range(31,0) == 64);
        }else if(scenario >= 9 && scenario <= 12){
            assert(!doBacktrack && answerHeight == scenario-5);
            assert(firstAnswer == topLiteral &&
                secondAnswer == (lit)(3*scenario-5));
            assert(clauseRequests.read().data.range(31,0) == 8*scenario);
            assert(clauseRequests.read().data.range(31,0) == 1+8*scenario);
        }else{
            assert(!doBacktrack && answerHeight == 1);
            assert(firstAnswer == topLiteral);
        }
        assert(clauseLengths.empty());
        assert(clauseRequests.empty());
    }

    std::cout << "PROPAGATION_CLOSED_LOOP_COSIM_PASS\n";
    return 0;
}
