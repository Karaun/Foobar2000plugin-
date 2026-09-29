$ErrorActionPreference = "SilentlyContinue"

Write-Host "============================================================"
Write-Host "Windows memory / pagefile diagnostic"
Write-Host "============================================================"

$os = Get-CimInstance Win32_OperatingSystem
$cs = Get-CimInstance Win32_ComputerSystem

$totalRamGB = [math]::Round($cs.TotalPhysicalMemory / 1GB, 2)
$freeRamGB  = [math]::Round($os.FreePhysicalMemory * 1KB / 1GB, 2)
$totalVirtGB = [math]::Round($os.TotalVirtualMemorySize * 1KB / 1GB, 2)
$freeVirtGB  = [math]::Round($os.FreeVirtualMemory * 1KB / 1GB, 2)

Write-Host ("Physical RAM total : {0} GB" -f $totalRamGB)
Write-Host ("Physical RAM free  : {0} GB" -f $freeRamGB)
Write-Host ("Virtual memory total: {0} GB" -f $totalVirtGB)
Write-Host ("Virtual memory free : {0} GB" -f $freeVirtGB)
Write-Host ("Auto managed pagefile: {0}" -f $cs.AutomaticManagedPagefile)

Write-Host ""
Write-Host "Configured page files:"
$settings = Get-CimInstance Win32_PageFileSetting
if ($settings) {
    $settings | ForEach-Object {
        Write-Host ("  {0}  Initial={1}MB  Maximum={2}MB" -f $_.Name, $_.InitialSize, $_.MaximumSize)
    }
} else {
    Write-Host "  No explicit Win32_PageFileSetting entries."
}

Write-Host ""
Write-Host "Current page file usage:"
$usage = Get-CimInstance Win32_PageFileUsage
if ($usage) {
    $usage | ForEach-Object {
        Write-Host ("  {0}  Allocated={1}MB  CurrentUsage={2}MB  PeakUsage={3}MB" -f $_.Name, $_.AllocatedBaseSize, $_.CurrentUsage, $_.PeakUsage)
    }
} else {
    Write-Host "  No active page file was reported."
}

Write-Host ""
if (-not $usage -or $os.TotalVirtualMemorySize -le ($os.TotalVisibleMemorySize + 1024*1024)) {
    Write-Host "[WARN] Pagefile appears disabled or too small." -ForegroundColor Yellow
    Write-Host "Recommended: Windows -> Advanced system settings -> Performance -> Advanced"
    Write-Host "             -> Virtual memory -> Automatically manage paging file size."
} else {
    Write-Host "[INFO] Pagefile exists. If C1060 persists, increase available commit memory." -ForegroundColor Cyan
}

Write-Host ""
Read-Host "Press Enter to close"
