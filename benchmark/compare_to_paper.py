#!/usr/bin/env python3
"""Join the measured board results against the SAT-Accel paper's U55C numbers.

Reads:
    benchmark/paper_baseline.csv                     paper reference times
    benchmark/results/paper_suite/<instance>.csv     raw host metrics, one row per run
    benchmark/results/paper_suite/run_status.csv     exit code per run

Writes:
    benchmark/results/paper_suite/comparison.csv     machine-readable join
    benchmark/results/paper_suite/comparison.md      Markdown tables (paste into the tracking doc)

Column 5 (index 4) of the host metrics CSV is the kernel execution time in
seconds -- host.cpp writes `executionTime/1e9` there, taken from the OpenCL
profiling counters. That is the number comparable to the paper's "Time in ms"
for SAT-Accel. (The repo README's claim that column J holds the runtime is
stale; column J is a cycle-counter percentage.)

Instances whose run hit a resource limit are reported as such and are excluded
from every aggregate -- their kernel time is time-to-failure, not solve time.
"""

import csv
import os
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULTS = Path(os.environ.get("OUT_DIR", ROOT / "results" / "paper_suite"))
BASELINE = Path(os.environ.get("BASELINE", ROOT / "paper_baseline.csv"))

# Board/config provenance recorded in the report header. Override via env.
BOARD = os.environ.get("BOARD", "VCK5000")
CLOCK_MHZ = os.environ.get("CLOCK_MHZ", "223")
PAPER_BOARD = "U55C @ 230 MHz"

# The paper prints SAT-Accel times to one or two significant figures, so a
# printed "1" can be anything in [0.5, 1.5). Ratios against such values carry
# ~50% error from rounding alone. Aggregates are therefore reported twice:
# over everything solved, and over the subset where the paper's own value is
# large enough that its rounding no longer dominates.
RESOLUTION_FLOOR_MS = float(os.environ.get("RESOLUTION_FLOOR_MS", 10))

# max/min across repeats above which a measurement is treated as contaminated.
NOISE_SPREAD = float(os.environ.get("NOISE_SPREAD", 2.0))

# Ratios this far from 1.0 with a *tight* spread cannot be explained by clock
# or memory topology; they indicate the halved store changed the clause-deletion
# schedule and therefore the search path itself.
DIVERGENCE_LO = float(os.environ.get("DIVERGENCE_LO", 0.7))
DIVERGENCE_HI = float(os.environ.get("DIVERGENCE_HI", 1.7))

EXIT_MEANING = {
    "0": "solved",
    "3": "on-chip memory exhausted",
    # host.cpp exits 4 when the kernel's answer disagrees with the expected
    # SAT/UNSAT result. That is a correctness failure of this build, not a
    # capacity limit, and it invalidates every timing number from the same
    # build -- so it is called out separately rather than folded into the
    # not-solved bucket.
    "4": "WRONG ANSWER",
    "124": "timeout",
    "137": "timeout (killed)",
    "missing": "input file missing",
}


