#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <math.h>
#include <set>
#include <chrono>
#include <queue>
#include <cstdlib>
#include <thread>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

#include "data_structures.h"
#include "fpga_solver.h"
#include "host_validation.h"
#include "incremental_session.h"
#include "query_clause_layout.h"
#include "xcl2.hpp"

std::string comma(uint64_t n) {
    std::string result = std::to_string(n);
    for(int i = result.size() - 3; i > 0; i -= 3){
        result.insert(i,",");
    }
    return result;		
}

double safeRatio(const uint64_t numerator, const uint64_t denominator){
    return denominator == 0 ? 0.0 : (double)numerator/(double)denominator;
}

std::string timeString(){
	std::time_t rawtime;
	struct tm* timeinfo;
	char buffer[256];

	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 256,"%F %T", timeinfo);
	std::string printTime(buffer);

	return printTime;
}

bool compareFuncInput(int a, int b){
    return abs(a) < abs(b);
}

bool compareFunc(int a, int b){
    return abs(a) < abs(b);
}

void cleanUp(problemData pd){
    free(pd.clauseStore);
    free(pd.cmd);
    free(pd.queryClauseStore);
    free(pd.queryCmd);
    free(pd.litStore);
    free(pd.answerStack);
    free(pd.lmd);
    free(pd.clsStates);
    free(pd.decisionDomain);
    free(pd.assumptions);
    free(pd.clsToLitStorePos);
    free(pd.litToClsStorePos);
}

template <typename T>
T* allocateAligned(std::size_t count){
    void* memory = nullptr;
    const int error = posix_memalign(&memory, 4096, count * sizeof(T));
    if(error != 0){
        std::cerr << "Failed to allocate " << count * sizeof(T)
                  << " bytes of aligned host memory, error number: " << error << "\n";
        exit(EXIT_FAILURE);
    }
    return static_cast<T*>(memory);
}

