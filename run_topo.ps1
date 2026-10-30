# Topology-constrained partitioning - batch run vs baseline (Windows PowerShell)
# Usage: powershell -ExecutionPolicy Bypass -File run_topo.ps1

# Auto-detect executable (supports MSVC, MinGW, and custom build dirs)
$exe = $null
foreach ($candidate in @(".\build\Release\TopoPartitioner.exe", ".\build\TopoPartitioner.exe", ".\build-mingw\TopoPartitioner.exe")) {
    if (Test-Path $candidate) { $exe = $candidate; break }
}
if (-not $exe) {
    Write-Host "Please compile TopoPartitioner.exe first" -ForegroundColor Red
    Write-Host "  cmake -B build -G 'MinGW Makefiles'; cmake --build build -j" -ForegroundColor Yellow
    exit
}
$bench_dir = ".\benchmark"

# ========== MFS1 + Titan23 (22 cases) ==========
# Baseline from TopoPart paper, MFS1 topology
$env:TOPO_BUDGET_MS = "3000"  # MFS1 only 8 FPGAs, 3s is enough
$titan_opt = @{
    sparcT1_core = 54841; neuron = 8815; stereo_vision = 32068; des90 = 87211
    SLAM_spheric = 54402; cholesky_mc = 37658; segmentation = 34092
    bitonic_mesh = 184894; dart = 168197; openCV = 120428
    stap_qrd = 134256; minres = 96178; cholesky_bdti = 75479
    denoise = 93464; sparcT2_core = 98128; gsm_switch = 267424
    mes_noc = 297140; LU_Network = 52508; LU230 = 134877
    sparcT1_chip2 = 207814; directrf = 113493; bitcoin_miner = 413566
}

Write-Host "============================================================"
Write-Host " MFS1 Topology + Titan23 Benchmarks (22 cases)"
Write-Host "============================================================"
Write-Host ("{0,-18} {1,10} {2,8} {3,8} {4,10} {5,8} {6,10}" -f "Bench", "MyCut", "Viol", "Feas", "Optimal", "Gap%", "Time(s)")
Write-Host ("{0,-18} {1,10} {2,8} {3,8} {4,10} {5,8} {6,10}" -f "------------------", "------", "----", "----", "-------", "----", "------")

$t_total_cut = 0; $t_total_opt = 0; $t_count = 0

foreach ($ds in ($titan_opt.Keys | Sort-Object)) {
    $netlist = "$bench_dir\$ds"
    if (-not (Test-Path $netlist)) {
        Write-Host ("{0,-18} file not found" -f $ds)
        continue
    }
    Write-Host ("Running {0,-18} ..." -f $ds) -NoNewline
    $t0 = Get-Date
    $output = & $exe topo "$bench_dir\MFS1" $netlist 0.05 8 2026 2>&1 | Out-String
    $elapsed = ((Get-Date) - $t0).TotalSeconds

    $cut = 0; $viol = -1; $feas = "no"
    foreach ($line in $output -split "`n") {
        if ($line -match "Total Cut Cost:\s+(\d+)") { $cut = [int]$Matches[1] }
        if ($line -match "Number of Topology Violations:\s+(\d+)") { $viol = [int]$Matches[1] }
        if ($line -match "Feasible:\s+(yes|no)") { $feas = $Matches[1] }
    }
    $opt = $titan_opt[$ds]
    $gap = if ($feas -eq "yes" -and $opt -gt 0) { "{0:N1}" -f ((($cut - $opt) / $opt) * 100) } else { "-" }
    if ($feas -eq "yes") { $t_total_cut += $cut; $t_total_opt += $opt; $t_count++ }

    Write-Host ("`r{0,-18} {1,10} {2,8} {3,8} {4,10} {5,8} {6,10:N1}" -f $ds, $cut, $viol, $feas, $opt, $gap, $elapsed)
}

if ($t_count -gt 0) {
    $t_gap = "{0:N1}" -f ((($t_total_cut - $t_total_opt) / $t_total_opt) * 100)
    Write-Host ("{0,-18} {1,10} {2,8} {3,8} {4,10} {5,8}" -f "TITAN23-TOTAL", $t_total_cut, "", "", $t_total_opt, $t_gap)
}

Write-Host ""
Write-Host "Done."
