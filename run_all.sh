#!/usr/bin/env bash
# Baseline bipartition - batch run all IBM benchmarks (Linux/WSL)
# Usage: bash run_all.sh [runs] [max_passes] [seed]

set -uo pipefail
cd "$(dirname "$0")"

RUNS="${1:-8}"
PASSES="${2:-8}"
SEED="${3:-2026}"
BENCH_DIR="./benchmark"

EXE="./build/TopoPartitioner"
[ ! -f "$EXE" ] && [ -f "./build/Release/TopoPartitioner" ] && EXE="./build/Release/TopoPartitioner"

if [ ! -f "$EXE" ]; then
    echo "[BUILD] Compiling..."
    # Remove build cache if it was generated on a different OS (e.g. Windows MSVC)
    if [ -f "build/CMakeCache.txt" ] && ! grep -q "Unix Makefiles" build/CMakeCache.txt 2>/dev/null; then
        echo "[BUILD] Removing stale cache from another OS..."
        rm -rf build
    fi
    cmake -B build 2>&1
    cmake --build build -j$(nproc) 2>&1
    EXE="./build/TopoPartitioner"
    [ ! -f "$EXE" ] && [ -f "./build/Release/TopoPartitioner" ] && EXE="./build/Release/TopoPartitioner"
    [ ! -f "$EXE" ] && { echo "[ERROR] Build failed"; exit 1; }
fi

mkdir -p results

echo "============================================================"
echo " Baseline Bipartition - IBM Benchmarks (18 cases)"
echo "============================================================"
printf "%-8s %10s %10s %10s %10s\n" "Bench" "MyCut" "Optimal" "Gap" "Gap%"
printf "%-8s %10s %10s %10s %10s\n" "--------" "------" "-------" "---" "----"

# epsilon=2% optimal cuts
declare -A OPT
OPT[ibm01]=200; OPT[ibm02]=307; OPT[ibm03]=951; OPT[ibm04]=573
OPT[ibm05]=1706; OPT[ibm06]=962; OPT[ibm07]=878; OPT[ibm08]=1140
OPT[ibm09]=620; OPT[ibm10]=1253; OPT[ibm11]=1051; OPT[ibm12]=1919
OPT[ibm13]=831; OPT[ibm14]=1842; OPT[ibm15]=2730; OPT[ibm16]=1827
OPT[ibm17]=2270; OPT[ibm18]=1521

TC=0; TO=0
for i in $(seq 1 18); do
    id=$(printf "%02d" "$i")
    ds="ibm${id}"
    hgr="${BENCH_DIR}/${ds}.hgr"
    [ ! -f "$hgr" ] && { printf "%-8s file not found\n" "$ds"; continue; }
    out=$("$EXE" baseline "$hgr" "$RUNS" "$PASSES" "$SEED" 2>&1)
    cut=$(echo "$out" | grep "\[BEST\] cutsize" | grep -oP '\d+' | head -1)
    [ -z "$cut" ] && cut=0
    opt=${OPT[$ds]}
    gap=$((cut - opt))
    gap_pct=$(echo "scale=1; $gap * 100 / $opt" | bc 2>/dev/null || echo "-")
    TC=$((TC + cut)); TO=$((TO + opt))
    printf "%-8s %10s %10s %10s %9s%%\n" "$ds" "$cut" "$opt" "$gap" "$gap_pct"
done
tg=$((TC - TO))
tg_pct=$(echo "scale=1; $tg * 100 / $TO" | bc 2>/dev/null || echo "-")
printf "%-8s %10s %10s %10s %9s%%\n" "--------" "------" "-------" "---" "----"
printf "%-8s %10s %10s %10s %9s%%\n" "TOTAL" "$TC" "$TO" "$tg" "$tg_pct"

echo ""
echo "Done."
