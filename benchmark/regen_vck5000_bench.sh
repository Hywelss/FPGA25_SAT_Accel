#!/usr/bin/env bash
# Regenerate VCK5000 SAT-Accel benchmark data with full provenance.
#
# Copy to the server, run from the repository root:
#   ./regen_vck5000_bench.sh
#
# Env:
#   REPEATS=3            runs per (instance, config)
#   TIMEOUT_SECONDS=1800 per-run wall clock limit
#   OUTDIR=...           output directory (default: benchmark/regen_<UTC>)
#   CONFIGS="default aggressive"

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPEATS="${REPEATS:-3}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-1800}"
CONFIGS="${CONFIGS:-default aggressive}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTDIR="${OUTDIR:-$ROOT_DIR/benchmark/regen_$STAMP}"

CONFIG_default="$ROOT_DIR/src/configuration.json"
CONFIG_aggressive="$ROOT_DIR/benchmark/configuration_vck5000_aggressive.json"

# instance:cnf_basename:expected  (expected: 1=SAT, 0=UNSAT, per benchmark/README.md)
INSTANCES=(
  "rphp4_065:rphp4_065_shuffled:0"
  "sp4-33:sp4-33-bin-nons-flat-noid:0"
  "quad_res_r29_m32:quad_res_r29_m32:1"
  "sp5-26-19:sp5-26-19-bin-nons-tree-noid:1"
  "randomG-B-Mix-n15-d05:randomG-B-Mix-n15-d05:0"
  "hidden-k3-s0-r4-n500-03:hidden-k3-s0-r4-n500-03:1"
  "unif-r3-v500-c1500-01:unif-r3-v500-c1500-01:1"
)

mkdir -p "$OUTDIR"
MANIFEST="$OUTDIR/manifest.txt"
INDEX="$OUTDIR/index.csv"

# ---------------------------------------------------------------- provenance
{
  echo "timestamp_utc=$STAMP"
  echo "hostname=$(hostname)"
  echo "user=$(id -un)"
  echo "kernel=$(uname -a)"
  echo
  echo "[git]"
  echo "commit=$(git -C "$ROOT_DIR" rev-parse HEAD)"
  echo "branch=$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD)"
  echo "describe=$(git -C "$ROOT_DIR" describe --always --dirty --tags 2>/dev/null || true)"
  echo "dirty_files:"
  git -C "$ROOT_DIR" status --porcelain || true
  echo
  echo "[artifacts]"
  for f in src/bin/workload-hw.xclbin src/bin/test.real.out \
           src/configuration.json benchmark/configuration_vck5000_aggressive.json; do
    if [[ -f "$ROOT_DIR/$f" ]]; then
      echo "$(sha256sum "$ROOT_DIR/$f" | awk '{print $1}')  $f  $(stat -c%s "$ROOT_DIR/$f") bytes  mtime=$(stat -c%y "$ROOT_DIR/$f")"
    else
      echo "MISSING  $f"
    fi
  done
  echo
  echo "[inputs]"
  for spec in "${INSTANCES[@]}"; do
    base="${spec#*:}"; base="${base%:*}"
    cnf="$ROOT_DIR/benchmark/$base.cnf"
    [[ -f "$cnf" ]] && echo "$(sha256sum "$cnf" | awk '{print $1}')  $base.cnf" || echo "MISSING  $base.cnf"
  done
  echo
  echo "[toolchain]"
  echo "xrt_version:";  (cat /opt/xilinx/xrt/version.json 2>/dev/null || xbutil --version 2>/dev/null || echo "unknown") | sed 's/^/  /'
  echo "vitis: ${VITIS_ROOT:-/tools/Xilinx/Vitis/2022.2}"
  echo
  echo "[board]"
  xbutil examine 2>&1 | sed 's/^/  /' || echo "  xbutil unavailable"
  echo
  echo "[xclbin]"
  xclbinutil --info --input "$ROOT_DIR/src/bin/workload-hw.xclbin" 2>&1 \
    | grep -iE "clock|freq|kernel|platform|uuid|target" | sed 's/^/  /' || echo "  xclbinutil unavailable"
} > "$MANIFEST"

echo "provenance -> $MANIFEST"

# --------------------------------------------------------------- board check
if command -v xbutil >/dev/null 2>&1; then
  if xbutil examine 2>/dev/null | grep -qiE "in use|busy"; then
    echo "WARNING: board may be in use by another process. Runs will not be exclusive." >&2
  fi
fi

# --------------------------------------------------------------------- runs
echo "instance,config,rep,exit_status,expected,timed_out,csv,log" > "$INDEX"

for spec in "${INSTANCES[@]}"; do
  name="${spec%%:*}"
  rest="${spec#*:}"
  base="${rest%:*}"
  expected="${rest##*:}"
  cnf="$ROOT_DIR/benchmark/$base.cnf"

  if [[ ! -f "$cnf" ]]; then
    echo "SKIP $name: $cnf not found" >&2
    continue
  fi

  for cfg in $CONFIGS; do
    cfg_var="CONFIG_$cfg"
    cfg_path="${!cfg_var:-}"
    if [[ -z "$cfg_path" || ! -f "$cfg_path" ]]; then
      echo "SKIP $name/$cfg: config $cfg_path not found" >&2
      continue
    fi

    for rep in $(seq 1 "$REPEATS"); do
      csv="$OUTDIR/${name}_${cfg}_r${rep}.csv"
      log="${csv%.csv}.log"
      rm -f "$csv" "$log"          # never append across runs

      echo ">>> $name / $cfg / rep $rep"
      set +e
      CONFIG_FILE="$cfg_path" \
      CNF_FILE="$cnf" \
      EXPECTED_RESULT="$expected" \
      TIMEOUT_SECONDS="$TIMEOUT_SECONDS" \
      "$ROOT_DIR/benchmark/run_vck5000.sh" "$csv"
      status=$?
      set -e

      timed_out=0
      [[ "$status" -eq 124 ]] && timed_out=1
      echo "$name,$cfg,$rep,$status,$expected,$timed_out,$(basename "$csv"),$(basename "$log")" >> "$INDEX"
      [[ "$status" -ne 0 ]] && echo "    -> FAILED exit=$status" >&2
    done
  done
done

# ------------------------------------------------------------------ summary
echo
echo "=== run index: $INDEX ==="
column -s, -t < "$INDEX"
echo
echo "Runs with non-zero exit (EXCLUDE from any performance claim):"
awk -F, 'NR>1 && $4!=0 {print "  "$1" / "$2" / rep "$3" -> exit "$4}' "$INDEX" || true
echo
echo "Output directory: $OUTDIR"
echo "Archive it together with manifest.txt before reporting any number."
