#!/usr/bin/env python3
"""Select a SAT-Accel-compatible evaluation set from the Global Benchmark Database.

Applies the three hard capacity constraints of the VCK5000 build plus a
difficulty floor, then reports whether the surviving set is large and diverse
enough to evaluate a branching heuristic on.

Large files (GBD databases, downloaded CNFs) go under --data-root, which
defaults to /mnt/data/satbench.

Stages
------
  query    query GBD, write candidates.csv          (no downloads, seconds)
  fetch    download candidate CNFs                  (bulk, resumable)
  verify   stream-count literals, apply constraint C, write evalset.csv
  all      run all three

Usage
-----
  pip install gbd-tools
  ./gbd_select_evalset.py --stage all
  ./gbd_select_evalset.py --stage query --max-candidates 500
"""

import argparse
import bz2
import csv
import gzip
import lzma
import os
import re
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# ---------------------------------------------------------------- constraints

# Hard capacity limits of the VCK5000 build (src/fpga_solver.h, k2k_vck5000.cfg).
MAX_VARIABLES = 32_768          # _FPGA_MAX_LITERALS
MAX_CLAUSES = 131_072           # _FPGA_MAX_CLAUSES
MAX_LITERAL_ELEMENTS = 524_288  # _FPGA_MAX_LITERAL_ELEMENTS, halved for VCK5000
                                # upstream U55C value is 1_048_576

# Families with exponential resolution lower bounds: no branching heuristic,
# learned or otherwise, can help on these.  Matched case-insensitively against
# both the GBD family field and the filename.
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

# Verdict thresholds.  Fixed in advance so the outcome is not rationalised
# after the fact.
VERDICT_GO_INSTANCES, VERDICT_GO_FAMILIES = 30, 6
VERDICT_MARGINAL_INSTANCES = 10


# --------------------------------------------------------------------- helpers

def log(msg):
    print(msg, file=sys.stderr, flush=True)


def is_immune(*fields):
    blob = " ".join(str(f) for f in fields if f).lower()
    return any(p in blob for p in IMMUNE_PATTERNS)


def download(url, dest, desc=None):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=120) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    os.replace(tmp, dest)
    if desc:
        log(f"  fetched {desc} ({os.path.getsize(dest)/1e6:.1f} MB)")
    return True


def open_maybe_compressed(path):
    """Open a possibly xz/bz2/gzip-compressed DIMACS file as text, streaming."""
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

    Literal occurrences are counted as every non-zero integer token in the
    body, which is exactly what _FPGA_MAX_LITERAL_ELEMENTS bounds.  Counting
    tokens rather than lines is robust to clauses split across lines.
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


# ----------------------------------------------------------------------- stages

def stage_query(args):
    try:
        from gbd_core.api import GBD
    except ImportError:
        log("ERROR: gbd-tools not installed.  Run: pip install gbd-tools")
        sys.exit(1)

    dbdir = os.path.join(args.data_root, "gbd")
    paths = []
    for name, url in DB_URLS.items():
        dest = os.path.join(dbdir, name)
        log(f"[db] {name}")
        download(url, dest, desc=name)
        paths.append(dest)

    query = f"variables <= {MAX_VARIABLES} and clauses <= {MAX_CLAUSES}"
    log(f"[query] {query}")

    wanted = ["family", "variables", "clauses", "verified-result", args.runtime_feature]
    with GBD(paths) as gbd:
        available = set()
        try:
            available = set(gbd.get_features())
        except Exception:
            pass
        resolve = [w for w in wanted if not available or w in available]
        missing = [w for w in wanted if w not in resolve]
        if missing:
            log(f"[query] features unavailable, skipping: {', '.join(missing)}")
        df = gbd.query(query, resolve=resolve)

    df = df.reset_index()
    log(f"[query] {len(df)} instances within capacity constraints A and B")

    rt = args.runtime_feature
    if rt in df.columns:
        df["_rt"] = df[rt].apply(_as_float)
        before = len(df)
        df = df[df["_rt"].notna()
                & (df["_rt"] >= args.min_runtime)
                & (df["_rt"] <= args.max_runtime)]
        log(f"[query] difficulty floor {args.min_runtime}-{args.max_runtime}s "
            f"({rt}): {before} -> {len(df)}")
    else:
        log(f"[query] WARNING: no runtime feature; difficulty floor NOT applied")
        df["_rt"] = None

    hashcol = next((c for c in df.columns if c.lower() == "hash"), None)
    if hashcol is None:
        log(f"ERROR: no hash column in result.  Columns: {list(df.columns)}")
        sys.exit(1)

    before = len(df)
    id_cols = [c for c in ("family", "filename") if c in df.columns]
    if id_cols:
        mask = df.apply(lambda r: not is_immune(*(r[c] for c in id_cols)), axis=1)
        df = df[mask]
    log(f"[query] constraint E (resolution-hard families): {before} -> {len(df)}")

    if args.max_candidates and len(df) > args.max_candidates:
        df = df.sort_values("_rt", ascending=False).head(args.max_candidates)
        log(f"[query] capped to hardest {args.max_candidates} candidates")

    out = os.path.join(args.out_dir, "candidates.csv")
    os.makedirs(args.out_dir, exist_ok=True)
    df.rename(columns={hashcol: "hash"}).to_csv(out, index=False)
    log(f"[query] -> {out}")

    if "family" in df.columns:
        log("\n[query] family histogram (top 25):")
        for f, n in df["family"].value_counts().head(25).items():
            log(f"    {n:5d}  {f}")
    return out


