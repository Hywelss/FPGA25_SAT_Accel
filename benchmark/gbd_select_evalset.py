#!/usr/bin/env python3
"""Select a SAT-Accel-compatible evaluation set from the Global Benchmark Database.

Applies the hard capacity constraints of the VCK5000 build, then reports
whether the surviving set is large and diverse enough to evaluate a branching
heuristic on.

Dependency-free: standard library only.

How GBD actually behaves, as measured rather than assumed:

  * /getdatabase/<anything>.db returns the same 30 MB meta.db (verified by md5),
    so base.db and friends are not separately available.
  * meta.db holds one wide "features" table with family, result, filename,
    track and minisat1m -- but no instance sizes.  Its track column defaults to
    the instance hash and is unusable.
  * variables and clauses live server side and are reachable only through the
    /getinstances query endpoint.  Exactly six features are accepted there
    (see SERVER_FEATURES); every other name returns HTTP 500.
  * No runtime feature is queryable and no total-literal feature exists, so
    difficulty has to be measured after download, and the literal budget has to
    be counted from the CNFs.

So: filter sizes on the server, join the returned hashes against a local
meta.db for family and result, and count literals locally.

Stages
------
  schema   dump meta.db's schema and probe the server features (diagnostic)
  query    filter GBD, write candidates.csv        (no CNF downloads, seconds)
  fetch    download candidate CNFs                 (resumable, ~0.08 MB each)
  verify   stream-count literals, apply the literal budget, write evalset.csv
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
import urllib.error
import urllib.parse
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

DB_URL = "https://benchmark-database.de/getdatabase/meta.db"
FILE_URL = "https://benchmark-database.de/file/{hash}?context=cnf"
QUERY_URL = "https://benchmark-database.de/getinstances"

# Confirmed queryable server side; everything else returns HTTP 500.
SERVER_FEATURES = ("variables", "clauses", "result", "family", "track", "minisat1m")

# Columns read from meta.db's features table, when present.
META_COLUMNS = ("hash", "family", "result", "filename", "minisat1m")

# Fixed in advance so the outcome is not rationalised after the fact.
VERDICT_GO_INSTANCES, VERDICT_GO_FAMILIES = 30, 6
VERDICT_MARGINAL_INSTANCES = 10


def log(msg=""):
    print(msg, file=sys.stderr, flush=True)


def is_immune(*fields):
    blob = " ".join(str(f) for f in fields if f).lower()
    return any(p in blob for p in IMMUNE_PATTERNS)


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
        log("  fetched {} ({:.1f} MB)".format(desc, os.path.getsize(dest) / 1e6))
    return True


def ensure_db(data_root):
    dest = os.path.join(data_root, "gbd", "meta.db")
    if not os.path.exists(dest):
        log("[db] downloading meta.db")
    download(DB_URL, dest, desc="meta.db")
    return dest


# -------------------------------------------------------------- meta.db + API

def read_meta(path):
    """Return {hash: {column: value}} from meta.db's wide features table."""
    conn = sqlite3.connect(path)
    cols = [r[1] for r in conn.execute('PRAGMA table_info("features")')]
    if not cols:
        log('ERROR: meta.db has no "features" table.  Run --stage schema.')
        sys.exit(1)
    use = [c for c in META_COLUMNS if c in cols]
    if "hash" not in use:
        log("ERROR: features table has no hash column.  Columns: {}".format(cols))
        sys.exit(1)
    sel = ", ".join('"{}"'.format(c) for c in use)
    out = {}
    for row in conn.execute("SELECT {} FROM features".format(sel)):
        d = dict(zip(use, row))
        out[d["hash"]] = d
    log("[meta] {} instances, columns: {}".format(len(out), ", ".join(use)))
    return out


def server_query(expr, timeout=900):
    """Run a GBD query server side; return the list of instance hashes."""
    url = QUERY_URL + "?" + urllib.parse.urlencode({"query": expr, "context": "cnf"})
    try:
        body = urllib.request.urlopen(url, timeout=timeout).read().decode("utf8", "replace")
    except urllib.error.HTTPError as e:
        log("ERROR: server rejected the query (HTTP {}).".format(e.code))
        log("       query: {}".format(expr))
        log("       queryable features: {}".format(", ".join(SERVER_FEATURES)))
        sys.exit(1)
    return [l.rsplit("/", 1)[-1].strip() for l in body.splitlines() if l.startswith("http")]


