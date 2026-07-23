# VCK5000 vs. SAT-Accel paper (U55C @ 230 MHz)

- Board: VCK5000 @ 223 MHz data clock
- Paper reference: U55C @ 230 MHz, Table 3 + Table 4
- Instances in paper suite: 59
- Solved on this board: 55
- Not solved (resource limit / timeout / not run): 4
- Ratio vs. paper, all 55 solved: median 1.03x, min 0.17x, max 1.99x
- **Ratio vs. paper, 14 instances with paper time >= 10 ms: median 1.25x, min 0.17x, max 1.50x** <- use this one

> Two caveats on the numbers above. **Rounding:** the paper prints SAT-Accel times to one or two significant figures, so a printed `1` means somewhere in [0.5, 1.5) and a ratio against it carries ~50% error by itself -- this is why the restricted aggregate exists. **Coverage:** ratios cover only what this board solved; until that set covers the paper's set, this is not a like-for-like reproduction of the paper's average speedup.

> Clock accounts for a known 3.0% of any slowdown (223 MHz vs. the paper's 230 MHz). Divide the ratios by 1.0314 to isolate the architectural difference.

> 11 instances sit outside [0.7x, 1.7x] with a tight spread. A lower clock and a narrower memory path cannot make this board faster than the paper's, so these are almost certainly a **different search path**: the halved store changes when clauses are deleted, which changes VSIDS scores and the decision sequence. They measure a different amount of work, not a different speed -- keep them out of any speed claim. Listed in their own table below.

## Solved on this board

| Instance | Lits | Cls | Paper SA (ms) | This board min (ms) | median | spread | Ratio (min) | MiniSat (ms) | Kissat (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `uf150-08_sat` | 150 | 645 | 1 | 1.987 | 2.017 | 1.0x | 1.99x |  |  |
| `ii16e2_sat` | 532 | 7825 | 4 | 6.284 | 6.295 | 1.0x | 1.57x |  |  |
| `bmc-ibm-1` | 9685 | 55870 | 54 | 80.951 | 80.984 | 1.0x | 1.50x | 63 | 80 |
| `dp10s10.shuffled` | 7759 | 23004 | 48 | 69.879 | 69.919 | 1.0x | 1.46x | 48 | 380 |
| `qg6-11` | 1331 | 49204 | 37 | 53.976 | 53.997 | 1.0x | 1.46x | 53 | 60 |
| `qg6-12` | 1728 | 69931 | 925 | 1347.720 | 1347.740 | 1.0x | 1.46x | 373 | 960 |
| `marg3x3add8.shuffled-as.sat03-1449` | 41 | 224 | 328 | 466.777 | 466.783 | 1.0x | 1.42x | 868 | 90 |
| `4_4_2` | 176 | 1054 | 1 | 1.415 | 1.425 | 1.0x | 1.42x | 3 | <10 |
| `qg7-10` | 1000 | 33736 | 1 | 1.411 | 1.431 | 1.0x | 1.41x | 11 | <10 |
| `ii32e1_sat` | 222 | 1186 | 0.1 | 0.141 | 0.154 | 1.1x | 1.41x |  |  |
| `blocksworld` | 459 | 7054 | 0.3 | 0.404 | 0.409 | 1.1x | 1.35x | 4 | <10 |
| `logisticsb` | 843 | 7301 | 4 | 5.364 | 5.400 | 1.0x | 1.34x | 6 | <10 |
| `4_4_4` | 346 | 2246 | 1 | 1.342 | 1.378 | 1.0x | 1.34x | 3 | <10 |
| `battleship-6-9-unsat` | 54 | 171 | 214 | 268.729 | 269.127 | 1.0x | 1.26x | 105 | 240 |
| `hole8_unsat` | 72 | 297 | 691 | 872.462 | 872.485 | 1.0x | 1.26x |  |  |
| `uf100-010_sat` | 100 | 430 | 1 | 1.261 | 1.264 | 1.0x | 1.26x |  |  |
| `php-010-008.shuffled-as.sat05-1171` | 80 | 370 | 1951 | 2417.080 | 2417.100 | 1.0x | 1.24x | 203 | 690 |
| `logisticsa` | 828 | 6718 | 2 | 2.469 | 2.482 | 1.0x | 1.23x | 5 | <10 |
| `nqueens_32` | 1024 | 52608 | 9 | 10.945 | 10.953 | 1.0x | 1.22x | 50 | 10 |
| `bmc-ibm-5` | 9396 | 41207 | 8 | 9.524 | 9.535 | 1.0x | 1.19x | 69 | 10 |
| `qg7-11` | 1331 | 49534 | 26 | 29.979 | 29.998 | 1.0x | 1.15x | 27 | 20 |
| `9_6_3` | 945 | 8403 | 3 | 3.361 | 3.385 | 1.0x | 1.12x | 9 | <10 |
| `9_8_7` | 3132 | 28620 | 0.3 | 0.334 | 0.345 | 1.1x | 1.11x | 35 | 10 |
| `logisticsc` | 1141 | 10719 | 6 | 6.618 | 6.630 | 1.0x | 1.10x | 14 | <10 |
| `rand_net60-30-1.shuffled` | 3600 | 10681 | 401 | 436.272 | 436.286 | 1.0x | 1.09x | 57 | 110 |
| `nqueens_16` | 256 | 6336 | 0.2 | 0.212 | 0.273 | 1.4x | 1.06x | 11 | <10 |
| `bmc-ibm-2` | 2810 | 11683 | 1 | 1.049 | 1.065 | 1.1x | 1.05x | 7 | <10 |
| `qg7-12` | 1728 | 70327 | 292 | 301.227 | 301.233 | 1.0x | 1.03x | 110 | 230 |
| `16_8_5` | 3948 | 45759 | 0.5 | 0.502 | 0.517 | 1.1x | 1.00x | 61 | 10 |
| `CBS_k3_n100_m403_b10_1_sat` | 100 | 403 | 2 | 2.003 | 2.036 | 1.0x | 1.00x |  |  |
| `uuf125-05_unsat` | 125 | 538 | 7 | 6.659 | 6.687 | 1.0x | 0.95x |  |  |
| `ssa2670-141` | 4843 | 2315 | 7 | 6.455 | 6.495 | 1.0x | 0.92x | 3 | <10 |
| `4_4_1` | 82 | 434 | 0.2 | 0.182 | 0.197 | 1.4x | 0.91x | 2 | <10 |
| `4_4_3` | 252 | 1614 | 3 | 2.734 | 2.754 | 1.0x | 0.91x | 1 | <10 |
| `9_6_1` | 312 | 2261 | 1 | 0.909 | 0.916 | 1.0x | 0.91x | 1 | 10 |
| `aim-200-1_6-no-4` | 200 | 320 | 0.3 | 0.268 | 0.292 | 1.1x | 0.89x |  |  |
| `uuf100-02_unsat` | 100 | 430 | 4 | 3.433 | 3.458 | 1.0x | 0.86x |  |  |
| `16_16_3` | 5544 | 56261 | 1 | 0.852 | 0.861 | 1.1x | 0.85x | 37 | 10 |
| `ssa0432-003` | 435 | 1027 | 1 | 0.842 | 0.858 | 1.1x | 0.84x | 2 | <10 |
| `sudoku_9_3_3` | 729 | 8910 | 0.4 | 0.313 | 0.315 | 1.2x | 0.78x | 4 | <10 |
| `logisticsd` | 4713 | 21991 | 7 | 5.476 | 5.507 | 1.0x | 0.78x | 33 | 10 |
| `uf125-01_sat` | 125 | 538 | 4 | 3.105 | 3.112 | 1.0x | 0.78x |  |  |
| `ssa6288-047` | 17303 | 34238 | 0.4 | 0.298 | 0.310 | 1.1x | 0.75x | 4 | <10 |
| `qg6-10` | 1000 | 33466 | 8 | 5.828 | 5.835 | 1.0x | 0.73x | 14 | 10 |
| `bmc-ibm-7` | 8710 | 39774 | 3 | 2.133 | 2.142 | 1.0x | 0.71x | 60 | 10 |
| `16_8_7` | 5484 | 64894 | 1 | 0.681 | 0.738 | 1.1x | 0.68x | 44 | 10 |
| `16_8_6` | 4716 | 55311 | 1 | 0.607 | 0.613 | 1.1x | 0.61x | 38 | 10 |
| `hole7_unsat` | 56 | 204 | 125 | 74.963 | 74.982 | 1.0x | 0.60x |  |  |
| `qg3-08` | 512 | 10469 | 1 | 0.572 | 0.580 | 1.1x | 0.57x | 11 | <10 |
| `blocksworldc` | 3016 | 50457 | 25 | 13.325 | 13.329 | 1.0x | 0.53x | 30 | 30 |
| `9_6_4` | 1275 | 11537 | 6 | 3.003 | 3.033 | 1.0x | 0.50x | 14 | <10 |
| `9_6_2` | 642 | 5377 | 9 | 3.129 | 3.173 | 1.0x | 0.35x | 10 | <10 |
| `ssa7552-160` | 1391 | 3126 | 1 | 0.299 | 0.334 | 1.2x | 0.30x | 4 | <10 |
| `aim-200-3_4-yes1-4` | 200 | 680 | 4 | 0.827 | 0.842 | 1.1x | 0.21x |  |  |
| `4blocks` | 758 | 47820 | 69 | 11.411 | 11.423 | 1.0x | 0.17x | 44 | 30 |

## Suspected search-path divergence (exclude from speed claims)

| Instance | Paper SA (ms) | This board min (ms) | Ratio |
|---|---:|---:|---:|
| `4blocks` | 69 | 11.411 | 0.17x |
| `aim-200-3_4-yes1-4` | 4 | 0.827 | 0.21x |
| `ssa7552-160` | 1 | 0.299 | 0.30x |
| `9_6_2` | 9 | 3.129 | 0.35x |
| `9_6_4` | 6 | 3.003 | 0.50x |
| `blocksworldc` | 25 | 13.325 | 0.53x |
| `qg3-08` | 1 | 0.572 | 0.57x |
| `hole7_unsat` | 125 | 74.963 | 0.60x |
| `16_8_6` | 1 | 0.607 | 0.61x |
| `16_8_7` | 1 | 0.681 | 0.68x |
| `uf150-08_sat` | 1 | 1.987 | 1.99x |

## Not solved on this board

| Instance | Lits | Cls | Paper SA (ms) | Status |
|---|---:|---:|---:|---|
| `logistics-rotate-07t5.shuffled-as.sat05-1137` | 2721 | 88373 | 1088 | on-chip memory exhausted |
| `289-sat-6x30` | 720 | 27360 | 556 | on-chip memory exhausted |
| `marg3x3add8ch.shuffled-as.sat03-1448` | 41 | 272 | 6387 | on-chip memory exhausted |
| `hole9_unsat` | 90 | 415 | NA | on-chip memory exhausted |