def _as_float(v):
    try:
        f = float(v)
        return f if f == f else None
    except (TypeError, ValueError):
        return None


def stage_fetch(args):
    src = os.path.join(args.out_dir, "candidates.csv")
    rows = list(csv.DictReader(open(src)))
    cnfdir = os.path.join(args.data_root, "cnf")
    os.makedirs(cnfdir, exist_ok=True)
    log(f"[fetch] {len(rows)} candidates -> {cnfdir}")

    def one(r):
        h = r["hash"]
        dest = os.path.join(cnfdir, h + ".cnf.xz")
        try:
            fresh = download(FILE_URL.format(hash=h), dest)
            return h, dest, None, fresh
        except Exception as e:
            return h, dest, str(e), False

    done = failed = skipped = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(one, r) for r in rows]
        for i, fut in enumerate(as_completed(futs), 1):
            h, _, err, fresh = fut.result()
            if err:
                failed += 1
                log(f"  [{i}/{len(rows)}] FAIL {h}: {err}")
            else:
                done += 1
                skipped += 0 if fresh else 1
            if i % 50 == 0:
                log(f"  [{i}/{len(rows)}] ok={done} fail={failed}")
    log(f"[fetch] ok={done} (cached={skipped}) failed={failed}")


def stage_verify(args):
    src = os.path.join(args.out_dir, "candidates.csv")
    rows = list(csv.DictReader(open(src)))
    cnfdir = os.path.join(args.data_root, "cnf")

    kept, dropped_c, missing = [], 0, 0
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
        if (lits <= MAX_LITERAL_ELEMENTS
                and (hv or 0) <= MAX_VARIABLES
                and (hc or 0) <= MAX_CLAUSES):
            kept.append(r)
        else:
            dropped_c += 1
        if i % 100 == 0:
            log(f"  [{i}/{len(rows)}] kept={len(kept)} dropped={dropped_c}")

    log(f"[verify] constraint C (<= {MAX_LITERAL_ELEMENTS} literal elements): "
        f"{len(rows)-missing} checked -> {len(kept)} kept, {dropped_c} over budget, "
        f"{missing} unavailable")

    out = os.path.join(args.out_dir, "evalset.csv")
    if kept:
        cols = list(kept[0].keys())
        with open(out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            w.writerows(kept)
    log(f"[verify] -> {out}")

    fams = {}
    for r in kept:
        fams[r.get("family") or "?"] = fams.get(r.get("family") or "?", 0) + 1
    log("\n[verify] surviving families:")
    for f, n in sorted(fams.items(), key=lambda x: -x[1]):
        log(f"    {n:5d}  {f}")

    n, nf = len(kept), len(fams)
    log("\n" + "=" * 62)
    log(f"VERDICT: {n} instances across {nf} families")
    if n >= VERDICT_GO_INSTANCES and nf >= VERDICT_GO_FAMILIES:
        log("  GO -- proceed to the software experiment with NeuroBack's")
        log("       pretrained model on this set.")
    elif n >= VERDICT_MARGINAL_INSTANCES:
        log("  MARGINAL -- usable but thin.  Pad with CNFgen-generated")
        log("       families before drawing conclusions.")
    else:
        log("  STOP -- the three constraints have an effectively empty")
        log("       intersection.  Either restore _FPGA_MAX_LITERAL_ELEMENTS")
        log(f"       to 1048576 (needs the URAM back) or change the target.")
    log("=" * 62)


# ------------------------------------------------------------------------ main

def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--stage", choices=["query", "fetch", "verify", "all"], default="all")
    p.add_argument("--data-root", default="/mnt/data/satbench",
                   help="bulk storage for GBD databases and CNFs (default: %(default)s)")
    p.add_argument("--out-dir", default="./evalset",
                   help="small outputs: candidates.csv, evalset.csv (default: %(default)s)")
    p.add_argument("--runtime-feature", default="runtime-kissat",
                   help="GBD runtime feature used as the difficulty proxy")
    p.add_argument("--min-runtime", type=float, default=1.0,
                   help="seconds; SAT-Accel is ~3x a 64-core Kissat (default: %(default)s)")
    p.add_argument("--max-runtime", type=float, default=4000.0,
                   help="seconds; below the competition timeout so it is known solvable")
    p.add_argument("--max-candidates", type=int, default=1500,
                   help="cap before downloading; keeps the hardest N")
    p.add_argument("--jobs", type=int, default=8)
    args = p.parse_args()

    os.makedirs(args.data_root, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)

    log(f"capacity limits: vars<={MAX_VARIABLES} clauses<={MAX_CLAUSES} "
        f"literals<={MAX_LITERAL_ELEMENTS}")
    log(f"data root: {args.data_root}")

    if args.stage in ("query", "all"):
        stage_query(args)
    if args.stage in ("fetch", "all"):
        stage_fetch(args)
    if args.stage in ("verify", "all"):
        stage_verify(args)


if __name__ == "__main__":
    main()
