#include <cstdlib>
#include <iostream>

#include "learn.h"

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testMissingNextClauseTerminates(){
    static ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static cls unitByCls[_FPGA_MAX_LITERALS]{};
    static literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};

    answerStack[0] = 1;
    answerStack[1] = 2;
    int trailEndIndex = 1;
    cls nextClause = -1;
    bool foundNextClause = true;
    ap_uint<64> stats = 0;

    findNextClsDataflow(trailEndIndex, nextClause, foundNextClause,
        mergeScratchPad, validBit, answerStack, unitByCls, lmmd, stats);

    require(!foundNextClause, "missing reason clause must be reported explicitly");
    require(nextClause == 0, "missing reason clause must return the zero sentinel");
}

void testFoundNextClauseConsumesTerminator(){
    static ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static cls unitByCls[_FPGA_MAX_LITERALS]{};
    static literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};

    answerStack[1] = 1;
    validBit[0][0] = 1;
    mergeScratchPad[0].range(_FPGA_MAX_LEARN_ELE_BITS+1,
        _FPGA_MAX_LEARN_ELE_BITS) = 1;
    unitByCls[0] = 7;

    int trailEndIndex = 1;
    cls nextClause = 0;
    bool foundNextClause = false;
    ap_uint<64> stats = 0;

    findNextClsDataflow(trailEndIndex, nextClause, foundNextClause,
        mergeScratchPad, validBit, answerStack, unitByCls, lmmd, stats);

    require(foundNextClause, "available reason clause must be reported");
    require(nextClause == 7, "reason clause ID must be preserved");
    require(trailEndIndex == 0, "trail scan must resume before the selected literal");
}

void testTrailIndexZeroBecomesExhausted(){
    static ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2> mergeScratchPad[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512]{};
    static lit answerStack[_FPGA_MAX_LITERALS]{};
    static cls unitByCls[_FPGA_MAX_LITERALS]{};
    static literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};

    answerStack[0] = 1;
    validBit[0][0] = 1;
    mergeScratchPad[0].range(_FPGA_MAX_LEARN_ELE_BITS+1,
        _FPGA_MAX_LEARN_ELE_BITS) = 1;
    unitByCls[0] = 9;

    int trailEndIndex = 0;
    cls nextClause = 0;
    bool foundNextClause = false;
    ap_uint<64> stats = 0;

    findNextClsDataflow(trailEndIndex, nextClause, foundNextClause,
        mergeScratchPad, validBit, answerStack, unitByCls, lmmd, stats);
    require(foundNextClause && nextClause == 9,
        "the reason at trail index zero must still be returned");
    require(trailEndIndex == -1,
        "the trail scan must record that index zero was consumed");

    findNextClsDataflow(trailEndIndex, nextClause, foundNextClause,
        mergeScratchPad, validBit, answerStack, unitByCls, lmmd, stats);
    require(!foundNextClause && nextClause == 0,
        "an exhausted trail must terminate instead of wrapping around");
    require(trailEndIndex == -1,
        "an exhausted trail index must remain stable");
}

void testBacktrackHeightRejectsInvalidState(){
    static literalMetaData lmd[_FPGA_MAX_LITERALS]{};
    unsigned int backtrackHeight = 0;

    lmd[0].decisionLevelStackEnd = 3;
    require(computeBacktrackHeight(0, 5, lmd, backtrackHeight),
        "root-level backtrack metadata must be accepted");
    require(backtrackHeight == 2,
        "backtrack height must be measured from the target stack position");

    require(!computeBacktrackHeight(-1, 5, lmd, backtrackHeight),
        "a negative decision level must not index the metadata array");
    require(!computeBacktrackHeight(_FPGA_MAX_LITERALS, 5, lmd,
            backtrackHeight),
        "an out-of-range decision level must be rejected");

    lmd[0].decisionLevelStackEnd = 6;
    require(!computeBacktrackHeight(0, 5, lmd, backtrackHeight),
        "a target above the current stack must not underflow the loop bound");
}

