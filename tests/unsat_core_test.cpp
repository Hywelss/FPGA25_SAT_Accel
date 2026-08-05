#include "unsat_core.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned int kNumLiterals = 8;

static lit coreOutput[_FPGA_MAX_LITERALS];
static lit assumptions[_FPGA_MAX_LITERALS];
static lit trail[_FPGA_MAX_LITERALS];
static literalMetaData lmd[_FPGA_MAX_LITERALS];
static literalMinimizeMetaData lmmd[_FPGA_PARALLEL_MINIMIZE][_FPGA_MAX_LITERALS];
static cls unitByCls[_FPGA_MAX_LITERALS];

using InputStream = hls::stream<ap_axiu<96, 0, 0, 0>>;
using OutputStream = hls::stream<ap_axiu<32, 0, 0, 0>>;

void resetState() {
    std::memset(coreOutput, 0, sizeof(coreOutput));
    std::memset(assumptions, 0, sizeof(assumptions));
    std::memset(trail, 0, sizeof(trail));
    std::memset(lmd, 0, sizeof(lmd));
    std::memset(lmmd, 0, sizeof(lmmd));
    std::memset(unitByCls, 0, sizeof(unitByCls));
}

void assign(lit literal, unsigned int trailIndex, unsigned int decisionLevel,
            bool isDecision, cls reasonClause = 0) {
    const unsigned int variable = std::abs(literal);
    trail[trailIndex] = literal;
    LMD_IS_IN_STACK(lmd[variable - 1].compactlmd) = true;
    LMD_INSERT_LVL(lmd[variable - 1].compactlmd) = trailIndex;
    LMD_DEC_LVL(lmd[variable - 1].compactlmd) = decisionLevel;
    LMMD_IS_DECIDE(lmmd[0][variable - 1].compactlmmd) = isDecision;
    unitByCls[variable - 1] = reasonClause;
}

void writeClause(OutputStream& output, std::initializer_list<lit> clause) {
    for(lit literal : clause){
        ap_axiu<32, 0, 0, 0> packet;
        packet.data = literal;
        output.write(packet);
    }
    ap_axiu<32, 0, 0, 0> end;
    end.data = 0;
    output.write(end);
}

void drain(InputStream& stream) {
    while(!stream.empty()){
        stream.read();
    }
}

std::vector<lit> extract(unsigned int assumptionCount,
                         unsigned int trailHeight,
                         myStream<cls, 64, 7>& conflicts,
                         int conflictingAssumptionIndex,
                         OutputStream& clauseOutput) {
    InputStream input1;
    InputStream input2;
    unsigned int coreCount = 0;
    const bool ok = extractUnsatCore(
        coreOutput, coreCount, assumptions, assumptionCount, trail,
        trailHeight, lmd, lmmd, unitByCls, conflicts,
        conflictingAssumptionIndex, kNumLiterals, input1, input2,
        clauseOutput);
    assert(ok);
    drain(input1);
    drain(input2);
    assert(clauseOutput.empty());
    return std::vector<lit>(coreOutput, coreOutput + coreCount);
}

void testConflictExcludesIrrelevantAssumption() {
    resetState();
    assumptions[0] = 1;
    assumptions[1] = 2;
    assumptions[2] = 3;
    assign(1, 0, 1, true);
    assign(2, 1, 2, true);
    assign(3, 2, 3, true);

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 1;
    conflicts.tail = 0;
    conflicts.array[0] = 1;
    OutputStream output;
    writeClause(output, {-1, -3});

    const auto core = extract(3, 3, conflicts, -1, output);
    assert(core.size() == 2);
    assert(std::find(core.begin(), core.end(), 1) != core.end());
    assert(std::find(core.begin(), core.end(), 3) != core.end());
    assert(std::find(core.begin(), core.end(), 2) == core.end());
}

void testDirectAssumptionConflictTracesItsReason() {
    resetState();
    assumptions[0] = 1;
    assumptions[1] = -3;
    assign(1, 0, 1, true);
    assign(3, 1, 1, false, 2);

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 0;
    conflicts.tail = 0;
    OutputStream output;
    writeClause(output, {-1, 3});

    const auto core = extract(2, 2, conflicts, 1, output);
    assert((core == std::vector<lit>{-3, 1}));
}

void testOppositeAssumptionsNeedNoReasonClause() {
    resetState();
    assumptions[0] = 1;
    assumptions[1] = -1;
    assign(1, 0, 1, true);

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 0;
    conflicts.tail = 0;
    OutputStream output;

    const auto core = extract(2, 1, conflicts, 1, output);
    assert((core == std::vector<lit>{-1, 1}));
}

void testRootConflictHasEmptyCore() {
    resetState();
    assign(4, 0, 0, false, 1);

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 1;
    conflicts.tail = 0;
    conflicts.array[0] = 1;
    OutputStream output;
    writeClause(output, {-4});

    assert(extract(0, 1, conflicts, -1, output).empty());
}

void testRejectsTrailHeightAboveVariableCount() {
    resetState();
    myStream<cls, 64, 7> conflicts;
    conflicts.head = 0;
    conflicts.tail = 0;
    OutputStream output;
    InputStream input1;
    InputStream input2;
    unsigned int coreCount = 0;

    const bool ok = extractUnsatCore(
        coreOutput, coreCount, assumptions, 0, trail, kNumLiterals + 1,
        lmd, lmmd, unitByCls, conflicts, -1, kNumLiterals, input1,
        input2, output);

    assert(!ok);
    assert(input1.empty());
    assert(input2.empty());
}

void testRejectsOutOfRangeTrailLiteralBeforeIndexing() {
    resetState();
    assumptions[0] = 1;
    assign(1, 0, 1, true);
    trail[0] = kNumLiterals + 1;

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 1;
    conflicts.tail = 0;
    conflicts.array[0] = 1;
    OutputStream output;
    writeClause(output, {-1});
    InputStream input1;
    InputStream input2;
    unsigned int coreCount = 0;

    const bool ok = extractUnsatCore(
        coreOutput, coreCount, assumptions, 1, trail, 1, lmd, lmmd,
        unitByCls, conflicts, -1, kNumLiterals, input1, input2, output);

    assert(!ok);
    drain(input1);
    drain(input2);
    assert(output.empty());
}

void testRejectsCoreOutputBeyondAssumptionCapacity() {
    resetState();
    assumptions[0] = 1;
    assign(1, 0, 1, true);
    assign(2, 1, 1, true);

    myStream<cls, 64, 7> conflicts;
    conflicts.head = 1;
    conflicts.tail = 0;
    conflicts.array[0] = 1;
    OutputStream output;
    writeClause(output, {-1, -2});
    InputStream input1;
    InputStream input2;
    unsigned int coreCount = 0;

    const bool ok = extractUnsatCore(
        coreOutput, coreCount, assumptions, 1, trail, 2, lmd, lmmd,
        unitByCls, conflicts, -1, kNumLiterals, input1, input2, output);

    assert(!ok);
    assert(coreCount == 1);
    drain(input1);
    drain(input2);
    assert(output.empty());
}
}  // namespace

int main() {
    testConflictExcludesIrrelevantAssumption();
    testDirectAssumptionConflictTracesItsReason();
    testOppositeAssumptionsNeedNoReasonClause();
    testRootConflictHasEmptyCore();
    testRejectsTrailHeightAboveVariableCount();
    testRejectsOutOfRangeTrailLiteralBeforeIndexing();
    testRejectsCoreOutputBeyondAssumptionCapacity();
}
