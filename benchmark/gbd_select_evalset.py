#!/usr/bin/env python3
"""Select a SAT-Accel-compatible evaluation set from the Global Benchmark Database.

Applies the three hard capacity constraints of the VCK5000 build plus a
difficulty floor, then reports whether the surviving set is large and diverse
enough to evaluate a branching heuristic on.

Dependency-free: standard library only.  GBD's databases are plain SQLite, so
gbd-tools is not needed.  Large files go under --data-root (default
/mnt/data/satbench).

Stages
------
  schema   dump the SQLite schema of the downloaded databases (diagnostic)
  query    filter GBD, write candidates.csv          (no CNF downloads, seconds)
  fetch    download candidate CNFs                   (bulk, resumable)
  verify   stream-count literals, apply constraint C, write evalset.csv
  all      query + fetch + verify

Usage
-----
  python3 gbd_select_evalset.py --stage query
  python3 gbd_select_evalset.py --stage all
"""

import argparse
import bz2
import csv
import gzip
import lzma
import os
import re
import sqlite3
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# ---------------------------------------------------------------- constraints

# Hard capacity limits of the VCK5000 build (src/fpga_solver.h, k2k_vck5000.cfg).
MAX_VARIABLES = 32_768          # _FPGA_MAX_LITERALS
MAX_CLAUSES = 131_072           # _FPGA_MAX_CLAUSES
MAX_LITERAL_ELEMENTS = 524_288  # _FPGA_MAX_LITERAL_ELEMENTS, halved for VCK5000;
                                # the upstream U55C value is 1_048_576

# Families with exponential resolution lower bounds: no branching heuristic,
# learned or otherwise, can help on these.
IMMUNE_PATTERNS = [
    "php", "hole", "pigeon",           # pigeonhole
    "marg", "chess", "mutilated",      # mutilated chessboard
    "tseitin", "urquhart", "parity",   # parity / expander
    "pebbl",                           # pebbling
    "xor",                             # xor chains
]

DB_URLS = {
    "meta.db": "https://benchmark-database.de/getdatabase/meta.db",
    "base.db": "https://benchmark-database.de/getdatabase/base.db",
}
FILE_URL = "https://benchmark-database.de/file/{hash}?context=cnf"

# Fixed in advance so the outcome is not rationalised after the fact.
VERDICT_GO_INSTANCES, VERDICT_GO_FAMILIES = 30, 6
VERDICT_MARGINAL_INSTANCES = 10


def log(msg=""):
    print(msg, file=sys.stderr, flush=True)


def is_immune(*fields):
    blob = " ".join(str(f) for f in fields if f).lower()
    return any(p in blob for p in IMMUNE_PATTERNS)


def as_float(v):
    try:
        f = float(v)
        return f if f == f else None      # reject NaN
    except (TypeError, ValueError):
        return None


# ------------------------------------------------------------------ downloads

def download(url, dest, desc=None):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return False
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=180) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    os.replace(tmp, dest)
    if desc:
        log(f"  fetched {desc} ({os.path.getsize(dest)/1e6:.1f} MB)")
    return True


def ensure_dbs(data_root):
    paths = []
    for name, url in DB_URLS.items():
        dest = os.path.join(data_root, "gbd", name)
        if not os.path.exists(dest):
            log(f"[db] downloading {name}")
        download(url, dest, desc=name)
        paths.append(dest)
    return paths


# --------------------------------------------------------------- sqlite access

def connect(paths):
    conn = sqlite3.connect(":memory:")
    for i, p in enumerate(paths):
        conn.execute("ATTACH DATABASE ? AS ?", (p, f"db{i}"))
    return conn


