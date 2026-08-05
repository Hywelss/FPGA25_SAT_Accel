# Inductor Stage A

## Goal

Close the single-engine rIC3 loop with SAT-Accel as a one-shot SAT backend.
Stage A prioritizes functional correctness over incremental-query performance.

## VCK5000 runbook

- Use the checked-in `src/bin/workload-hw.xclbin` from the
  `vck5000-adaptation` branch for Stage A.
- Do not rebuild the xclbin or reflash the card while validating the existing
  kernel. Rebuild and reload only after a kernel change requires it.
- Load XRT before running host tools:

  ```bash
  source /opt/xilinx/xrt/setup.sh
  ```

- Run XRT tools from a process that has inherited the `render` group. After a
  recent group-membership change, start a new login session or use
  `sg render -c '<command>'`.
- Check the card with `xbutil examine`. If XRT reports that a compute unit was
  deadlocked, reset the user function without `sudo` using
  `xbutil reset --device <user-BDF>`, then examine again. On this machine the
  management BDF is `0000:91:00.0` and the user BDF is `0000:91:00.1`.
- Loading an xclbin through the host program is a runtime operation; it is not
  a persistent card flash.

## Stage A acceptance

1. The checked-in xclbin returns the correct verdict on representative SAT and
   UNSAT DIMACS inputs.
2. A one-shot backend can submit `T + frame clauses + temporary clauses +
   assumptions-as-unit-clauses` to SAT-Accel.
3. SAT returns the model data required by rIC3. UNSAT returns the actual
   assumption core traced by the FPGA from the final conflict and propagation
   reasons. An extraction failure is an error; it never falls back to the full
   assumption set.
4. Small rIC3 models reach the same final verdict as the GipSAT baseline.

## Build only the Stage A host

The Stage A protocol requires the modified host executable, but it does not
require a kernel rebuild. This command compiles only the OpenCL host and leaves
`src/bin/workload-hw.xclbin` untouched:

```bash
./build_stage_a_host.sh
```

The default output is `build/stage-a/inductor-sat-host`. Override Xilinx paths
with `XRT_ROOT` and `VITIS_HLS_ROOT` if required.

## Run rIC3 with SAT-Accel

The SAT-Accel backend is opt-in. With these variables unset, rIC3 continues to
use GipSAT. From the rIC3 checkout, enable the VCK5000 backend with:

```bash
export INDUCTOR_SAT_ACCEL_HOST=/path/to/FPGA25_SAT_Accel/build/stage-a/inductor-sat-host
export INDUCTOR_SAT_ACCEL_XCLBIN=/path/to/FPGA25_SAT_Accel/src/bin/workload-hw.xclbin
export INDUCTOR_SAT_ACCEL_CONFIG=/path/to/FPGA25_SAT_Accel/src/configuration.json
export INDUCTOR_SAT_ACCEL_DEVICE=vck5000
export INDUCTOR_XRT_ROOT=/opt/xilinx/xrt
```

Each IC3 query is submitted as a complete one-shot DIMACS formula containing
the transition relation, permanent frame lemmas, temporary clauses, and
assumptions as unit clauses. SAT models are returned to rIC3. For UNSAT, the
FPGA returns only assumptions reached by tracing the final conflict through
the assignment trail and reason clauses.

For query capture, enable:

```bash
export INDUCTOR_SAT_ACCEL_CAPTURE_DIR=/path/to/capture
```

The production interface does not invoke a CPU SAT solver. It validates each
SAT model against the submitted clauses and validates every UNSAT-core literal
against the input assumptions. Capture writes matching `.cnf` and `.result`
files without overwriting an existing query. Software-emulation tests use
fixed expected verdicts and cores; they do not invoke a CPU SAT solver.

## Single-session query replay

The Stage A host can replay captured queries while programming the device only
once. The manifest is strict TSV; each non-comment line contains the expected
answer (`0` for UNSAT or `1` for SAT), one tab, and a CNF path. Relative paths
are resolved from the manifest directory.

