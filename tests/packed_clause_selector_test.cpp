#include <cstdlib>
#include <iostream>
#include <random>

#include "packed_clause_selector.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <bool includeNextPartition>
void checkSelection(const cls candidates[8], unsigned int partitionID){
    ap_int<256> packed = 0;
    int expectedIndex = -1;
    for(unsigned int i = 0; i < 8; i++){
        packed.range(32*i+31, 32*i) = candidates[i];
        const unsigned int candidatePartition =
            ((unsigned int)candidates[i]-1) &
            (_FPGA_CLS_STATES_PARTITION-1);
        const bool matches = candidates[i] != 0 &&
            (candidatePartition == partitionID ||
             (includeNextPartition &&
              candidatePartition == partitionID+1));
        if(matches && expectedIndex < 0){
            expectedIndex = i;
        }
    }

    cls selectedClause = 0;
    ap_uint<3> selectedIndex = 0;
    const bool found = selectPackedClause<includeNextPartition>(packed,
        partitionID, selectedClause, selectedIndex);
    require(found == (expectedIndex >= 0),
        "optimized partition comparison must preserve match detection");
    if(!found){
        require(selectedClause == 0,
            "an unmatched lane must not expose a residual clause ID");
    }
    if(found){
        require((unsigned int)selectedIndex == (unsigned int)expectedIndex,
            "selector must preserve lowest-position priority");
        require(selectedClause == candidates[expectedIndex],
            "selector must return the clause from the selected position");
    }
}

} // namespace

int main(){
    std::mt19937 random(0x51ec7u);
    for(unsigned int trial = 0; trial < 2000; trial++){
        cls candidates[8];
        for(unsigned int i = 0; i < 8; i++){
            switch(random()%5){
            case 0: candidates[i] = 0; break;
            case 1: candidates[i] = -(cls)(random()%32+1); break;
            case 2:
                candidates[i] = _FPGA_MAX_CLAUSES+(random()%32+1);
                break;
            default:
                candidates[i] = random()%_FPGA_MAX_CLAUSES+1;
                break;
            }
        }
        for(unsigned int partition = 0;
                partition < _FPGA_CLS_STATES_PARTITION; partition++){
            checkSelection<false>(candidates, partition);
            checkSelection<true>(candidates, partition);
        }
    }

    std::cout << "PACKED_CLAUSE_SELECTOR_TEST_PASS\n";
    return 0;
}
