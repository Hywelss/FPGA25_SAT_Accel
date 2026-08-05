#include "incremental_session.h"
#include "query_clause_layout.h"

#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
    const auto layout = buildQueryClauseLayout(
        {{1, -2}, {3, 4, -5, 6, 7}, {-8}}, 3);
    assert((layout.literals == std::vector<int>{1, -2, 3, 4, -5, 6, 7, -8}));
    assert(layout.clauses.size() == 3);
    assert(layout.clauses[0].addressStart == 0);
    assert(layout.clauses[0].numElements == 2);
    assert(layout.clauses[1].addressStart == 2);
    assert(layout.clauses[1].numElements == 5);
    assert(layout.clauses[2].addressStart == 7);
    assert(layout.clauses[2].numElements == 1);

    IncrementalFormulaSession session;

    const auto first = session.prepare(3, 3, {{1, -2}}, 1, 2);
    assert(first.reset);
    assert(first.previousPermanentCount == 0);
    assert(first.newPermanentCount == 1);

    const auto next = session.prepare(3, 3, {{1, -2}}, 2, 3);
    assert(!next.reset);
    assert(next.previousPermanentCount == 1);
    assert(next.newPermanentCount == 0);
    assert(next.temporaryCount == 2);

    const auto grown = session.prepare(4, 4, {{1, -2}, {2, 3}}, 0, 1);
    assert(grown.reset);
    assert(grown.newPermanentCount == 2);

    const auto changed = session.prepare(4, 4, {{1, 2}, {2, 3}}, 0, 0);
    assert(changed.reset);
    assert(changed.newPermanentCount == 2);

    bool rejected = false;
    try {
        session.prepare(4, 3, {}, 0, 0);
    } catch(const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}