def list_feature_tables(conn, n_dbs):
    """Return {feature_name: (schema, hash_col, value_col)} for GBD feature tables.

    A GBD feature is stored as its own table with a hash column and a value
    column.  Names are discovered rather than assumed, so a schema change shows
    up as a missing feature instead of a wrong answer.
    """
    found = {}
    for i in range(n_dbs):
        schema = f"db{i}"
        try:
            tables = conn.execute(
                f"SELECT name FROM {schema}.sqlite_master "
                "WHERE type IN ('table','view')").fetchall()
        except sqlite3.Error:
            continue
        for (t,) in tables:
            try:
                cols = [r[1] for r in conn.execute(
                    f'PRAGMA {schema}.table_info("{t}")').fetchall()]
            except sqlite3.Error:
                continue
            low = [c.lower() for c in cols]
            if "hash" not in low:
                continue
            hcol = cols[low.index("hash")]
            vcol = None
            for cand in ("value", t):
                if cand.lower() in low:
                    vcol = cols[low.index(cand.lower())]
                    break
            if vcol is None:
                others = [c for c in cols if c.lower() != "hash"]
                if len(others) == 1:
                    vcol = others[0]
            if vcol is not None and t not in found:
                found[t] = (schema, hcol, vcol)
    return found


def read_feature(conn, feat, spec):
    schema, hcol, vcol = spec
    out = {}
    for h, v in conn.execute(
            f'SELECT "{hcol}", "{vcol}" FROM {schema}."{feat}"'):
        if h is not None and h not in out:
            out[h] = v
    return out


def stage_schema(args):
    paths = ensure_dbs(args.data_root)
    conn = connect(paths)
    feats = list_feature_tables(conn, len(paths))
    log(f"[schema] {len(feats)} feature tables across {len(paths)} databases\n")
    for name in sorted(feats):
        schema, hcol, vcol = feats[name]
        try:
            n = conn.execute(f'SELECT COUNT(*) FROM {schema}."{name}"').fetchone()[0]
        except sqlite3.Error:
            n = -1
        log(f"  {name:<38} rows={n:<9} ({schema}: {hcol}, {vcol})")
    log("\n[schema] runtime-like features:")
    for name in sorted(feats):
        if "runtime" in name.lower() or "solver" in name.lower():
            log(f"    {name}")


# ---------------------------------------------------------------------- query

def stage_query(args):
    paths = ensure_dbs(args.data_root)
    conn = connect(paths)
    feats = list_feature_tables(conn, len(paths))
    if not feats:
        log("ERROR: no feature tables found.  Run --stage schema and send me the output.")
        sys.exit(1)

    def pick(*names):
        for n in names:
            for f in feats:
                if f.lower() == n.lower():
                    return f
        return None

    f_vars = pick("variables", "num_variables")
    f_cls = pick("clauses", "num_clauses")
    f_fam = pick("family")
    f_res = pick("verified-result", "result")
    f_rt = pick(args.runtime_feature) or next(
        (f for f in sorted(feats) if "runtime" in f.lower()), None)

    for label, f in [("variables", f_vars), ("clauses", f_cls)]:
        if f is None:
            log(f"ERROR: required feature '{label}' not found. "
                "Run --stage schema and send me the output.")
            sys.exit(1)
    log(f"[query] using features: variables={f_vars} clauses={f_cls} "
        f"family={f_fam} result={f_res} runtime={f_rt}")

    variables = read_feature(conn, f_vars, feats[f_vars])
    clauses = read_feature(conn, f_cls, feats[f_cls])
    family = read_feature(conn, f_fam, feats[f_fam]) if f_fam else {}
    result = read_feature(conn, f_res, feats[f_res]) if f_res else {}
    runtime = read_feature(conn, f_rt, feats[f_rt]) if f_rt else {}
    log(f"[query] {len(variables)} instances carry a variable count")

    rows, n_ab, n_e = [], 0, 0
    for h, v in variables.items():
        nv, nc = as_float(v), as_float(clauses.get(h))
        if nv is None or nc is None:
            continue
        if nv > MAX_VARIABLES or nc > MAX_CLAUSES:
            continue
        n_ab += 1
        fam = family.get(h) or ""
        if is_immune(fam):
            n_e += 1
            continue
        rt = as_float(runtime.get(h))
        rows.append({"hash": h, "family": fam, "variables": int(nv),
                     "clauses": int(nc), "result": result.get(h) or "",
                     "runtime": "" if rt is None else rt})

    log(f"[query] constraints A+B (vars<={MAX_VARIABLES}, clauses<={MAX_CLAUSES}): {n_ab}")
    log(f"[query] constraint E (resolution-hard families): -{n_e} -> {len(rows)}")

    if runtime:
        before = len(rows)
        rows = [r for r in rows
                if r["runtime"] != ""
                and args.min_runtime <= r["runtime"] <= args.max_runtime]
        log(f"[query] constraint D (difficulty, {f_rt} in "
            f"[{args.min_runtime}, {args.max_runtime}]s): {before} -> {len(rows)}")
    else:
        log("[query] WARNING: no runtime feature found; constraint D NOT applied")

    rows.sort(key=lambda r: -(r["runtime"] if r["runtime"] != "" else 0))
    if args.max_candidates and len(rows) > args.max_candidates:
        rows = rows[:args.max_candidates]
        log(f"[query] capped to the hardest {args.max_candidates}")

    os.makedirs(args.out_dir, exist_ok=True)
    out = os.path.join(args.out_dir, "candidates.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["hash", "family", "variables",
                                          "clauses", "result", "runtime"])
        w.writeheader()
        w.writerows(rows)
    log(f"[query] {len(rows)} candidates -> {out}")

    hist = {}
    for r in rows:
        hist[r["family"] or "?"] = hist.get(r["family"] or "?", 0) + 1
    log(f"\n[query] {len(hist)} families; top 30:")
    for fam, n in sorted(hist.items(), key=lambda x: -x[1])[:30]:
        log(f"    {n:5d}  {fam}")


