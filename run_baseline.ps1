# 基线二路FM划分 - 批量运行脚本 (Windows PowerShell)
# 用法: powershell -ExecutionPolicy Bypass -File run_baseline.ps1

# Auto-detect executable (supports MSVC, MinGW, and custom build dirs)
$exe = $null
foreach ($candidate in @(".\build\Release\TopoPartitioner.exe", ".\build\TopoPartitioner.exe", ".\build-mingw\TopoPartitioner.exe")) {
    if (Test-Path $candidate) { $exe = $candidate; break }
}
if (-not $exe) {
    Write-Host "请先编译 TopoPartitioner.exe" -ForegroundColor Red
    Write-Host "在项目根目录下执行:" -ForegroundColor Yellow
    Write-Host "  cmake -B build -G 'MinGW Makefiles'" -ForegroundColor Yellow
    Write-Host "  cmake --build build -j" -ForegroundColor Yellow
    exit
}

Write-Host "============================================================"
Write-Host " 基线二路FM划分 - IBM Benchmark 批量测试"
Write-Host "============================================================"

# epsilon=2% 最优解
$optimal = @{
    ibm01 = 200
    ibm02 = 307
    ibm03 = 951
    ibm04 = 573
    ibm05 = 1706
    ibm06 = 962
    ibm07 = 878
    ibm08 = 1140
    ibm09 = 620
    ibm10 = 1253
    ibm11 = 1051
    ibm12 = 1919
    ibm13 = 831
    ibm14 = 1842
    ibm15 = 2730
    ibm16 = 1827
    ibm17 = 2270
    ibm18 = 1521
}

Write-Host ("{0,-8} {1,10} {2,10} {3,10} {4,10}" -f "Bench", "MyCut", "Optimal", "Gap", "Gap%")
Write-Host ("{0,-8} {1,10} {2,10} {3,10} {4,10}" -f "--------", "------", "-------", "---", "----")

$total_cut = 0
$total_opt = 0

for ($i = 1; $i -le 18; $i++) {
    $ds = "ibm{0:D2}" -f $i
    $hgr = ".\benchmark\$ds.hgr"
    
    if (-not (Test-Path $hgr)) {
        Write-Host ("{0,-8} file not found" -f $ds)
        continue
    }
    
    Write-Host "Running $ds ..." -NoNewline
    
    $output = & $exe baseline $hgr 8 8 2026 2>&1 | Out-String
    
    $cut = 0
    
    foreach ($line in $output -split "`n") {
        if ($line -match "\[BEST\] cutsize\s+:\s+(\d+)") { $cut = [int]$Matches[1] }
    }
    
    $opt = $optimal[$ds]
    $gap = $cut - $opt
    $gap_pct = "{0:N1}" -f (($gap / $opt) * 100)
    
    $total_cut += $cut
    $total_opt += $opt
    
    Write-Host ("`r{0,-8} {1,10} {2,10} {3,10} {4,10}" -f $ds, $cut, $opt, $gap, "$gap_pct%")
}

$total_gap = $total_cut - $total_opt
$total_gap_pct = "{0:N1}" -f (($total_gap / $total_opt) * 100)

Write-Host ("{0,-8} {1,10} {2,10} {3,10} {4,10}" -f "--------", "------", "-------", "---", "----")
Write-Host ("{0,-8} {1,10} {2,10} {3,10} {4,10}" -f "TOTAL", $total_cut, $total_opt, $total_gap, "$total_gap_pct%")

Write-Host ""
Write-Host "Done. Results saved to results\"
