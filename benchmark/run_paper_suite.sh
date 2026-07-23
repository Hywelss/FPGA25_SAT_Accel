#!/usr/bin/env bash
#
# Re-run the instances that the SAT-Accel paper measured on the U55C, on this
# board, so that the two can be compared instance-by-instance.
#
# Reads  benchmark/paper_baseline.csv  (instance list + paper reference times)
# Writes benchmark/results/paper_suite/<instance>.csv   raw host metrics (appended per run)
#        benchmark/results/paper_suite/<instance>.log   full console output
#        benchmark/results/paper_suite/run_status.csv   instance,repeat,exit_code,wall_seconds
#
# Usage:
#   ./benchmark/run_paper_suite.sh                    # all instances, 5 repeats
#   REPEATS=3 ./benchmark/run_paper_suite.sh          # 3 repeats
#   ONLY='qg6-|bmc-ibm' ./benchmark/run_paper_suite.sh  # regex filter on instance name
#   CONFIG_FILE=benchmark/configuration_vck5000_aggressive.json ./benchmark/run_paper_suite.sh
#
# Host exit codes (see host.cpp / testcases.sh):
#   0 = solved and answer matched the expected result
#   3 = ran out of on-chip memory (NOT a solve; excluded from any speedup number)
#   other = program error

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

BASELINE="${BASELINE:-$SCRIPT_DIR/paper_baseline.csv}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/results/paper_suite}"
CONFIG_FILE="${CONFIG_FILE:-$ROOT_DIR/src/configuration.json}"
XCLBIN="${XCLBIN:-$ROOT_DIR/src/bin/workload-hw.xclbin}"
SOLVER="${SOLVER:-$ROOT_DIR/src/bin/test.real.out}"
DEVICE_NAME="${FPGA_DEVICE_NAME:-vck5000}"
REPEATS="${REPEATS:-5}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-1800}"
ONLY="${ONLY:-}"

for f in "$BASELINE" "$CONFIG_FILE" "$XCLBIN" "$SOLVER"; do
	[[ -e "$f" ]] || { echo "missing: $f" >&2; exit 1; }
done

mkdir -p "$OUT_DIR"
STATUS_FILE="$OUT_DIR/run_status.csv"
echo "instance,repeat,exit_code,wall_seconds" > "$STATUS_FILE"

while IFS=, read -r instance repo_path expect _rest; do
	[[ -z "${instance:-}" || "$instance" == \#* || "$instance" == "instance" ]] && continue
	[[ -n "$ONLY" && ! "$instance" =~ $ONLY ]] && continue

	cnf="$ROOT_DIR/$repo_path"
	if [[ ! -f "$cnf" ]]; then
		echo "SKIP $instance: $repo_path not found" | tee -a "$OUT_DIR/$instance.log"
		echo "$instance,0,missing," >> "$STATUS_FILE"
		continue
	fi

	metrics="$OUT_DIR/$instance.csv"
	log="$OUT_DIR/$instance.log"
	: > "$metrics"   # fresh file: host.cpp appends, so clear before a new sweep
	: > "$log"

	for ((r = 1; r <= REPEATS; r++)); do
		echo "=== $instance run $r/$REPEATS ===" >> "$log"
		start=$(date +%s.%N)
		timeout --kill-after=30s "${TIMEOUT_SECONDS}s" \
			env FPGA_DEVICE_NAME="$DEVICE_NAME" \
			"$SOLVER" "$XCLBIN" "$CONFIG_FILE" "$cnf" "$metrics" "$expect" \
			>> "$log" 2>&1
		status=$?
		end=$(date +%s.%N)
		wall=$(awk -v a="$start" -v b="$end" 'BEGIN{printf "%.3f", b-a}')
		echo "$instance,$r,$status,$wall" >> "$STATUS_FILE"

		case $status in
			0) printf '  %-46s run %d  ok      %ss\n' "$instance" "$r" "$wall" ;;
			3) printf '  %-46s run %d  OOM     %ss\n' "$instance" "$r" "$wall"
			   # a resource limit is deterministic; no point repeating it
			   break ;;
			124|137) printf '  %-46s run %d  TIMEOUT %ss\n' "$instance" "$r" "$wall"; break ;;
			*) printf '  %-46s run %d  ERR(%d) %ss\n' "$instance" "$r" "$status" "$wall"; break ;;
		esac
	done
done < "$BASELINE"

echo
echo "Raw metrics : $OUT_DIR/<instance>.csv"
echo "Run status  : $STATUS_FILE"
echo "Next        : python3 $SCRIPT_DIR/compare_to_paper.py"