void parseJSON(std::string filePath, rapidjson::Document& configuration){
    std::ifstream file(filePath); 
    if(!file.is_open()){
        std::cerr << "Could not open configuration file: " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
  
    rapidjson::IStreamWrapper isw(file);
    configuration.ParseStream(isw);
  
    if(configuration.HasParseError()) { 
        std::cout << "Error parsing JSON: " << configuration.GetParseError() << "\n"; 
        exit(EXIT_FAILURE);
    } 
}

void parseDIMACS(std::string filePath, problemData& pd, const rapidjson::Document& configuration){
    std::ifstream input(filePath);

    if(!input.is_open()){
        std::cout << "Could not open CNF file" << "\n";
        exit(EXIT_FAILURE);
    }
    std::string line;
    int state = 0;
    int counter = 0;

    std::vector<std::vector<lit>> litStore[2];
    std::vector<std::vector<cls>> clsStore;
    std::vector<std::vector<unsigned int>> clauseLiteralAddresses;
    std::set<lit> decisionStore;
    std::set<lit> checkLit;
    std::vector<unsigned int> decisionDomain;
    bool hasDecisionDomain = false;
    bool hasQueryLayout = false;
    unsigned int numPermanentClauses = 0;
    unsigned int numTemporaryClauses = 0;
    unsigned int numAssumptions = 0;
    unsigned int constraintActivation = 0;
    unsigned int parsedLiteralCount = 0;

    while(std::getline(input, line)){
        std::string token;
        std::vector<std::string> tokens;
        std::istringstream iss(line);

        while(iss >> token) {
            tokens.push_back(token);
        }

        if(tokens.empty()){
            continue;
        }
        if(tokens[0] == "c"){
            if(tokens.size() >= 3 && tokens[1] == "ind-domain"){
                if(hasDecisionDomain){
                    std::cerr << "DIMACS contains more than one ind-domain declaration in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
                hasDecisionDomain = true;
                bool terminated = false;
                for(unsigned int i = 2; i < tokens.size(); i++){
                    unsigned int variable = 0;
                    if(!parseUnsignedDecimal(tokens[i], _FPGA_MAX_LITERALS, variable)){
                        std::cerr << "Invalid variable in ind-domain declaration in " << filePath << "\n";
                        exit(EXIT_FAILURE);
                    }
                    if(variable == 0){
                        if(i != tokens.size() - 1){
                            std::cerr << "Invalid ind-domain declaration in " << filePath << "\n";
                            exit(EXIT_FAILURE);
                        }
                        terminated = true;
                        break;
                    }
                    decisionDomain.push_back(variable);
                }
                if(!terminated){
                    std::cerr << "ind-domain declaration must end with 0 in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
            }else if(tokens.size() == 3 && tokens[1] == "ind-activation"){
                if(constraintActivation != 0){
                    std::cerr << "DIMACS contains more than one ind-activation declaration in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
                if(!parseUnsignedDecimal(tokens[2], _FPGA_MAX_LITERALS,
                        constraintActivation) || constraintActivation == 0){
                    std::cerr << "Invalid ind-activation declaration in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
            }else if(tokens.size() == 5 && tokens[1] == "ind-query-v1"){
                if(hasQueryLayout){
                    std::cerr << "DIMACS contains more than one ind-query-v1 declaration in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
                hasQueryLayout = true;
                if(!parseUnsignedDecimal(tokens[2], _FPGA_MAX_CLAUSES, numPermanentClauses) ||
                   !parseUnsignedDecimal(tokens[3], _FPGA_MAX_CLAUSES, numTemporaryClauses) ||
                   !parseUnsignedDecimal(tokens[4], _FPGA_MAX_LITERALS, numAssumptions)){
                    std::cerr << "Invalid ind-query-v1 declaration in " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
            }
            continue;
        }else if(state == 0 && tokens[0] == "p"){
            if(tokens.size() != 4 || tokens[1] != "cnf"){
                std::cerr << "Invalid DIMACS header in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            if(!parseUnsignedDecimal(tokens[2], _FPGA_MAX_LITERALS,
                    pd.md.numLiterals) || pd.md.numLiterals == 0){
                std::cerr << "DIMACS variable count must be between 1 and "
                          << _FPGA_MAX_LITERALS << " in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            if(!parseUnsignedDecimal(tokens[3], _FPGA_MAX_CLAUSES,
                    pd.md.numClauses)){
                std::cerr << "DIMACS clause count exceeds " << _FPGA_MAX_CLAUSES
                          << " in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }

            litStore[0] = std::vector<std::vector<lit>>(pd.md.numLiterals);
            litStore[1] = std::vector<std::vector<lit>>(pd.md.numLiterals);
            clsStore = std::vector<std::vector<cls>>(pd.md.numClauses);
            clauseLiteralAddresses = std::vector<std::vector<unsigned int>>(pd.md.numClauses);
            state = 1;
        }else if(state == 1){
            if(counter >= (int)clsStore.size()){
                std::cerr << "DIMACS contains more clauses than declared in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            unsigned int length = tokens.size();
            if(tokens[tokens.size()-1] == "0"){
                length--;
            }
            for(unsigned int i = 0; i < length; i++){
                lit getLit = 0;
                if(!parseSignedDecimal(tokens[i], getLit)){
                    std::cerr << "Invalid DIMACS literal in " << filePath
                              << ": " << tokens[i] << "\n";
                    exit(EXIT_FAILURE);
                }
                const uint64_t variable = getLit < 0
                    ? static_cast<uint64_t>(-static_cast<int64_t>(getLit))
                    : static_cast<uint64_t>(getLit);
                if(getLit == 0 || variable > pd.md.numLiterals){
                    std::cerr << "DIMACS literal out of range in " << filePath << ": " << getLit << "\n";
                    exit(EXIT_FAILURE);
                }
                const bool isIn = checkLit.find(getLit) != checkLit.end();
                if(!isIn){
                    if(parsedLiteralCount >= _HOST_MAX_CLAUSE_ELEMENTS){
                        std::cerr << "DIMACS contains too many literal occurrences for the FPGA in "
                                  << filePath << "\n";
                        exit(EXIT_FAILURE);
                    }
                    clsStore[counter].push_back(getLit);
                    parsedLiteralCount++;
                }
                checkLit.insert(getLit);
            }

            if(tokens[tokens.size()-1] == "0"){
                checkLit.clear();
                std::sort(clsStore[counter].begin(), clsStore[counter].end(), compareFuncInput);
                counter++;
            }else{
                continue;
            }
        }else{
            std::cerr << "DIMACS content appears before its header in " << filePath << "\n";
            exit(EXIT_FAILURE);
        }
    }
    input.close(); 

    if(state == 0 || !checkLit.empty() || counter != (int)clsStore.size()){
        std::cerr << "DIMACS clause count does not match its header in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    if(hasQueryLayout && numPermanentClauses + numTemporaryClauses + numAssumptions != pd.md.numClauses){
        std::cerr << "ind-query-v1 clause counts do not match the DIMACS header in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    if(numAssumptions > pd.md.numLiterals){
        std::cerr << "ind-query-v1 contains more assumptions than variables in "
                  << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    std::vector<int> normalizedAssumptions;
    if(hasQueryLayout){
        const unsigned int assumptionBegin =
            numPermanentClauses + numTemporaryClauses;
        std::vector<int> parsedAssumptions;
        parsedAssumptions.reserve(numAssumptions);
        for(unsigned int i = 0; i < numAssumptions; i++){
            if(clsStore[assumptionBegin + i].size() != 1){
                std::cerr << "SAT-Accel assumptions must be unit clauses in "
                          << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            parsedAssumptions.push_back(clsStore[assumptionBegin + i][0]);
        }
        normalizedAssumptions = deduplicateAssumptions(parsedAssumptions);
    }
    if(!hasQueryLayout){
        numPermanentClauses = pd.md.numClauses;
        numTemporaryClauses = 0;
        numAssumptions = 0;
    }
    pd.md.numPermanentClauses = numPermanentClauses;
    pd.md.numTemporaryClauses = numTemporaryClauses;
    pd.md.numAssumptions = hasQueryLayout
        ? normalizedAssumptions.size()
        : numAssumptions;
    pd.md.constraintActivation = constraintActivation;
    pd.md.numStoredClauses = numPermanentClauses + numTemporaryClauses;

    if(hasQueryLayout){
        if(constraintActivation == 0 || constraintActivation > pd.md.numLiterals){
            std::cerr << "ind-activation is missing or outside the variable range in " << filePath << "\n";
            exit(EXIT_FAILURE);
        }
        const unsigned int temporaryBegin = numPermanentClauses;
        const unsigned int temporaryEnd = temporaryBegin + numTemporaryClauses;
        for(unsigned int i = 0; i < numPermanentClauses; i++){
            if(std::any_of(clsStore[i].begin(), clsStore[i].end(),
                   [constraintActivation](lit literal){
                       return (unsigned int)abs(literal) == constraintActivation;
                   })){
                std::cerr << "The reusable constraint activation must be fresh in "
                          << filePath << "\n";
                exit(EXIT_FAILURE);
            }
        }
        for(unsigned int i = temporaryBegin; i < temporaryEnd; i++){
            if(clsStore[i].size() < 2){
                std::cerr << "rIC3 temporary clauses must contain a literal and the constraint activation in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            if(std::find(clsStore[i].begin(), clsStore[i].end(), -(int)constraintActivation) == clsStore[i].end()){
                std::cerr << "Temporary clause is missing the negative constraint activation literal in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
        }
        const unsigned int assumptionBegin = temporaryEnd;
        if(numTemporaryClauses > 0 &&
           (numAssumptions == 0 || clsStore[assumptionBegin][0] != (int)constraintActivation)){
            std::cerr << "Temporary clauses require the positive constraint activation as the first assumption in " << filePath << "\n";
            exit(EXIT_FAILURE);
        }
    }

    if(!hasDecisionDomain){
        decisionDomain.reserve(pd.md.numLiterals);
        for(unsigned int variable = 1; variable <= pd.md.numLiterals; variable++){
            decisionDomain.push_back(variable);
        }
    }
    std::sort(decisionDomain.begin(), decisionDomain.end());
    decisionDomain.erase(std::unique(decisionDomain.begin(), decisionDomain.end()), decisionDomain.end());
    if(decisionDomain.empty()){
        std::cerr << "DIMACS decision domain is empty in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    if(decisionDomain.back() > pd.md.numLiterals){
        std::cerr << "DIMACS decision domain exceeds the variable count in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    if(hasQueryLayout &&
       !std::binary_search(decisionDomain.begin(), decisionDomain.end(), constraintActivation)){
        std::cerr << "ind-domain is missing the constraint activation variable in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    pd.md.numDomainLiterals = decisionDomain.size();

    std::vector<int> histogram[2];
    histogram[0] = std::vector<int>(18,0);
    histogram[1] = std::vector<int>(18,0);

    std::vector<int> permanentRoots;
    bool initialRootConflict = false;
    if(!collectPermanentRootLiterals(clsStore, pd.md.numPermanentClauses,
            pd.md.numLiterals, permanentRoots, initialRootConflict)){
        std::cerr << "Could not collect permanent root literals in " << filePath << "\n";
        exit(EXIT_FAILURE);
    }
    decisionStore.insert(permanentRoots.begin(), permanentRoots.end());
    for(unsigned int i = 0; i < pd.md.numStoredClauses; i++){
        for(unsigned int j = 0; j < clsStore[i].size(); j++){
            if(clsStore[i][j] > 0){
                litStore[0][abs(clsStore[i][j])-1].push_back(i+1);
            }else{
                litStore[1][abs(clsStore[i][j])-1].push_back(-(i+1));
            }
        }
    }

    memset(pd.md.miscCounters,0,sizeof(int)*(256));
    pd.md.miscCounters[15] = initialRootConflict ? 1 : 0;

    const QueryClauseLayout queryLayout =
        buildQueryClauseLayout(clsStore, pd.md.numStoredClauses);
    pd.md.queryClauseElements = queryLayout.literals.size();
    pd.clauseStore = allocateAligned<cls>(_HOST_MAX_CLAUSE_ELEMENTS);
    pd.cmd = allocateAligned<clauseMetaData>(_FPGA_MAX_CLAUSES);
    pd.queryClauseStore = allocateAligned<lit>(
        std::max(1u, pd.md.queryClauseElements));
    pd.queryCmd = allocateAligned<clauseMetaData>(
        std::max(1u, pd.md.numStoredClauses));
    pd.litStore = allocateAligned<lit>(_HOST_MAX_LITERAL_ELEMENTS);
    pd.answerStack = allocateAligned<lit>(pd.md.numLiterals);
    pd.lmd = allocateAligned<literalMetaDataPCIE>(pd.md.numLiterals);
    pd.clsStates = allocateAligned<clsStatePCIE>(_FPGA_MAX_CLAUSES);
    pd.decisionDomain = allocateAligned<unsigned int>(pd.md.numLiterals);
    pd.assumptions = allocateAligned<lit>(std::max(1u, pd.md.numAssumptions));
    pd.clsToLitStorePos = allocateAligned<unsigned int>(_HOST_MAX_CLAUSE_ELEMENTS);
    pd.litToClsStorePos = allocateAligned<unsigned int>(_HOST_MAX_LITERAL_ELEMENTS);

    memset(pd.cmd,0,_FPGA_MAX_CLAUSES*sizeof(clauseMetaData));
    memset(pd.queryClauseStore,0,
        std::max(1u, pd.md.queryClauseElements)*sizeof(lit));
    memset(pd.queryCmd,0,
        std::max(1u, pd.md.numStoredClauses)*sizeof(clauseMetaData));
    memset(pd.litStore,0,_HOST_MAX_LITERAL_ELEMENTS*sizeof(lit));
    memset(pd.answerStack,0,pd.md.numLiterals*sizeof(lit));
    memset(pd.clsStates,0,_FPGA_MAX_CLAUSES*sizeof(clsStatePCIE));
    memset(pd.clauseStore,0,_HOST_MAX_CLAUSE_ELEMENTS*sizeof(cls));
    memset(pd.decisionDomain,0,pd.md.numLiterals*sizeof(unsigned int));
    memset(pd.assumptions,0,std::max(1u, pd.md.numAssumptions)*sizeof(lit));
    memset(pd.clsToLitStorePos,0,_HOST_MAX_CLAUSE_ELEMENTS*sizeof(unsigned int));
    memset(pd.litToClsStorePos,0,_HOST_MAX_LITERAL_ELEMENTS*sizeof(unsigned int));
    std::copy(queryLayout.literals.begin(), queryLayout.literals.end(),
        pd.queryClauseStore);
    for(unsigned int i = 0; i < pd.md.numStoredClauses; i++){
        pd.queryCmd[i].addressStart = queryLayout.clauses[i].addressStart;
        pd.queryCmd[i].numElements = queryLayout.clauses[i].numElements;
    }
    std::copy(decisionDomain.begin(), decisionDomain.end(), pd.decisionDomain);
    std::copy(normalizedAssumptions.begin(), normalizedAssumptions.end(),
        pd.assumptions);

    unsigned int index1D = 0;
    unsigned int index = 0;
    unsigned int preSolvedCount = 0;
    
    for(unsigned int i = 0; i < pd.md.numStoredClauses; i++){
        pd.cmd[i].addressStart = index1D;
        pd.cmd[i].numElements = clsStore[i].size();
        lit earliestSolved = 0;
        int xorCompact = 0;
        index = 0;
        bool needZero = true;

        for(unsigned int j = 0; j < clsStore[i].size(); j++){
            if(index1D >= _HOST_MAX_CLAUSE_ELEMENTS){
                std::cerr << "Exceeded clause storage while inserting clause " << i + 1 << "\n";
                exit(EXIT_FAILURE);
            }
            clauseLiteralAddresses[i].push_back(index1D);
            pd.clauseStore[index1D] = clsStore[i][j];
            xorCompact ^= pd.clauseStore[index1D];
            const bool isIn = decisionStore.find(clsStore[i][j]) != decisionStore.end();
            if(isIn && earliestSolved == 0){
                earliestSolved = clsStore[i][j];
                preSolvedCount++;
            }
            index1D++;
            index++;
            
            if(index == 4-1){
                if(index1D >= _HOST_MAX_CLAUSE_ELEMENTS){
                    std::cerr << "Exceeded clause storage while linking clause " << i + 1 << "\n";
                    exit(EXIT_FAILURE);
                }
                if(j != clsStore[i].size()-1){
                    pd.clauseStore[index1D] = index1D + 1;
                }else{
                    pd.clauseStore[index1D] = 0;
                    needZero = false;
                }
                index1D++;
                index = 0;
            }
        }

        if(needZero){
            for(unsigned int j = 0; j < 4-index; j++){
                if(index1D >= _HOST_MAX_CLAUSE_ELEMENTS){
                    std::cerr << "Exceeded clause storage while padding clause " << i + 1 << "\n";
                    exit(EXIT_FAILURE);
                }
                pd.clauseStore[index1D] = 0;
                index1D++;
            }
        }

        pd.clsStates[i].remainingUnassigned = pd.cmd[i].numElements;
        pd.clsStates[i].compressedList = xorCompact;  
    }
    if(index1D%configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint() != 0){
        unsigned int fill = configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint() - (index1D%configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint());
        for(unsigned int j = 0; j < fill; j++){
            if(index1D >= _HOST_MAX_CLAUSE_ELEMENTS){
                std::cerr << "Exceeded clause storage while padding its final page\n";
                exit(EXIT_FAILURE);
            }
            pd.clauseStore[index1D] = 0;
            index1D++;
        }
    }
    pd.md.clauseElements = index1D;

    index1D = 0;
    for(unsigned int i = 0; i < litStore[0].size(); i++){
        pd.lmd[i].compactlmd = 0;
        LMD_SHORTEST_CLS_LENGTH(pd.lmd[i].compactlmd) = 0;
        LMD_UNIT_BY_LIT(pd.lmd[i].compactlmd) = 0;
        LMD_NUM_ELE(pd.lmd[i].compactlmd, 0) = litStore[0][i].size();
        LMD_NUM_ELE(pd.lmd[i].compactlmd, 1) = litStore[1][i].size();
        LMD_FREE_SPACE(pd.lmd[i].compactlmd, 0) = 0;
        LMD_FREE_SPACE(pd.lmd[i].compactlmd, 1) = 0;
        LMD_INSERT_LVL(pd.lmd[i].compactlmd) = 0;
        LMD_DEC_LVL(pd.lmd[i].compactlmd) = 0;
        LMD_PHASE(pd.lmd[i].compactlmd) = !(configuration["_HOST_POSITIVE_LIT_PHASE_VAL"].GetBool());
        pd.answerStack[i] = 0;

        bool bothEmpty = true;
        for(unsigned int a = 0; a < 2; a++){
            if(litStore[a][i].size() != 0){
                bothEmpty = false;
            }
        }
        if(bothEmpty && !hasQueryLayout){
            LMD_ADDR_START(pd.lmd[i].compactlmd, 0) = index1D;
            LMD_ADDR_START(pd.lmd[i].compactlmd, 1) = index1D;
            LMD_LATEST_PAGE(pd.lmd[i].compactlmd, 0) = index1D;
            LMD_LATEST_PAGE(pd.lmd[i].compactlmd, 1) = index1D;
            decisionStore.insert(i+1);
            continue;
        }
        for(unsigned int a = 0; a < 2; a++){
            LMD_ADDR_START(pd.lmd[i].compactlmd, a) = index1D;
            LMD_LATEST_PAGE(pd.lmd[i].compactlmd, a) = index1D;
            
            index = 0;
            if(litStore[a][i].size() >= 17){
                histogram[a][17]++;
            }else{
                histogram[a][litStore[a][i].size()]++;
            }

            for(unsigned int j = 0; j < litStore[a][i].size(); j++){
                const unsigned int clauseIndex = abs(litStore[a][i][j])-1;
                const lit occurrence = a == 0 ? (lit)(i+1) : -(lit)(i+1);
                const auto position = std::find(clsStore[clauseIndex].begin(),
                    clsStore[clauseIndex].end(), occurrence);
                if(position == clsStore[clauseIndex].end()){
                    std::cerr << "Could not map clause occurrence while parsing " << filePath << "\n";
                    exit(EXIT_FAILURE);
                }
                const unsigned int literalIndex = position - clsStore[clauseIndex].begin();
                const unsigned int clauseAddress = clauseLiteralAddresses[clauseIndex][literalIndex];
                if(index1D >= _HOST_MAX_LITERAL_ELEMENTS){
                    std::cerr << "Exceeded literal storage while inserting variable "
                              << i + 1 << "\n";
                    exit(EXIT_FAILURE);
                }
                pd.clsToLitStorePos[clauseAddress] = index1D;
                pd.litToClsStorePos[index1D] = clauseAddress;
                pd.litStore[index1D] = abs(litStore[a][i][j]);
                index1D++;
                index++;

                if(index == configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-2){
                    if(index1D >= _HOST_MAX_LITERAL_ELEMENTS){
                        std::cerr << "Exceeded literal storage while terminating a page\n";
                        exit(EXIT_FAILURE);
                    }
                    pd.litStore[index1D] = 0;
                    index1D++;
                    if(index1D >= _HOST_MAX_LITERAL_ELEMENTS){
                        std::cerr << "Exceeded literal storage while linking a page\n";
                        exit(EXIT_FAILURE);
                    }
                    pd.litStore[index1D] = index1D + 1;
                    index1D++;
                    LMD_LATEST_PAGE(pd.lmd[i].compactlmd, a) = index1D;

                    index = 0;
                }
            
            }

            for(unsigned int j = 0; j < configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-index; j++){
                if(index1D >= _HOST_MAX_LITERAL_ELEMENTS){
                    std::cerr << "Exceeded literal storage while padding variable "
                              << i + 1 << "\n";
                    exit(EXIT_FAILURE);
                }
                pd.litStore[index1D] = 0;
                index1D++;
            }
            
            LMD_FREE_SPACE(pd.lmd[i].compactlmd,a) = configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-index-2;
        }
    }

    std::set<lit>::iterator itr = decisionStore.begin();
    for (unsigned int i = 0; itr != decisionStore.end(); itr++, i++){
        pd.answerStack[i] = *itr;
    }

    pd.md.decayFactor = configuration["_HOST_DECAY_FACTOR"].GetDouble();
    pd.md.literalElements = index1D;
    pd.md.miscCounters[0] = pd.md.literalElements;
    pd.md.miscCounters[1] = pd.md.numStoredClauses;
    pd.md.miscCounters[2] = pd.md.numLiterals;
    pd.md.miscCounters[3] = decisionStore.size();
    pd.md.miscCounters[4] = configuration["_HOST_POSITIVE_LIT_PHASE_VAL"].GetBool();
    pd.md.miscCounters[5] = _HOST_MAX_LITERAL_ELEMENTS;
    pd.md.miscCounters[6] = configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint();
    pd.md.miscCounters[7] = configuration["_HOST_RESET_MULTIPLIER"].GetUint();


    if(pd.md.numLiterals > _FPGA_MAX_LITERALS){
        std::cout << "Exceeded max literal support: " << pd.md.numLiterals << "\n";
        exit(EXIT_FAILURE);
    }
    if(pd.md.numClauses > _FPGA_MAX_CLAUSES){
        std::cout << "Exceeded max clause support: " << pd.md.numClauses << "\n";
        exit(EXIT_FAILURE);
    }
    if(pd.md.literalElements > _HOST_MAX_LITERAL_ELEMENTS){
        std::cout << "Exceeded max literal elements support: " << pd.md.literalElements << "\n";
        exit(EXIT_FAILURE);
    }
    
    if(pd.md.clauseElements > _HOST_MAX_CLAUSE_ELEMENTS){
        std::cout << "Exceeded max clause elements support: " << pd.md.clauseElements << "\n";
        exit(EXIT_FAILURE);
    }

    /*std::cout << "PRINT: " << "\n";
    for(unsigned int i = 0; i < pd.md.numLiterals; i++){
        for(unsigned int j = 0; j < 2; j++){
            std::cout << "(" << i+1 << ")(" << LMD_ADDR_START(pd.lmd[i].compactlmd,j) << ")" << LMD_FREE_SPACE(pd.lmd[i].compactlmd,j) << ": ";
            unsigned int addr = LMD_ADDR_START(pd.lmd[i].compactlmd,j);

            int index = 0;
            while(true){
                if(index == _HOST_PAGE_SIZE-1){
                    addr = pd.litStore[addr+index];
                    index = 0;
                    std::cout << "-" << addr << "- ";
                }
                std::cout << pd.litStore[0][addr+index] << " ";
                if(pd.litStore[addr+index] == 0){
                    break;
                }
                index++;
            }
            std::cout << "\n";
        }
    }*/

    std::cout << "INPUT CHECK: " << 
        "ABSOLUTE LITERALS: " << decisionStore.size() << "/" << pd.md.numLiterals <<
        " DECISION DOMAIN: " << pd.md.numDomainLiterals << "/" << pd.md.numLiterals <<
        " PRESOLVED COUNT: " << preSolvedCount << "/" << pd.md.numClauses << 
        " NUMBER OF ELEMENTS: " << pd.md.literalElements << " " << pd.md.clauseElements << "\n";
    std::cout << "DISTRIBUTION OF MEMORY: " << "\n";
    for(unsigned int i = 0; i < 2; i++){
        for(unsigned int j = 0; j < histogram[i].size(); j++){
            std::cout << "(" << j << "," << histogram[i][j] << "),";
        }
        std::cout << "\n";
    }
}

struct FpgaSession {
    cl::CommandQueue q;
    cl::Device device;
    cl::Context context;
    cl::Program program;
    cl::Kernel satSolverKernel;
    cl::Kernel clsStoreKernel;
    cl::Kernel storePositionKernel;
    cl::Kernel restartCalculateKernel;
    cl::Kernel timerKernel;
    cl::Kernel pqHandlerKernel;
    cl::Kernel messageKernel;
    cl::Buffer usedClsIDBucketsBuffer;
    bool useHostOnlyDebug = false;
    bool resetPriorityQueue = false;
    IncrementalFormulaSession incrementalFormula;

    bool initialize(const std::string& xclBinFile){
        const std::vector<cl::Device> devices = xcl::get_xil_devices();
        const std::vector<unsigned char> fileBuf = xcl::read_binary_file(xclBinFile);
        const cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
        const char* requestedDevice = std::getenv("FPGA_DEVICE_NAME");

        for(unsigned int i = 0; i < devices.size(); i++){
            cl_int err = CL_SUCCESS;
            device = devices[i];
            const std::string deviceName = device.getInfo<CL_DEVICE_NAME>();
            if(requestedDevice != nullptr && deviceName.find(requestedDevice) == std::string::npos){
                continue;
            }
            context = cl::Context(device, NULL, NULL, NULL, &err);
            if(err != CL_SUCCESS){
                std::cerr << "Could not create context for device[" << i << "], error number: " << err << "\n";
                continue;
            }
            q = cl::CommandQueue(context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE|CL_QUEUE_PROFILING_ENABLE, &err);
            if(err != CL_SUCCESS){
                std::cerr << "Could not create command queue for device[" << i << "], error number: " << err << "\n";
                continue;
            }
            std::cout << "Trying to program device[" << i << "]: " << deviceName << std::endl;
            program = cl::Program(context, {device}, bins, NULL, &err);
            if(err != CL_SUCCESS){
                std::cerr << "Failed to program device[" << i << "] with xclbin file, error number: " << err << "\n";
                continue;
            }
            std::cout << "Device[" << i << "]: program successful!\n";

            satSolverKernel = cl::Kernel(program, "solver", &err);
            if(err == CL_SUCCESS) storePositionKernel = cl::Kernel(program, "location_handler", &err);
            if(err == CL_SUCCESS) clsStoreKernel = cl::Kernel(program, "clause_store_handler", &err);
            if(err == CL_SUCCESS) restartCalculateKernel = cl::Kernel(program, "restartCalculator", &err);
            if(err == CL_SUCCESS) timerKernel = cl::Kernel(program, "timer", &err);
            if(err == CL_SUCCESS) pqHandlerKernel = cl::Kernel(program, "pqHandler", &err);
            if(err == CL_SUCCESS) messageKernel = cl::Kernel(program, "message", &err);
            if(err != CL_SUCCESS){
                std::cerr << "Could not create SAT-Accel kernels, error number: " << err << "\n";
                return false;
            }
            usedClsIDBucketsBuffer = cl::Buffer(context,
                CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_WRITE,
                _FPGA_MAX_LBD_BUCKETS * _FPGA_MAX_CLAUSES * sizeof(unsigned int),
                nullptr, &err);
            if(err != CL_SUCCESS){
                std::cerr << "Could not allocate the persistent learned-clause index buffer, error number: "
                          << err << "\n";
                return false;
            }
            useHostOnlyDebug = deviceName.find("u55c") != std::string::npos;
            return true;
        }
        if(requestedDevice != nullptr){
            std::cerr << "No programmable Xilinx device matched FPGA_DEVICE_NAME=" << requestedDevice << "\n";
        }
        std::cerr << "Failed to program any device found, exit!\n";
        return false;
    }
};

std::vector<int> readOriginalClause(const problemData& pd, unsigned int clauseID){
    std::vector<int> result;
    const clauseMetaData metadata = pd.cmd[clauseID];
    result.reserve(metadata.numElements);
    unsigned int address = metadata.addressStart;
    unsigned int pageOffset = 0;
    for(unsigned int i = 0; i < metadata.numElements; i++){
        result.push_back(pd.clauseStore[address]);
        address++;
        pageOffset++;
        if(pageOffset == 3 && i + 1 < metadata.numElements){
            address = pd.clauseStore[address];
            pageOffset = 0;
        }
    }
    return result;
}

std::vector<std::vector<int>> readPermanentClauses(const problemData& pd){
    std::vector<std::vector<int>> result;
    result.reserve(pd.md.numPermanentClauses);
    for(unsigned int i = 0; i < pd.md.numPermanentClauses; i++){
        result.push_back(readOriginalClause(pd, i));
    }
    return result;
}

bool solve(std::string inputFilePath, std::string outputResultFile, problemData& pd,
    const rapidjson::Document& configuration, const int expectedAnswer, FpgaSession& session){
    cl_int err = CL_SUCCESS;
    cl::CommandQueue& q = session.q;
    cl::Context& context = session.context;
    cl::Kernel& satSolverKernel = session.satSolverKernel;
    cl::Kernel& clsStoreKernel = session.clsStoreKernel;
    cl::Kernel& storePositionKernel = session.storePositionKernel;
    cl::Kernel& restartCalculateKernel = session.restartCalculateKernel;
    cl::Kernel& timerKernel = session.timerKernel;
    cl::Kernel& pqHandlerKernel = session.pqHandlerKernel;
    cl::Kernel& messageKernel = session.messageKernel;
    const bool useHostOnlyDebug = session.useHostOnlyDebug;

    IncrementalQueryPlan queryPlan = {
        true,
        0,
        pd.md.numPermanentClauses,
        pd.md.numTemporaryClauses,
        pd.md.numAssumptions,
        pd.md.constraintActivation,
        "non-incremental input",
    };
    if(pd.md.constraintActivation != 0){
        try {
            queryPlan = session.incrementalFormula.prepare(
                pd.md.numLiterals,
                pd.md.constraintActivation,
                readPermanentClauses(pd),
                pd.md.numTemporaryClauses,
                pd.md.numAssumptions);
        } catch(const std::exception& error){
            std::cerr << "Invalid rIC3 incremental query: " << error.what() << "\n";
            return false;
        }
        std::cout << "INCREMENTAL QUERY: "
                  << (queryPlan.reset ? "RESET" : "REUSE")
                  << " previous-permanent=" << queryPlan.previousPermanentCount
                  << " new-permanent=" << queryPlan.newPermanentCount
                  << " temporary=" << queryPlan.temporaryCount
                  << " assumptions=" << queryPlan.assumptionCount;
        if(queryPlan.reset){
            std::cout << " reason=" << queryPlan.resetReason;
        }
        std::cout << "\n";
    }else{
        session.incrementalFormula.clear();
    }

    int argN = 0;

    cl::Buffer hostMemDebugBuffer;
    std::vector<int, aligned_allocator<int>> debugStorage(8192, 0);
    int* hostMemDebug = debugStorage.data();

	if(useHostOnlyDebug){
		cl_mem_ext_ptr_t hostBufferExt;
		hostBufferExt.flags = XCL_MEM_EXT_HOST_ONLY;
		hostBufferExt.obj = nullptr;
		hostBufferExt.param = 0;
		OCL_CHECK(err, hostMemDebugBuffer = cl::Buffer(context, CL_MEM_READ_WRITE | CL_MEM_EXT_PTR_XILINX, sizeof(int) * 8192, &hostBufferExt, &err));
		hostMemDebug = (int*)q.enqueueMapBuffer(hostMemDebugBuffer, CL_TRUE, CL_MAP_WRITE, 0, sizeof(int) * 8192, nullptr, nullptr, &err);
		memset(hostMemDebug, 0, sizeof(int) * 8192);
	}else{
		OCL_CHECK(err, hostMemDebugBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(int) * 8192, hostMemDebug, &err));
		OCL_CHECK(err, err = q.enqueueMigrateMemObjects({hostMemDebugBuffer}, 0));
		OCL_CHECK(err, err = q.finish());
	}

    OCL_CHECK(err, err = messageKernel.setArg(0, hostMemDebugBuffer));
    if(configuration["_HOST_ENABLE_DEBUG"].GetBool()){
        OCL_CHECK(err, err = messageKernel.setArg(1, true));
    }else{
        OCL_CHECK(err, err = messageKernel.setArg(1, false));
    }

    cl::Buffer clsStoreBuffer;
    cl::Buffer cmdBuffer;
    cl::Buffer litStoreBuffer;
    cl::Buffer lbdBucketBuffer;
    cl::Buffer trackLBDCountBuffer;
    cl::Buffer answerStackBuffer;
    cl::Buffer lmdBuffer;
    cl::Buffer clsStatesBuffer;
    cl::Buffer miscBuffer;
    cl::Buffer decisionDomainBuffer;
    cl::Buffer assumptionsBuffer;
    cl::Buffer queryClauseStoreBuffer;
    cl::Buffer queryCmdBuffer;
    cl::Buffer clsToLitStorePosBuffer;
    cl::Buffer litToClsStorePosBuffer;

    std::vector<unsigned int, aligned_allocator<unsigned int>> trackLBDCount(
        2 * _FPGA_MAX_LBD_BUCKETS, 0);
    std::vector<int, aligned_allocator<int>> miscCounters(
        pd.md.miscCounters, pd.md.miscCounters + 256);
    miscCounters[8] = pd.md.numPermanentClauses;
    miscCounters[9] = pd.md.numTemporaryClauses;
    miscCounters[10] = pd.md.numAssumptions;
    miscCounters[11] = pd.md.constraintActivation;
    miscCounters[12] = queryPlan.reset ? 1 : 0;
    miscCounters[13] = queryPlan.previousPermanentCount;
    miscCounters[14] = queryPlan.newPermanentCount;
    const bool priorityQueueReset = queryPlan.reset || session.resetPriorityQueue;
    if(session.resetPriorityQueue && !queryPlan.reset){
        std::cout << "VARIABLE SELECTION: RESET after prior exact-heap switch\n";
    }

    OCL_CHECK(err, clsStoreBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _HOST_MAX_CLAUSE_ELEMENTS * sizeof(cls), pd.clauseStore, &err));
    OCL_CHECK(err, trackLBDCountBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_LBD_BUCKETS*2*sizeof(unsigned int), trackLBDCount.data(), &err));
   
    OCL_CHECK(err, cmdBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_CLAUSES * sizeof(clauseMetaData), pd.cmd, &err));
    OCL_CHECK(err, litStoreBuffer = 
        cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _HOST_MAX_LITERAL_ELEMENTS*sizeof(lit), pd.litStore, &err));   
    
    OCL_CHECK(err, answerStackBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, pd.md.numLiterals * sizeof(lit), pd.answerStack, &err));
    OCL_CHECK(err, lmdBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, pd.md.numLiterals * sizeof(literalMetaDataPCIE), pd.lmd, &err));
    OCL_CHECK(err, clsStatesBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_CLAUSES * sizeof(clsStatePCIE), pd.clsStates, &err));
    OCL_CHECK(err, miscBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(int)*256, miscCounters.data(), &err));
    OCL_CHECK(err, decisionDomainBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        pd.md.numLiterals * sizeof(unsigned int), pd.decisionDomain, &err));
    OCL_CHECK(err, assumptionsBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        std::max(1u, pd.md.numAssumptions) * sizeof(lit), pd.assumptions, &err));
    OCL_CHECK(err, queryClauseStoreBuffer = cl::Buffer(context,
        CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        std::max(1u, pd.md.queryClauseElements) * sizeof(lit),
        pd.queryClauseStore, &err));
    OCL_CHECK(err, queryCmdBuffer = cl::Buffer(context,
        CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        std::max(1u, pd.md.numStoredClauses) * sizeof(clauseMetaData),
        pd.queryCmd, &err));
    OCL_CHECK(err, clsToLitStorePosBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        _HOST_MAX_CLAUSE_ELEMENTS * sizeof(unsigned int), pd.clsToLitStorePos, &err));
    OCL_CHECK(err, litToClsStorePosBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
        _HOST_MAX_LITERAL_ELEMENTS * sizeof(unsigned int), pd.litToClsStorePos, &err));

    argN=0;
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, clsStoreBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, cmdBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, session.usedClsIDBucketsBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, trackLBDCountBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, pd.md.clauseElements));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, _HOST_MAX_CLAUSE_ELEMENTS));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, pd.md.numStoredClauses));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint()));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, configuration["_HOST_PRUNE_PERCENTAGE"].GetDouble()));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, queryPlan.reset));
    
    OCL_CHECK(err, err = pqHandlerKernel.setArg(0, decisionDomainBuffer));
    OCL_CHECK(err, err = pqHandlerKernel.setArg(1, pd.md.numLiterals));
    OCL_CHECK(err, err = pqHandlerKernel.setArg(2, pd.md.numDomainLiterals));
    OCL_CHECK(err, err = pqHandlerKernel.setArg(3, pd.md.decayFactor));
    OCL_CHECK(err, err = pqHandlerKernel.setArg(4, priorityQueueReset));

    OCL_CHECK(err, err = storePositionKernel.setArg(0, clsToLitStorePosBuffer));
    OCL_CHECK(err, err = storePositionKernel.setArg(1, litToClsStorePosBuffer));
    OCL_CHECK(err, err = storePositionKernel.setArg(2, pd.md.clauseElements));
    OCL_CHECK(err, err = storePositionKernel.setArg(3, pd.md.literalElements));
    OCL_CHECK(err, err = storePositionKernel.setArg(4, queryPlan.reset));

    argN=0;
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, decisionDomainBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, pd.md.numDomainLiterals));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, assumptionsBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, queryClauseStoreBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, queryCmdBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, clsStatesBuffer));

    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, litStoreBuffer));

    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, answerStackBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, lmdBuffer));
    OCL_CHECK(err, err = satSolverKernel.setArg(argN++, miscBuffer));

    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({clsStoreBuffer, 
        trackLBDCountBuffer,
        cmdBuffer,
        litStoreBuffer,
        answerStackBuffer, 
        lmdBuffer, clsStatesBuffer, miscBuffer, decisionDomainBuffer, assumptionsBuffer,
        queryClauseStoreBuffer, queryCmdBuffer,
        clsToLitStorePosBuffer, litToClsStorePosBuffer}, 0 /* 0 means from host*/));
    OCL_CHECK(err, err = q.finish());

    cl::Event evt;

    OCL_CHECK(err, err = q.enqueueTask(messageKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(clsStoreKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(storePositionKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(timerKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(restartCalculateKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(pqHandlerKernel, nullptr, nullptr));
    OCL_CHECK(err, err = q.enqueueTask(satSolverKernel, nullptr, &evt));

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    uint64_t totalTime = 0;
    unsigned int iterationNumber = 0;
    unsigned int oldIteration = 0;
    unsigned int stuckCount = 0;

    if(configuration["_HOST_ENABLE_DEBUG"].GetBool() && useHostOnlyDebug){
        std::cout << "\n\n\n\n";

        while(((volatile int*)hostMemDebug)[0] == 0){
            std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
            std::chrono::duration<double> time_span = stop - start;
        
            std::this_thread::sleep_for(std::chrono::seconds(1));

            if(std::chrono::duration_cast<std::chrono::seconds>(time_span).count() > 30){
                if((unsigned int)((volatile int*)hostMemDebug)[1] == iterationNumber){
                    stuckCount++;
                    if(stuckCount == 60){
                        std::cout << "Cosim seems to have been stuck somewhere. Exiting" << "\n";
                        exit(EXIT_FAILURE);
                    }
                }else{
                    stuckCount = 0;
                }
                oldIteration = iterationNumber;
                iterationNumber = ((volatile int*)hostMemDebug)[1];

                totalTime += std::chrono::duration_cast<std::chrono::seconds>(time_span).count();
                start = std::chrono::steady_clock::now();

                std::cout << "\033[A\033[A\33[2K\033[A\33[2K\033[A\33[2K\r" << std::flush;
                std::cout << "[ " << timeString() << " " << totalTime/60 << " Minutes " << totalTime%60 << " Seconds ]" << "\n";
                std::cout << "Iteration per 30 seconds: " << iterationNumber - oldIteration << "\n";
                std::cout << ((volatile int*)hostMemDebug)[1] << " " << ((volatile int*)hostMemDebug)[2] 
                    << " " << ((volatile int*)hostMemDebug)[3] << " " << ((volatile int*)hostMemDebug)[4] 
                    << " " << ((volatile int*)hostMemDebug)[5] << " " << ((volatile int*)hostMemDebug)[6]
                    << " " << ((volatile int*)hostMemDebug)[7] << "\n\n";
            
            }
        }

        std::cout << "FINISHED: " << ((volatile int*)hostMemDebug)[0] << "\n";
	}else if(configuration["_HOST_ENABLE_DEBUG"].GetBool()){
		std::cout << "Live debug polling is unavailable on this platform; debug data will be read after kernel completion.\n";
    }

    evt.wait();

    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());
    OCL_CHECK(err, err = q.finish());

    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({answerStackBuffer, miscBuffer, trackLBDCountBuffer}, CL_MIGRATE_MEM_OBJECT_HOST));
	if(!useHostOnlyDebug){
		OCL_CHECK(err, err = q.enqueueMigrateMemObjects({hostMemDebugBuffer}, CL_MIGRATE_MEM_OBJECT_HOST));
	}
    OCL_CHECK(err, err = q.finish());
    std::copy(miscCounters.begin(), miscCounters.end(), pd.md.miscCounters);
    // The bucket-to-exact-heap conversion reuses the activity-heap storage.
    // Start only this kernel afresh on the next query; formula state remains resident.
    session.resetPriorityQueue = pd.md.miscCounters[4] >= 11;

    // GipSAT's reusable activation is unconstrained when a round has no
    // temporary clauses. Complete that irrelevant model bit at the interface.
    if(pd.md.miscCounters[6] == 1 && pd.md.constraintActivation != 0 &&
       pd.md.numTemporaryClauses == 0){
        bool activationAssigned = false;
        for(unsigned int i = 0; i < (unsigned int)pd.md.miscCounters[5]; i++){
            if((unsigned int)abs(pd.answerStack[i]) == pd.md.constraintActivation){
                activationAssigned = true;
                break;
            }
        }
        if(!activationAssigned){
            if((unsigned int)pd.md.miscCounters[5] < pd.md.numLiterals){
                pd.answerStack[pd.md.miscCounters[5]++] = pd.md.constraintActivation;
            }
        }
    }


    uint64_t executionTime = evt.getProfilingInfo<CL_PROFILING_COMMAND_END>() - evt.getProfilingInfo<CL_PROFILING_COMMAND_START>();

    uint64_t learnedStats[5];
    uint64_t longestClause[2];
    uint64_t accessStats[2][2];
    uint64_t cycleCounter[9];
    int overhead;
    int clearStream;
    unsigned int checkCnt;

    memcpy(learnedStats, &pd.md.miscCounters[7], sizeof(uint64_t) * 5);
    memcpy(longestClause, &pd.md.miscCounters[17], sizeof(uint64_t) * 2);
    memcpy(&checkCnt, &pd.md.miscCounters[21], sizeof(unsigned int));
    memcpy(accessStats[0], &pd.md.miscCounters[22], sizeof(uint64_t) * 2);
    memcpy(accessStats[1], &pd.md.miscCounters[26], sizeof(uint64_t) * 2);
    memcpy(cycleCounter, &pd.md.miscCounters[30], sizeof(uint64_t) * 9);
    memcpy(&overhead, &pd.md.miscCounters[48], sizeof(int));
    memcpy(&clearStream,&pd.md.miscCounters[49], sizeof(int));

    uint64_t totalCycleCount = 0;

    for(unsigned int i = 0; i < 9; i ++){
        totalCycleCount += cycleCounter[i];
    }

    std::cout << "\n\n\n";
    std::cout << "TRIP COUNT FOR LEARNING (iteration/mergecount): " << learnedStats[0] << " " << learnedStats[1] << "\n";
    std::cout << "TRIP COUNT FOR MINIMIZE (iteration/mergecount/simplified): " << learnedStats[2] << " " << learnedStats[3] << " " << learnedStats[4] << "\n";
    std::cout << "STATS FOR LONGEST CLAUSE (longest clause/longest simplified): " << longestClause[0] << " " << longestClause[1] << "\n";
    std::cout << "AVERAGE LATENCY FOR BCP CHECK VAR: " << checkCnt << " "
              << safeRatio(cycleCounter[2], checkCnt) << "\n";
    std::cout << "ZEROING OVEHREAD: " << overhead << "\n";
    std::cout << "CLEAR STREAM OVERHEAD: " << clearStream << "\n";
    std::cout << "ACCESS LIT STORE STATS: " << accessStats[0][0] << " " << accessStats[0][1] << " " << accessStats[1][0] << " " << accessStats[1][1] << "\n";
    std::cout << "STATS (TOTAL, DECISION, RETRY, BACKTRACK, RST): " << pd.md.miscCounters[0] << " " << pd.md.miscCounters[1] << " " << pd.md.miscCounters[2]
        << " " << pd.md.miscCounters[3] << " " << pd.md.miscCounters[4] << "\n";
    std::cout << "STATS FOR CLAUSE LBD SCORE: ";
    for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
        std::cout << "(" << i+2 << ") " << trackLBDCount[i] << " ";
    }
    std::cout << "\n";
    std::cout << "STATS FOR CLAUSE REMOVAL LBD SCORE: ";
    for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
        std::cout << "(" << i+2 << ") " << trackLBDCount[i+_FPGA_MAX_LBD_BUCKETS] << " ";
    }
    std::cout << "\n";
    std::cout << "CYCLE COUNTERS (COPY, PQ-FIND, BRANCH, LEARN, LEARN_MIN, SAVE, RESIZE, BACKTRACK, DELETE): " 
        << comma(cycleCounter[0]) << " " << comma(cycleCounter[1]) << " " << comma(cycleCounter[2]) << " " 
        << comma(cycleCounter[3]) << " " << comma(cycleCounter[4]) << " " << comma(cycleCounter[5]) << " " 
        << comma(cycleCounter[6]) << " " << comma(cycleCounter[7]) << " " << comma(cycleCounter[8]) << "\n";
    uint64_t minutes = (executionTime/(1000*1000*1000))/60;
    uint64_t seconds = (executionTime/(1000*1000*1000))%60;
    uint64_t milliseconds = (executionTime/(1000*1000))%1000;
    uint64_t microseconds = (executionTime/1000)%1000;
    std::cout << "Kernel execution time: " << minutes << " (min) " << seconds << " (s) " << milliseconds << " (ms) " << microseconds << " (us) RAW: " << executionTime << "\n";

    if(((volatile int*)hostMemDebug)[0] != -1){
        std::cout << "ERROR DURING CLAUSE LEARNING ALLOCATION: " << ((volatile int*)hostMemDebug)[0] << "\n";
        std::ofstream outputFile;
        outputFile.open(outputResultFile,std::ios_base::app);

        if(!outputFile.is_open()){
            std::cout << "Could not open results file" << "\n";
            exit(EXIT_FAILURE);
        }
    
        outputFile << std::filesystem::path(inputFilePath).stem() << "," << "0" << "," << pd.md.numLiterals << "," << pd.md.numClauses << ",";
        outputFile << (double)executionTime/(1000*1000*1000) << "," << pd.md.miscCounters[1] << "," << pd.md.miscCounters[3] << "," << pd.md.miscCounters[4] << ",";
        outputFile << cycleCounter[0] << "," << safeRatio(cycleCounter[0], totalCycleCount) << "," << cycleCounter[1] << "," << safeRatio(cycleCounter[1], totalCycleCount) << ",";
        outputFile << cycleCounter[2] << "," << safeRatio(cycleCounter[2], totalCycleCount) << "," << cycleCounter[3] << "," << safeRatio(cycleCounter[3], totalCycleCount) << ",";
        outputFile << cycleCounter[4] << "," << safeRatio(cycleCounter[4], totalCycleCount) << "," << cycleCounter[5] << "," << safeRatio(cycleCounter[5], totalCycleCount) << ",";
        outputFile << cycleCounter[6] << "," << safeRatio(cycleCounter[6], totalCycleCount) << "," << cycleCounter[7] << "," << safeRatio(cycleCounter[7], totalCycleCount) << ",";
        outputFile << cycleCounter[8] << "," << safeRatio(cycleCounter[8], totalCycleCount) << "," <<  checkCnt << "," << safeRatio(cycleCounter[2], checkCnt) << ",";
        outputFile << totalCycleCount << "\r\n";

        outputFile.close();
        exit(3);
    }

    if(expectedAnswer >= 0 && pd.md.miscCounters[6] != expectedAnswer){
        std::cout << "ANSWERS ARE NOT THE SAME---GAVE: " << pd.md.miscCounters[6] << "\n";
        exit(4);
    }
    if(pd.md.miscCounters[6] == 0){
        const int encodedCoreCount = pd.md.miscCounters[50];
        if(encodedCoreCount < 0 ||
           (unsigned int)encodedCoreCount > pd.md.numAssumptions){
            std::cerr << "FPGA returned an invalid UNSAT core size: "
                      << encodedCoreCount << " for " << pd.md.numAssumptions
                      << " assumptions\n";
            return false;
        }
        std::set<lit> coreLiterals;
        for(unsigned int i = 0; i < (unsigned int)encodedCoreCount; i++){
            const lit coreLiteral = pd.answerStack[i];
            if(std::find(pd.assumptions,
                    pd.assumptions + pd.md.numAssumptions,
                    coreLiteral) == pd.assumptions + pd.md.numAssumptions){
                std::cerr << "FPGA returned an UNSAT core literal outside the assumptions: "
                          << coreLiteral << "\n";
                return false;
            }
            if(!coreLiterals.insert(coreLiteral).second){
                std::cerr << "FPGA returned a duplicate UNSAT core literal: "
                          << coreLiteral << "\n";
                return false;
            }
        }
    }
    if(pd.md.miscCounters[6] == 1){
        const unsigned int assignedCount = pd.md.miscCounters[5];
        if(assignedCount > pd.md.numLiterals){
            std::cerr << "FPGA returned more assignments than variables\n";
            return false;
        }
        std::vector<int> model(pd.answerStack, pd.answerStack + assignedCount);
        std::vector<int> assumptions(pd.assumptions,
            pd.assumptions + pd.md.numAssumptions);
        std::vector<unsigned int> decisionDomain(pd.decisionDomain,
            pd.decisionDomain + pd.md.numDomainLiterals);
        std::vector<std::vector<int>> storedClauses;
        storedClauses.reserve(pd.md.numStoredClauses);
        for(unsigned int i = 0; i < pd.md.numStoredClauses; i++){
            storedClauses.push_back(readOriginalClause(pd, i));
        }
        std::string validationError;
        if(!validateFpgaPartialModel(pd.md.numLiterals, storedClauses,
                assumptions, decisionDomain, model, validationError)){
            std::cerr << "FPGA returned an invalid partial model: "
                      << validationError << ":";
            for(const int literal : model){
                std::cerr << " " << literal;
            }
            std::cerr << "\n";
            return false;
        }
    }

    if(pd.md.miscCounters[6] == 1){
        std::cout << "s SATISFIABLE\n";
        std::cout << "v";
        for(unsigned int i = 0; i < (unsigned int)pd.md.miscCounters[5]; i++){
            std::cout << " " << pd.answerStack[i];
        }
        std::cout << " 0\n";
    }else{
        std::cout << "s UNSATISFIABLE\n";
        std::cout << "u";
        for(unsigned int i = 0; i < (unsigned int)pd.md.miscCounters[50]; i++){
            std::cout << " " << pd.answerStack[i];
        }
        std::cout << " 0\n";
    }

    std::ofstream outputFile;
    outputFile.open(outputResultFile,std::ios_base::app);

    if(!outputFile.is_open()){
        std::cout << "Could not open results file" << "\n";
        exit(EXIT_FAILURE);
    }
    
    outputFile << std::filesystem::path(inputFilePath).stem() << "," << "1" << "," << pd.md.numLiterals << "," << pd.md.numClauses << ",";
    outputFile << (double)executionTime/(1000*1000*1000) << "," << pd.md.miscCounters[1] << "," << pd.md.miscCounters[3] << "," << pd.md.miscCounters[4] << ",";
    outputFile << cycleCounter[0] << "," << safeRatio(cycleCounter[0], totalCycleCount) << "," << cycleCounter[1] << "," << safeRatio(cycleCounter[1], totalCycleCount) << ",";
    outputFile << cycleCounter[2] << "," << safeRatio(cycleCounter[2], totalCycleCount) << "," << cycleCounter[3] << "," << safeRatio(cycleCounter[3], totalCycleCount) << ",";
    outputFile << cycleCounter[4] << "," << safeRatio(cycleCounter[4], totalCycleCount) << "," << cycleCounter[5] << "," << safeRatio(cycleCounter[5], totalCycleCount) << ",";
    outputFile << cycleCounter[6] << "," << safeRatio(cycleCounter[6], totalCycleCount) << "," << cycleCounter[7] << "," << safeRatio(cycleCounter[7], totalCycleCount) << ",";
    outputFile << cycleCounter[8] << "," << safeRatio(cycleCounter[8], totalCycleCount) << "," << checkCnt << "," << safeRatio(cycleCounter[2], checkCnt) << ",";
    outputFile << totalCycleCount << "\r\n";

    outputFile.close();

    return true;
}

