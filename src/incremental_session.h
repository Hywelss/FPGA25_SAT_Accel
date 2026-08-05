#ifndef INCREMENTAL_SESSION_H
#define INCREMENTAL_SESSION_H

#include <stdexcept>
#include <string>
#include <vector>

struct IncrementalQueryPlan {
    bool reset;
    unsigned int previousPermanentCount;
    unsigned int newPermanentCount;
    unsigned int temporaryCount;
    unsigned int assumptionCount;
    unsigned int constraintActivation;
    std::string resetReason;
};

class IncrementalFormulaSession {
  public:
    IncrementalQueryPlan prepare(
        unsigned int numVariables,
        unsigned int constraintActivation,
        const std::vector<std::vector<int>>& permanentClauses,
        unsigned int temporaryCount,
        unsigned int assumptionCount) {
        if(numVariables == 0 || constraintActivation != numVariables){
            throw std::invalid_argument(
                "rIC3 constraint activation must be the final DIMACS variable");
        }

        bool reset = !initialized_;
        std::string reason = reset ? "first query" : "";
        if(initialized_ && numVariables != numVariables_){
            reset = true;
            reason = "variable layout changed";
        }
        if(initialized_ && !reset && permanentClauses.size() < permanentClauses_.size()){
            reset = true;
            reason = "permanent clause prefix shrank";
        }
        if(initialized_ && !reset){
            for(std::size_t i = 0; i < permanentClauses_.size(); i++){
                if(permanentClauses[i] != permanentClauses_[i]){
                    reset = true;
                    reason = "permanent clause prefix changed";
                    break;
                }
            }
        }

        const unsigned int previousCount = reset ? 0 : permanentClauses_.size();
        const unsigned int newCount = permanentClauses.size() - previousCount;
        initialized_ = true;
        numVariables_ = numVariables;
        constraintActivation_ = constraintActivation;
        permanentClauses_ = permanentClauses;

        return {
            reset,
            previousCount,
            newCount,
            temporaryCount,
            assumptionCount,
            constraintActivation,
            reason,
        };
    }

    void clear() {
        initialized_ = false;
        numVariables_ = 0;
        constraintActivation_ = 0;
        permanentClauses_.clear();
    }

  private:
    bool initialized_ = false;
    unsigned int numVariables_ = 0;
    unsigned int constraintActivation_ = 0;
    std::vector<std::vector<int>> permanentClauses_;
};

#endif
