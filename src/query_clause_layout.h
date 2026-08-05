#ifndef QUERY_CLAUSE_LAYOUT_H
#define QUERY_CLAUSE_LAYOUT_H

#include <cstddef>
#include <stdexcept>
#include <vector>

struct QueryClauseSpan {
    unsigned int addressStart;
    unsigned int numElements;
};

struct QueryClauseLayout {
    std::vector<int> literals;
    std::vector<QueryClauseSpan> clauses;
};

inline QueryClauseLayout buildQueryClauseLayout(
    const std::vector<std::vector<int>>& clauses,
    std::size_t clauseCount) {
    if(clauseCount > clauses.size()){
        throw std::invalid_argument("query clause count exceeds parsed clauses");
    }

    QueryClauseLayout layout;
    layout.clauses.reserve(clauseCount);
    for(std::size_t clauseID = 0; clauseID < clauseCount; clauseID++){
        const auto& clause = clauses[clauseID];
        layout.clauses.push_back({
            static_cast<unsigned int>(layout.literals.size()),
            static_cast<unsigned int>(clause.size()),
        });
        layout.literals.insert(layout.literals.end(), clause.begin(), clause.end());
    }
    return layout;
}

#endif
