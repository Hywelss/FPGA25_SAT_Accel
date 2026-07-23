#!/usr/bin/env python3
"""Rebuild the paper's Table 5 from this board's cycle counters.

Table 5 breaks the solve down into seven stages and reports each as a
percentage of total cycles, per benchmark category. host.cpp already writes
all nine hardware cycle counters and their share of totalCycleCount into the
metrics CSV, so the table can be rebuilt from a normal sweep -- no extra
instrumentation and no extra runs.

Nine counters map onto seven columns, so two columns are sums. The mapping
below is a hypothesis; it is checked by how closely the rebuilt table matches
the paper's, which is printed as a per-cell delta. A mapping that is wrong
shows up as a large systematic error in the columns it touches.

Only the paper's 10% deletion-threshold half is rebuilt here, because that is
what src/configuration.json runs (_HOST_PRUNE_PERCENTAGE 0.1, matching the
configuration the paper states). The 90% half needs a separate sweep with a
prune percentage of 0.9.

Usage:
    python3 benchmark/table5_compare.py
"""

import csv
import os
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULTS = Path(os.environ.get("OUT_DIR", ROOT / "results" / "paper_suite"))
BASELINE = Path(os.environ.get("BASELINE", ROOT / "paper_baseline.csv"))

# Counter order printed by host.cpp: COPY, PQ-FIND, BRANCH, LEARN, LEARN_MIN,
# SAVE, RESIZE, BACKTRACK, DELETE. Raw counts sit at CSV index 8 + 2*i.
COUNTER_INDEX = {name: 8 + 2 * i for i, name in enumerate(
    ["COPY", "PQ-FIND", "BRANCH", "LEARN", "LEARN_MIN", "SAVE", "RESIZE",
     "BACKTRACK", "DELETE"])}
TOTAL_INDEX = 28

STAGES = [
    ("Load", ["COPY"]),
    ("Decide", ["PQ-FIND"]),
    ("Propagate", ["BRANCH"]),
    ("Learn", ["LEARN"]),
    ("Min|Bktrk", ["LEARN_MIN", "SAVE", "BACKTRACK"]),
    ("Allocate", ["RESIZE"]),
    ("Delete", ["DELETE"]),
]

# Table 5, 10% deletion threshold -- the half this configuration reproduces.
PAPER = {
    "SAT-CMP": [0.04, 2.59, 42.25, 13.25, 39.10, 0.33, 2.45],
    "IBM": [4.01, 25.02, 39.51, 9.05, 22.16, 0.11, 0.14],
    "SSA": [24.63, 11.65, 29.65, 18.40, 15.39, 0.16, 0.11],
    "QG": [3.76, 0.95, 45.25, 19.91, 29.48, 0.25, 0.40],
    "PUZZLE": [10.10, 6.37, 37.34, 11.75, 33.81, 0.18, 0.46],
    "PLAN": [1.33, 13.44, 38.20, 15.44, 31.02, 0.32, 0.25],
    "QTMCKT": [21.03, 2.91, 33.34, 20.99, 20.97, 0.44, 0.32],
}

# Table 5, 90% deletion threshold. Recorded for the sweep that reproduces the
# paper's claim that deleting 90% of learned clauses instead of 10% costs
# nothing noticeable. Reaching it needs _HOST_PRUNE_PERCENTAGE 0.9, so it is
# only used when PRUNE=90 selects it.
PAPER_90 = {
    "SAT-CMP": [0.01, 2.37, 37.83, 17.81, 38.51, 0.43, 3.05],
    "IBM": [3.91, 25.01, 40.17, 8.95, 21.55, 0.11, 0.30],
    "SSA": [24.49, 11.01, 28.88, 17.76, 17.17, 0.16, 0.53],
    "QG": [3.61, 0.96, 45.20, 20.47, 28.68, 0.26, 0.81],
    "PUZZLE": [9.95, 6.24, 36.76, 12.44, 32.93, 0.18, 1.50],
    "PLAN": [1.09, 15.19, 39.12, 14.00, 29.24, 0.28, 1.09],
    "QTMCKT": [22.25, 3.22, 31.27, 20.74, 20.51, 0.43, 1.57],
}

if os.environ.get("PRUNE") == "90":
    PAPER = PAPER_90

