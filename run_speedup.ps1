# Parallel speedup and runtime comparison vs baseline (Windows PowerShell)
# Shows: cut cost, runtime, speedup ratio, and comparison with TopoPart baseline runtime
# Usage: powershell -ExecutionPolicy Bypass -File run_speedup.ps1

# Auto-detect executable (supports MSVC, MinGW, and custom build dirs)
$exe = $null
foreach ($candidate in @(".\build\Release\TopoPartitioner.exe", ".\build\TopoPartitioner.exe", ".\build-mingw\TopoPartitioner.exe")) {
    if (Test-Path $candidate) { $exe = $candidate; break }
}
if (-not $exe) {
    Write-Host "Please compile TopoPartitioner.exe first" -ForegroundColor Red
    exit
}
$bench_dir = ".\benchmark"

# Baseline runtime (seconds) from TopoPart paper, MFS1+Titan23
$baseline_time = @{
    sparcT1_core = 86.342; neuron = 23.692; stereo_vision = 55.616; des90 = 126.112
    SLAM_spheric = 90.432; cholesky_mc = 51.957; segmentation = 48.945
    bitonic_mesh = 149.847; dart = 251.163; openCV = 117.595
    stap_qrd = 296.304; minres = 145.168; cholesky_bdti = 110.059
    denoise = 115.618; sparcT2_core = 185.788; gsm_switch = 471.599
    mes_noc = 372.403; LU_Network = 407.184; LU230 = 261.887
    sparcT1_chip2 = 517.124; directrf = 248.817; bitcoin_miner = 1231.155
}

$titan_opt = @{
    sparcT1_core = 54841; neuron = 8815; stereo_vision = 32068; des90 = 87211
    SLAM_spheric = 54402; cholesky_mc = 37658; segmentation = 34092
    bitonic_mesh = 184894; dart = 168197; openCV = 120428
    stap_qrd = 134256; minres = 96178; cholesky_bdti = 75479
    denoise = 93464; sparcT2_core = 98128; gsm_switch = 267424
    mes_noc = 297140; LU_Network = 52508; LU230 = 134877
    sparcT1_chip2 = 207814; directrf = 113493; bitcoin_miner = 413566
}

$env:TOPO_BUDGET_MS = "3000"

Write-Host "============================================================"
Write-Host " Runtime & Speedup Comparison (MFS1 + Titan23, 3s budget, 8 threads)"
Write-Host "============================================================"
Write-Host ("{0,-16} {1,9} {2,9} {3,9} {4,9} {5,9} {6,9}" -f "Bench", "MyCut", "MyTime", "BL_Time", "TimeX", "Speedup", "CutGap%")
Write-Host ("{0,-16} {1,9} {2,9} {3,9} {4,9} {5,9} {6,9}" -f "----------------", "-------", "-------", "-------", "-----", "-------", "------")

$tc = 0; $to = 0; $tmy = 0; $tbl = 0
foreach ($ds in ($titan_opt.Keys | Sort-Object)) {
    $nl = "$bench_dir\$ds"
    if (-not (Test-Path $nl)) { continue }
    $o = & $exe topo "$bench_dir\MFS1" $nl 0.05 8 2026 2>&1 | Out-String
    $cut = 0; $viol = -1; $f = "no"; $mytime = 0; $sp = 0
    foreach ($l in $o -split "`n") {
        if ($l -match "Total Cut Cost:\s+(\d+)") { $cut = [int]$Matches[1] }
        if ($l -match "Topology Violations:\s+(\d+)") { $viol = [int]$Matches[1] }
        if ($l -match "Feasible:\s+(yes|no)") { $f = $Matches[1] }
        if ($l -match "Multi-thread Time Seconds:\s+([\d.]+)") { $mytime = [double]$Matches[1] }
        if ($l -match "Parallel Speedup Ratio:\s+([\d.]+)") { $sp = [double]$Matches[1] }
    }
    $opt = $titan_opt[$ds]
    $blt = $baseline_time[$ds]
    $timex = if ($mytime -gt 0) { "{0:N1}" -f ($blt / $mytime) } else { "-" }
    $gap = if ($f -eq "yes" -and $opt -gt 0) { "{0:N1}" -f ((($cut - $opt) / $opt) * 100) } else { "-" }
    if ($f -eq "yes") { $tc += $cut; $to += $opt; $tmy += $mytime; $tbl += $blt }
    Write-Host ("{0,-16} {1,9} {2,9:N1} {3,9:N1} {4,9} {5,9:N2} {6,9}" -f $ds, $cut, $mytime, $blt, $timex, $sp, $gap)
}
$tg = "{0:N1}" -f ((($tc - $to) / $to) * 100)
$tx = "{0:N1}" -f ($tbl / $tmy)
Write-Host ("{0,-16} {1,9} {2,9:N1} {3,9:N1} {4,9} {5,9} {6,9}" -f "----------------", "-------", "-------", "-------", "-----", "-------", "------")
Write-Host ("{0,-16} {1,9} {2,9:N1} {3,9:N1} {4,9}x {5,9} {6,9}%" -f "TOTAL", $tc, $tmy, $tbl, $tx, "", $tg)

Write-Host ""
Write-Host "===== Thread Scaling Test (neuron, varying thread count) ====="
Write-Host ("{0,-10} {1,12} {2,12} {3,12}" -f "Threads", "Time(s)", "Speedup", "Efficiency")
Write-Host ("{0,-10} {1,12} {2,12} {3,12}" -f "-------", "-------", "-------", "-----------")
foreach ($nt in @(1, 2, 4, 8)) {
    $o = & $exe topo "$bench_dir\MFS1" "$bench_dir\neuron" 0.05 $nt 2026 2>&1 | Out-String
    $st = 0; $mt = 0; $sp = 0
    foreach ($l in $o -split "`n") {
        if ($l -match "Single-thread Time Seconds:\s+([\d.]+)") { $st = [double]$Matches[1] }
        if ($l -match "Multi-thread Time Seconds:\s+([\d.]+)") { $mt = [double]$Matches[1] }
        if ($l -match "Parallel Speedup Ratio:\s+([\d.]+)") { $sp = [double]$Matches[1] }
    }
    $eff = "{0:N1}" -f ($sp / $nt * 100)
    Write-Host ("{0,-10} {1,12:N3} {2,12:N2} {3,12}%" -f $nt, $mt, $sp, $eff)
}

Write-Host ""
Write-Host "Done."
