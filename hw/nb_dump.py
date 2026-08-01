#!/usr/bin/env python3
"""Emit hardware reference vectors for a NeuroBack GTModel(3,3) accelerator.

Two independent stages:

  graph    DIMACS -> the exact graph NeuroBack's model consumes, written as
           flat binaries.  Pure standard library, no torch.  This is the
           reference the PL gather/scatter kernel must reproduce bit for bit.

  tensors  Instantiate GTModel(3,3), run a forward pass over that graph with
           hooks on every submodule, and dump each module's inputs, outputs and
           parameters, plus per-tensor statistics for the int8-vs-int16
           decision.  Needs torch and torch_geometric.

Weights may be random: the hardware is verified against shapes, dataflow and
numerics, none of which depend on the weights being trained.  Pass --checkpoint
once a trained .ptg exists and the same reference vectors are regenerated
against it without any change to the kernels.

Graph construction mirrors NeuroBack's graph.py and data.py exactly:

  * variable node ids are assigned in order of first appearance in the file,
    not by variable number
  * clause nodes follow all variable nodes, in file order
  * one root node last, joined to every clause node with attribute 0
  * x is 1 for a variable, -1 for a clause, 0 for the root
  * edges are stored directed var -> clause, then symmetrised by appending the
    reversed index with the attribute copied unchanged, so the model sees
    2 * (literal_occurrences + clauses) edges
  * edge_type = attr + 1, giving 0 for a negative occurrence, 1 for a root
    edge, and 2 for a positive occurrence

Usage
-----
  python3 nb_dump.py graph   instance.cnf  -o ref/
  python3 nb_dump.py tensors instance.cnf  -o ref/ [--checkpoint finetune-best.ptg]
"""

import argparse
import bz2
import gzip
import json
import lzma
import os
import struct
import sys

# GTModel(3,3) shape constants, from gt_model.py.
OUT_CHANNELS = 48
HEAD_CNT = 8
PATCH_DIM = 16
NUM_RELATIONS = 3
RB_NUM = 3
DECODE_NUM = 3


def log(msg=""):
    print(msg, file=sys.stderr, flush=True)


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


# ----------------------------------------------------------------- graph stage