struct BatchQuery {
    int expectedAnswer;
    std::filesystem::path inputPath;
};

bool parseBatchManifest(const std::filesystem::path& manifestPath, std::vector<BatchQuery>& queries){
    std::ifstream manifest(manifestPath);
    if(!manifest.is_open()){
        std::cerr << "Could not open batch manifest: " << manifestPath << "\n";
        return false;
    }

    const std::filesystem::path manifestDir = std::filesystem::absolute(manifestPath).parent_path();
    std::string line;
    unsigned int lineNumber = 0;
    while(std::getline(manifest, line)){
        lineNumber++;
        if(!line.empty() && line.back() == '\r'){
            line.pop_back();
        }
        if(line.empty() || line[0] == '#'){
            continue;
        }

        const std::size_t separator = line.find('\t');
        if(separator == std::string::npos || line.find('\t', separator + 1) != std::string::npos){
            std::cerr << "Invalid batch manifest line " << lineNumber
                      << ": expected '<0|1>\\t<CNF path>'\n";
            return false;
        }
        const std::string expected = line.substr(0, separator);
        const std::string pathText = line.substr(separator + 1);
        if((expected != "0" && expected != "1") || pathText.empty()){
            std::cerr << "Invalid batch manifest line " << lineNumber
                      << ": expected '<0|1>\\t<CNF path>'\n";
            return false;
        }

        std::filesystem::path inputPath(pathText);
        if(inputPath.is_relative()){
            inputPath = manifestDir / inputPath;
        }
        if(!std::filesystem::is_regular_file(inputPath)){
            std::cerr << "CNF from batch manifest line " << lineNumber
                      << " does not exist: " << inputPath << "\n";
            return false;
        }
        queries.push_back({expected[0] - '0', inputPath.lexically_normal()});
    }

    if(queries.empty()){
        std::cerr << "Batch manifest contains no queries: " << manifestPath << "\n";
        return false;
    }
    return true;
}

