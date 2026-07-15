# ScaleSAT architecture

## Research hypothesis

ScaleSAT tests whether a learning-aware memory hierarchy can remove the
on-chip capacity wall of a modern FPGA CDCL solver without giving up the
fine-grained BCP throughput that makes SAT-Accel attractive.

## Phase 1: pressure-aware on-chip baseline

The first implementation keeps SAT-Accel's clause and occurrence stores in
URAM so that correctness and performance can be compared directly. It adds:

- independent literal-store and clause-store capacity constants;
- a VCK5000 learned-clause scratchpad of 2,048 literals instead of 1,024;
- bounds-safe conflict merging;
- a clause-store status protocol exposing free elements, clause IDs, and the
  learned-clause count;
- proactive level-zero restart and LBD-based garbage collection at allocator
  low watermarks;
- minimum-headroom and pressure-reset telemetry returned to the host.

The low watermark reserves two worst-case learned clauses. A pressure restart
is therefore requested before the current conflict can exhaust the allocator.
The existing deletion path runs only after conflict analysis has backtracked to
level zero.

### Phase-1 implementation results

The VCK5000 build completed HLS for all seven kernels, hardware implementation,
bitstream generation, DFX packaging, and xclbin generation with zero build
errors. All 20 repository SAT/UNSAT cases passed software emulation. A real-card
SAT smoke test (`SAT_test_cases/sat/aalto.dimacs`) also passed and returned the
new allocator telemetry.

The routed hardware footprint is 115,165 CLB LUTs (12.80%), 142,188 CLB
registers (7.90%), 216.5 BRAM tiles (22.39%), and 414/463 URAMs (89.42%). The
220 MHz requested data clock missed timing; Vitis selected 176 MHz for the
runtime data clock while keeping the kernel/control clock at 500 MHz. This is a
useful phase boundary: increasing monolithic on-chip arrays further would leave
too little placement and routing margin, so phase 2 should spend the remaining
URAM on caches, watch queues, and request coalescing rather than raw capacity.

On this shared host, a subsequent UNSAT smoke test did not reach kernel
execution because a second XRT reprogram operation entered an uninterruptible
driver wait. XRT then reported the hardware as unstable; a user hot reset
completed, but reloading the xclbin still blocked at the same DFX programming
step. That run is not counted as a solver failure because no kernel was
launched and the same case passed software emulation. Real-card regression
should be repeated after an administrative device recovery. The host flow
should also evolve toward one programming operation followed by a batch of CNF
runs, avoiding repeated DFX loads between benchmark cases.

## Phase 2: tiered clause memory

The target VCK5000 design separates storage by access behavior:

| Data | Fast tier | Capacity tier | Policy |
|---|---|---|---|
| Assignments, trail, watch heads | BRAM/URAM | none | always resident |
| Binary and low-LBD clauses | URAM cache | DDR clause arena | pin or high priority |
| Other original/learned clauses | small URAM cache | DDR clause arena | stream/prefetch |
| Clause metadata and allocator state | banked URAM cache | DDR metadata log | batched writeback |

The DDR representation should use segmented variable-length clauses and stable
clause IDs. BCP should move toward two watched literals, banked watch queues,
burst clause fetches, and multiple outstanding NoC requests. Phase 1's separate
capacity constants and status protocol are the migration boundary for this
work.

The installed VCK5000 platform describes four DDR4-3200 channels but exposes
them to Vitis kernels through the aggregate `MC_NOC0` memory tag. The tiered
design therefore should not depend on HBM-style bank tags. It should expose
independent AXI masters for clause payloads, metadata, feature batches, and
writeback, use 512-bit aligned bursts, and keep enough outstanding requests for
the Versal NoC and memory controllers to distribute traffic across channels.
The hot-tier cache and request coalescer must absorb the irregular single-clause
access pattern before it reaches that interface.

## Phase 3: AIE heuristic plane

GNN inference is kept off the correctness-critical BCP path. A PL collector
emits batched variable deltas using the versioned 128-bit packets under
`aie/include/`. An AIE dataflow graph produces quantized branching scores
asynchronously. The PL priority queue accepts a score batch only at a matching
restart epoch and otherwise continues with VSIDS.

PLIO is the preferred low-latency path for feature deltas and result scores;
GMIO is reserved for model weights, static graph data, and large snapshots.
Batching is essential because per-decision AIE synchronization would put the
solver on the communication critical path. The Vitis 2022.2 AIE methodology
supports PLIO, GMIO, and runtime parameters for this split.

## Evaluation contract

Each phase must report correctness, solved count/PAR-2, end-to-end and kernel
time, allocator headroom, learned-clause growth, DDR traffic/cache hit rate,
resource use, Fmax, and board power. AIE experiments must compare against the
same ScaleSAT memory architecture with VSIDS, use fixed model checkpoints, and
include inference/communication overhead.