# ------------------------------------------------------------------- fetch

def stage_fetch(args):
    rows = list(csv.DictReader(open(os.path.join(args.out_dir, "candidates.csv"))))
    cnfdir = os.path.join(args.data_root, "cnf")
    os.makedirs(cnfdir, exist_ok=True)
    log(f"[fetch] {len(rows)} candidates -> {cnfdir}")

    def one(r):
        h = r["hash"]
        try:
            return h, None, download(FILE_URL.format(hash=h),
                                     os.path.join(cnfdir, h + ".cnf.xz"))
        except Exception as e:
            return h, str(e), False

    ok = failed = cached = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(one, r) for r in rows]
        for i, fut in enumerate(as_completed(futs), 1):
            h, err, fresh = fut.result()
            if err:
                failed += 1
                log(f"  [{i}/{len(rows)}] FAIL {h}: {err}")
            else:
                ok += 1
                cached += 0 if fresh else 1
            if i % 50 == 0:
                log(f"  [{i}/{len(rows)}] ok={ok} failed={failed}")
    log(f"[fetch] ok={ok} (cached={cached}) failed={failed}")


# ------------------------------------------------------------------- verify

def open_maybe_compressed(path):
    with open(path, "rb") as probe:
        magic = probe.read(6)
    if magic.startswith(b"\xfd7zXZ"):
        return lzma.open(path, "rt", errors="replace")
    if magic.startswith(b"BZh"):
        return bz2.open(path, "rt", errors="replace")
    if magic.startswith(b"\x1f\x8b"):
        return gzip.open(path, "rt", errors="replace")
    return open(path, "rt", errors="replace")


TOKEN = re.compile(r"-?\d+")


def count_cnf(path):
    """Return (header_vars, header_clauses, literal_occurrences).

    Counts every non-zero integer token in the body, which is exactly what
    _FPGA_MAX_LITERAL_ELEMENTS bounds, and is robust to clauses split across
    lines.
    """
    hv = hc = None
    lits = 0
    with open_maybe_compressed(path) as f:
        for line in f:
            if not line:
                continue
            c = line[0]
            if c in "c%\n\r":
                continue
            if c == "p":
                parts = line.split()
                if len(parts) >= 4:
                    try:
                        hv, hc = int(parts[2]), int(parts[3])
                    except ValueError:
                        pass
                continue
            for t in TOKEN.findall(line):
                if t != "0":
                    lits += 1
    return hv, hc, lits


