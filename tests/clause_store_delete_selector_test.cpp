#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include "data_structures.h"

void getDeletedClsID(hls::stream<cls>& removeIDStream,
    cls* usedClsIDBuckets, minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS],
    cls lastInsertedID, unsigned int removeTotal,
    unsigned int LBDBucketCount[_FPGA_MAX_LBD_BUCKETS]);

namespace {

void require(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<cls> drain(hls::stream<cls>& stream){
    std::vector<cls> result;
    while(!stream.empty()){
        result.push_back(stream.read());
    }
    return result;
}

void clearTrackers(minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS],
    unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS]){
    for(unsigned int i = 0; i < _FPGA_MAX_LBD_BUCKETS; i++){
        tracker[i] = {0, 0, 0};
        removedByBucket[i] = 0;
    }
}

void testProtectedClauseDoesNotHideOlderEntries(){
    std::vector<cls> buckets(_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES, 0);
    minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
    unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];
    clearTrackers(tracker, removedByBucket);

    const unsigned int high = _FPGA_MAX_LBD_BUCKETS-1;
    buckets[high*_FPGA_MAX_CLAUSES+0] = 90;
    buckets[high*_FPGA_MAX_CLAUSES+1] = 99;
    buckets[high*_FPGA_MAX_CLAUSES+2] = 91;
    buckets[high*_FPGA_MAX_CLAUSES+3] = 92;
    tracker[high] = {4, 0, 4};

    const unsigned int lower = _FPGA_MAX_LBD_BUCKETS-2;
    buckets[lower*_FPGA_MAX_CLAUSES+0] = 80;
    buckets[lower*_FPGA_MAX_CLAUSES+1] = 81;
    tracker[lower] = {2, 0, 2};

    hls::stream<cls> selected;
    getDeletedClsID(selected, buckets.data(), tracker, 99, 4,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({90, 91, 92, 80}),
        "selection must continue past a protected clause and then use lower buckets");
    require(tracker[high].usedCount == 1,
        "only the protected clause must remain in the high bucket");
    require(buckets[high*_FPGA_MAX_CLAUSES+tracker[high].accessIdx] == 99,
        "the protected clause must remain at the ring head");
    require(tracker[lower].usedCount == 1,
        "the lower bucket must retain its unselected tail");

    getDeletedClsID(selected, buckets.data(), tracker, 99, 1,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({81}),
        "a later pruning session must make progress past the same protected clause");
    require(removedByBucket[high] == 3 && removedByBucket[lower] == 2,
        "per-bucket statistics must count each selected clause exactly once");
}

void testProtectedClauseAcrossRingWrap(){
    std::vector<cls> buckets(_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES, 0);
    minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
    unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];
    clearTrackers(tracker, removedByBucket);

    const unsigned int bucket = 7;
    const unsigned int first = _FPGA_MAX_CLAUSES-2;
    buckets[bucket*_FPGA_MAX_CLAUSES+first] = 70;
    buckets[bucket*_FPGA_MAX_CLAUSES+first+1] = 79;
    buckets[bucket*_FPGA_MAX_CLAUSES+0] = 71;
    tracker[bucket] = {1, first, 3};

    hls::stream<cls> selected;
    getDeletedClsID(selected, buckets.data(), tracker, 79, 2,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({70, 71}),
        "selection must remain ordered across a wrapped ring");
    require(tracker[bucket].usedCount == 1,
        "wrapped selection must preserve exactly the protected clause");
    require(buckets[bucket*_FPGA_MAX_CLAUSES+tracker[bucket].accessIdx] == 79,
        "wrapped protected clause must remain reachable");
}

void testPartialRemovalAfterProtectedClauseKeepsRingContiguous(){
    std::vector<cls> buckets(_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES, 0);
    minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
    unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];
    clearTrackers(tracker, removedByBucket);

    const unsigned int bucket = _FPGA_MAX_LBD_BUCKETS-1;
    buckets[bucket*_FPGA_MAX_CLAUSES+0] = 90;
    buckets[bucket*_FPGA_MAX_CLAUSES+1] = 99;
    buckets[bucket*_FPGA_MAX_CLAUSES+2] = 91;
    buckets[bucket*_FPGA_MAX_CLAUSES+3] = 92;
    tracker[bucket] = {4, 0, 4};

    hls::stream<cls> selected;
    getDeletedClsID(selected, buckets.data(), tracker, 99, 2,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({90, 91}),
        "partial selection must continue past the protected clause");
    require(tracker[bucket].usedCount == 2,
        "partial selection must retain the unselected tail and protected clause");
    require(buckets[bucket*_FPGA_MAX_CLAUSES+tracker[bucket].accessIdx] == 92,
        "the unselected tail must remain at the ring head");
    require(buckets[bucket*_FPGA_MAX_CLAUSES+
        ((tracker[bucket].accessIdx+1)%_FPGA_MAX_CLAUSES)] == 99,
        "the protected clause must follow the unselected tail without a hole");

    getDeletedClsID(selected, buckets.data(), tracker, 99, 1,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({92}),
        "the next pruning pass must reach the retained tail");
    require(tracker[bucket].usedCount == 1 &&
        buckets[bucket*_FPGA_MAX_CLAUSES+tracker[bucket].accessIdx] == 99,
        "only the protected clause must remain after the second pass");
}

