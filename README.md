# FPGA25_artifact
## Instructions:

Following needs to be installed and is the version used for our design:  
-XRT version 2.14.384  
-Vitis version 2022.2  
-xilinx_vck5000_gen4x8_qdma_2_202220_1 platform (default)
-gcc/g++ version 10+  

`runCompile.sh` compiles the host program and HLS kernels and generates the
bitstream. Binary artifacts are platform- and branch-specific; rebuild them
after changing either the target or a kernel.

The build script defaults to the paths used on this machine. They can be
overridden without editing the script:

```sh
XRT_ROOT=/path/to/xrt VITIS_ROOT=/path/to/Vitis/2022.2 \
VITIS_HLS_ROOT=/path/to/Vitis_HLS/2022.2 ./runCompile.sh hls
```
To build for real hardware:  
```sh
./runCompile.sh hls && ./runCompile.sh hw
```

VCK5000 uses the `MC_NOC0` memory tag rather than the U55C `HBM[n]` tags.
`runCompile.sh` selects `src/k2k_vck5000.cfg` automatically. The installed
2022.2 VCK5000 platform does not support hardware emulation; use
`./runCompile.sh sw_emu` for software emulation. For hardware, the script
links a Versal `.xsa` first and then packages `src/bin/workload-hw.xclbin`.
The VCK5000 hardware link defaults to 220 MHz (override with `FREQ_SC`). A
235 MHz trial routed successfully but Vitis automatically scaled it to
223 MHz, so 220 MHz avoids relying on automatic frequency scaling. The U55C
default remains 235 MHz.
Because VCK5000 provides 463 URAMs versus 960 on U55C, the VCK5000 build sets
`FPGA_VCK5000` and reduces `_FPGA_MAX_LITERAL_ELEMENTS` from 1,048,576 to
524,288. The host and every kernel receive the same compile-time definition.

To run all emulation modes supported by the selected platform:
```sh
./runCompile.sh doall
```

## To run hardware execution after hardware build:

VCK5000 uses device DDR through `MC_NOC0`; host-memory configuration is not
required. When building for the original U55C platform, enable its host memory
once with:

```sh
sudo PATH_TO_XBUTIL/xbutil configure --host-mem -d DEVICE_ID -s 1G ENABLE  (only need to do once)
```
where PATH_TO_XBUTIL is the install path for xbutil and DEVICE_ID is the device
id reported by `xbutil examine`. If several Xilinx cards are installed, set
`FPGA_DEVICE_NAME` to a unique substring of the desired OpenCL device name.

### To run provided testcases:
-First compile openCL with ./runCompile.sh opencl  
-Then run the ./testcases.sh  
-A .txt file (which is in CSV format) called answers.txt will be found in src/bin  
-ColumnJ is the FPGA runtime in seconds.  
-Please note answers.txt is an appended file. Therefore, it is advised to remove it for new runs.

### To run other SAT instances:
```sh
cd src/bin
./test.real.out workload-hw.xclbin ../configuration.json <YOU_SAT_DIMACS> <SAVE_METRICS_FILE.TXT> <0_OR_1>
```

-To verify our solver, the SAT instance needed to match other solvers. Therefore, you must know beforehand if it is SAT(1) or UNSAT(0).  
-You should modify the host.cpp in src to your desire to not do this check.  
-If host.cpp is modified, remember to recompile with ./runCompile.sh opencl  

## Benchmark

The [`benchmark/`](benchmark/) directory contains SAT Competition instances,
their original compressed archives, SHA256 checksums, a reproducible VCK5000
runner, raw CSV/log output, and a detailed
[`results/summary.md`](benchmark/results/summary.md).

### Capacity audit of the provided test cases

The 78 version-controlled instances under `SAT_test_cases/` were scanned using
the same clause padding and literal-page allocation rules as `host.cpp`. All 78
fit the VCK5000 build statically, but the collection is weighted toward small
and medium instances:

| Metric | Median | Maximum |
|---|---:|---:|
| Variables | 256 | 17,303 |
| Clauses | 1,692 | 88,373 |
| Literal-store elements | 13,728 | 402,496 / 524,288 |
| Clause-store elements | 9,772 | 488,216 / 524,288 |

Of these inputs, 54/78 have at most 1,000 variables and 69/78 have at most
50,000 clauses. The largest variable count is in `ssa6288-047.dimacs`; the
largest clause count is in
`logistics-rotate-07t5.shuffled-as.sat05-1137.dimacs`. The most storage-heavy
input, `16_8_7.txt`, consumes 93.1% of the VCK5000 clause store before solving
starts, leaving only 36,072 elements for learned clauses.