def stage_schema(args):
    path = ensure_db(args.data_root)
    conn = sqlite3.connect(path)
    log("[schema] {}".format(path))
    log("")
    for (name,) in conn.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table','view') ORDER BY name"):
        try:
            n = conn.execute('SELECT COUNT(*) FROM "{}"'.format(name)).fetchone()[0]
        except sqlite3.Error:
            n = -1
        cols = [r[1] for r in conn.execute('PRAGMA table_info("{}")'.format(name))]
        log("  {:<20} rows={:<9} cols={}".format(name, n, cols))
    log("")
    log("[schema] probing which features the server accepts:")
    for f in SERVER_FEATURES:
        expr = "{} > 0".format(f) if f in ("variables", "clauses") \
            else "{} = probe_nonexistent_value".format(f)
        url = QUERY_URL + "?" + urllib.parse.urlencode({"query": expr, "context": "cnf"})
        try:
            urllib.request.urlopen(url, timeout=180).read()
            log("    {:<12} OK".format(f))
        except urllib.error.HTTPError as e:
            log("    {:<12} HTTP {}".format(f, e.code))
        except Exception as e:
            log("    {:<12} {}".format(f, str(e)[:60]))


# ---------------------------------------------------------------------- query

def stage_query(args):
    meta = read_meta(ensure_db(args.data_root))

    expr = "variables <= {} and clauses <= {}".format(MAX_VARIABLES, MAX_CLAUSES)
    log("[query] server side: {}".format(expr))
    hashes = server_query(expr)
    log("[query] constraints A+B: {}".format(len(hashes)))

    rows, n_meta, n_e, n_res = [], 0, 0, 0
    for h in hashes:
        d = meta.get(h)
        if d is None:
            continue
        n_meta += 1
        fam = d.get("family") or ""
        if is_immune(fam, d.get("filename") or ""):
            n_e += 1
            continue
        res = (d.get("result") or "").lower()
        if args.require_known_result and res not in ("sat", "unsat"):
            n_res += 1
            continue
        rows.append({"hash": h, "family": fam, "result": res,
                     "minisat1m": d.get("minisat1m") or "",
                     "filename": d.get("filename") or ""})

    log("[query] joined against meta.db: {}".format(n_meta))
    log("[query] constraint E (resolution-hard families): -{}".format(n_e))
    if args.require_known_result:
        log("[query] result in sat/unsat: -{}".format(n_res))
    log("[query] -> {} candidates".format(len(rows)))

    if args.max_candidates and len(rows) > args.max_candidates:
        # There is no server-side difficulty feature to rank by, so sample
        # evenly across families rather than truncating, which would bias the
        # set towards whatever the server happens to return first.
        byfam = {}
        for r in rows:
            byfam.setdefault(r["family"], []).append(r)
        picked, i = [], 0
        while len(picked) < args.max_candidates:
            added = False
            for fam in sorted(byfam):
                if i < len(byfam[fam]):
                    picked.append(byfam[fam][i])
                    added = True
                    if len(picked) >= args.max_candidates:
                        break
            if not added:
                break
            i += 1
        rows = picked
        log("[query] sampled {} evenly across {} families".format(len(rows), len(byfam)))

    os.makedirs(args.out_dir, exist_ok=True)
    out = os.path.join(args.out_dir, "candidates.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["hash", "family", "result",
                                          "minisat1m", "filename"])
        w.writeheader()
        w.writerows(rows)
    log("[query] -> {}".format(out))

    hist = {}
    for r in rows:
        hist[r["family"] or "?"] = hist.get(r["family"] or "?", 0) + 1
    log("")
    log("[query] {} families; top 30:".format(len(hist)))
    for fam, n in sorted(hist.items(), key=lambda x: -x[1])[:30]:
        log("    {:6d}  {}".format(n, fam))


# --------------------------------------------------------------------- fetch

