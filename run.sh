#!/usr/bin/env bash
# Topology-constrained partitioning - run speedup comparison (Linux/WSL)
# Usage: bash run.sh
# Or:    chmod +x run.sh && ./run.sh

set -uo pipefail
cd "$(dirname "$0")"

EXE="./build/TopoPartitioner"
BENCH_DIR="./benchmark"

# Build if not exists (clear stale cache from other OS if detected)
if [ ! -f "$EXE" ]; then
    echo "[BUILD] Compiling..."
    # Remove build cache if it was generated on a different OS (e.g. Windows MSVC)
    if [ -f "build/CMakeCache.txt" ] && ! grep -q "Unix Makefiles" build/CMakeCache.txt 2>/dev/null; then
        echo "[BUILD] Removing stale cache from another OS..."
        rm -rf build
    fi
    cmake -B build 2>&1
    cmake --build build -j$(nproc) 2>&1
    if [ ! -f "$EXE" ] && [ -f "./build/Release/TopoPartitioner" ]; then
        EXE="./build/Release/TopoPartitioner"
    fi
    if [ ! -f "$EXE" ]; then
        echo "[ERROR] Build failed"
        exit 1
    fi
    echo "[BUILD] Done."
fi

export TOPO_BUDGET_MS=3000

# Baseline runtime (seconds) from TopoPart paper, MFS1+Titan23
declare -A BL_TIME
BL_TIME[bitcoin_miner]=1231.155; BL_TIME[bitonic_mesh]=149.847
BL_TIME[cholesky_bdti]=110.059; BL_TIME[cholesky_mc]=51.957
BL_TIME[dart]=251.163; BL_TIME[denoise]=115.618
BL_TIME[des90]=126.112; BL_TIME[directrf]=248.817
BL_TIME[gsm_switch]=471.599; BL_TIME[LU_Network]=407.184
BL_TIME[LU230]=261.887; BL_TIME[mes_noc]=372.403
BL_TIME[minres]=145.168; BL_TIME[neuron]=23.692
BL_TIME[openCV]=117.595; BL_TIME[segmentation]=48.945
BL_TIME[SLAM_spheric]=90.432; BL_TIME[sparcT1_chip2]=517.124
BL_TIME[sparcT1_core]=86.342; BL_TIME[sparcT2_core]=185.788
BL_TIME[stap_qrd]=296.304; BL_TIME[stereo_vision]=55.616

declare -A OPT_CUT
OPT_CUT[bitcoin_miner]=413566; OPT_CUT[bitonic_mesh]=184894
OPT_CUT[cholesky_bdti]=75479; OPT_CUT[cholesky_mc]=37658
OPT_CUT[dart]=168197; OPT_CUT[denoise]=93464
OPT_CUT[des90]=87211; OPT_CUT[directrf]=113493
OPT_CUT[gsm_switch]=267424; OPT_CUT[LU_Network]=52508
OPT_CUT[lu230]=134877; OPT_CUT[mes_noc]=297140
OPT_CUT[minres]=96178; OPT_CUT[neuron]=8815
OPT_CUT[opencv]=120428; OPT_CUT[segmentation]=34092
OPT_CUT[slam_spheric]=54402; OPT_CUT[sparct1_chip2]=207814
OPT_CUT[sparct1_core]=54841; OPT_CUT[sparct2_core]=98128
OPT_CUT[stap_qrd]=134256; OPT_CUT[stereo_vision]=32068

echo "============================================================"
echo " Runtime & Speedup Comparison (MFS1 + Titan23, 3s budget, 8 threads)"
echo "============================================================"
printf "%-16s %9s %9s %9s %9s %9s %9s\n" "Bench" "MyCut" "MyTime" "BL_Time" "TimeX" "Speedup" "CutGap%"
printf "%-16s %9s %9s %9s %9s %9s %9s\n" "----------------" "-------" "-------" "-------" "-----" "-------" "------"

TC=0; TO=0; TMY=0; TBL=0
for ds in $(echo "${!OPT_CUT[@]}" | tr ' ' '\n' | sort); do
    nl="$BENCH_DIR/$ds"
    [ ! -f "$nl" ] && continue
    out=$("$EXE" topo "$BENCH_DIR/MFS1" "$nl" 0.05 8 2026 2>&1)
    cut=$(echo "$out" | grep "Total Cut Cost" | grep -oP '\d+' | tail -1)
    viol=$(echo "$out" | grep "Topology Violations" | grep -oP '\d+' | tail -1)
    feas=$(echo "$out" | grep "Feasible" | grep -oP 'yes|no')
    mytime=$(echo "$out" | grep "Multi-thread Time" | grep -oP '[\d.]+')
    sp=$(echo "$out" | grep "Speedup Ratio" | grep -oP '[\d.]+')
    [ -z "$cut" ] && cut=0
    [ -z "$mytime" ] && mytime=0
    [ -z "$sp" ] && sp=0
    opt=${OPT_CUT[$ds]}
    blt=${BL_TIME[$ds]}
    timex=$(echo "scale=1; $blt / $mytime" | bc 2>/dev/null || echo "-")
    if [ "$feas" = "yes" ]; then
        gap=$(echo "scale=1; ($cut - $opt) * 100 / $opt" | bc 2>/dev/null || echo "-")
        TC=$((TC + cut)); TO=$((TO + opt)); TMY=$(echo "$TMY + $mytime" | bc); TBL=$(echo "$TBL + $blt" | bc)
    else
        gap="-"
    fi
    printf "%-16s %9s %9.1f %9.1f %9s %9.2f %9s\n" "$ds" "$cut" "$mytime" "$blt" "$timex" "$sp" "$gap"
done
tg=$(echo "scale=1; ($TC - $TO) * 100 / $TO" | bc 2>/dev/null || echo "-")
tx=$(echo "scale=1; $TBL / $TMY" | bc 2>/dev/null || echo "-")
printf "%-16s %9s %9.1f %9.1f %9sx %9s %9s%%\n" "TOTAL" "$TC" "$TMY" "$TBL" "$tx" "" "$tg"

echo ""
echo "===== Thread Scaling Test (neuron, varying thread count) ====="
printf "%-10s %12s %12s %12s\n" "Threads" "Time(s)" "Speedup" "Efficiency"
printf "%-10s %12s %12s %12s\n" "-------" "-------" "-------" "-----------"
for nt in 1 2 4 8; do
    out=$("$EXE" topo "$BENCH_DIR/MFS1" "$BENCH_DIR/neuron" 0.05 $nt 2026 2>&1)
    mt=$(echo "$out" | grep "Multi-thread Time" | grep -oP '[\d.]+')
    sp=$(echo "$out" | grep "Speedup Ratio" | grep -oP '[\d.]+')
    [ -z "$mt" ] && mt=0
    [ -z "$sp" ] && sp=0
    eff=$(echo "scale=1; $sp * 100 / $nt" | bc 2>/dev/null || echo "-")
    printf "%-10s %12.3f %12.2f %12s%%\n" "$nt" "$mt" "$sp" "$eff"
done

echo ""
echo "Done."
