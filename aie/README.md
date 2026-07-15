# ScaleSAT AI Engine integration

This directory defines the interface for a future GNN-based branching
heuristic. It is intentionally not part of the current hardware build: phase 1
first establishes a correct, pressure-aware CDCL memory baseline.

The protocol in `include/scalesat_heuristic_protocol.h` uses 128-bit feature
and score packets. The intended VCK5000 path is:

```text
PL CDCL counters -> delta batcher -> 128-bit PLIO -> AIE GNN graph
PL priority queue <- epoch score arbiter <- 128-bit PLIO <- AIE GNN graph
```

Only changed variables should be streamed during solving. Static graph data and
model weights can be staged through DDR/GMIO. Scores are tagged with an epoch
and consumed at restart boundaries; the existing VSIDS path remains the
fallback if an AIE result misses its deadline.

The AIE graph will be added as a separate build target after a software model,
quantization scheme, and replayable heuristic trace have been validated. AMD's
Vitis 2022.2 flow supports PLIO streams, GMIO access to global memory, and RTP
configuration for this decomposition:

- <https://docs.amd.com/r/2022.2-English/ug1273-versal-acap-design/Developing-and-Verifying-the-AI-Engine-Graph>
- <https://docs.amd.com/r/2022.2-English/ug1079-ai-engine-kernel-coding/Configuring-input_plio/output_plio>
