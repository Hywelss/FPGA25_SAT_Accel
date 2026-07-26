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

## Phase 2: DDR-backed original-clause tier

The current prototype implements the first capacity tier. Original-clause
payloads are no longer copied into the clause-store URAM at kernel start.
Instead, the host packs them into a 512-bit-aligned buffer in VCK5000 DDR and
the clause-store kernel reads them on demand through two independent AXI
masters. The two readers serve BCP and conflict analysis without sharing an
HLS memory port. A burst loader and a lane unpacker are connected by a dataflow
FIFO; both loops synthesize at II=1.

Clause IDs form a stable and inexpensive tier selector: IDs below the original
clause count refer to the immutable DDR arena, while later IDs refer to learned
clauses in the existing linked-page URAM store. Learned-clause allocation,
deletion, LBD buckets, and location updates therefore keep their existing
low-latency path. The whole 524,288-element clause URAM is now available for
learned clauses instead of being partially occupied by the input CNF. The
VCK5000 host-side original-clause arena is 4,194,304 32-bit elements (16 MiB).

The input buffer is allocated by XRT from the `MC_NOC0` device-memory resource.
Host memory remains the pinned staging tier used to populate that buffer. A
direct host-memory BCP path is intentionally not used: fine-grained clause
misses would add PCIe latency to the correctness-critical propagation loop and
the installed VCK5000 platform exposes device DDR, rather than coherent host
memory, as its supported kernel capacity tier.

HLS and software-emulation validation completed successfully. All 20
repository regression cases passed. Three cases now use more original-clause
payload than the old 524,288-element limit (`qg6-10`: 535,456,
`bmc-ibm-5`: 661,952, and `nqueens_32`: 842,240), demonstrating that the input
payload is fetched from DDR rather than hidden behind an enlarged on-chip
constant.

The VCK5000 hardware build also completed system link, placement, routing,
bitstream generation, DFX packaging, and xclbin generation with zero build
errors. The routed design uses 118,602 CLB LUTs (13.18%), 147,373 CLB
registers (8.19%), 229.5 BRAM tiles (23.73%), and 414/463 URAMs (89.42%). At
the nominal 220 MHz target the routed design had WNS -0.160 ns and no hold
violations; Vitis selected a 214 MHz runtime data clock and retained the 500
MHz kernel/control clock. The added DDR readers therefore did not increase
URAM usage and the packaged design runs at a higher selected data clock than
the 176 MHz phase-1 build.

The first medium-difficulty board target was the SAT Competition 2021 Main
Track instance `randomG-B-Mix-n15-d05`, which previously exhausted learned
clause pages. It did not reach kernel execution on this shared host: XRT first
reported a stale CU deadlock, and after a successful user hot reset the DFX
programming call remained blocked at `Trying to program device`. This is
recorded as an infrastructure block, not a solver failure or timeout. The
medium-instance claim therefore remains a software-emulation/capacity target
until the card receives an administrative recovery and the new xclbin can be
loaded.

### Next memory steps

The complete target design continues to separate storage by access behavior:

| Data | Fast tier | Capacity tier | Policy |
|---|---|---|---|
| Assignments, trail, watch heads | BRAM/URAM | none | always resident |
| Binary and low-LBD clauses | URAM cache | DDR clause arena | pin or high priority |
| Other original/learned clauses | small URAM cache | DDR clause arena | stream/prefetch |
| Clause metadata and allocator state | banked URAM cache | DDR metadata log | batched writeback |

The implemented DDR arena uses stable clause IDs, 512-bit alignment, burst
fetches, and multiple outstanding NoC requests. The next capacity wall is the
URAM occurrence-list store; Phase 3 below removes it. That phase caches
occurrence pages by access rather than moving the immutable original
occurrences wholesale to DDR, because those lists are read on every BCP step and
relegating them all to DDR would slow propagation for every instance. BCP should
then move toward banked watch queues, request coalescing, and a small
low-LBD/original-clause cache.

