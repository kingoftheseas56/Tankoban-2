# scripts/stop-tankoban.ps1
# Tankoban 2 smoke-cleanup helper. Kills Tankoban.exe + ffmpeg_sidecar.exe
# if running, reports what was killed. Closes GOVERNANCE Rule 17 (added
# 2026-04-20 at Agent 4 suggestion after Hemanth caught 38-min stale
# Tankoban.exe holding 1435 handles post-smoke).
#
# Invocation:
#   powershell -NoProfile -File scripts/stop-tankoban.ps1
#
# Agent-invokable from bash:
#   powershell -NoProfile -File scripts/stop-tankoban.ps1
#
# Exit codes:
#   0 - clean (nothing was running OR processes killed successfully)
#   1 - one or more Stop-Process calls failed (process held by system, etc.)
#
# Rule 17 mandate: run this (or equivalent) at the END of any agent-driven
# MCP smoke wake before declaring the wake done. Orthogonal to Rule 1
# (kill-before-rebuild); Rule 17 is kill-after-smoke regardless of rebuild.
#
# NOTE: ASCII-only per GOVERNANCE Rule 16 (matches repo-health.ps1 style).

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

$targets = @('Tankoban', 'ffmpeg_sidecar')
$killed = 0
$notRunning = 0
$failed = 0

Write-Host "Tankoban 2 smoke-cleanup  $(Get-Date -Format 'HH:mm:ss')" -ForegroundColor Cyan

foreach ($name in $targets) {
    $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
    if (-not $procs) {
        Write-Host ("  [SKIP] {0,-18} not running" -f "${name}.exe") -ForegroundColor DarkGray
        $notRunning++
        continue
    }
    foreach ($p in $procs) {
        $pid_ = $p.Id
        $uptime = (Get-Date) - $p.StartTime
        try {
            Stop-Process -Id $pid_ -Force -ErrorAction Stop
            Write-Host ("  [KILL] {0,-18} PID {1,-6} uptime {2:hh\:mm\:ss}" -f "${name}.exe", $pid_, $uptime) -ForegroundColor Yellow
            $killed++
        } catch {
            Write-Host ("  [FAIL] {0,-18} PID {1,-6} - {2}" -f "${name}.exe", $pid_, $_.Exception.Message) -ForegroundColor Red
            $failed++
        }
    }
}

Write-Host ("-" * 50)
if ($failed -gt 0) {
    Write-Host ("${killed} killed, ${failed} failed. Investigate stuck processes manually.") -ForegroundColor Red
    exit 1
} elseif ($killed -gt 0) {
    Write-Host ("${killed} process(es) killed. Wake can end.") -ForegroundColor Green
    exit 0
} else {
    Write-Host 'Nothing to clean. Wake can end.' -ForegroundColor Green
    exit 0
}
