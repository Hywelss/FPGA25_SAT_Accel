#ifndef FIXED_LITERAL_H
#define FIXED_LITERAL_H

enum FixedLiteralStatus {
    FIXED_LITERAL_APPEND = 0,
    FIXED_LITERAL_ALREADY_ASSIGNED = 1,
    FIXED_LITERAL_CONFLICT = 2,
    FIXED_LITERAL_INVALID = 3,
};

inline FixedLiteralStatus classifyFixedLiteral(const int* fixedLiterals,
    const unsigned int fixedLiteralCount, const unsigned int numVariables,
    const int literal){
    if(literal == 0 || fixedLiteralCount > numVariables){
        return FIXED_LITERAL_INVALID;
    }
    const unsigned int variable = literal < 0 ? -literal : literal;
    if(variable == 0 || variable > numVariables){
        return FIXED_LITERAL_INVALID;
    }

    for(unsigned int i = 0; i < fixedLiteralCount; i++){
        #pragma HLS loop_tripcount min=0 max=1024
        const int existing = fixedLiterals[i];
        const unsigned int existingVariable = existing < 0 ? -existing : existing;
        if(existingVariable == variable){
            return existing == literal
                ? FIXED_LITERAL_ALREADY_ASSIGNED
                : FIXED_LITERAL_CONFLICT;
        }
    }
    return fixedLiteralCount < numVariables
        ? FIXED_LITERAL_APPEND
        : FIXED_LITERAL_INVALID;
}

#endif