```bash
build/stage-a/inductor-sat-host --batch \
  src/bin/workload-hw.xclbin src/configuration.json manifest.tsv metrics.csv
```

This mode reuses the XRT context, program, command queue, and kernel handles.
It still allocates fresh buffers and relaunches all cooperating kernels for
every query. It is therefore session reuse, not incremental SAT.

The same session can serve online rIC3 queries. This is opt-in and currently
intended for the single-process `ic3` flow:

```bash
export INDUCTOR_SAT_ACCEL_SESSION=1
ric3 check model.aig ic3
```

rIC3 starts one host process, waits until it has programmed the device exactly
once, and serializes query paths over a request-ID protocol. All differential
verification, model validation, and capture checks remain active in this mode.

## Validation record (2026-08-04)

The VCK5000 reported `Ready: Yes`. Validation used the existing checked-in
`src/bin/workload-hw.xclbin`; it was not rebuilt or reflashed. Its SHA-256 was:

```text
8a97cc5499d401d1bcbcbabee852ef01a89533f4b11691abcc691edd0f4da312
```

- `benchmark/unif-r3-v500-c1500-01.cnf`: SAT, complete 500-variable model,
  approximately 1.17 ms kernel time.
- `SAT_test_cases/satlib/hole7_unsat.cnf`: UNSAT, approximately 75.05 ms kernel
  time.
- rIC3 `cnt1e.aag`: five SAT-Accel queries, final SAT verdict matching GipSAT.
- rIC3 `mult2.aig`: thirteen SAT-Accel queries, final UNSAT verdict matching
  GipSAT. This run exercised temporary clauses, up to four assumptions, SAT
  models, and conservative full-assumption UNSAT cores.

Additional validation on 2026-08-04 used a five-query corpus captured from
`cnt1e.aag`. One batch process produced the expected sequence UNSAT, SAT,
UNSAT, SAT, UNSAT. The xclbin was programmed once. Host wall time was about
0.657 seconds total: about 0.644 seconds for the first query and about 3
milliseconds for each later query; kernel times were 59--159 microseconds.

With model checking, oracle verification, and capture all enabled, `mult2.aig`
again completed UNSAT on VCK5000. Its preprocessing path issued seven FPGA
queries (two SAT and five UNSAT), including queries with two assumptions and
queries with two or four temporary clauses. Every verdict matched CaDiCaL.

Enabling the online session for the same `mult2.aig` path reduced end-to-end
time from about 12.43 seconds to 2.55 seconds while preserving the seven query
verdicts and final UNSAT result. This measures avoided host/xclbin startup; it
does not claim kernel-state reuse.

## Unified hardware checkpoint (2026-08-06)

The domain-restricted decisions, bucket-based variable selection, temporary
clause reuse and cleanup, activation-clause learning, and FPGA UNSAT-core
extraction were built together for VCK5000. Before the hardware build:

- ten focused C++ tests passed;
- the nine-query incremental software-emulation manifest passed;
- all five cross-query reuse manifests passed;
- four UNSAT-core software-emulation cases passed; and
- the rIC3 `cnt1e` and `mult2` closed loops passed through the FPGA software-
  emulation path without a CPU SAT fallback.

The hardware link and packaging completed with zero VPL check errors. The
resulting artifacts are:

```text
test.real.out       0d94cdf9e4ede494971521a74756460eef8ef2440c393df5d5ba2c1c43bca407
workload-hw.xsa     07f0252f38186adb98fbab25737ef1578aac0afcdbcb5d0bfe659bff8e101340
workload-hw.xclbin  e7b1828e6fdc61fe87faf9886653c8f44d7c95d7a1c08c7fc7e812448d619822
```

The routed design did not meet the requested 220 MHz data clock. Its worst
setup slack was -1.299 ns against a 4.500 ns period, so Vitis selected 172 MHz.
The worst path is the original SAT-Accel `colorStream` page-walk index update,
not the added domain, bucket, temporary-clause, or UNSAT-core logic. Hold
timing passed. This checkpoint is functionally complete but remains the
pre-frequency-optimization baseline; it must not be reported as 220 MHz.
