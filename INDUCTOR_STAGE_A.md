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
- Check the card with `xbutil examine`. If XRT still reports no devices in the
  correct device-group context, locate the VCK5000 management and user PCIe
  functions with `lspci`, reset the management function with
  `xbmgmt reset --device <management-BDF>`, and examine again. On this machine
  the management BDF is `0000:91:00.0` and the user BDF is `0000:91:00.1`.
- Loading an xclbin through the host program is a runtime operation; it is not
  a persistent card flash.

## Stage A acceptance

1. The checked-in xclbin returns the correct verdict on representative SAT and
   UNSAT DIMACS inputs.
2. A one-shot backend can submit `T + frame clauses + temporary clauses +
   assumptions-as-unit-clauses` to SAT-Accel.
3. SAT returns the model data required by rIC3. UNSAT may initially return the
   complete assumption set as a valid, non-minimal core.
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
assumptions as unit clauses. SAT models are returned to rIC3. For UNSAT, Stage
A conservatively marks every assumption as part of the core.

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