# Categories follow the grouping of Table 4. SATLIB-T3 instances are the
# Table 3 set, which Table 5 does not cover, so they are collected but not
# compared.
CATEGORIES = [
    ("SAT-CMP", ("dp10s10", "rand_net60", "battleship", "logistics-rotate",
                 "289-sat", "php-010-008", "marg3x3add8")),
    ("IBM", ("bmc-ibm",)),
    ("SSA", ("ssa",)),
    ("QG", ("qg3-", "qg6-", "qg7-")),
    ("PUZZLE", ("nqueens", "sudoku")),
    ("PLAN", ("blocksworld", "4blocks", "logistics")),
    ("QTMCKT", ("4_4_", "9_6_", "9_8_", "16_8_", "16_16_")),
    ("SATLIB-T3", ("hole", "uf1", "uuf1", "CBS_", "aim-", "ii16", "ii32")),
]


def category_of(name):
    for cat, prefixes in CATEGORIES:
        if name.startswith(prefixes):
            return cat
    return None


def load_status():
    path = RESULTS / "run_status.csv"
    status = {}
    if not path.exists():
        return status
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            inst, code = row["instance"], row["exit_code"]
            if inst not in status or code != "0":
                status[inst] = code
    return status


def counters_for(instance):
    """Median raw counter values and total cycles across the repeats."""
    path = RESULTS / f"{instance}.csv"
    if not path.exists():
        return None
    runs = []
    with path.open(newline="", encoding="utf-8") as fh:
        for rec in csv.reader(fh):
            if len(rec) <= TOTAL_INDEX:
                continue
            try:
                runs.append(
                    ({n: int(rec[i]) for n, i in COUNTER_INDEX.items()},
                     int(rec[TOTAL_INDEX]))
                )
            except ValueError:
                continue
    if not runs:
        return None
    return (
        {n: statistics.median([r[0][n] for r in runs]) for n in COUNTER_INDEX},
        statistics.median([r[1] for r in runs]),
    )


def main():
    status = load_status()
    with BASELINE.open(newline="", encoding="utf-8") as fh:
        instances = [r["instance"] for r in csv.DictReader(fh)]

    # Cycle-weighted per category: sum the counters, then divide once. An
    # unweighted mean of per-instance percentages would let a 0.2 ms instance
    # count as much as a 2.4 s one.
    totals = {}
    counts = {}
    for inst in instances:
        if status.get(inst) != "0":
            continue
        cat = category_of(inst)
        if cat is None:
            continue
        got = counters_for(inst)
        if got is None:
            continue
        counters, total = got
        acc, tot = totals.setdefault(cat, ({n: 0.0 for n in COUNTER_INDEX}, [0.0]))
        for n, v in counters.items():
            acc[n] += v
        tot[0] += total
        counts[cat] = counts.get(cat, 0) + 1

    header = " | ".join(f"{s:>9}" for s, _ in STAGES)
    print(f"{'category':<11} {'n':>3}  {header}   sum")
    print("-" * (17 + len(header) + 8))

    deltas = []
    for cat, _ in CATEGORIES:
        if cat not in totals:
            continue
        acc, tot = totals[cat]
        if tot[0] <= 0:
            continue
        pct = [100.0 * sum(acc[n] for n in names) / tot[0] for _, names in STAGES]
        row = " | ".join(f"{v:>9.2f}" for v in pct)
        print(f"{cat:<11} {counts[cat]:>3}  {row}   {sum(pct):>6.2f}")
        if cat in PAPER:
            d = [b - p for b, p in zip(pct, PAPER[cat])]
            drow = " | ".join(f"{v:>+9.2f}" for v in d)
            print(f"{'  vs paper':<11} {'':>3}  {drow}")
            deltas.extend(abs(x) for x in d)

    if deltas:
        print()
        print(f"mapping check over {len(deltas)} compared cells: "
              f"mean |delta| {statistics.mean(deltas):.2f} pp, "
              f"median {statistics.median(deltas):.2f} pp, "
              f"max {max(deltas):.2f} pp")
        print("Large, one-sided errors concentrated in a column mean that "
              "column's counter mapping is wrong.")


if __name__ == "__main__":
    main()