def stage_verify(args):
    rows = list(csv.DictReader(open(os.path.join(args.out_dir, "candidates.csv"))))
    cnfdir = os.path.join(args.data_root, "cnf")

    kept, over, missing = [], 0, 0
    for i, r in enumerate(rows, 1):
        path = os.path.join(cnfdir, r["hash"] + ".cnf.xz")
        if not os.path.exists(path):
            missing += 1
            continue
        try:
            hv, hc, lits = count_cnf(path)
        except Exception as e:
            log(f"  parse failed {r['hash']}: {e}")
            missing += 1
            continue
        r["header_variables"], r["header_clauses"] = hv, hc
        r["literal_elements"] = lits
        if (lits <= MAX_LITERAL_ELEMENTS and (hv or 0) <= MAX_VARIABLES
                and (hc or 0) <= MAX_CLAUSES):
            kept.append(r)
        else:
            over += 1
        if i % 100 == 0:
            log(f"  [{i}/{len(rows)}] kept={len(kept)} over={over}")

    log(f"[verify] constraint C (<= {MAX_LITERAL_ELEMENTS} literal elements): "
        f"{len(rows)-missing} checked -> {len(kept)} kept, {over} over budget, "
        f"{missing} unavailable")

    out = os.path.join(args.out_dir, "evalset.csv")
    if kept:
        with open(out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(kept[0].keys()))
            w.writeheader()
            w.writerows(kept)
        log(f"[verify] -> {out}")

    fams = {}
    for r in kept:
        fams[r.get("family") or "?"] = fams.get(r.get("family") or "?", 0) + 1
    log("\n[verify] surviving families:")
    for fam, n in sorted(fams.items(), key=lambda x: -x[1]):
        log(f"    {n:5d}  {fam}")

    n, nf = len(kept), len(fams)
    log("\n" + "=" * 64)
    log(f"VERDICT: {n} instances across {nf} families")
    if n >= VERDICT_GO_INSTANCES and nf >= VERDICT_GO_FAMILIES:
        log("  GO -- proceed to the software experiment with NeuroBack's")
        log("        pretrained model on this set.")
    elif n >= VERDICT_MARGINAL_INSTANCES:
        log("  MARGINAL -- usable but thin.  Pad with CNFgen-generated")
        log("        families before drawing conclusions.")
    else:
        log("  STOP -- the three constraints have an effectively empty")
        log("        intersection.  Either restore _FPGA_MAX_LITERAL_ELEMENTS")
        log("        to 1048576 (needs the URAM back) or change the target.")
    log("=" * 64)


# --------------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--stage", choices=["schema", "query", "fetch", "verify", "all"],
                   default="all")
    p.add_argument("--data-root", default="/mnt/data/satbench",
                   help="bulk storage for GBD databases and CNFs (default: %(default)s)")
    p.add_argument("--out-dir", default="./evalset",
                   help="small outputs: candidates.csv, evalset.csv (default: %(default)s)")
    p.add_argument("--runtime-feature", default="runtime-kissat",
                   help="GBD feature used as the difficulty proxy; falls back to the "
                        "first feature whose name contains 'runtime'")
    p.add_argument("--min-runtime", type=float, default=1.0,
                   help="seconds; SAT-Accel is ~3x a 64-core Kissat (default: %(default)s)")
    p.add_argument("--max-runtime", type=float, default=4000.0,
                   help="seconds; below the competition timeout, so known solvable")
    p.add_argument("--max-candidates", type=int, default=1500,
                   help="cap before downloading; keeps the hardest N")
    p.add_argument("--jobs", type=int, default=8)
    args = p.parse_args()

    os.makedirs(args.data_root, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)
    log(f"capacity limits: vars<={MAX_VARIABLES} clauses<={MAX_CLAUSES} "
        f"literals<={MAX_LITERAL_ELEMENTS}")
    log(f"data root: {args.data_root}")

    if args.stage == "schema":
        stage_schema(args)
        return
    if args.stage in ("query", "all"):
        stage_query(args)
    if args.stage in ("fetch", "all"):
        stage_fetch(args)
    if args.stage in ("verify", "all"):
        stage_verify(args)


if __name__ == "__main__":
    main()
