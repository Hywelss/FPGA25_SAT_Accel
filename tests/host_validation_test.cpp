#include <cassert>
#include <climits>
#include <iostream>
#include <string>
#include <vector>

#include "host_validation.h"

int main(){
    assert((deduplicateAssumptions({1, 1, -1, 2, -1}) ==
        std::vector<int>{1, -1, 2}));

    std::vector<int> roots;
    bool rootConflict = false;
    assert(collectPermanentRootLiterals(
        {{1}, {1}, {-1}, {2, 3}, {-2}}, 5, 3, roots, rootConflict));
    assert((roots == std::vector<int>{1, -2}));
    assert(rootConflict);
    assert(!collectPermanentRootLiterals({{4}}, 1, 3,
        roots, rootConflict));
    assert(collectPermanentRootLiterals(
        {{1}, {2}, {-1, -2}}, 3, 2, roots, rootConflict));
    assert(rootConflict);
    assert(collectPermanentRootLiterals(
        {{1}, {1, -2}}, 2, 2, roots, rootConflict));
    assert(!rootConflict);
    assert(collectPermanentRootLiterals(
        {{}}, 1, 1, roots, rootConflict));
    assert(rootConflict);
    assert(!collectPermanentRootLiterals(
        {{1, 4}}, 1, 3, roots, rootConflict));
    assert(!collectPermanentRootLiterals(
        {{1, 0}}, 1, 3, roots, rootConflict));

    unsigned int unsignedValue = 0;
    assert(parseUnsignedDecimal("0", 10, unsignedValue) && unsignedValue == 0);
    assert(parseUnsignedDecimal("10", 10, unsignedValue) && unsignedValue == 10);
    assert(!parseUnsignedDecimal("11", 10, unsignedValue));
    assert(!parseUnsignedDecimal("-1", 10, unsignedValue));
    assert(!parseUnsignedDecimal("1x", 10, unsignedValue));
    assert(!parseUnsignedDecimal("", 10, unsignedValue));

    int signedValue = 0;
    assert(parseSignedDecimal("-10", signedValue) && signedValue == -10);
    assert(parseSignedDecimal(std::to_string(INT_MAX), signedValue));
    assert(parseSignedDecimal(std::to_string(INT_MIN), signedValue));
    assert(!parseSignedDecimal("2147483648", signedValue));
    assert(!parseSignedDecimal("--1", signedValue));

    std::size_t invalidTemporaryClause = 99;
    std::vector<std::vector<int>> boundedTemporaryClauses = {
        {1, 2},
        std::vector<int>(1024, 1),
    };
    assert(validateTemporaryClauseLengths(boundedTemporaryClauses, 1, 1,
        1024, invalidTemporaryClause));
    boundedTemporaryClauses[1].push_back(1);
    assert(!validateTemporaryClauseLengths(boundedTemporaryClauses, 1, 1,
        1024, invalidTemporaryClause));
    assert(invalidTemporaryClause == 1);
    assert(!validateTemporaryClauseLengths(boundedTemporaryClauses, 2, 1,
        1024, invalidTemporaryClause));

    const std::vector<std::vector<int>> clauses = {
        {1, 2},
        {-1, 3},
        {-3, 4},
    };
    const std::vector<int> assumptions = {1};
    const std::vector<unsigned int> domain = {1, 3};
    std::string error;

    assert(validateFpgaPartialModel(4, clauses, assumptions, domain,
        {1, 3}, error));
    assert(!validateFpgaPartialModel(4, clauses, assumptions, domain,
        {1, -3}, error));
    assert(error.find("clause 2") != std::string::npos);
    assert(!validateFpgaPartialModel(4, clauses, assumptions, domain,
        {-1, 3}, error));
    assert(error.find("assumption") != std::string::npos);
    assert(!validateFpgaPartialModel(4, clauses, assumptions, domain,
        {1}, error));
    assert(error.find("missing decision-domain") != std::string::npos);
    assert(!validateFpgaPartialModel(4, clauses, assumptions, domain,
        {1, 3, -3}, error));
    assert(error.find("duplicate") != std::string::npos);

    const std::vector<std::vector<int>> emptyClause = {{}};
    assert(!validateFpgaPartialModel(1, emptyClause, {}, {1}, {1}, error));

    std::cout << "Host validation tests passed\n";
    return 0;
}