def load_baseline():
    rows = []
    with BASELINE.open(newline="", encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            rows.append(line)
    return list(csv.DictReader(rows))


def load_status():
    """instance -> (worst_exit_code, meaning). 0 only if every run returned 0."""
    path = RESULTS / "run_status.csv"
    if not path.exists():
        return {}
    status = {}
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            inst, code = row["instance"], row["exit_code"]
            if inst not in status or code != "0":
                status[inst] = code
    return {k: (v, EXIT_MEANING.get(v, f"error {v}")) for k, v in status.items()}


def load_kernel_times_ms(instance):
    """Kernel times in ms, one per completed run."""
    path = RESULTS / f"{instance}.csv"
    if not path.exists():
        return []
    times = []
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.reader(fh):
            if len(row) < 5:
                continue
            try:
                times.append(float(row[4]) * 1000.0)
            except ValueError:
                continue
    return times


def fmt(x, digits=3):
    return "" if x is None else f"{x:.{digits}f}"


def main():
    baseline = load_baseline()
    status = load_status()

    joined = []
    for row in baseline:
        inst = row["instance"]
        times = load_kernel_times_ms(inst)
        code, meaning = status.get(inst, ("not-run", "not run"))

        median = statistics.median(times) if times else None
        fastest = min(times) if times else None
        slowest = max(times) if times else None
        # Run-to-run spread. The solver is deterministic -- identical input and
        # config replay an identical search -- so a wide spread is interference
        # in the measurement (queueing, contention), not solver behaviour.
        # Report it, and take the minimum as the least-contaminated estimate.
        spread = (slowest / fastest) if fastest else None

        paper_sa = row["paper_sa_ms"]
        paper_val = None
        if paper_sa not in ("", "NA"):
            paper_val = float(paper_sa)

        # Ratio is only meaningful when this board actually solved the instance.
        ratio = None
        if code == "0" and fastest is not None and paper_val:
            ratio = fastest / paper_val

        joined.append(
            {
                "instance": inst,
                "table": row["paper_table"],
                "lits": row["paper_lits"],
                "cls": row["paper_cls"],
                "paper_sa_ms": paper_sa,
                "paper_minisat_ms": row["paper_minisat_ms"],
                "paper_kissat_ms": row["paper_kissat_ms"],
                "runs": len(times),
                "board_min_ms": fmt(fastest),
                "board_median_ms": fmt(median),
                "board_max_ms": fmt(slowest),
                "spread_max_over_min": fmt(spread, 1),
                "noisy": "yes" if (spread and spread > NOISE_SPREAD) else "",
                "status": meaning,
                "ratio_board_over_paper": fmt(ratio, 2),
            }
        )

    RESULTS.mkdir(parents=True, exist_ok=True)
    out_csv = RESULTS / "comparison.csv"
    with out_csv.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=list(joined[0].keys()))
        w.writeheader()
        w.writerows(joined)

    solved = [r for r in joined if r["status"] == "solved" and r["ratio_board_over_paper"]]
    unsolved = [r for r in joined if r["status"] not in ("solved",)]
    ratios = [float(r["ratio_board_over_paper"]) for r in solved]

    lines = []
    lines.append(f"# {BOARD} vs. SAT-Accel paper ({PAPER_BOARD})")
    lines.append("")
    lines.append(f"- Board: {BOARD} @ {CLOCK_MHZ} MHz data clock")
    lines.append(f"- Paper reference: {PAPER_BOARD}, Table 3 + Table 4")
    lines.append(f"- Instances in paper suite: {len(joined)}")
    lines.append(f"- Solved on this board: {len(solved)}")
    lines.append(f"- Not solved (resource limit / timeout / not run): {len(unsolved)}")

    wrong = [r for r in joined if r["status"] == "WRONG ANSWER"]
    if wrong:
        lines.append("")
        lines.append(
            f"> **STOP: {len(wrong)} instance(s) returned the wrong SAT/UNSAT answer** "
            f"({', '.join('`' + r['instance'] + '`' for r in wrong)}). This build is "
            f"incorrect, so none of the timings below mean anything -- a solver that "
            f"answers wrongly can answer quickly. Fix correctness before reading any "
            f"ratio on this page."
        )

    trusted = [
        r
        for r in solved
        if r["paper_sa_ms"] not in ("", "NA")
        and float(r["paper_sa_ms"]) >= RESOLUTION_FLOOR_MS
    ]
    trusted_ratios = [float(r["ratio_board_over_paper"]) for r in trusted]

    if ratios:
        lines.append(
            f"- Ratio vs. paper, all {len(ratios)} solved: "
            f"median {statistics.median(ratios):.2f}x, "
            f"min {min(ratios):.2f}x, max {max(ratios):.2f}x"
        )
        if trusted_ratios:
            lines.append(
                f"- **Ratio vs. paper, {len(trusted_ratios)} instances with paper time "
                f">= {RESOLUTION_FLOOR_MS:g} ms: "
                f"median {statistics.median(trusted_ratios):.2f}x, "
                f"min {min(trusted_ratios):.2f}x, max {max(trusted_ratios):.2f}x** "
                f"<- use this one"
            )
        else:
            lines.append(
                f"- No solved instance has a paper time >= {RESOLUTION_FLOOR_MS:g} ms, "
                f"so every ratio above is dominated by the paper's rounding."
            )
        lines.append("")
        lines.append(
            "> Two caveats on the numbers above. **Rounding:** the paper prints "
            "SAT-Accel times to one or two significant figures, so a printed `1` "
            "means somewhere in [0.5, 1.5) and a ratio against it carries ~50% "
            "error by itself -- this is why the restricted aggregate exists. "
            "**Coverage:** ratios cover only what this board solved; until that "
            "set covers the paper's set, this is not a like-for-like reproduction "
            "of the paper's average speedup."
        )
        lines.append("")
        lines.append(
            f"> Clock accounts for a known {(1 - 223 / 230) * 100:.1f}% of any slowdown "
            f"({CLOCK_MHZ} MHz vs. the paper's 230 MHz). Divide the ratios by "
            f"{230 / 223:.4f} to isolate the architectural difference."
        )
    noisy = [r for r in solved if r["noisy"]]
    if noisy:
        lines.append("")
        lines.append(
            f"> {len(noisy)} of {len(solved)} solved instances varied by more than "
            f"{NOISE_SPREAD:g}x across repeats. The solver is deterministic, so that "
            f"spread is measurement interference, not solver behaviour. Every ratio "
            f"here uses the **minimum** across repeats for that reason; the median "
            f"column is shown only so the contamination stays visible."
        )

    diverged = [
        r
        for r in solved
        if not r["noisy"]
        and not (DIVERGENCE_LO <= float(r["ratio_board_over_paper"]) <= DIVERGENCE_HI)
    ]
    if diverged:
        lines.append("")
        lines.append(
            f"> {len(diverged)} instances sit outside "
            f"[{DIVERGENCE_LO:g}x, {DIVERGENCE_HI:g}x] with a tight spread. A lower "
            f"clock and a narrower memory path cannot make this board faster than the "
            f"paper's, so these are almost certainly a **different search path**: the "
            f"halved store changes when clauses are deleted, which changes VSIDS "
            f"scores and the decision sequence. They measure a different amount of "
            f"work, not a different speed -- keep them out of any speed claim. "
            f"Listed in their own table below."
        )

    lines.append("")
    lines.append("## Solved on this board")
    lines.append("")
    lines.append(
        "| Instance | Lits | Cls | Paper SA (ms) | This board min (ms) | median | spread | Ratio (min) | MiniSat (ms) | Kissat (ms) |"
    )
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in sorted(solved, key=lambda r: -float(r["ratio_board_over_paper"])):
        flag = " ⚠" if r["noisy"] else ""
        lines.append(
            f"| `{r['instance']}` | {r['lits']} | {r['cls']} | {r['paper_sa_ms']} | "
            f"{r['board_min_ms']} | {r['board_median_ms']} | {r['spread_max_over_min']}x{flag} | "
            f"{r['ratio_board_over_paper']}x | {r['paper_minisat_ms']} | {r['paper_kissat_ms']} |"
        )
    lines.append("")
    if diverged:
        lines.append("## Suspected search-path divergence (exclude from speed claims)")
        lines.append("")
        lines.append("| Instance | Paper SA (ms) | This board min (ms) | Ratio |")
        lines.append("|---|---:|---:|---:|")
        for r in sorted(diverged, key=lambda r: float(r["ratio_board_over_paper"])):
            lines.append(
                f"| `{r['instance']}` | {r['paper_sa_ms']} | {r['board_min_ms']} | "
                f"{r['ratio_board_over_paper']}x |"
            )
        lines.append("")
    lines.append("## Not solved on this board")
    lines.append("")
    lines.append("| Instance | Lits | Cls | Paper SA (ms) | Status |")
    lines.append("|---|---:|---:|---:|---|")
    for r in unsolved:
        lines.append(
            f"| `{r['instance']}` | {r['lits']} | {r['cls']} | {r['paper_sa_ms']} | {r['status']} |"
        )
    lines.append("")

    out_md = RESULTS / "comparison.md"
    out_md.write_text("\n".join(lines), encoding="utf-8")

    print(f"wrote {out_csv}")
    print(f"wrote {out_md}")
    print(f"solved {len(solved)}/{len(joined)}")


if __name__ == "__main__":
    main()