Static fit does not guarantee that an instance can be solved. The baseline
`vck5000-adaptation` hardware limits are 32,768 variables, 131,072 clauses,
524,288 literal/clause-store elements, and 1,024 literals in one learned
clause. The experimental `scalesat-ddr-clause-tier` branch raises the learned
clause limit to 2,048 and places immutable original-clause payloads in a
4,194,304-element (16 MiB) VCK5000 DDR arena. The 524,288-element clause URAM
is consequently dedicated to learned clauses. Original occurrence lists still
use the 524,288-element literal URAM and are now the principal static capacity
limit. Difficult searches can still exhaust dynamic learned-clause or
learned-occurrence pages. `testcases.sh` treats host exit code 3 as an expected
capacity failure and skips that instance.

### VCK5000 measurements

Measurements below used the VCK5000 xclbin at a 223 MHz data clock. The
reduced-difficulty SAT Competition 2003 Random Track instance completed and
validated successfully in all five runs:

| Instance | Result | Size | Runs | Kernel median | Kernel range |
|---|---:|---:|---:|---:|---:|
| `unif-r3-v500-c1500-01` | SAT | 500 variables / 1,500 clauses | 5/5 | 0.777118 ms | 0.770218--1.097930 ms |

Each successful run followed the same search path: 240 decisions, 44 retries,
and 44 backtracks. End-to-end host wall time was 1.18--1.90 seconds because it
also includes loading the xclbin and programming the card.

Modern difficult SAT Competition instances exposed two VCK5000 resource
limits instead of completing: difficult UNSAT cases exhausted learned-clause
pages (`-4`), while two difficult SAT cases generated learned clauses longer
than 1,024 literals (`-2`). Their short kernel runtimes are time to a detected
resource limit, not solution times, and must not be used for speedup claims.
See the benchmark summary and raw logs for the complete measurements.

## ScaleSAT development

The `scalesat-ddr-clause-tier` branch is an experimental successor to the
VCK5000 port. Its first phase introduces separate literal/clause capacities,
pressure-aware learned-clause garbage collection, allocator telemetry, a
2,048-literal conflict scratchpad on VCK5000, and bounds-safe conflict merging.
Its second phase streams immutable original clauses from VCK5000 DDR through
two 512-bit AXI readers while keeping learned clauses in URAM.
The design and staged DDR/AIE roadmap are documented in
[`docs/scalesat_architecture.md`](docs/scalesat_architecture.md). A versioned
128-bit contract for a future asynchronous GNN branching heuristic is under
[`aie/`](aie/); it is not yet connected to the solver or included in the
hardware build.

Phase-1 verification on VCK5000 (Vitis/XRT 2022.2):

- all seven HLS kernels built successfully and all 20 provided SAT/UNSAT cases
  passed software emulation;
- hardware link, route, bitstream generation, DFX packaging, and xclbin
  generation completed with zero errors;
- the routed design uses 115,165 CLB LUTs (12.80%), 142,188 CLB registers
  (7.90%), 216.5 BRAM tiles (22.39%), and 414 of 463 URAMs (89.42%);
- the requested 220 MHz data clock did not close timing, so Vitis selected a
  runtime data clock of 176 MHz; the independent kernel/control clock remains
  500 MHz;
- the on-board `aalto.dimacs` SAT smoke test passed and reported the new 2,048
  learned-clause limit and capacity telemetry.

Phase-2 verification on VCK5000 (Vitis/XRT 2022.2):

- both DDR burst-load and lane-unpack loops synthesize at II=1;
- all 20 SAT/UNSAT software-emulation regressions pass, including original
  clause payloads of 535,456, 661,952, and 842,240 elements, all above the old
  524,288-element clause-store limit;
- hardware link, route, bitstream, DFX package, and xclbin generation complete
  with zero build errors;
- the routed design uses 118,602 CLB LUTs (13.18%), 147,373 CLB registers
  (8.19%), 229.5 BRAM tiles (23.73%), and 414 of 463 URAMs (89.42%);
- the nominal 220 MHz route has WNS -0.160 ns and no hold violations; Vitis
  selects a 214 MHz runtime data clock and retains the 500 MHz control clock.

The first board attempt for the medium-difficulty SAT Competition 2021
`randomG-B-Mix-n15-d05` instance did not reach kernel execution. XRT reported
a stale CU deadlock; a user hot reset restored `Device Ready`, but the DFX load
then remained blocked at `Trying to program device`. This is a shared-host/card
infrastructure block, not a solver result. The new xclbin should be tested on
`randomG-B-Mix-n15-d05` and `sp5-26-19-bin-nons-tree-noid` after an
administrative device recovery.

