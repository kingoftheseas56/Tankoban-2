# test-clean-torrent-state.ps1
# Non-destructive isolation test for "Failed to Add Magnet" bug.
# Renames torrent_cache/ aside (keeps everything safe), launches Tankoban
# fresh, prints next steps.

$ErrorActionPreference = "Stop"
$dataRoot = Join-Path $env:LOCALAPPDATA "Tankoban\data"
$cache = Join-Path $dataRoot "torrent_cache"
$ts = Get-Date -Format "yyyyMMdd_HHmmss"
$backup = Join-Path $dataRoot "torrent_cache_backup_$ts"

Write-Host "=== Tankorent clean-state isolation test ===" -ForegroundColor Cyan

# 1. Kill any running Tankoban
Write-Host "`n[1/4] Killing any running Tankoban..."
taskkill /F /IM Tankoban.exe 2>$null
Start-Sleep -Seconds 1

# 2. Rename torrent_cache aside (preserves data, easily reversible)
if (Test-Path $cache) {
    Write-Host "[2/4] Moving $cache aside to $backup"
    Move-Item -Path $cache -Destination $backup
} else {
    Write-Host "[2/4] No torrent_cache found (already clean)" -ForegroundColor Yellow
}

# 3. Launch Tankoban fresh
Write-Host "[3/4] Launching Tankoban with --dev-control..."
$tankoban = Join-Path (Get-Location) "out\Tankoban.exe"
if (-not (Test-Path $tankoban)) {
    Write-Host "ERROR: $tankoban not found. Run build_and_run.bat first." -ForegroundColor Red
    exit 1
}
$env:PATH = "C:\Qt\6.10.2\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;$env:PATH"
$env:TANKOBAN_STREAM_TELEMETRY = "1"
$env:TANKOBAN_ALERT_TRACE = "1"
$env:TANKOBAN_STREMIO_TUNE = "1"
Start-Process -FilePath $tankoban -ArgumentList "--dev-control"
Start-Sleep -Seconds 5

# 4. Confirm bridge alive + print next steps
Write-Host "[4/4] Pinging dev-bridge..."
& (Join-Path (Get-Location) "out\tankoctl.exe") ping
Write-Host "`n=== READY TO TEST ===" -ForegroundColor Green
Write-Host "Now do this:"
Write-Host "  1. In Tankoban: Sources -> Tankorent -> search anything"
Write-Host "  2. Click the down-arrow (download) on row 0"
Write-Host "  3. Tell me what happens:"
Write-Host "       - Dialog opens? -> state corruption was the cause"
Write-Host "       - Same 'Failed to Add Magnet'? -> not state, runtime/env"
Write-Host ""
Write-Host "Then capture logs by running:"
Write-Host "  out\tankoctl.exe logs 100 > out\click_logs.txt"
Write-Host ""
Write-Host "Your old torrent_cache is preserved at:"
Write-Host "  $backup" -ForegroundColor Cyan
Write-Host ""
Write-Host "To restore your saved torrents later (if dialog opened cleanly):"
Write-Host "  Stop-Process -Name Tankoban -Force"
Write-Host "  Move-Item `"$backup\resume`" `"$cache\resume`" -Force"
Write-Host "  Move-Item `"$backup\session.state`" `"$cache\session.state`" -Force"
