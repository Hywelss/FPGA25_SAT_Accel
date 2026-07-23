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

EXIT_MEANING = {
    "0": "solved",
    "3": "on-chip memory exhausted",
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
        paper_sa = row["paper_sa_ms"]
        paper_val = None
        if paper_sa not in ("", "NA"):
            paper_val = float(paper_sa)

        # Ratio is only meaningful when this board actually solved the instance.
        ratio = None
        if code == "0" and median is not None and paper_val:
            ratio = median / paper_val

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
                "board_min_ms": fmt(min(times)) if times else "",
                "board_median_ms": fmt(median),
                "board_max_ms": fmt(max(times)) if times else "",
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
    if ratios:
        lines.append(
            f"- Slowdown vs. paper over the solved set: "
            f"geometric-mean-free median {statistics.median(ratios):.2f}x, "
            f"min {min(ratios):.2f}x, max {max(ratios):.2f}x"
        )
        lines.append("")
        lines.append(
            "> Coverage caveat: the ratio above is computed only over instances this "
            "board solved. It is not a like-for-like reproduction of the paper's "
            "average speedup until the solved set covers the paper's set."
        )
    lines.append("")
    lines.append("## Solved on this board")
    lines.append("")
    lines.append(
        "| Instance | Lits | Cls | Paper SA (ms) | This board median (ms) | min-max (ms) | Ratio | MiniSat (ms) | Kissat (ms) |"
    )
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in sorted(solved, key=lambda r: -float(r["ratio_board_over_paper"])):
        lines.append(
            f"| `{r['instance']}` | {r['lits']} | {r['cls']} | {r['paper_sa_ms']} | "
            f"{r['board_median_ms']} | {r['board_min_ms']} - {r['board_max_ms']} | "
            f"{r['ratio_board_over_paper']}x | {r['paper_minisat_ms']} | {r['paper_kissat_ms']} |"
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
