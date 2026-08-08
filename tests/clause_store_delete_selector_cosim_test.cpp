#include <cassert>

#include "data_structures.h"

void getDeletedClsID(hls::stream<cls>& removeIDStream,
    cls* usedClsIDBuckets,
    minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS],
    cls lastInsertedID, unsigned int removeTotal,
    unsigned int LBDBucketCount[_FPGA_MAX_LBD_BUCKETS]);

namespace {

static cls buckets[_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES];
static minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
static unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];

}

int main(){
    const unsigned int bucket = _FPGA_MAX_LBD_BUCKETS-1;
    buckets[bucket*_FPGA_MAX_CLAUSES+0] = 90;
    buckets[bucket*_FPGA_MAX_CLAUSES+1] = 99;
    buckets[bucket*_FPGA_MAX_CLAUSES+2] = 91;
    buckets[bucket*_FPGA_MAX_CLAUSES+3] = 92;
    tracker[bucket] = {4, 0, 4};

    hls::stream<cls> selected;
    getDeletedClsID(selected, buckets, tracker, 99, 2, removedByBucket);
    assert(selected.read() == 90);
    assert(selected.read() == 91);
    assert(selected.empty());
    assert(tracker[bucket].usedCount == 2);
    assert(buckets[bucket*_FPGA_MAX_CLAUSES+tracker[bucket].accessIdx] == 92);
    assert(buckets[bucket*_FPGA_MAX_CLAUSES+
        ((tracker[bucket].accessIdx+1)%_FPGA_MAX_CLAUSES)] == 99);

    getDeletedClsID(selected, buckets, tracker, 99, 1, removedByBucket);
    assert(selected.read() == 92);
    assert(selected.empty());
    assert(tracker[bucket].usedCount == 1);
    assert(buckets[bucket*_FPGA_MAX_CLAUSES+tracker[bucket].accessIdx] == 99);
    assert(removedByBucket[bucket] == 3);

    const unsigned int longBucket = _FPGA_MAX_LBD_BUCKETS-2;
    const cls firstLongID = 1000;
    const cls protectedLongID = firstLongID+64;
    tracker[bucket] = {0, 0, 0};
    for(unsigned int i = 0; i < 131; i++){
        buckets[longBucket*_FPGA_MAX_CLAUSES+i] = firstLongID+i;
    }
    tracker[longBucket] = {131, 0, 131};

    getDeletedClsID(selected, buckets, tracker, protectedLongID, 130,
        removedByBucket);
    unsigned int emitted = 0;
    while(!selected.empty()){
        const cls clauseID = selected.read();
        assert(clauseID != protectedLongID);
        const cls expected = emitted < 64
            ? firstLongID+emitted
            : firstLongID+emitted+1;
        assert(clauseID == expected);
        emitted++;
    }
    assert(emitted == 130);
    assert(tracker[longBucket].usedCount == 1);
    assert(buckets[longBucket*_FPGA_MAX_CLAUSES+
        tracker[longBucket].accessIdx] == protectedLongID);
    assert(removedByBucket[longBucket] == 130);
    return 0;
}