Phase-3 verification on VCK5000 (Vitis/XRT 2022.2):

- the occurrence table is now DDR-authoritative with the on-chip array acting as
  a direct-mapped cache of 512-bit page words, so residency follows access;
- the delete-time occurrence-to-clause back-reference map moved to DDR, which
  freed 48 URAMs and removed the ceiling that made occurrence capacity grow with
  on-chip metadata;
- occurrence capacity rises from 524,288 to 1,048,576 elements, lifting the
  usable variable count from roughly 16,384 to the full 32,768 (each variable
  costs at least 32 occurrence elements: two lists padded to 16-element pages);
- all 20 cases pass software emulation both normally and under
  `EXTRA_DEFINES=-DOCC_CACHE_STRESS`, which shrinks the cache to 64 lines so the
  small instances are forced through the miss, eviction, write-through and
  cross-tier walk paths while returning identical answers;
- the BCP walk holds II=1; the routed design has WNS -0.163 ns against the
  nominal 220 MHz target, level with the phase-2 baseline's -0.160 ns, and uses
  336/463 URAMs (72.6%) against 414 in phase 2;
- `bmc-ibm-3` (SATLIB industrial, 14,930 variables) builds a 538,496-element
  occurrence image the phase-2 store could not have held, reports
  `EXCEEDS CACHE`, and solves on the board in 1.18 s under a full load: 60,765
  learning iterations, 2,175 backtracks, 13 restarts and real garbage
  collection;
- `nqueens_32` solves in 61 ms, a 20,000-variable synthetic instance whose
  638,544-element image is larger still in 11 ms, and the whole `testcases.sh`
  suite passes.

Two defects that predate the tier surfaced during this work and are fixed: the
phase-1 `csh::STATUS` query, whose blind three-word reply desynchronised the
clause-store streams on the conflict path and hung the board, and the phase-2
learned-clause allocator, bounded by the DDR arena's size rather than the
on-chip array it allocates into. See `docs/scalesat_architecture.md`.

`randomG-B-Mix-n15-d05` and `sp5-26-19-bin-nons-tree-noid` remain untried.

To reproduce the successful run from the repository root:

```sh
CNF_FILE="$PWD/benchmark/unif-r3-v500-c1500-01.cnf" \
EXPECTED_RESULT=1 \
./benchmark/run_vck5000.sh \
  "$PWD/benchmark/results/unif-r3-v500-c1500-01_vck5000_metrics.csv"
```

## To run MiniSat or Kissat:  
-First clone repository and follow the install instructions provided by those authors.  
https://github.com/niklasso/minisat  
https://github.com/arminbiere/kissat  
-Locate the installed executables and run each individual SAT instance, for example:  
```sh
./minisat PATH_TO_DIMACS/unsat/4_4_2.dimacs  
./kisat PATH_TO_DIMACS/unsat/4_4_2.dimacs
```
where PATH_TO_DIMACS is the folder SAT_test_cases in this repository.  

## Publication
To cite this work:

```bibtex
@inproceedings{10.1145/3706628.3708869,
author = {Lo, Michael and Chang, Mau-Chung Frank and Cong, Jason},
title = {SAT-Accel: A Modern SAT Solver on a FPGA},
year = {2025},
isbn = {9798400713965},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3706628.3708869},
doi = {10.1145/3706628.3708869},
abstract = {Boolean satisfiability (SAT) solving is the first known NP-complete problem and is widely used in many application domains. Over the years, there have been so many consistent improvements in this area such that larger instances can be solved relatively quickly. Although these improvements have found their way onto CPU implementations, there has been limited progress adopting this on hardware accelerators mainly because it is difficult to implement the dynamic data structures needed to support a modern SAT solving algorithm. In this work, we present SAT-Accel, an algorithm-hardware co-design solver that applies many of the core improvements found in modern SAT solvers. SAT-Accel uses a novel memory management system and representation that supports the dynamic data structures required by a modern SAT solving algorithm. Our design can achieve on average a 17.9x speedup against MiniSat, the previous state of the art CPU solver, and a 2.8x speedup against Kissat, the current state of the art CPU solver. Compared to the current state-of-the-art stand-alone hardware accelerator, SAT-Hard, SAT-Accel achieves on average 800.0x speedup.},
booktitle = {Proceedings of the 2025 ACM/SIGDA International Symposium on Field Programmable Gate Arrays},
pages = {234–246},
numpages = {13},
keywords = {accelerator, boolean satisfiability, fpga, high-level synthesis},
location = {Monterey, CA, USA},
series = {FPGA '25}
}
```
