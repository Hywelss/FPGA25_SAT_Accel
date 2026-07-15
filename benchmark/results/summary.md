# VCK5000 benchmark results

Run date: 2026-07-15

Board: AMD/Xilinx VCK5000

Platform: `xilinx_vck5000_gen4x8_qdma_2_202220_1`

XRT: 2.14.354

xclbin data clock: 223 MHz

The reduced-difficulty SAT Competition 2003 instance completed successfully in
all five runs. The selected modern difficult instances did not complete with
the current VCK5000 build because they reached dynamic-storage or learned-clause
length limits.

## Successful competition run

| Instance | Track | Result | Runs | Kernel min | Kernel median | Kernel mean | Kernel max |
|---|---|---:|---:|---:|---:|---:|---:|
| `unif-r3-v500-c1500-01` | SAT Competition 2003 Random | SAT | 5/5 | 0.770218 ms | 0.777118 ms | 0.845174 ms | 1.097930 ms |

Each successful run followed the same search path: 328 total iterations, 240
decisions, 44 retries, 44 backtracks, and no restart. Host wall time was
1.18--1.90 seconds because each process also loaded the xclbin and programmed
the card; kernel time is the relevant accelerator solve measurement.

## Resource-limit runs

The kernel times below are **time to a detected hardware solver limit**, not
solution times and not valid speedup measurements.

| Instance | Expected | Configuration | Kernel time | Host wall | Progress | Result |
|---|---:|---|---:|---:|---:|---|
| `rphp4_065_shuffled` | UNSAT | default (10% prune, reset 100) | 2.361963 s | 4.26 s | 1,331,396 iterations | `-4`: learned-clause page allocation exhausted |
| `rphp4_065_shuffled` | UNSAT | aggressive (50% prune, reset 50) | 3.069535 s | 4.36 s | 3,124,242 iterations | `-4`: learned-clause page allocation exhausted |
| `sp4-33-bin-nons-flat-noid` | UNSAT | default | 2.806268 s | 4.36 s | 150,993 iterations | `-4`: learned-clause page allocation exhausted |
| `sp4-33-bin-nons-flat-noid` | UNSAT | aggressive | 1.999320 s | 3.87 s | 214,716 iterations | `-4`: learned-clause page allocation exhausted |
| `quad_res_r29_m32` | SAT | default | 0.025514 s | 1.22 s | 3,387 iterations | `-2`: learned clause grew to 1,026 literals (limit 1,024) |
| `sp5-26-19-bin-nons-tree-noid` | SAT | default | 0.231120 s | 1.36 s | 7,916 iterations | `-2`: learned clause grew to 1,025 literals (limit 1,024) |
| `randomG-B-Mix-n15-d05` | UNSAT | default | 1.722389 s | 3.23 s | 147,564 iterations | `-4`: learned-clause page allocation exhausted |
| `randomG-B-Mix-n15-d05` | UNSAT | aggressive | 2.379989 s | 3.73 s | 356,257 iterations | `-4`: learned-clause page allocation exhausted |
| `hidden-k3-s0-r4-n500-03` | SAT | default | 2.540133 s | 3.64 s | 307,563 iterations | `-4`: learned-clause page allocation exhausted |

## Interpretation

The VCK5000 adaptation halves `_FPGA_MAX_LITERAL_ELEMENTS` to 524,288 to fit
the card's URAM budget. Difficult UNSAT instances can fill the remaining
dynamic learned-clause pages in about 2--3 seconds even when 50% of learned
clauses are pruned at twice the normal reset rate. The difficult SAT instances
instead exposed the independent `_FPGA_MAX_LEARN_ELE=1024` compile-time limit.

The accelerator is processing substantial search work quickly (for example,
3.12 million iterations in 3.07 seconds on `rphp4_065_shuffled`). The 2003
low-density random instance demonstrates that the VCK5000 build can return and
validate a complete SAT answer; the resource-limit rows must not be used for
speedup claims.

Likely next design work:

1. Split literal-store and clause-store capacities instead of using one shared
   compile-time constant, then give more URAM to learned clauses for workloads
   with small initial literal stores.
2. Raise `_FPGA_MAX_LEARN_ELE` above 1,024 and measure its BRAM/URAM and timing
   cost.
3. Add an explicit resource-exhausted result to the host CSV instead of writing
   the placeholder result field `0` for allocation errors.

The `.log` files in this directory contain complete console output. The `.csv`
files contain the raw metrics emitted by the host program.
