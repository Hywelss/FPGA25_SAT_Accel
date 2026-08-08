#include <cassert>

#include "learn.h"
#include "temporary_clause_tracker.h"

int main() {
    static ap_uint<1> mask[_FPGA_MAX_CLAUSES];
    unsigned int count = 123;

    clearTemporaryClauseTracker(mask, count);
    assert(count == 0);
    assert(mask[0] == 0);
    assert(mask[_FPGA_MAX_CLAUSES - 1] == 0);

    assert(trackTemporaryClause(mask, count, 7));
    assert(count == 1);
    assert(mask[7] == 1);

    assert(trackTemporaryClause(mask, count, 7));
    assert(count == 1);

    assert(trackTemporaryClause(mask, count, _FPGA_MAX_CLAUSES - 1));
    assert(count == 2);
    assert(!trackTemporaryClause(mask, count, -1));
    assert(!trackTemporaryClause(mask, count, _FPGA_MAX_CLAUSES));
    assert(count == 2);

    const unsigned int variableCount = 9;
    const unsigned int activation = 4;
    assert(isConstraintActivationLiteral(4, activation));
    assert(isConstraintActivationLiteral(-4, activation));
    assert(!isConstraintActivationLiteral(9, activation));
    assert(activation != variableCount);

    clearTemporaryClauseTracker(mask, count);
    assert(count == 0);
    assert(mask[7] == 0);
    assert(mask[_FPGA_MAX_CLAUSES - 1] == 0);

    hls::stream<ap_axiu<96,0,0,0>> deleteIDs;
    unsigned int remaining = 0;
    assert(trackTemporaryClause(mask, count, 7));
    assert(trackTemporaryClause(mask, count, 11));
    assert(emitTemporaryDeleteIDs(mask, count, deleteIDs, remaining));
    assert(remaining == 0);
    assert(deleteIDs.read().data.range(31,0) == 7);
    assert(deleteIDs.read().data.range(31,0) == 11);
    assert(deleteIDs.empty());
    assert(mask[7] == 0 && mask[11] == 0);

    clearTemporaryClauseTracker(mask, count);
    assert(trackTemporaryClause(mask, count, 9));
    assert(!emitTemporaryDeleteIDs(mask, 2, deleteIDs, remaining));
    assert(remaining == 0);
    assert(deleteIDs.read().data.range(31,0) == 9);
    assert(deleteIDs.read().data.range(31,0) == _FPGA_MAX_CLAUSES);
    assert(deleteIDs.empty());

    clearTemporaryClauseTracker(mask, count);
    assert(trackTemporaryClause(mask, count, 3));
    assert(trackTemporaryClause(mask, count, 5));
    assert(!emitTemporaryDeleteIDs(mask, 1, deleteIDs, remaining));
    assert(remaining == 1);
    assert(deleteIDs.read().data.range(31,0) == 3);
    assert(deleteIDs.empty());
    assert(mask[3] == 0 && mask[5] == 1);
}