void testEmptyHighBucketsAndAbsentProtection(){
    std::vector<cls> buckets(_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES, 0);
    minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
    unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];
    clearTrackers(tracker, removedByBucket);

    buckets[2*_FPGA_MAX_CLAUSES] = 20;
    buckets[1*_FPGA_MAX_CLAUSES] = 10;
    tracker[2] = {1, 0, 1};
    tracker[1] = {1, 0, 1};

    hls::stream<cls> selected;
    getDeletedClsID(selected, buckets.data(), tracker, 1234, 2,
        removedByBucket);
    require(drain(selected) == std::vector<cls>({20, 10}),
        "empty higher buckets must be skipped without unsigned underflow");
    require(tracker[2].usedCount == 0 && tracker[1].usedCount == 0,
        "all selected entries must be removed from their trackers");
}

void testRandomizedSelectionAlwaysMatchesDeclaredCount(){
    std::vector<cls> buckets(_FPGA_MAX_LBD_BUCKETS*_FPGA_MAX_CLAUSES, 0);
    std::mt19937 random(0x51ec7u);

    for(unsigned int trial = 0; trial < 300; trial++){
        minimumStreamTracker tracker[_FPGA_MAX_LBD_BUCKETS];
        unsigned int removedByBucket[_FPGA_MAX_LBD_BUCKETS];
        clearTrackers(tracker, removedByBucket);
        std::set<cls> liveIDs;
        cls nextID = 1;

        for(unsigned int bucket = 0; bucket < _FPGA_MAX_LBD_BUCKETS; bucket++){
            const unsigned int used = random()%17;
            const unsigned int access = random()%_FPGA_MAX_CLAUSES;
            tracker[bucket].accessIdx = access;
            tracker[bucket].insertIdx = (access+used)%_FPGA_MAX_CLAUSES;
            tracker[bucket].usedCount = used;
            for(unsigned int offset = 0; offset < used; offset++){
                buckets[bucket*_FPGA_MAX_CLAUSES+
                    (access+offset)%_FPGA_MAX_CLAUSES] = nextID;
                liveIDs.insert(nextID++);
            }
        }

        cls protectedID = _FPGA_MAX_CLAUSES+1;
        if(!liveIDs.empty() && (random()&1u) != 0){
            auto selected = liveIDs.begin();
            std::advance(selected, random()%liveIDs.size());
            protectedID = *selected;
        }
        const unsigned int deletable = liveIDs.size() -
            (liveIDs.count(protectedID) != 0 ? 1u : 0u);
        const unsigned int requested = deletable == 0
            ? 0 : random()%(deletable+1);

        hls::stream<cls> selected;
        getDeletedClsID(selected, buckets.data(), tracker, protectedID,
            requested, removedByBucket);
        const std::vector<cls> removed = drain(selected);
        require(removed.size() == requested,
            "selector must emit exactly the declared removal count");
        std::set<cls> uniqueRemoved(removed.begin(), removed.end());
        require(uniqueRemoved.size() == removed.size(),
            "selector must not emit a clause twice in one pruning pass");
        require(uniqueRemoved.count(protectedID) == 0,
            "selector must never emit the protected newest clause");
        for(const cls clauseID : removed){
            require(liveIDs.count(clauseID) != 0,
                "selector must only emit live bucket entries");
        }

        unsigned int remaining = 0;
        for(unsigned int bucket = 0; bucket < _FPGA_MAX_LBD_BUCKETS; bucket++){
            remaining += tracker[bucket].usedCount;
        }
        require(remaining == liveIDs.size()-requested,
            "tracker count must decrease by exactly the emitted count");
    }
}

} // namespace

int main(){
    testProtectedClauseDoesNotHideOlderEntries();
    testProtectedClauseAcrossRingWrap();
    testPartialRemovalAfterProtectedClauseKeepsRingContiguous();
    testEmptyHighBucketsAndAbsentProtection();
    testRandomizedSelectionAlwaysMatchesDeclaredCount();
    std::cout << "CLAUSE_STORE_DELETE_SELECTOR_TEST_PASS\n";
    return 0;
}
