#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-1800}"
DEVICE_NAME="${FPGA_DEVICE_NAME:-vck5000}"
CONFIG_FILE="${CONFIG_FILE:-$ROOT_DIR/src/configuration.json}"
CNF_FILE="${CNF_FILE:-$SCRIPT_DIR/rphp4_065_shuffled.cnf}"
EXPECTED_RESULT="${EXPECTED_RESULT:-0}"
METRICS_FILE="${1:-$SCRIPT_DIR/results/rphp4_065_vck5000_metrics.csv}"
LOG_FILE="${METRICS_FILE%.*}.log"

mkdir -p "$(dirname -- "$METRICS_FILE")"

set +e
/usr/bin/time -f $'HOST_WALL_SECONDS=%e\nHOST_MAX_RSS_KB=%M' \
    timeout --kill-after=30s "${TIMEOUT_SECONDS}s" \
    env FPGA_DEVICE_NAME="$DEVICE_NAME" \
    "$ROOT_DIR/src/bin/test.real.out" \
    "$ROOT_DIR/src/bin/workload-hw.xclbin" \
    "$CONFIG_FILE" \
    "$CNF_FILE" \
    "$METRICS_FILE" \
    "$EXPECTED_RESULT" 2>&1 | tee "$LOG_FILE"
status="${PIPESTATUS[0]}"
set -e

if [[ "$status" -eq 124 ]]; then
    echo "Benchmark timed out after ${TIMEOUT_SECONDS} seconds" >&2
fi

exit "$status"