The installed VCK5000 platform describes four DDR4-3200 channels but exposes
them to Vitis kernels through the aggregate `MC_NOC0` memory tag. The tiered
design therefore should not depend on HBM-style bank tags. It should expose
independent AXI masters for clause payloads, metadata, feature batches, and
writeback, use 512-bit aligned bursts, and keep enough outstanding requests for
the Versal NoC and memory controllers to distribute traffic across channels.
The hot-tier cache and request coalescer must absorb the irregular single-clause
access pattern before it reaches that interface.

## Phase 3: occurrence hot/cold tier

Phase 2 left the URAM occurrence store as the capacity wall. Phase 3 removes it
by making DDR authoritative for the whole occurrence table and turning the
on-chip array into a direct-mapped cache of 512-bit page words.

### Why a cache rather than a static split

Two cheaper arrangements were tried first and rejected.

Packing the occurrence entries (21-bit clause IDs, 24 slots per 512-bit page
instead of 16) is functionally correct -- all 20 regression cases pass with it
enabled -- but timing-infeasible here. The geometry forces non-power-of-two
`/24` and `%24` address arithmetic and unaligned 21-bit slicing through a barrel
shifter, in the II=1 BCP walk. The routed design reached WNS -27.5 ns against a
baseline of -0.16 ns. It survives behind `LIT_STORE_PACK`, off by default.

Spilling instead of packing -- allocate URAM pages first, overflow to DDR --
routes and meets timing, but it is capacity fallback rather than a tiering
policy: placement follows allocation order, so a list that is never read can
hold fast memory while one BCP walks every step sits in DDR, and nothing ever
migrates.

Caching gives residency that follows access. It also avoids the expensive part
of migration: element addresses no longer encode a tier and never change, so
`lmd` and the occurrence-to-clause back-reference map are untouched by promotion
and eviction. A cache line is one page word, exactly the granularity
`colorStream` walks, which additionally makes the second of the two 8-slot chunk
reads of a 16-slot page a guaranteed hit. Writes are write-through and refresh
the line, and BCP never runs concurrently with learning or deletion, so there is
no coherence problem to solve.

### Cold metadata

`location_handler`'s `mLitToClsStorePos` is indexed by occurrence element
address and is read only while clauses are being deleted. Holding it on chip
cost 112 URAMs across the two position maps and, worse, forced it to grow in
lockstep with the occurrence address space -- it, not the occurrence store, was
the real capacity ceiling. Moving it to a device-only DDR buffer freed 48 URAMs
(location_handler 112 -> 64) and removed the ceiling.

It also fixed a latent out-of-bounds write: the map was sized for the URAM-only
address space, so it was overrun exactly when the tier began doing its job. All
20 software-emulation cases passed while that bug was present, because every
regression instance fits on chip and never exercised the tiered paths.

### Verification

`OCC_CACHE_STRESS` shrinks the cache to 64 lines while leaving capacity
untouched, so the existing instances overflow it by two orders of magnitude and
must execute misses, evictions, write-through, page walks that cross tiers and
delete-time compaction of DDR-resident pages, while still producing identical
answers. Under it the learning/minimize trip counts and decision/backtrack
statistics match the unstressed run exactly, so the tier changes where data
lives without perturbing the search.

Two scheduling results were needed to keep the BCP walk at II=1. HLS reserves
the *declared* m_axi latency on every iteration of a pipelined loop, so
`latency=40` on the occurrence port inflated the walk to II=24 whether or not a
miss occurred; a short declared latency lets the schedule stay tight and the AXI
handshake stall only on an actual miss. Separately, because a 16-slot page word
feeds two consecutive 8-slot iterations, a cache fill creates a distance-1
read-after-write on the line the next iteration reads, which costs II=2 on its
own; a small register window of recent (word, value) pairs answers the repeat
access without touching the arrays, the same bypass idiom the `lmd` and
`clsStates` walks already use.

An earlier attempt served misses from a sibling dataflow process over a
request/response stream pair. That reaches II=1 too, but the two processes feed
each other and a Vitis HLS dataflow region must be feed-forward: it built and
routed cleanly, then deadlocked on the board with all seven CUs stuck in START.
It had also been compiled in only under `FPGA_HW`, so software emulation
exercised a different path than the hardware and could not have caught it.
Misses are now served by an inline load, and simulation and hardware run the
same code.

### Phase-3 results