void testInvalidResolutionLiteralClosesBothStages(){
    static lit resolutionClause[_FPGA_MAX_LEARN_ELE]{};
    static ap_uint<_FPGA_MAX_LEARN_ELE_BITS+2>
        mergeScratchPad[_FPGA_MAX_LITERALS]{};
    static ap_uint<512> validBit[_FPGA_MAX_LITERALS/512]{};
    hls::stream<lit> clauseInput;
    hls::stream<lit_resolve> updates;
    clauseInput.write(0x7fffffff);
    clauseInput.write(0);

    unsigned int numElements = 0;
    bool clauseTooLong = false;
    ap_uint<64> stats = 0;
    merge_resolution_sort(updates, clauseInput, resolutionClause,
        mergeScratchPad, validBit, numElements, clauseTooLong, stats);

    require(clauseTooLong,
        "an invalid clause literal must fail learning conservatively");
    require(clauseInput.empty(),
        "an invalid clause literal must not leave its terminator unread");
    require(updates.read().literal == 0 && updates.empty(),
        "the learning merge must still close its downstream stream");
}

void testSavedClauseCountMatchesWritePredicate(){
    static lit resolutionClause[_FPGA_MAX_LEARN_ELE]{};
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};
    resolutionClause[0] = 1;
    resolutionClause[1] = -2;
    resolutionClause[2] = 3;
    for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
        LMMD_MIN_KEEP(lmmd[lane][0].compactlmmd) = 1;
        LMMD_MIN_KEEP(lmmd[lane][1].compactlmmd) = 1;
        LMMD_MIN_KEEP(lmmd[lane][2].compactlmmd) = lane == 0 ? 1 : 2;
    }
    LMMD_IS_IN_FIX_STACK(lmmd[0][1].compactlmmd) = 1;

    unsigned int savedCount = 0;
    require(countSavedClauseLiterals(resolutionClause, 3, 3, lmmd,
            savedCount), "a valid learned clause must be countable");
    require(savedCount == 1,
        "declared learned length must match the exact write predicate");

    bool keep = false;
    require(shouldSaveClauseLiteral(resolutionClause[0], lmmd, keep) && keep,
        "the shared write predicate must retain the counted literal");
    require(shouldSaveClauseLiteral(resolutionClause[1], lmmd, keep) && !keep,
        "the shared write predicate must remove a root-level literal");
    require(shouldSaveClauseLiteral(resolutionClause[2], lmmd, keep) && !keep,
        "the shared write predicate must honor every minimize lane");

    resolutionClause[2] = 4;
    require(!countSavedClauseLiterals(resolutionClause, 3, 3, lmmd,
            savedCount),
        "a learned literal outside the active formula must be rejected");
}

void testSavedClauseCountMaximumLength(){
    static lit resolutionClause[_FPGA_MAX_LEARN_ELE]{};
    static literalMinimizeMetaData
        lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS]{};

    for(unsigned int i = 0; i < _FPGA_MAX_LEARN_ELE; i++){
        resolutionClause[i] = i+1;
        for(unsigned int lane = 0; lane < _FPGA_PARALLEL_MINIMIZE; lane++){
            LMMD_MIN_KEEP(lmmd[lane][i].compactlmmd) = 1;
        }
    }

    unsigned int savedCount = 0;
    require(countSavedClauseLiterals(resolutionClause, _FPGA_MAX_LEARN_ELE,
            _FPGA_MAX_LEARN_ELE, lmmd, savedCount),
        "the maximum supported learned clause must be countable");
    require(savedCount == _FPGA_MAX_LEARN_ELE,
        "the maximum learned-clause length must remain exactly 1024");
}

} // namespace

int main(){
    testMissingNextClauseTerminates();
    testFoundNextClauseConsumesTerminator();
    testTrailIndexZeroBecomesExhausted();
    testBacktrackHeightRejectsInvalidState();
    testInvalidResolutionLiteralClosesBothStages();
    testSavedClauseCountMatchesWritePredicate();
    testSavedClauseCountMaximumLength();
    std::cout << "LEARN_PROTOCOL_TEST_PASS\n";
    return 0;
}