void printUsage(const char* executable){
    std::cerr << "Usage:\n"
              << "  " << executable
              << " <xclbin> <configuration.json> <input.dimacs> <metrics.csv> [expected: 0|1]\n"
              << "  " << executable
              << " --batch <xclbin> <configuration.json> <manifest.tsv> <metrics.csv>\n"
              << "  " << executable
              << " --server <xclbin> <configuration.json> <metrics.csv>\n";
}

int main(int argc, char* argv[]){
    const bool batchMode = argc > 1 && std::string(argv[1]) == "--batch";
    const bool serverMode = argc > 1 && std::string(argv[1]) == "--server";
    const bool singleMode = !batchMode && !serverMode;
    if((singleMode && argc != 5 && argc != 6) || (batchMode && argc != 6) ||
       (serverMode && argc != 5)){
        printUsage(argv[0]);
        return 2;
    }

    for(int i = 0; i < argc; i++){
        std::cout << std::string(argv[i]) << " ";
    }
    std::cout << "\n";

    const int argumentOffset = singleMode ? 0 : 1;
    const std::string xclbinPath = argv[1 + argumentOffset];
    const std::string configurationPath = argv[2 + argumentOffset];
    const std::string inputPath = serverMode ? "" : argv[3 + argumentOffset];
    const std::string metricsPath = serverMode ? argv[4] : argv[4 + argumentOffset];

    std::vector<BatchQuery> queries;
    if(batchMode && !parseBatchManifest(inputPath, queries)){
        return 2;
    }

    int expectedAnswer = -1;
    if(!batchMode && argc == 6){
        const std::string expected = argv[5];
        if(expected != "0" && expected != "1"){
            std::cerr << "Expected answer must be 0 (UNSAT) or 1 (SAT)\n";
            return 2;
        }
        expectedAnswer = expected[0] - '0';
    }

    rapidjson::Document configuration;
    parseJSON(configurationPath, configuration);

    FpgaSession session;
    if(!session.initialize(xclbinPath)){
        return 2;
    }

    if(serverMode){
        std::cout << "INDUCTOR-SAT-ACCEL\tREADY\n" << std::flush;
        std::string request;
        while(std::getline(std::cin, request)){
            if(!request.empty() && request.back() == '\r'){
                request.pop_back();
            }
            const std::size_t separator = request.find('\t');
            if(separator == std::string::npos ||
               request.find('\t', separator + 1) != std::string::npos){
                std::cerr << "Invalid server request: expected '<request id>\\t<CNF path>'\n";
                return 2;
            }
            const std::string requestId = request.substr(0, separator);
            const std::string queryPath = request.substr(separator + 1);
            if(requestId.empty() || queryPath.empty() || !std::filesystem::is_regular_file(queryPath)){
                std::cerr << "Invalid server request or missing CNF: " << request << "\n";
                return 2;
            }

            problemData pd{};
            parseDIMACS(queryPath, pd, configuration);
            const bool solved = solve(queryPath, metricsPath, pd, configuration, -1, session);
            const int answer = pd.md.miscCounters[6];
            cleanUp(pd);
            if(!solved){
                return 2;
            }
            const int resultCode = answer == 1 ? 10 : 20;
            std::cout << "INDUCTOR-SAT-ACCEL\tRESULT\t" << requestId << "\t"
                      << resultCode << "\n" << std::flush;
        }
        return 0;
    }

    if(singleMode){
        problemData pd{};
        parseDIMACS(inputPath, pd, configuration);
        const bool solved = solve(inputPath, metricsPath, pd, configuration, expectedAnswer, session);
        const int answer = pd.md.miscCounters[6];
        cleanUp(pd);
        if(!solved){
            return 2;
        }
        return argc == 6 ? 0 : (answer == 1 ? 10 : 20);
    }

    const auto batchStart = std::chrono::steady_clock::now();
    for(std::size_t i = 0; i < queries.size(); i++){
        const auto queryStart = std::chrono::steady_clock::now();
        problemData pd{};
        std::cout << "BATCH QUERY " << (i + 1) << "/" << queries.size()
                  << ": " << queries[i].inputPath << " expected=" << queries[i].expectedAnswer << "\n";
        parseDIMACS(queries[i].inputPath.string(), pd, configuration);
        const bool solved = solve(queries[i].inputPath.string(), metricsPath, pd, configuration,
                                  queries[i].expectedAnswer, session);
        cleanUp(pd);
        if(!solved){
            return 2;
        }
        const std::chrono::duration<double> queryTime =
            std::chrono::steady_clock::now() - queryStart;
        std::cout << "BATCH QUERY " << (i + 1) << " HOST WALL TIME: "
                  << queryTime.count() << " s\n";
    }
    const std::chrono::duration<double> batchTime =
        std::chrono::steady_clock::now() - batchStart;
    std::cout << "BATCH COMPLETE: " << queries.size() << " queries, "
              << batchTime.count() << " s host wall time\n";
    return 0;
}
