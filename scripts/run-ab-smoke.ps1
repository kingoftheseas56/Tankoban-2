# scripts/run-ab-smoke.ps1
# Orchestrates one A/B smoke pass. Two phases per smoke:
#   Phase Start : reset progress, truncate _player_debug.txt, set env, launch Tankoban, print click recipe
#   Phase Stop  : snapshot logs with a unique tag, kill Tankoban, run measure-smoke, append one CSV row
#
# Usage:
#   .\run-ab-smoke.ps1 -Phase Start -Treatment ON|OFF -Label "baseline-1"
#   [operator: MCP-click Stream tab, Continue Watching tile, EZTV source card; let playback run ~10 min]
#   .\run-ab-smoke.ps1 -Phase Stop  -Treatment ON|OFF -Label "baseline-1"
#
# PREREQUISITES:
#   - Tankoban is NOT running at Phase Start (caller handles Rule 17 cleanup beforehand).
#   - Claude Code has claimed Rule 19 MCP LANE LOCK for the full smoke.
#   - approve_automation called within the past 15 min.

param(
    [Parameter(Mandatory=$true)][ValidateSet("Start","Stop")][string]$Phase,
    [Parameter(Mandatory=$true)][ValidateSet("ON","OFF")][string]$Treatment,
    [Parameter(Mandatory=$true)][string]$Label
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# Tag shared between Start and Stop — we need Stop to re-compute the SAME tag.
# Use a small state file in out/ to persist the tag across phases.
$stateFile = Join-Path $repoRoot "out/ab_smoke_state.json"

if ($Phase -eq "Start") {
    # --- Phase Start ---

    # 1. Reset stream progress so every run measures fresh cold-open from position 0
    $progressFile = Join-Path $env:LOCALAPPDATA "Tankoban/data/stream_progress.json"
    if (Test-Path $progressFile) {
        Remove-Item $progressFile -Force
        Write-Host "[Start] Reset stream_progress.json"
    } else {
        Write-Host "[Start] stream_progress.json not present (already clean)"
    }

    # 2. Truncate _player_debug.txt so THIS smoke's log is self-contained
    $playerDebug = Join-Path $repoRoot "out/_player_debug.txt"
    if (Test-Path $playerDebug) {
        Clear-Content $playerDebug
        Write-Host "[Start] Truncated _player_debug.txt"
    }

    # 3. Truncate stream_telemetry.log likewise (per-smoke clean)
    $telemetry = Join-Path $repoRoot "out/stream_telemetry.log"
    if (Test-Path $telemetry) {
        Clear-Content $telemetry
        Write-Host "[Start] Truncated stream_telemetry.log"
    }

    # 4. Truncate sidecar_debug_live.log too for completeness
    $sidecarLog = Join-Path $repoRoot "out/sidecar_debug_live.log"
    if (Test-Path $sidecarLog) {
        Clear-Content $sidecarLog
        Write-Host "[Start] Truncated sidecar_debug_live.log"
    }

    # 5. Set env vars based on Treatment
    $env:PATH = "C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;" + $env:PATH
    $env:TANKOBAN_STREAM_TELEMETRY = "1"
    $env:TANKOBAN_ALERT_TRACE = "1"
    if ($Treatment -eq "ON") {
        $env:TANKOBAN_STREMIO_TUNE = "1"
    } else {
        Remove-Item Env:\TANKOBAN_STREMIO_TUNE -ErrorAction SilentlyContinue
    }

    # 6. Compute tag + persist state for Stop phase
    $ts = Get-Date -Format 'HHmmss'
    $tag = "AB_${Treatment}_${ts}_${Label}"
    $startEpoch = [int][double]::Parse((Get-Date -UFormat %s))
    @{ tag = $tag; label = $Label; treatment = $Treatment; startEpoch = $startEpoch } |
        ConvertTo-Json | Out-File $stateFile -Encoding utf8

    # 7. Launch Tankoban
    $tankobanExe = Join-Path $repoRoot "out/Tankoban.exe"
    $outDir = Join-Path $repoRoot "out"
    Start-Process -FilePath $tankobanExe -WorkingDirectory $outDir
    Start-Sleep 6
    $proc = Get-Process -Name Tankoban -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { throw "[Start] Tankoban failed to start." }

    Write-Host ""
    Write-Host "=== AB SMOKE STARTED: $tag ==="
    Write-Host "  Tankoban PID: $($proc.Id)"
    Write-Host "  Treatment: $Treatment"
    Write-Host "  Env: TANKOBAN_STREMIO_TUNE=$(if($env:TANKOBAN_STREMIO_TUNE){'1'}else{'<unset>'})"
    Write-Host ""
    Write-Host "NOW: operator MCP-clicks (actual screen coords):"
    Write-Host "  (1060, 76)   Stream tab"
    Write-Host "  (86, 280)    Continue Watching Invincible tile  [wait 2s for detail view]"
    Write-Host "  (1521, 759)  Torrentio EZTV source card (Card 2)"
    Write-Host ""
    Write-Host "Let playback run ~600s, then call: .\run-ab-smoke.ps1 -Phase Stop -Treatment $Treatment -Label $Label"

} elseif ($Phase -eq "Stop") {
    # --- Phase Stop ---

    # 1. Load state from Start
    if (-not (Test-Path $stateFile)) {
        throw "[Stop] No state file at $stateFile — did Phase Start run first?"
    }
    $state = Get-Content $stateFile | ConvertFrom-Json
    if ($state.label -ne $Label -or $state.treatment -ne $Treatment) {
        throw "[Stop] Label/Treatment mismatch with Start state (Start: $($state.label)/$($state.treatment), Stop: $Label/$Treatment)"
    }
    $tag = $state.tag
    $startEpoch = [int]$state.startEpoch
    $nowEpoch = [int][double]::Parse((Get-Date -UFormat %s))
    $elapsedSec = $nowEpoch - $startEpoch
    Write-Host "[Stop] Tag=$tag elapsed=${elapsedSec}s"

    # 2. Snapshot log files BEFORE killing Tankoban (so PERF tail captures)
    $playerLogSrc    = Join-Path $repoRoot "out/_player_debug.txt"
    $sidecarLogSrc   = Join-Path $repoRoot "out/sidecar_debug_live.log"
    $telemetryLogSrc = Join-Path $repoRoot "out/stream_telemetry.log"

    $playerLogDst    = Join-Path $repoRoot "out/_player_debug_${tag}.txt"
    $sidecarLogDst   = Join-Path $repoRoot "out/sidecar_debug_${tag}.log"
    $telemetryLogDst = Join-Path $repoRoot "out/stream_telemetry_${tag}.log"

    if (Test-Path $playerLogSrc)    { Copy-Item $playerLogSrc    $playerLogDst    -Force }
    if (Test-Path $sidecarLogSrc)   { Copy-Item $sidecarLogSrc   $sidecarLogDst   -Force }
    if (Test-Path $telemetryLogSrc) { Copy-Item $telemetryLogSrc $telemetryLogDst -Force }
    Write-Host "[Stop] Snapshotted logs to out/*_${tag}*"

    # 3. Cleanup
    & (Join-Path $scriptDir "stop-tankoban.ps1")

    # 4. Extract metrics
    $measureScript = Join-Path $scriptDir "measure-smoke.ps1"
    $metrics = & $measureScript `
        -PlayerLog $playerLogDst `
        -TelemetryLog $telemetryLogDst `
        -Treatment $Treatment `
        -Label $Label

    # 5. Append to results CSV (create header on first write)
    $csvPath = Join-Path $repoRoot "out/stremio_tune_ab_results.csv"
    if (-not (Test-Path $csvPath)) {
        "label,treatment,stalls,cold_open_ms,first_piece_ms,p50_wait_ms,p99_wait_ms,playback_s" |
            Out-File $csvPath -Encoding utf8
    }
    $metrics | Out-File $csvPath -Append -Encoding utf8

    # 6. Clean up state file
    Remove-Item $stateFile -Force -ErrorAction SilentlyContinue

    Write-Host ""
    Write-Host "=== AB SMOKE DONE: $tag ==="
    Write-Host "  Metrics: $metrics"
    Write-Host "  Appended to: $csvPath"
}
