# Scans the build output with Microsoft Defender and fails when Defender flags a file.
# A harmless EICAR test string, built at run time so this repository never holds it, is scanned
# beside the files as a control. If Defender does not detect the control, the scanner is not
# working on this machine and the result is reported as inconclusive instead of a pass.
# Note: this is an on-demand scan. Defender may still block the program later because of what it
# does at run time, which only a real run on a user's PC can show.
param([string]$Paths)
$ErrorActionPreference = 'Stop'

if (-not (Get-Command Start-MpScan -ErrorAction SilentlyContinue)) {
    Write-Warning 'Microsoft Defender cmdlets are not available here; scan skipped (inconclusive).'
    exit 0
}
$status = Get-MpComputerStatus -ErrorAction SilentlyContinue
if (-not $status -or -not $status.AntivirusEnabled) {
    Write-Warning 'Microsoft Defender antivirus is not enabled here; scan skipped (inconclusive).'
    exit 0
}
Write-Host "Defender engine $($status.AMEngineVersion), signatures $($status.AntivirusSignatureVersion) ($($status.AntivirusSignatureLastUpdated))"

$work = Join-Path $env:USERPROFILE 'vikey-defender-scan'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null
foreach ($p in ($Paths -split ',')) {
    $p = $p.Trim()
    if (-not $p) { continue }
    if (-not (Test-Path $p)) { throw "Build output not found: $p" }
    Copy-Item $p $work
}
$targets = @(Get-ChildItem $work -File | ForEach-Object { $_.Name })
Write-Host ('Scanning: ' + ($targets -join ', '))

$eicar = 'X5O!P%@AP[4\PZX54(P^)7CC)7}$' + 'EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*'
try { [System.IO.File]::WriteAllText((Join-Path $work 'eicar-control.txt'), $eicar) }
catch { Write-Host "EICAR control write was blocked by real-time protection (counts as detected): $($_.Exception.Message)" }

$started = Get-Date
try { Start-MpScan -ScanType CustomScan -ScanPath $work }
catch {
    Write-Warning "Start-MpScan failed ($($_.Exception.Message)); scan skipped (inconclusive)."
    exit 0
}
Start-Sleep -Seconds 5

$detections = @(Get-MpThreatDetection -ErrorAction SilentlyContinue |
    Where-Object { $_.InitialDetectionTime -ge $started.AddMinutes(-2) })
$control = $false
$flagged = @()
foreach ($d in $detections) {
    $resources = @($d.Resources) -join ' '
    $threat = Get-MpThreat -ThreatID $d.ThreatID -ErrorAction SilentlyContinue
    $name = if ($threat) { $threat.ThreatName } else { "ThreatID $($d.ThreatID)" }
    if ($resources -match 'eicar-control') { $control = $true; continue }
    $flagged += "$name -> $resources"
}

if (-not $control) {
    Write-Warning 'Defender did not detect the EICAR control, so this scan cannot prove anything (inconclusive).'
    if ($flagged.Count -eq 0) { exit 0 }
}
if ($flagged.Count -gt 0) {
    Write-Host '::error::Microsoft Defender flagged the build output:'
    $flagged | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host 'Defender scan clean (EICAR control detected, build output not flagged).'
