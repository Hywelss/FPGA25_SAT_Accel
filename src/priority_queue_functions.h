#ifndef PRIORITY_QUEUE_FUNCTIONS_H
#define PRIORITY_QUEUE_FUNCTIONS_H

#include "fpga_solver.h"
#include "data_structures.h"

inline int comp_fp64(double op1, double op2);
void loadPositioning(pqPosition mPositioning[_FPGA_MAX_LITERALS], pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS);
void swapLower(pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const pqData initialpqData, const unsigned int initialPosition, const unsigned int remainingLiterals);
void swapHigher(pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const pqData initialpqData, const unsigned int initialPosition);
void hideElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], unsigned int& remainingLiterals);
void unhideElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], const unsigned int NUM_DOMAIN_LITERALS,
    unsigned int& remainingLiterals);
void decayEveryElement(hls::stream<lit>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const unsigned int remainingLiterals, double& multiplier, const double decayFactor, const unsigned int NUM_LITERALS);
void axiStreamBuffer(hls::stream<ap_axiu<32,0,0,0>>& input, hls::stream<lit>& intermediateStream);
void hide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], unsigned int& remainingLiterals);
void unhide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS],
    pqPosition mPositioning[_FPGA_MAX_LITERALS], const unsigned int NUM_DOMAIN_LITERALS,
    unsigned int& remainingLiterals);
void unhide_wrapper(hls::stream<ap_axiu<32,0,0,0>>& input, pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS], 
    const unsigned int remainingLiterals, double& multiplier, const double decayFactor, const unsigned int NUM_LITERALS);

unsigned int gipsatBucketIndex(unsigned int position);
void loadGipsatBuckets(pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    unsigned int& bucketHead, unsigned int& activityHeapSize);
void reloadGipsatBuckets(const pqData mActivityHeap[2][_FPGA_MAX_LITERALS],
    const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], const unsigned int* decisionDomain,
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    unsigned int activityHeapSize, unsigned int& bucketHead);
void gipsatBucketPush(unsigned int variable,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], unsigned int activityHeapSize,
    unsigned int& bucketHead);
lit gipsatBucketPop(ap_uint<3> bucketState[_FPGA_MAX_LITERALS],
    const unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], unsigned int& bucketHead);
void gipsatBumpActivity(hls::stream<lit>& input,
    pqData mActivityHeap[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    unsigned int& activityHeapSize, double& multiplier, const double decayFactor);
void gipsatBumpActivityWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    pqData mActivityHeap[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    unsigned int& activityHeapSize, double& multiplier, const double decayFactor);
void gipsatBucketUnhide(hls::stream<lit>& input,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], unsigned int activityHeapSize,
    unsigned int& bucketHead);
void gipsatBucketUnhideWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    const pqData mActivityHeap[2][_FPGA_MAX_LITERALS], const pqPosition mPositioning[_FPGA_MAX_LITERALS],
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int bucketNext[_FPGA_MAX_LITERALS],
    unsigned int bucketHeads[GIPSAT_NUM_BUCKETS], unsigned int activityHeapSize,
    unsigned int& bucketHead);
void gipsatBucketHide(hls::stream<lit>& input,
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS]);
void gipsatBucketHideWrapper(hls::stream<ap_axiu<32,0,0,0>>& input,
    ap_uint<3> bucketState[_FPGA_MAX_LITERALS]);
void gipsatSwitchToHeap(const unsigned int* decisionDomain,
    pqData mPriorityQueue[2][_FPGA_MAX_LITERALS], pqPosition mPositioning[_FPGA_MAX_LITERALS],
    const ap_uint<3> bucketState[_FPGA_MAX_LITERALS], unsigned int newPosition[_FPGA_MAX_LITERALS],
    unsigned int NUM_LITERALS, unsigned int NUM_DOMAIN_LITERALS,
    unsigned int activityHeapSize, unsigned int& remainingLiterals);
#endif
