#!/usr/bin/env python3
"""Rebuild the paper's Table 4 left-hand columns: Literals, Clauses, SA time.

Row order and grouping follow Table 4, with the Table 3 instances appended as
a final group. Literals and Clauses are what host.cpp counted while parsing the
DIMACS file, so they double as an identity check: if they match the paper's for
every instance, both sides really are reading the same files, which rules out
file-level differences as an explanation for any timing discrepancy.

SA time is the minimum across repeats. The solver is deterministic -- same
instance and config replay the same search -- so spread across repeats is
measurement interference, and the minimum is the least contaminated estimate.

Usage:
    python3 benchmark/table4_cols.py
"""

import csv
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RES = Path(os.environ.get("OUT_DIR", ROOT / "results" / "paper_suite"))
OUT = RES / "table4_cols.md"

status = {}
for r in csv.DictReader((RES / "run_status.csv").open(newline="", encoding="utf-8")):
    if r["instance"] not in status or r["exit_code"] != "0":
        status[r["instance"]] = r["exit_code"]

MEAN = {"3": "OOM", "124": "超时", "4": "答案错误"}

# Group boundaries follow the horizontal rules in the paper's Table 4.
GROUPS = {
    "dp10s10.shuffled": "SAT Competition",
    "bmc-ibm-1": "IBM",
    "ssa7552-160": "SSA",
    "qg3-08": "QG",
    "nqueens_16": "Puzzle / Planning",
    "logisticsa": "Logistics",
    "4_4_1": "QtmCkt",
    "hole7_unsat": "Table 3 (SATLIB)",
}

rows = []
BASELINE = Path(os.environ.get("BASELINE", ROOT / "paper_baseline.csv"))
for b in csv.DictReader(BASELINE.open(newline="", encoding="utf-8")):
    inst = b["instance"]
    f = RES / f"{inst}.csv"
    lits = cls = None
    times = []
    if f.exists():
        for rec in csv.reader(f.open(newline="", encoding="utf-8")):
            if len(rec) < 5:
                continue
            if lits is None:
                lits, cls = int(rec[2]), int(rec[3])
            try:
                times.append(float(rec[4]) * 1000.0)
            except ValueError:
                pass
    code = status.get(inst, "not-run")
    rows.append({
        "inst": inst, "grp": GROUPS.get(inst),
        "pl": int(b["paper_lits"]), "pc": int(b["paper_cls"]),
        "bl": lits, "bc": cls,
        "psa": b["paper_sa_ms"],
        "bsa": min(times) if (times and code == "0") else None,
        "code": code,
    })

L = []
L.append("| Problem Name | Literals 论文 | 实测 | Clauses 论文 | 实测 | SA 论文 (ms) | SA 实测 (ms) | 比值 |")
L.append("|---|---:|---:|---:|---:|---:|---:|---:|")
for r in rows:
    if r["grp"]:
        L.append(f"| **{r['grp']}** | | | | | | | |")
    lm = "" if r["bl"] is None else (f"{r['bl']:,}" + ("" if r["bl"] == r["pl"] else " ⚠"))
    cm = "" if r["bc"] is None else (f"{r['bc']:,}" + ("" if r["bc"] == r["pc"] else " ⚠"))
    if r["bsa"] is not None:
        bsa = f"{r['bsa']:.3f}"
        ratio = ("—" if r["psa"] in ("", "NA")
                 else f"{r['bsa'] / float(r['psa']):.2f}x")
    else:
        bsa = f"*{MEAN.get(r['code'], r['code'])}*"
        ratio = "—"
    L.append(f"| `{r['inst']}` | {r['pl']:,} | {lm} | {r['pc']:,} | {cm} | "
             f"{r['psa']} | {bsa} | {ratio} |")

txt = "\n".join(L)
OUT.write_text(txt, encoding="utf-8")
print(txt)
print()
solved = [r for r in rows if r["bsa"] is not None]
print(f"instances: {len(rows)}   solved: {len(solved)}")
print(f"Literals mismatched: {sum(1 for r in rows if r['bl'] is not None and r['bl'] != r['pl'])}")
print(f"Clauses  mismatched: {sum(1 for r in rows if r['bc'] is not None and r['bc'] != r['pc'])}")
print(f"wrote {OUT}")
