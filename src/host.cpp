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
#include "xcl2.hpp"

std::string comma(uint64_t n) {
    std::string result = std::to_string(n);
    for(int i = result.size() - 3; i > 0; i -= 3){
        result.insert(i,",");
    }
    return result;		
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
    free(pd.litStore);
    free(pd.answerStack);
    free(pd.lmd);
    free(pd.clsStates);
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
    std::set<lit> decisionStore;
    std::set<lit> checkLit;

    while(std::getline(input, line)){
        std::string token;
        std::vector<std::string> tokens;
        std::istringstream iss(line);

        while(std::getline(iss, token, ' ')) {
            if(token != ""){
                tokens.push_back(token);
            }
        }

        if(tokens.empty()){
            continue;
        }
        if(tokens[0] == "c"){
            continue;
        }else if(state == 0 && tokens[0] == "p"){
            if(tokens.size() != 4 || tokens[1] != "cnf"){
                std::cerr << "Invalid DIMACS header in " << filePath << "\n";
                exit(EXIT_FAILURE);
            }
            pd.md.numLiterals = std::stoi(tokens[2]);
            pd.md.numClauses = std::stoi(tokens[3]);

            litStore[0] = std::vector<std::vector<lit>>(pd.md.numLiterals);
            litStore[1] = std::vector<std::vector<lit>>(pd.md.numLiterals);
            clsStore = std::vector<std::vector<cls>>(pd.md.numClauses);
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
                lit getLit = std::stoi(tokens[i]);
                if(getLit == 0 || (unsigned int)abs(getLit) > pd.md.numLiterals){
                    std::cerr << "DIMACS literal out of range in " << filePath << ": " << getLit << "\n";
                    exit(EXIT_FAILURE);
                }
                const bool isIn = checkLit.find(getLit) != checkLit.end();
                if(!isIn){
                    clsStore[counter].push_back(getLit);
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

    std::vector<int> histogram[2];
    histogram[0] = std::vector<int>(18,0);
    histogram[1] = std::vector<int>(18,0);

    unsigned int totalElements = 0;
    for(unsigned int i = 0; i < clsStore.size(); i++){
        for(unsigned int j = 0; j < clsStore[i].size(); j++){
            if(clsStore[i][j] > 0){
                litStore[0][abs(clsStore[i][j])-1].push_back(i+1);
            }else{
                litStore[1][abs(clsStore[i][j])-1].push_back(-(i+1));
            }
            totalElements++;
        }
        if(clsStore[i].size() == 1){
            decisionStore.insert(clsStore[i][0]);
        }
    }

    memset(pd.md.miscCounters,0,sizeof(int)*(256));

    pd.clauseStore = allocateAligned<cls>(_HOST_MAX_CLAUSE_ELEMENTS);
    pd.cmd = allocateAligned<clauseMetaData>(_FPGA_MAX_CLAUSES);
    pd.litStore = allocateAligned<lit>(_HOST_MAX_LITERAL_ELEMENTS);
    pd.answerStack = allocateAligned<lit>(pd.md.numLiterals);
    pd.lmd = allocateAligned<literalMetaDataPCIE>(pd.md.numLiterals);
    pd.clsStates = allocateAligned<clsStatePCIE>(_FPGA_MAX_CLAUSES);

    memset(pd.cmd,0,_FPGA_MAX_CLAUSES*sizeof(clauseMetaData));
    memset(pd.litStore,0,_HOST_MAX_LITERAL_ELEMENTS*sizeof(lit));
    memset(pd.answerStack,0,pd.md.numLiterals*sizeof(lit));
    memset(pd.lmd,0,pd.md.numLiterals*sizeof(literalMetaDataPCIE));
    memset(pd.clsStates,0,_FPGA_MAX_CLAUSES*sizeof(clsStatePCIE));
    memset(pd.clauseStore,0,_HOST_MAX_CLAUSE_ELEMENTS*sizeof(cls));

    unsigned int index1D = 0;
    unsigned int index = 0;
    unsigned int preSolvedCount = 0;
    
    for(unsigned int i = 0; i < clsStore.size(); i++){
        pd.cmd[i].addressStart = index1D;
        pd.cmd[i].numElements = clsStore[i].size();
        lit earliestSolved = 0;
        int xorCompact = 0;
        index = 0;
        bool needZero = true;

        for(unsigned int j = 0; j < clsStore[i].size(); j++){
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
                if(j != clsStore[i].size()-1){
                    pd.clauseStore[index1D] = index1D + 1;
                }else{
                    pd.clauseStore[index1D] = 0;
                    needZero = false;
                }
                index1D++;
                index = 0;
            }
            if(index1D > _HOST_MAX_CLAUSE_ELEMENTS){
                std::cout << "WENT OVER ALLOCATION LIMIT - INSERT CLAUSE: " << i+1 << "\n";
                exit(EXIT_FAILURE);
            }
        }

        if(needZero){
            for(unsigned int j = 0; j < 4-index; j++){
                pd.clauseStore[index1D] = 0;
                index1D++;
                if(index1D > _HOST_MAX_CLAUSE_ELEMENTS){
                    std::cout << "WENT OVER ALLOCATION LIMIT - ZERO INSERT CLAUSE: " << i+1 << "\n";
                    exit(EXIT_FAILURE);
                }
            }
        }

        pd.clsStates[i].remainingUnassigned = pd.cmd[i].numElements;
        pd.clsStates[i].compressedList = xorCompact;  
    }
    if(index1D%configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint() != 0){
        unsigned int fill = configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint() - (index1D%configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint());
        for(unsigned int j = 0; j < fill; j++){
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
        if(bothEmpty){
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
                pd.litStore[index1D] = abs(litStore[a][i][j]);
                index1D++;
                index++;

                if(index == configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-2){
                    pd.litStore[index1D] = 0;
                    index1D++;
                    pd.litStore[index1D] = index1D + 1;
                    index1D++;
                    LMD_LATEST_PAGE(pd.lmd[i].compactlmd, a) = index1D;

                    index = 0;
                }
            
                if(index1D > _HOST_MAX_LITERAL_ELEMENTS){
                    std::cout << "WENT OVER ALLOCATION LIMIT - INSERT: LITERAL " << i+1 << " " << a << "\n";
                    exit(EXIT_FAILURE);
                }
            }

            for(unsigned int j = 0; j < configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-index; j++){
                pd.litStore[index1D] = 0;
                index1D++;
                if(index1D > _HOST_MAX_LITERAL_ELEMENTS){
                    std::cout << "WENT OVER ALLOCATION LIMIT - FREE SPACE 1: " << i+1 << " " << a << "\n";
                    exit(EXIT_FAILURE);
                }
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
    pd.md.miscCounters[1] = pd.md.numClauses;
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
    bool useHostOnlyDebug = false;

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
    cl::Buffer usedClsIDBucketsBuffer;
    cl::Buffer cmdBuffer;
    cl::Buffer litStoreBuffer;
    cl::Buffer lbdBucketBuffer;
    cl::Buffer trackLBDCountBuffer;
    cl::Buffer answerStackBuffer;
    cl::Buffer lmdBuffer;
    cl::Buffer clsStatesBuffer;
    cl::Buffer miscBuffer;

    std::vector<unsigned int, aligned_allocator<unsigned int>> trackLBDCount(
        2 * _FPGA_MAX_LBD_BUCKETS, 0);
    std::vector<int, aligned_allocator<int>> miscCounters(
        pd.md.miscCounters, pd.md.miscCounters + 256);

    OCL_CHECK(err, clsStoreBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _HOST_MAX_CLAUSE_ELEMENTS * sizeof(cls), pd.clauseStore, &err));
    OCL_CHECK(err, usedClsIDBucketsBuffer = cl::Buffer(context, CL_MEM_HOST_NO_ACCESS | CL_MEM_READ_WRITE, _FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES*sizeof(unsigned int), nullptr, &err));
    OCL_CHECK(err, trackLBDCountBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_LBD_BUCKETS*2*sizeof(unsigned int), trackLBDCount.data(), &err));
   
    OCL_CHECK(err, cmdBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_CLAUSES * sizeof(clauseMetaData), pd.cmd, &err));
    OCL_CHECK(err, litStoreBuffer = 
        cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _HOST_MAX_LITERAL_ELEMENTS*sizeof(lit), pd.litStore, &err));   
    
    OCL_CHECK(err, answerStackBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, pd.md.numLiterals * sizeof(lit), pd.answerStack, &err));
    OCL_CHECK(err, lmdBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, pd.md.numLiterals * sizeof(literalMetaDataPCIE), pd.lmd, &err));
    OCL_CHECK(err, clsStatesBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, _FPGA_MAX_CLAUSES * sizeof(clsStatePCIE), pd.clsStates, &err));
    OCL_CHECK(err, miscBuffer = cl::Buffer(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, sizeof(int)*256, miscCounters.data(), &err));

    argN=0;
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, clsStoreBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, cmdBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, usedClsIDBucketsBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, trackLBDCountBuffer));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, pd.md.clauseElements));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, _HOST_MAX_CLAUSE_ELEMENTS));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, pd.md.numClauses));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, configuration["_HOST_CLAUSE_PAGE_SIZE"].GetUint()));
    OCL_CHECK(err, err = clsStoreKernel.setArg(argN++, configuration["_HOST_PRUNE_PERCENTAGE"].GetDouble()));
    
    OCL_CHECK(err, err = pqHandlerKernel.setArg(0, pd.md.numLiterals));
    OCL_CHECK(err, err = pqHandlerKernel.setArg(1, pd.md.decayFactor));

    argN=0;
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
        lmdBuffer, clsStatesBuffer, miscBuffer}, 0 /* 0 means from host*/));
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
    std::cout << "AVERAGE LATENCY FOR BCP CHECK VAR: " << checkCnt << " " << (double)cycleCounter[2]/(double)checkCnt << "\n";
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
        outputFile << cycleCounter[0] << "," << (double)cycleCounter[0]/totalCycleCount << "," << cycleCounter[1] << "," << (double)cycleCounter[1]/totalCycleCount << ",";
        outputFile << cycleCounter[2] << "," << (double)cycleCounter[2]/totalCycleCount << "," << cycleCounter[3] << "," << (double)cycleCounter[3]/totalCycleCount << ",";
        outputFile << cycleCounter[4] << "," << (double)cycleCounter[4]/totalCycleCount << "," << cycleCounter[5] << "," << (double)cycleCounter[5]/totalCycleCount << ",";
        outputFile << cycleCounter[6] << "," << (double)cycleCounter[6]/totalCycleCount << "," << cycleCounter[7] << "," << (double)cycleCounter[7]/totalCycleCount << ",";
        outputFile << cycleCounter[8] << "," << (double)cycleCounter[8]/totalCycleCount << "," <<  checkCnt << "," << (double)cycleCounter[2]/(double)checkCnt << ",";
        outputFile << totalCycleCount << "\r\n";

        outputFile.close();
        exit(3);
    }

    if(expectedAnswer >= 0 && pd.md.miscCounters[6] != expectedAnswer){
        std::cout << "ANSWERS ARE NOT THE SAME---GAVE: " << pd.md.miscCounters[6] << "\n";
        exit(4);
    }
    if(pd.md.miscCounters[6] == 1){
        std::vector<bool> didSolveClause(pd.md.numClauses, false);
        std::vector<lit> valuesNotInserted;
        for(unsigned int i = 0; i < pd.md.numLiterals; i++){
            valuesNotInserted.push_back(i+1);
        }

        for(unsigned int i = 0; i < (unsigned int)pd.md.miscCounters[5]; i++){
            for(unsigned int j = 0; j < valuesNotInserted.size(); j++){
                if(abs(pd.answerStack[i]) == valuesNotInserted[j]){
                    valuesNotInserted.erase(valuesNotInserted.begin()+j);
                    break;
                }
            }
        }

        for(unsigned int i = 0; i < valuesNotInserted.size(); i++){
            std::cout << "Inserting: " << valuesNotInserted[i] << "\n";
        }

        for(unsigned int i = pd.md.miscCounters[5]; i < pd.md.numLiterals; i++){
            pd.answerStack[i] = valuesNotInserted[i-pd.md.miscCounters[5]];
        }

        std::sort(pd.answerStack,pd.answerStack+pd.md.numLiterals,compareFunc);
        for(unsigned int i = 0; i < pd.md.numLiterals; i++){
            if(abs(pd.answerStack[i]) != i+1){
                std::cout << "Missing literal in the stack: " << i+1 << "\n";
            }
        }

        for(unsigned int i = 0; i < pd.md.numLiterals; i++){
            lit answerLitFromStack = pd.answerStack[i];
            int select = 0;
            if(answerLitFromStack < 0){
                select = 1;
            }
            
            unsigned int addr = LMD_ADDR_START(pd.lmd[i].compactlmd,select);

            unsigned index = 0;
            while(true){
                if(index == configuration["_HOST_LITERAL_PAGE_SIZE"].GetUint()-2){
                    addr = pd.litStore[addr+index+1];
                    index = 0;
                }
                cls get = pd.litStore[addr+index];
                index++;
                if(get == 0){
                    break;
                }

                if(select == 1){
                    get = -get;
                }
                if((answerLitFromStack > 0 && get > 0) || (answerLitFromStack < 0 && get < 0)){
                    didSolveClause[abs(get)-1] = true;
                }
            }
        }

        for(unsigned int i = 0; i < didSolveClause.size(); i++){
            if(didSolveClause[i] == false){
                std::cout << "Clause# " << i+1 << " was not solved" << "\n";
                exit(4);
            }
        }
    }

    if(pd.md.miscCounters[6] == 1){
        std::cout << "s SATISFIABLE\n";
        std::cout << "v";
        for(unsigned int i = 0; i < pd.md.numLiterals; i++){
            std::cout << " " << pd.answerStack[i];
        }
        std::cout << " 0\n";
    }else{
        std::cout << "s UNSATISFIABLE\n";
    }

    std::ofstream outputFile;
    outputFile.open(outputResultFile,std::ios_base::app);

    if(!outputFile.is_open()){
        std::cout << "Could not open results file" << "\n";
        exit(EXIT_FAILURE);
    }
    
    outputFile << std::filesystem::path(inputFilePath).stem() << "," << "1" << "," << pd.md.numLiterals << "," << pd.md.numClauses << ",";
    outputFile << (double)executionTime/(1000*1000*1000) << "," << pd.md.miscCounters[1] << "," << pd.md.miscCounters[3] << "," << pd.md.miscCounters[4] << ",";
    outputFile << cycleCounter[0] << "," << (double)cycleCounter[0]/totalCycleCount << "," << cycleCounter[1] << "," << (double)cycleCounter[1]/totalCycleCount << ",";
    outputFile << cycleCounter[2] << "," << (double)cycleCounter[2]/totalCycleCount << "," << cycleCounter[3] << "," << (double)cycleCounter[3]/totalCycleCount << ",";
    outputFile << cycleCounter[4] << "," << (double)cycleCounter[4]/totalCycleCount << "," << cycleCounter[5] << "," << (double)cycleCounter[5]/totalCycleCount << ",";
    outputFile << cycleCounter[6] << "," << (double)cycleCounter[6]/totalCycleCount << "," << cycleCounter[7] << "," << (double)cycleCounter[7]/totalCycleCount << ",";
    outputFile << cycleCounter[8] << "," << (double)cycleCounter[8]/totalCycleCount << "," << checkCnt << "," << (double)cycleCounter[2]/(double)checkCnt << ",";
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
              << " --batch <xclbin> <configuration.json> <manifest.tsv> <metrics.csv>\n";
}

int main(int argc, char* argv[]){
    const bool batchMode = argc > 1 && std::string(argv[1]) == "--batch";
    if((!batchMode && argc != 5 && argc != 6) || (batchMode && argc != 6)){
        printUsage(argv[0]);
        return 2;
    }

    for(int i = 0; i < argc; i++){
        std::cout << std::string(argv[i]) << " ";
    }
    std::cout << "\n";

    const int argumentOffset = batchMode ? 1 : 0;
    const std::string xclbinPath = argv[1 + argumentOffset];
    const std::string configurationPath = argv[2 + argumentOffset];
    const std::string inputPath = argv[3 + argumentOffset];
    const std::string metricsPath = argv[4 + argumentOffset];

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

    if(!batchMode){
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
