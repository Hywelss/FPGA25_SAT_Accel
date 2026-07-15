#ifndef SCALESAT_HEURISTIC_PROTOCOL_H
#define SCALESAT_HEURISTIC_PROTOCOL_H

#include <stdint.h>

// Versioned, fixed-width packets shared by a future PL feature collector and
// an AI Engine GNN inference graph.  Keeping the protocol independent of HLS
// ap_int types allows the same header to be used by host, PL and AIE code.
namespace scalesat {

static constexpr uint16_t HEURISTIC_PROTOCOL_VERSION = 1;

enum FeatureFlags : uint16_t {
    FEATURE_ASSIGNED = 1u << 0,
    FEATURE_PHASE = 1u << 1,
    FEATURE_IS_DECISION = 1u << 2,
    FEATURE_IS_FIXED = 1u << 3,
    FEATURE_RESTART_BOUNDARY = 1u << 4,
    FEATURE_END_OF_BATCH = 1u << 15
};

// One 128-bit PLIO word per variable delta.  Occurrence counts saturate at
// UINT16_MAX; activity uses signed Q16.16 fixed point.
struct alignas(16) VariableFeaturePacket {
    uint32_t variableId;
    uint16_t positiveOccurrences;
    uint16_t negativeOccurrences;
    int32_t activityQ16_16;
    uint16_t decisionLevel;
    uint16_t flags;
};

// One 128-bit result word.  Scores are applied only at a solver-defined epoch
// boundary so delayed AIE inference cannot stall or reorder the CDCL pipeline.
struct alignas(16) HeuristicScorePacket {
    uint32_t variableId;
    int32_t scoreQ16_16;
    uint32_t epoch;
    uint16_t confidenceQ0_16;
    int8_t preferredPhase;
    uint8_t flags;
};

static_assert(sizeof(VariableFeaturePacket) == 16, "feature packet must be 128 bits");
static_assert(sizeof(HeuristicScorePacket) == 16, "score packet must be 128 bits");

} // namespace scalesat

#endif