def stage_fetch(args):
    rows = list(csv.DictReader(open(os.path.join(args.out_dir, "candidates.csv"))))
    cnfdir = os.path.join(args.data_root, "cnf")
    os.makedirs(cnfdir, exist_ok=True)
    log("[fetch] {} candidates -> {}".format(len(rows), cnfdir))

    def one(r):
        h = r["hash"]
        try:
            return h, None, download(FILE_URL.format(hash=h),
                                     os.path.join(cnfdir, h + ".cnf.xz"))
        except Exception as e:
            return h, str(e)[:80], False

    ok = failed = cached = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(one, r) for r in rows]
        for i, fut in enumerate(as_completed(futs), 1):
            h, err, fresh = fut.result()
            if err:
                failed += 1
                log("  [{}/{}] FAIL {}: {}".format(i, len(rows), h, err))
            else:
                ok += 1
                cached += 0 if fresh else 1
            if i % 200 == 0:
                log("  [{}/{}] ok={} failed={}".format(i, len(rows), ok, failed))
    log("[fetch] ok={} (cached={}) failed={}".format(ok, cached, failed))


# -------------------------------------------------------------------- verify

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
            log("  parse failed {}: {}".format(r["hash"], e))
            missing += 1
            continue
        r["header_variables"], r["header_clauses"] = hv, hc
        r["literal_elements"] = lits
        if (lits <= MAX_LITERAL_ELEMENTS and (hv or 0) <= MAX_VARIABLES
                and (hc or 0) <= MAX_CLAUSES):
            kept.append(r)
        else:
            over += 1
        if i % 500 == 0:
            log("  [{}/{}] kept={} over={}".format(i, len(rows), len(kept), over))

    log("[verify] literal budget (<= {}): {} checked -> {} kept, {} over, "
        "{} unavailable".format(MAX_LITERAL_ELEMENTS, len(rows) - missing,
                                len(kept), over, missing))

    out = os.path.join(args.out_dir, "evalset.csv")
    if kept:
        with open(out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(kept[0].keys()))
            w.writeheader()
            w.writerows(kept)
        log("[verify] -> {}".format(out))

    fams = {}
    for r in kept:
        fams[r.get("family") or "?"] = fams.get(r.get("family") or "?", 0) + 1
    log("")
    log("[verify] {} surviving families; top 30:".format(len(fams)))
    for fam, n in sorted(fams.items(), key=lambda x: -x[1])[:30]:
        log("    {:6d}  {}".format(n, fam))

    n, nf = len(kept), len(fams)
    log("")
    log("=" * 66)
    log("VERDICT: {} instances across {} families".format(n, nf))
    if n >= VERDICT_GO_INSTANCES and nf >= VERDICT_GO_FAMILIES:
        log("  GO on capacity.  The remaining unknown is difficulty: no GBD")
        log("  runtime feature exists, so run a CPU CDCL solver over evalset.csv")
        log("  and keep what clears the time floor.  That baseline run is also")
        log("  step one of the NeuroBack phase-initialisation experiment, so it")
        log("  is not extra work.")
    elif n >= VERDICT_MARGINAL_INSTANCES:
        log("  MARGINAL -- usable but thin.  Pad with CNFgen-generated families.")
    else:
        log("  STOP -- the capacity constraints have an effectively empty")
        log("  intersection.  Either restore _FPGA_MAX_LITERAL_ELEMENTS to")
        log("  1048576 (needs the URAM back) or change the target.")
    log("=" * 66)


# ----------------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--stage", choices=["schema", "query", "fetch", "verify", "all"],
                   default="all")
    p.add_argument("--data-root", default="/mnt/data/satbench",
                   help="bulk storage for meta.db and CNFs (default: %(default)s)")
    p.add_argument("--out-dir", default="./evalset",
                   help="small outputs: candidates.csv, evalset.csv (default: %(default)s)")
    p.add_argument("--require-known-result", action="store_true", default=True,
                   help="keep only instances whose result is sat or unsat (default)")
    p.add_argument("--all-results", dest="require_known_result", action="store_false",
                   help="also keep instances whose result is unknown")
    p.add_argument("--max-candidates", type=int, default=0,
                   help="0 = no cap.  Downloads average 0.08 MB per instance, so the "
                        "full candidate set is well under 1 GB")
    p.add_argument("--jobs", type=int, default=12)
    args = p.parse_args()

    os.makedirs(args.data_root, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)
    log("capacity limits: vars<={} clauses<={} literals<={}".format(
        MAX_VARIABLES, MAX_CLAUSES, MAX_LITERAL_ELEMENTS))
    log("data root: {}".format(args.data_root))

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