def build_graph(cnf_path):
    """Return (x, edge_index, edge_attr, n2v, stats) in NeuroBack's format."""
    v2n = {}
    x = []

    # Pass 1: variable nodes, in order of first appearance.
    with open_maybe_compressed(cnf_path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in "cp%":
                continue
            for tok in line.split():
                lit = int(tok)
                if lit == 0:
                    continue
                v = abs(lit)
                if v not in v2n:
                    v2n[v] = len(x)
                    x.append(1)
    var_num = len(x)

    # Pass 2: clause nodes and var -> clause edges.
    edge_src, edge_dst, edge_attr = [], [], []
    n_clauses = 0
    multiline = 0
    with open_maybe_compressed(cnf_path) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in "cp%":
                continue
            toks = line.split()
            if toks[-1] != "0":
                # NeuroBack's parser drops the last token unconditionally and so
                # silently corrupts clauses that span lines.  Count them rather
                # than guess.
                multiline += 1
            lits = [int(t) for t in toks[:-1]]
            cla = len(x)
            x.append(-1)
            n_clauses += 1
            for lit in lits:
                if lit == 0:
                    continue
                edge_src.append(v2n[abs(lit)])
                edge_dst.append(cla)
                edge_attr.append(1 if lit > 0 else -1)

    n_lit_edges = len(edge_src)

    # Root node joined to every clause node.
    root = len(x)
    for cla in range(var_num, var_num + n_clauses):
        edge_src.append(root)
        edge_dst.append(cla)
        edge_attr.append(0)
    x.append(0)

    # Symmetrise exactly as MyOwnDataset.get() does: append the reversed index
    # and copy the attribute unchanged.
    stored = len(edge_src)
    edge_src, edge_dst = edge_src + edge_dst, edge_dst + edge_src
    edge_attr = edge_attr + edge_attr

    n2v = [-1] * var_num
    for v, n in v2n.items():
        n2v[n] = v

    per_type = {0: 0, 1: 0, 2: 0}
    for a in edge_attr:
        per_type[a + 1] += 1

    stats = {
        "nodes": len(x),
        "variables": var_num,
        "clauses": n_clauses,
        "root_nodes": 1,
        "literal_occurrences": n_lit_edges,
        "edges_stored_directed": stored,
        "edges_after_symmetrisation": len(edge_src),
        "edges_by_type": {"0_negative": per_type[0],
                          "1_root": per_type[1],
                          "2_positive": per_type[2]},
        "clauses_not_ending_in_zero": multiline,
    }
    return x, (edge_src, edge_dst), edge_attr, n2v, stats


def write_bin(path, values, fmt):
    with open(path, "wb") as f:
        f.write(struct.pack("<{}{}".format(len(values), fmt), *values))
    return os.path.getsize(path)


def stage_graph(args):
    x, (src, dst), attr, n2v, stats = build_graph(args.cnf)
    gdir = os.path.join(args.out, "graph")
    os.makedirs(gdir, exist_ok=True)

    files = {
        "x.bin": (write_bin(os.path.join(gdir, "x.bin"), x, "b"), "int8", [len(x)]),
        "edge_src.bin": (write_bin(os.path.join(gdir, "edge_src.bin"), src, "i"),
                         "int32", [len(src)]),
        "edge_dst.bin": (write_bin(os.path.join(gdir, "edge_dst.bin"), dst, "i"),
                         "int32", [len(dst)]),
        "edge_attr.bin": (write_bin(os.path.join(gdir, "edge_attr.bin"), attr, "b"),
                          "int8", [len(attr)]),
        "edge_type.bin": (write_bin(os.path.join(gdir, "edge_type.bin"),
                                    [a + 1 for a in attr], "B"), "uint8", [len(attr)]),
        "n2v.bin": (write_bin(os.path.join(gdir, "n2v.bin"), n2v, "i"),
                    "int32", [len(n2v)]),
    }

    meta = {"source_cnf": os.path.abspath(args.cnf), "stats": stats,
            "layout": {k: {"bytes": v[0], "dtype": v[1], "shape": v[2]}
                       for k, v in files.items()},
            "note": "edge_src/edge_dst are parallel arrays; the model's "
                    "edge_index is their row-wise stack."}
    with open(os.path.join(gdir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2)

    log("[graph] {}".format(args.cnf))
    for k, v in stats.items():
        log("    {:<28} {}".format(k, v))
    if stats["clauses_not_ending_in_zero"]:
        log("    WARNING: {} clause lines do not end in 0.  NeuroBack's parser "
            "drops the last token regardless, so those clauses lose a literal."
            .format(stats["clauses_not_ending_in_zero"]))
    log("[graph] -> {}".format(gdir))


# --------------------------------------------------------------- tensor stage

def tensor_stats(t):
    import torch
    t = t.detach().float()
    if t.numel() == 0:
        return {"numel": 0}
    absmax = float(t.abs().max())
    return {
        "numel": int(t.numel()),
        "shape": list(t.shape),
        "min": float(t.min()),
        "max": float(t.max()),
        "absmax": absmax,
        "mean": float(t.mean()),
        "std": float(t.std()) if t.numel() > 1 else 0.0,
        # Bits needed to hold absmax at the precision of the smallest non-zero
        # magnitude is not knowable per tensor, so report the headroom that a
        # symmetric per-tensor scale would give at int8 and int16.
        "int8_lsb": absmax / 127.0 if absmax else 0.0,
        "int16_lsb": absmax / 32767.0 if absmax else 0.0,
    }


def stage_tensors(args):
    try:
        import torch
        from torch_geometric.data import Data  # noqa: F401
    except ImportError as e:
        log("ERROR: this stage needs torch and torch_geometric ({}).".format(e))
        log("       Miniconda installs to $HOME without root:")
        log("         bash Miniconda3-latest-Linux-x86_64.sh -b -p /mnt/data/conda")
        log("         conda create -p /mnt/data/conda/nb python=3.11 -y")
        log("         conda run -p /mnt/data/conda/nb pip install torch torch_geometric")
        sys.exit(1)

    sys.path.insert(0, args.neuroback)
    try:
        from gt_model import GTModel
    except ImportError:
        log("ERROR: gt_model.py not found.  Pass --neuroback <path to the "
            "neuroback checkout>.")
        sys.exit(1)

    x, (src, dst), attr, _, stats = build_graph(args.cnf)
    log("[tensors] graph: {} nodes, {} edges after symmetrisation".format(
        stats["nodes"], stats["edges_after_symmetrisation"]))

    torch.manual_seed(args.seed)
    model = GTModel(RB_NUM, DECODE_NUM)
    if args.checkpoint:
        ck = torch.load(args.checkpoint, map_location="cpu")
        model.load_state_dict(ck["model_state_dict"])
        log("[tensors] loaded {}".format(args.checkpoint))
    else:
        log("[tensors] random weights, seed={} (shapes and dataflow are "
            "independent of training)".format(args.seed))
    model.eval()

    xt = torch.tensor(x, dtype=torch.float32).unsqueeze(1)
    ei = torch.tensor([src, dst], dtype=torch.long)
    ea = torch.tensor(attr, dtype=torch.float32).unsqueeze(1)

    tdir = os.path.join(args.out, "tensors")
    wdir = os.path.join(tdir, "weights")
    adir = os.path.join(tdir, "activations")
    for d in (wdir, adir):
        os.makedirs(d, exist_ok=True)

    manifest = {"model": "GTModel({}, {})".format(RB_NUM, DECODE_NUM),
                "constants": {"out_channels": OUT_CHANNELS, "head_cnt": HEAD_CNT,
                              "patch_dim": PATCH_DIM,
                              "num_relations": NUM_RELATIONS},
                "graph": stats, "weights": {}, "activations": []}

    def save(t, path):
        import numpy as np
        arr = t.detach().cpu().float().numpy().ravel()
        arr.astype("<f4").tofile(path)
        return arr.size

    for name, p in model.named_parameters():
        fn = name.replace(".", "_") + ".f32.bin"
        save(p, os.path.join(wdir, fn))
        manifest["weights"][name] = {"file": "weights/" + fn, **tensor_stats(p)}

    order = []

    def hook(mod, inp, out, name=""):
        idx = len(order)
        rec = {"index": idx, "module": name, "type": type(mod).__name__,
               "inputs": [], "outputs": []}
        for j, t in enumerate(inp):
            if hasattr(t, "detach"):
                fn = "{:03d}_{}_in{}.f32.bin".format(idx, name.replace(".", "_"), j)
                save(t, os.path.join(adir, fn))
                rec["inputs"].append({"file": "activations/" + fn, **tensor_stats(t)})
        outs = out if isinstance(out, (tuple, list)) else (out,)
        for j, t in enumerate(outs):
            if hasattr(t, "detach"):
                fn = "{:03d}_{}_out{}.f32.bin".format(idx, name.replace(".", "_"), j)
                save(t, os.path.join(adir, fn))
                rec["outputs"].append({"file": "activations/" + fn, **tensor_stats(t)})
        order.append(rec)

    handles = []
    for name, mod in model.named_modules():
        if name and len(list(mod.children())) == 0:      # leaves only
            handles.append(mod.register_forward_hook(
                lambda m, i, o, n=name: hook(m, i, o, n)))

    with torch.no_grad():
        y = model(xt, ei, ea)
    for h in handles:
        h.remove()

    save(y, os.path.join(adir, "output_sigmoid.f32.bin"))
    manifest["activations"] = order
    manifest["final_output"] = {"file": "activations/output_sigmoid.f32.bin",
                                **tensor_stats(y)}

    with open(os.path.join(tdir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    n_w = sum(v["numel"] for v in manifest["weights"].values())
    log("[tensors] {} parameters in {} tensors".format(n_w, len(manifest["weights"])))
    log("[tensors] {} leaf modules captured".format(len(order)))
    log("[tensors] int8 weights would be {:.1f} KB, int16 {:.1f} KB "
        "(AIE local memory is 400 x 32 KB = 12.8 MB)".format(n_w / 1024, 2 * n_w / 1024))

    wmax = max((v["absmax"] for v in manifest["weights"].values()), default=0)
    amax = max((o["absmax"] for r in order for o in r["outputs"]), default=0)
    log("[tensors] max |weight| {:.4g}, max |activation| {:.4g}".format(wmax, amax))
    log("[tensors] -> {}".format(tdir))


# ----------------------------------------------------------------------- main

def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("stage", choices=["graph", "tensors"])
    p.add_argument("cnf")
    p.add_argument("-o", "--out", default="./ref")
    p.add_argument("--neuroback", default=".",
                   help="path to the neuroback checkout holding gt_model.py")
    p.add_argument("--checkpoint", default=None,
                   help="trained .ptg; omit to use random weights")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    if args.stage == "graph":
        stage_graph(args)
    else:
        stage_tensors(args)


if __name__ == "__main__":
    main()
