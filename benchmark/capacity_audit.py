#!/usr/bin/env python3
"""Static capacity audit: which of the paper's instances fit in a given build?

Replays the exact host-side allocation arithmetic from host.cpp (clause-store
packing with 4-word pages and next-pointers, transposed literal-store with
16-word pages) without needing an FPGA, so we can tell up front which paper
instances are even loadable on the reduced-capacity VCK5000 build.

This answers "will host.cpp reject it before the kernel starts?" only. An
instance that fits statically can still exhaust dynamic learned-clause pages
during the search -- that failure (host error -4) cannot be predicted here.

Usage:
    python3 benchmark/capacity_audit.py                 # VCK5000 limits (524288)
    LIMIT=1048576 python3 benchmark/capacity_audit.py   # U55C limits, for contrast
"""

import csv
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
BASELINE = Path(os.environ.get("BASELINE", ROOT / "paper_baseline.csv"))

# Compile-time limits. Defaults are the VCK5000 build.
LIMIT_ELEMENTS = int(os.environ.get("LIMIT", 128 * 4096))   # _FPGA_MAX_LITERAL_ELEMENTS
MAX_LITERALS = int(os.environ.get("MAX_LITERALS", 8192 * 4))  # _FPGA_MAX_LITERALS
MAX_CLAUSES = int(os.environ.get("MAX_CLAUSES", 131072))      # _FPGA_MAX_CLAUSES

# From src/configuration.json.
LITERAL_PAGE_SIZE = int(os.environ.get("LITERAL_PAGE_SIZE", 16))
CLAUSE_PAGE_SIZE = int(os.environ.get("CLAUSE_PAGE_SIZE", 4))


def parse_dimacs(path):
    """Return (num_vars, num_clauses_declared, clauses) with host.cpp's dedup."""
    num_vars = num_cls = 0
    clauses = []
    pending = []
    seen = set()
    started = False

    with open(path, "r", errors="replace") as fh:
        for line in fh:
            tokens = line.split()
            if not tokens:
                continue
            if tokens[0] == "c":
                continue
            if not started and tokens[0] == "p":
                num_vars, num_cls = int(tokens[2]), int(tokens[3])
                started = True
                continue
            if not started:
                continue

            terminated = tokens[-1] == "0"
            body = tokens[:-1] if terminated else tokens
            for tok in body:
                v = int(tok)
                if v not in seen:          # host.cpp keeps first occurrence only
                    pending.append(v)
                seen.add(v)
            if terminated:
                clauses.append(pending)
                pending = []
                seen = set()

    return num_vars, num_cls, clauses


def clause_store_elements(clauses):
    """host.cpp lines 176-232: 3 data words + 1 link word per 4-word page."""
    index1d = 0
    for clause in clauses:
        index = 0
        need_zero = True
        n = len(clause)
        for j in range(n):
            index1d += 1
            index += 1
            if index == CLAUSE_PAGE_SIZE - 1:
                if j == n - 1:
                    need_zero = False
                index1d += 1          # next-pointer (or terminating 0)
                index = 0
        if need_zero:
            index1d += CLAUSE_PAGE_SIZE - index
    if index1d % CLAUSE_PAGE_SIZE:
        index1d += CLAUSE_PAGE_SIZE - (index1d % CLAUSE_PAGE_SIZE)
    return index1d


def literal_store_elements(num_vars, clauses):
    """host.cpp lines 234-305: transposed store, 14 data + 2 link per 16-word page."""
    pos = [0] * (num_vars + 1)
    neg = [0] * (num_vars + 1)
    for i, clause in enumerate(clauses):
        for v in clause:
            if abs(v) > num_vars:
                continue
            if v > 0:
                pos[abs(v)] += 1
            else:
                neg[abs(v)] += 1

    index1d = 0
    for v in range(1, num_vars + 1):
        if pos[v] == 0 and neg[v] == 0:
            continue                      # both polarities empty -> pre-decided, no space
        for count in (pos[v], neg[v]):
            index = 0
            for _ in range(count):
                index1d += 1
                index += 1
                if index == LITERAL_PAGE_SIZE - 2:
                    index1d += 2          # zero + next-pointer
                    index = 0
            index1d += LITERAL_PAGE_SIZE - index
    return index1d


def main():
    rows = []
    with BASELINE.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(l for l in fh if not l.startswith("#")))

    out = []
    for row in rows:
        path = REPO / row["repo_path"]
        if not path.exists():
            out.append({"instance": row["instance"], "verdict": "MISSING"})
            continue

        num_vars, num_cls, clauses = parse_dimacs(path)
        lit_ele = literal_store_elements(num_vars, clauses)
        cls_ele = clause_store_elements(clauses)

        reasons = []
        if num_vars > MAX_LITERALS:
            reasons.append(f"vars {num_vars} > {MAX_LITERALS}")
        if len(clauses) > MAX_CLAUSES:
            reasons.append(f"clauses {len(clauses)} > {MAX_CLAUSES}")
        if lit_ele > LIMIT_ELEMENTS:
            reasons.append(f"lit-store {lit_ele} > {LIMIT_ELEMENTS}")
        if cls_ele > LIMIT_ELEMENTS:
            reasons.append(f"cls-store {cls_ele} > {LIMIT_ELEMENTS}")

        out.append(
            {
                "instance": row["instance"],
                "vars": num_vars,
                "clauses": len(clauses),
                "lit_elements": lit_ele,
                "lit_pct": round(100.0 * lit_ele / LIMIT_ELEMENTS, 1),
                "cls_elements": cls_ele,
                "cls_pct": round(100.0 * cls_ele / LIMIT_ELEMENTS, 1),
                "headroom_elements": LIMIT_ELEMENTS - cls_ele,
                "verdict": "FITS" if not reasons else "REJECTED",
                "reason": "; ".join(reasons),
            }
        )

    fits = [r for r in out if r["verdict"] == "FITS"]
    bad = [r for r in out if r["verdict"] not in ("FITS",)]

    out_csv = ROOT / "results" / "capacity_audit.csv"
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=list(out[0].keys()))
        w.writeheader()
        w.writerows(out)

    print(f"limit = {LIMIT_ELEMENTS} elements per store")
    print(f"fits      : {len(fits)}/{len(out)}")
    print(f"rejected  : {len(bad)}")
    for r in bad:
        print(f"  {r['instance']}: {r.get('reason', '')}")
    print()
    print("Tightest clause-store headroom (these are the ones at risk of a -4 mid-search):")
    for r in sorted(fits, key=lambda r: r["headroom_elements"])[:10]:
        print(
            f"  {r['instance']:<48} cls {r['cls_pct']:>5}%  lit {r['lit_pct']:>5}%  "
            f"headroom {r['headroom_elements']:,}"
        )
    print()
    print(f"wrote {out_csv}")


if __name__ == "__main__":
    sys.exit(main())
