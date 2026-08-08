#ifndef HOST_VALIDATION_H
#define HOST_VALIDATION_H

#include <charconv>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <vector>

inline std::vector<int> deduplicateAssumptions(
    const std::vector<int>& assumptions){
    std::set<int> seen;
    std::vector<int> normalized;
    normalized.reserve(assumptions.size());
    for(const int assumption : assumptions){
        if(seen.insert(assumption).second){
            normalized.push_back(assumption);
        }
    }
    return normalized;
}

inline bool collectPermanentRootLiterals(
    const std::vector<std::vector<int>>& clauses,
    const unsigned int permanentClauseCount,
    const unsigned int numVariables,
    std::vector<int>& roots,
    bool& conflict){
    if(permanentClauseCount > clauses.size()){
        return false;
    }
    std::vector<int> rootByVariable(numVariables + 1, 0);
    roots.clear();
    conflict = false;
    for(unsigned int i = 0; i < permanentClauseCount; i++){
        if(clauses[i].empty()){
            conflict = true;
            continue;
        }
        if(clauses[i].size() != 1){
            continue;
        }
        const int root = clauses[i][0];
        const uint64_t variable = root < 0
            ? static_cast<uint64_t>(-static_cast<int64_t>(root))
            : static_cast<uint64_t>(root);
        if(root == 0 || variable > numVariables){
            return false;
        }
        if(rootByVariable[variable] == 0){
            rootByVariable[variable] = root;
            roots.push_back(root);
        }else if(rootByVariable[variable] != root){
            conflict = true;
        }
    }
    for(unsigned int i = 0; i < permanentClauseCount; i++){
        bool satisfied = false;
        bool hasUnassigned = false;
        for(const int literal : clauses[i]){
            const int64_t wideLiteral = literal;
            const uint64_t variable = wideLiteral < 0
                ? static_cast<uint64_t>(-wideLiteral)
                : static_cast<uint64_t>(wideLiteral);
            if(literal == 0 || variable > numVariables){
                return false;
            }
            const int assigned = rootByVariable[variable];
            if(assigned == literal){
                satisfied = true;
                break;
            }
            if(assigned == 0){
                hasUnassigned = true;
            }
        }
        if(!satisfied && !hasUnassigned){
            conflict = true;
        }
    }
    return true;
}

inline bool parseUnsignedDecimal(const std::string& text,
    const unsigned int maximum, unsigned int& value){
    if(text.empty()){
        return false;
    }
    uint64_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed, 10);
    if(result.ec != std::errc() || result.ptr != end || parsed > maximum){
        return false;
    }
    value = static_cast<unsigned int>(parsed);
    return true;
}

inline bool parseSignedDecimal(const std::string& text, int& value){
    if(text.empty()){
        return false;
    }
    int64_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed, 10);
    if(result.ec != std::errc() || result.ptr != end ||
       parsed < std::numeric_limits<int>::min() ||
       parsed > std::numeric_limits<int>::max()){
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

inline bool validateTemporaryClauseLengths(
    const std::vector<std::vector<int>>& clauses,
    const unsigned int permanentClauseCount,
    const unsigned int temporaryClauseCount,
    const unsigned int maximumClauseLength,
    std::size_t& invalidClauseIndex){
    const uint64_t temporaryEnd =
        static_cast<uint64_t>(permanentClauseCount) + temporaryClauseCount;
    if(temporaryEnd > clauses.size()){
        return false;
    }
    for(std::size_t clauseIndex = permanentClauseCount;
            clauseIndex < temporaryEnd; clauseIndex++){
        if(clauses[clauseIndex].size() > maximumClauseLength){
            invalidClauseIndex = clauseIndex;
            return false;
        }
    }
    return true;
}

inline bool validateFpgaPartialModel(
    const unsigned int numVariables,
    const std::vector<std::vector<int>>& clauses,
    const std::vector<int>& assumptions,
    const std::vector<unsigned int>& decisionDomain,
    const std::vector<int>& model,
    std::string& error){
    if(model.size() > numVariables){
        error = "more assignments than variables";
        return false;
    }

    std::vector<int> assignment(numVariables + 1, 0);
    for(const int literal : model){
        const int64_t wideLiteral = literal;
        const uint64_t variable = wideLiteral < 0
            ? static_cast<uint64_t>(-wideLiteral)
            : static_cast<uint64_t>(wideLiteral);
        if(literal == 0 || variable > numVariables){
            error = "invalid model literal " + std::to_string(literal);
            return false;
        }
        if(assignment[variable] != 0){
            error = "duplicate model value for variable " +
                std::to_string(variable);
            return false;
        }
        assignment[variable] = literal > 0 ? 1 : -1;
    }

    std::vector<bool> inDomain(numVariables + 1, false);
    for(const unsigned int variable : decisionDomain){
        if(variable == 0 || variable > numVariables){
            error = "decision-domain variable outside the formula";
            return false;
        }
        inDomain[variable] = true;
        if(assignment[variable] == 0){
            error = "missing decision-domain variable " +
                std::to_string(variable);
            return false;
        }
    }

    for(const int assumption : assumptions){
        const unsigned int variable = assumption < 0
            ? static_cast<unsigned int>(-static_cast<int64_t>(assumption))
            : static_cast<unsigned int>(assumption);
        const int requiredPolarity = assumption > 0 ? 1 : -1;
        if(assumption == 0 || variable > numVariables ||
           assignment[variable] != requiredPolarity){
            error = "model does not contain assumption " +
                std::to_string(assumption);
            return false;
        }
    }

    for(std::size_t clauseIndex = 0; clauseIndex < clauses.size(); clauseIndex++){
        bool satisfied = false;
        bool extendable = false;
        for(const int literal : clauses[clauseIndex]){
            const unsigned int variable = literal < 0
                ? static_cast<unsigned int>(-static_cast<int64_t>(literal))
                : static_cast<unsigned int>(literal);
            if(literal == 0 || variable > numVariables){
                error = "stored clause contains an invalid literal";
                return false;
            }
            const int polarity = literal > 0 ? 1 : -1;
            if(assignment[variable] == polarity){
                satisfied = true;
                break;
            }
            if(assignment[variable] == 0 && !inDomain[variable]){
                extendable = true;
            }
        }
        if(!satisfied && !extendable){
            error = "clause " + std::to_string(clauseIndex + 1) +
                " is false under the FPGA partial model";
            return false;
        }
    }
    return true;
}

#endif
