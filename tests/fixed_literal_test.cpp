#include <cassert>

#include "fixed_literal.h"

int main(){
    const int fixed[] = {1, -3};
    assert(classifyFixedLiteral(fixed, 2, 4, 2) == FIXED_LITERAL_APPEND);
    assert(classifyFixedLiteral(fixed, 2, 4, 1) ==
        FIXED_LITERAL_ALREADY_ASSIGNED);
    assert(classifyFixedLiteral(fixed, 2, 4, -1) == FIXED_LITERAL_CONFLICT);
    assert(classifyFixedLiteral(fixed, 2, 4, 0) == FIXED_LITERAL_INVALID);
    assert(classifyFixedLiteral(fixed, 2, 4, 5) == FIXED_LITERAL_INVALID);

    const int full[] = {1, 2};
    assert(classifyFixedLiteral(full, 2, 2, -2) == FIXED_LITERAL_CONFLICT);
    assert(classifyFixedLiteral(full, 2, 2, 2) ==
        FIXED_LITERAL_ALREADY_ASSIGNED);
    assert(classifyFixedLiteral(full, 3, 2, 1) == FIXED_LITERAL_INVALID);
}
