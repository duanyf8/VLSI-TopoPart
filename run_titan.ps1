# Titan23 + MFS1 批量测试 (3秒预算)
# Auto-detect executable (supports MSVC, MinGW, and custom build dirs)
$exe = $null
foreach ($candidate in @(".\build\Release\TopoPartitioner.exe", ".\build\TopoPartitioner.exe", ".\build-mingw\TopoPartitioner.exe")) {
    if (Test-Path $candidate) { $exe = $candidate; break }
}
if (-not $exe) {
    Write-Host "Please compile TopoPartitioner.exe first" -ForegroundColor Red
    exit
}
$env:TOPO_BUDGET_MS = "3000"

$titan_opt = @{
    sparcT1_core = 54841; neuron = 8815; stereo_vision = 32068; des90 = 87211
    SLAM_spheric = 54402; cholesky_mc = 37658; segmentation = 34092
    bitonic_mesh = 184894; dart = 168197; openCV = 120428
    stap_qrd = 134256; minres = 96178; cholesky_bdti = 75479
    denoise = 93464; sparcT2_core = 98128; gsm_switch = 267424
    mes_noc = 297140; LU_Network = 52508; LU230 = 134877
    sparcT1_chip2 = 207814; directrf = 113493; bitcoin_miner = 413566
}

Write-Host "===== MFS1 + Titan23 (22 cases, 3s budget) ====="
Write-Host ("{0,-18} {1,10} {2,6} {3,6} {4,10} {5,8}" -f "Bench", "MyCut", "Viol", "Feas", "Optimal", "Gap%")
Write-Host ("{0,-18} {1,10} {2,6} {3,6} {4,10} {5,8}" -f "------------------", "------", "----", "----", "-------", "----")

$tc = 0; $to = 0
foreach ($ds in ($titan_opt.Keys | Sort-Object)) {
    $nl = ".\benchmark\$ds"
    if (-not (Test-Path $nl)) { continue }
    $o = & $exe topo .\benchmark\MFS1 $nl 0.05 8 2026 2>&1 | Out-String
    $cut = 0; $viol = -1; $f = "no"
    foreach ($l in $o -split "`n") {
        if ($l -match "Total Cut Cost:\s+(\d+)") { $cut = [int]$Matches[1] }
        if ($l -match "Topology Violations:\s+(\d+)") { $viol = [int]$Matches[1] }
        if ($l -match "Feasible:\s+(yes|no)") { $f = $Matches[1] }
    }
    $opt = $titan_opt[$ds]
    $gap = if ($f -eq "yes" -and $opt -gt 0) { "{0:N1}" -f ((($cut - $opt) / $opt) * 100) } else { "-" }
    if ($f -eq "yes") { $tc += $cut; $to += $opt }
    Write-Host ("{0,-18} {1,10} {2,6} {3,6} {4,10} {5,8}" -f $ds, $cut, $viol, $f, $opt, $gap)
}
$tg = "{0:N1}" -f ((($tc - $to) / $to) * 100)
Write-Host ("{0,-18} {1,10} {2,6} {3,6} {4,10} {5,8}" -f "TOTAL", $tc, "", "", $to, $tg)
