#!/bin/bash
# Build Kissat, CaDiCaL and MiniSat, then time them on the instances the VCK5000
# build has been measured on, so the FPGA numbers have a CPU reference.
#
#   ./benchmark/run_cpu_solvers.sh build     # fetch and compile into ~/solvers
#   ./benchmark/run_cpu_solvers.sh run       # measure, writes a CSV
#   ./benchmark/run_cpu_solvers.sh           # both
#
# Environment:
#   SOLVER_DIR   where to build           (default ~/solvers)
#   TIMEOUT_S    per solve                (default 300)
#   REPEATS      runs per pair, min kept  (default 3)
#   OUT_CSV      results file             (default benchmark/results/cpu_solvers.csv)
#
# Both the wall time and the solver's own reported process time are recorded.
# The FPGA figure this is meant to sit beside is "Kernel execution time", which
# excludes xclbin load and DMA but includes nothing of DIMACS parsing, so the
# process-time column is the one to compare against it; wall time is the honest
# end-to-end number and is kept as well.

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOLVER_DIR="${SOLVER_DIR:-$HOME/solvers}"
TIMEOUT_S="${TIMEOUT_S:-300}"
REPEATS="${REPEATS:-3}"
OUT_CSV="${OUT_CSV:-$REPO/benchmark/results/cpu_solvers.csv}"

KISSAT="$SOLVER_DIR/kissat/build/kissat"
CADICAL="$SOLVER_DIR/cadical/build/cadical"
MINISAT="$SOLVER_DIR/minisat/build/release/bin/minisat"

RD='\033[0;31m'; GN='\033[0;32m'; CY='\033[0;36m'; NC='\033[0m'

build_solvers(){
    mkdir -p "$SOLVER_DIR" && cd "$SOLVER_DIR" || exit 1

    for repo in kissat cadical; do
        echo -e "${CY}=== $repo ===${NC}"
        if [ ! -d "$repo" ]; then
            git clone --depth 1 "https://github.com/arminbiere/$repo.git" || continue
        fi
        ( cd "$repo" && ./configure && make -j"$(nproc)" ) || echo -e "${RD}$repo build failed${NC}"
    done

    echo -e "${CY}=== minisat ===${NC}"
    if [ ! -d minisat ]; then
        git clone --depth 1 https://github.com/niklasso/minisat.git || true
    fi
    if [ -d minisat ]; then
        # MiniSat predates the headers current libstdc++ expects to be included
        # explicitly, so it does not compile as published on a modern toolchain.
        ( cd minisat
          for h in minisat/utils/Options.h minisat/utils/System.h minisat/core/SolverTypes.h minisat/mtl/IntTypes.h; do
              [ -f "$h" ] && ! grep -q "<cstdint>" "$h" && sed -i '1i #include <cstdint>\n#include <cstring>' "$h"
          done
          make -j"$(nproc)" r ) || echo -e "${RD}minisat build failed -- see note in the script header${NC}"
    fi

    echo
    for s in "$KISSAT" "$CADICAL" "$MINISAT"; do
        if [ -x "$s" ]; then echo -e "${GN}ok   $s${NC}"; else echo -e "${RD}miss $s${NC}"; fi
    done
}

# Instances the VCK5000 build has on-board numbers for. Extend freely; anything
# missing is skipped with a warning rather than failing the run.
instances(){
    cat <<EOF
$REPO/SAT_test_cases/sat/nqueens_32.dimacs
$REPO/SAT_test_cases/bmc/bmc-ibm-3.cnf
$REPO/SAT_test_cases/unsat/marg3x3add8ch.shuffled-as.sat03-1448.cnf
$REPO/SAT_test_cases/unsat/logistics-rotate-07t5.shuffled-as.sat05-1137.dimacs
$REPO/SAT_test_cases/sat/bmc-ibm-1.dimacs
$REPO/SAT_test_cases/sat/aalto.dimacs
EOF
}

# Each solver prints its own solve time; pull it out so the comparison does not
# have to include DIMACS parsing.
process_time(){
    local name=$1 out=$2
    case "$name" in
        kissat)  sed -n 's/^c process-time:.*[^0-9.]\([0-9][0-9.]*\) seconds.*/\1/p' <<<"$out" | tail -1 ;;
        cadical) sed -n 's/^c total process time since initialization:[[:space:]]*\([0-9][0-9.]*\).*/\1/p' <<<"$out" | tail -1 ;;
        minisat) sed -n 's/^CPU time[^:]*:[[:space:]]*\([0-9][0-9.]*\).*/\1/p' <<<"$out" | tail -1 ;;
    esac
}

measure(){
    mkdir -p "$(dirname "$OUT_CSV")"
    echo "instance,solver,wall_seconds,process_seconds,result" > "$OUT_CSV"

    echo -e "${CY}CPU: $(lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1)${NC}"
    echo -e "${CY}timeout ${TIMEOUT_S}s, best of ${REPEATS}${NC}\n"

    while read -r f; do
        [ -n "$f" ] || continue
        if [ ! -f "$f" ]; then echo -e "${RD}missing: $f${NC}"; continue; fi

        for pair in "kissat:$KISSAT" "cadical:$CADICAL" "minisat:$MINISAT"; do
            local name="${pair%%:*}" bin="${pair#*:}"
            [ -x "$bin" ] || continue

            local best_wall="" best_proc="" result=UNKNOWN
            for _ in $(seq "$REPEATS"); do
                local t0 t1 out rc
                t0=$(date +%s.%N)
                out=$(timeout "$TIMEOUT_S" "$bin" "$f" 2>/dev/null); rc=$?
                t1=$(date +%s.%N)

                local wall; wall=$(echo "$t1 - $t0" | bc)
                local proc; proc=$(process_time "$name" "$out")
                [ -n "$proc" ] || proc="$wall"

                if [ "$rc" -eq 124 ]; then result=TIMEOUT
                elif grep -q '^s SATISFIABLE'   <<<"$out"; then result=SAT
                elif grep -q '^s UNSATISFIABLE' <<<"$out"; then result=UNSAT
                fi

                # Keep the fastest run: it is the one least polluted by whatever
                # else the shared machine was doing.
                if [ -z "$best_wall" ] || (( $(echo "$wall < $best_wall" | bc -l) )); then
                    best_wall="$wall"; best_proc="$proc"
                fi
                [ "$result" = TIMEOUT ] && break
            done

            printf "%s,%s,%.3f,%s,%s\n" \
                "$(basename "$f")" "$name" "$best_wall" "$best_proc" "$result" | tee -a "$OUT_CSV"
        done
        echo
    done < <(instances)

    echo -e "${GN}written: $OUT_CSV${NC}"
}

case "${1:-all}" in
    build) build_solvers ;;
    run)   measure ;;
    all)   build_solvers && echo && measure ;;
    *)     echo "usage: $0 {build|run|all}"; exit 1 ;;
esac