The routed VCK5000 design meets timing at the nominal 220 MHz target with
WNS -0.023 ns and no hold violations, and uses 336 of 463 URAMs (72.6%):
location_handler 64, clause_store_handler 124, solver 148.

| | Phase 2 baseline | Phase 3 |
|---|---|---|
| Occurrence capacity | 524,288 elements | 1,048,576 elements |
| Usable variables | ~16,384 | 32,768 |
| URAM | 414/463 (89.4%) | 336/463 (72.6%) |
| BCP initiation interval | 1 | 1 |
| Routed WNS | -0.160 ns | -0.023 ns |

The variable figure follows from the page layout: each variable keeps a positive
and a negative occurrence list, each padded to a 16-element page, so an instance
costs at least 32 occurrence elements per variable that appears. The Phase-2
store therefore capped usable instances near 16,384 variables regardless of the
32,768-variable literal limit, and Phase 3 unlocks the full range.

On-board results: `bmc-ibm-3`, a SATLIB industrial instance of 14,930 variables
and 72,106 clauses, builds a 538,496-element occurrence image that the Phase-2
store could not have held, reports `EXCEEDS CACHE`, and solves in 1.18 s under a
full load -- 60,765 learning iterations, 2,175 backtracks, 13 restarts and real
garbage collection. `nqueens_32` solves in 61 ms and a 20,000-variable synthetic
instance, whose 638,544-element image is larger still, in 11 ms. The whole
`testcases.sh` suite passes, with `marg3x3add8ch` skipped as the expected
capacity failure: 41 variables but combinatorially brutal, it learns ~198,000
clauses before exhausting the clause store and reports -4 cleanly.

### Two defects this phase exposed

Both predate the occurrence tier and were found by bisecting the committed
bitstreams, which is worth doing before assuming a board failure is a
regression: `git show <commit>:src/bin/workload-hw.xclbin` recovers any earlier
build, and running the last known-good one against the failing instance settles
the question in minutes.

`queryClauseStoreCapacity()`, added with the phase-1 pressure-aware prototype,
sent `csh::STATUS` and then blindly read three replies off the shared clause
store output stream -- on the conflict path, right after BCP has been cut short.
`controlSink` keeps forwarding unit clause IDs after a conflict is detected, so
that stream can still hold length replies nobody consumed; the three reads
swallow those instead, the real reply stays queued, and every later exchange is
off by one until the design wedges. On hardware an instance hung if and only if
it ran at least one query. It is removed: the figures fed telemetry and a
proactive-GC heuristic, while the literal-page pressure check the solver
actually needs is local and costs no round trip. A protocol that cannot
distinguish a reply from a leftover has no safe resynchronisation point.

The phase-2 clause tier then bounded the learned-clause page allocator by the
`maxClauseElements` argument, which the host fills with
`_HOST_MAX_CLAUSE_ELEMENTS` -- the size of the DDR arena holding the original
clauses, eight times the on-chip `mLearnedClsStore` it allocates into. Learning
past 524,288 elements handed out addresses beyond the array and corrupted
whatever URAM followed, so only instances that learn heavily ever saw it. The
allocator is now bounded by the array's own compile-time size. A neighbouring
off-by-one in the admission check is fixed too: `saveData` takes a fresh page
whenever its offset reaches `CLAUSE_PAGE_SIZE-1`, including on the last element
when `numElements` divides evenly, and reads the allocator without testing it
first, while an empty `mmuStream` returns an uninitialised entry rather than
failing.

Software emulation cannot reach either defect. It does not pipeline, so
dependence assertions are satisfied for free, and its kernels are threads with
unbounded streams, so protocol desynchronisation does not deadlock. Anything
whose failure mode is concurrency or scheduling has to be reasoned about, not
simulated, until hardware emulation is available for this platform.

### Remaining work

The 127 free URAMs are the budget for the clause-side hot tier the table above
still calls for: pinning binary and low-LBD clauses, a small original-clause
cache, and request coalescing in front of the NoC. The occurrence cache is
direct-mapped, so conflict misses are possible even when capacity would suffice;
set associativity, or biasing residency by VSIDS activity, would address that.

## Phase 4: AIE heuristic plane

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
