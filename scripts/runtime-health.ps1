# scripts/runtime-health.ps1
# Tankoban 2 runtime-health digest. One-screen summary of the last Tankoban
# session: process state + stream telemetry event counts + sidecar PERF + any
# ERROR/FATAL lines across the three runtime logs.
#
# Closes Agent 7 MCP smoke-harness audit recommendation #1 (2026-04-19).
#
# Invocation:
#   powershell -NoProfile -File scripts/runtime-health.ps1
#   powershell -NoProfile -File scripts/runtime-health.ps1 -SinceMinutes 5
#
# Designed to run in PowerShell 5.1 (default Windows) or 7+. No external deps.
# Agent-invokable from bash via:
#   powershell -NoProfile -File scripts/runtime-health.ps1
#
# Exit codes:
#   0 - no anomalies (no stall_detected without recovery, no recent ERROR lines)
#   1 - anomalies present (stalls without recovery OR recent errors)
#
# Checks (in order):
#   1. Tankoban.exe + ffmpeg_sidecar.exe process state
#   2. Stream telemetry event counts (last session window) with key digests
#   3. Sidecar PERF tick count + drops rate + last frame timing
#   4. Error/fatal line scan across runtime logs
#
# NOTE: ASCII-only per GOVERNANCE Rule 16 (matches repo-health.ps1 style).

[CmdletBinding()]
param(
    [int]$SinceMinutes = 15
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
Set-Location -LiteralPath $RepoRoot

$anomalies = 0

function Write-Check {
    param([string]$Status, [string]$Section, [string]$Detail)
    $color = switch ($Status) {
        'OK'   { 'Green' }
        'INFO' { 'DarkGray' }
        'WARN' { 'Yellow' }
        'FAIL' { 'Red' }
        default { 'White' }
    }
    Write-Host ("[{0,-4}] {1,-24} {2}" -f $Status, $Section, $Detail) -ForegroundColor $color
    if ($Status -eq 'WARN' -or $Status -eq 'FAIL') { $script:anomalies++ }
}

$sinceCutoff = (Get-Date).AddMinutes(-$SinceMinutes)

Write-Host "Tankoban 2 runtime-health digest  $(Get-Date -Format 'yyyy-MM-dd HH:mm')" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot   Window: last ${SinceMinutes} min" -ForegroundColor DarkGray
Write-Host ("-" * 78)

# 1. Process state
$proc = Get-Process -Name 'Tankoban' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
    $uptime = (Get-Date) - $proc.StartTime
    Write-Check 'OK' 'Tankoban.exe' ("PID {0} uptime {1:hh\:mm\:ss} handles {2}" -f $proc.Id, $uptime, $proc.HandleCount)
} else {
    Write-Check 'INFO' 'Tankoban.exe' 'not running (smoke may have closed)'
}
$sidecar = Get-Process -Name 'ffmpeg_sidecar' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($sidecar) {
    Write-Check 'OK' 'ffmpeg_sidecar.exe' ("PID {0} handles {1}" -f $sidecar.Id, $sidecar.HandleCount)
} else {
    Write-Check 'INFO' 'ffmpeg_sidecar.exe' 'not running'
}

# 2. Stream telemetry
$tel = 'out/stream_telemetry.log'
if (Test-Path -LiteralPath $tel) {
    $telInfo = Get-Item -LiteralPath $tel
    $telMb = [math]::Round($telInfo.Length / 1MB, 2)
    # Read last 3000 lines for speed (telemetry grows fast during streaming).
    $lines = Get-Content -LiteralPath $tel -Tail 3000
    $recent = $lines | Where-Object {
        if ($_ -match '^\[([0-9T:\-\.Z]+)\]') {
            try { return ([datetime]::Parse($matches[1])) -gt $sinceCutoff } catch { return $false }
        }
        return $false
    }
    $recentCount = ($recent | Measure-Object).Count
    Write-Check 'INFO' 'telemetry log' ("${telMb}MB total, ${recentCount} events in last ${SinceMinutes}min")

    if ($recentCount -gt 0) {
        $stallDetected = ($recent | Where-Object { $_ -match 'event=stall_detected' }).Count
        $stallRecovered = ($recent | Where-Object { $_ -match 'event=stall_recovered' }).Count
        $firstPiece = ($recent | Where-Object { $_ -match 'event=first_piece' }).Count
        $gatePass = ($recent | Where-Object { $_ -match 'event=gate_pass_sequential_off' }).Count
        $pieceDiag = ($recent | Where-Object { $_ -match 'event=piece_diag' }).Count
        $snapshot = ($recent | Where-Object { $_ -match 'event=snapshot' }).Count
        Write-Check 'INFO' 'event counts' ("first_piece=${firstPiece} gate_pass=${gatePass} stall_detected=${stallDetected} stall_recovered=${stallRecovered} piece_diag=${pieceDiag} snapshot=${snapshot}")

        if ($stallDetected -gt 0) {
            $unrecovered = $stallDetected - $stallRecovered
            if ($unrecovered -gt 0) {
                Write-Check 'FAIL' 'stall state' "${unrecovered} stall(s) without recovery - pipeline may be stuck"
            } else {
                Write-Check 'WARN' 'stall state' "${stallDetected} stall(s) all recovered - transient"
            }
            # Show last stall_detected event for context.
            $lastStall = $recent | Where-Object { $_ -match 'event=stall_detected' } | Select-Object -Last 1
            if ($lastStall) { Write-Host "        last: $lastStall" -ForegroundColor DarkYellow }
        }

        $firstPieceLine = $recent | Where-Object { $_ -match 'event=first_piece' } | Select-Object -Last 1
        if ($firstPieceLine -and $firstPieceLine -match 'arrivalMs=(\d+)') {
            $arrivalMs = [int]$matches[1]
            $arrivalS = [math]::Round($arrivalMs / 1000, 1)
            Write-Check 'INFO' 'cold-open' "last firstPieceMs=${arrivalMs} (${arrivalS}s)"
        }

        $lastDiag = $recent | Where-Object { $_ -match 'event=piece_diag' } | Select-Object -Last 1
        if ($lastDiag) { Write-Host "        last piece_diag: $lastDiag" -ForegroundColor DarkGray }
    } else {
        Write-Check 'INFO' 'event counts' 'no events in window'
    }
} else {
    Write-Check 'INFO' 'telemetry log' 'out/stream_telemetry.log not found (no stream session yet?)'
}

# 3. Sidecar PERF
$sidecarLog = 'out/sidecar_debug_live.log'
if (Test-Path -LiteralPath $sidecarLog) {
    $sidecarLines = Get-Content -LiteralPath $sidecarLog -Tail 1500
    $perfLines = $sidecarLines | Where-Object { $_ -match '\[PERF\] frames=' }
    $perfCount = ($perfLines | Measure-Object).Count
    if ($perfCount -gt 0) {
        $lastPerf = $perfLines | Select-Object -Last 1
        $dropsSum = 0
        foreach ($pl in $perfLines) {
            if ($pl -match 'drops=(\d+)/s') { $dropsSum += [int]$matches[1] }
        }
        Write-Check 'INFO' 'sidecar PERF' "${perfCount} ticks in tail, drops-per-sec sum=${dropsSum}"
        Write-Host "        last: $lastPerf" -ForegroundColor DarkGray
    } else {
        Write-Check 'INFO' 'sidecar PERF' 'no PERF ticks in recent tail'
    }
} else {
    Write-Check 'INFO' 'sidecar log' 'out/sidecar_debug_live.log not found'
}

# 4. Error scan across runtime logs
# Patterns flag real errors. Benign filter strips ffmpeg HTTP/tcp reconnect
# chatter (expected behavior per the Phase-1 reconnect_delay_max=5 setting;
# not a real fault).
$errorPatterns = 'ERROR|FATAL|CRITICAL|SEGFAULT|\bassert(ion)? failed\b|Stream ends prematurely'
$benignPatterns = '(Will reconnect at .* in \d+ second)|(Connection to tcp://.*failed: Error number -138)|(\[http @ [0-9a-f]+\] HTTP error)'
$logRoots = @('out/stream_telemetry.log', 'out/sidecar_debug_live.log', 'alert_trace.log', 'out/alert_trace.log')
$errorHits = @()
foreach ($lp in $logRoots) {
    if (-not (Test-Path -LiteralPath $lp)) { continue }
    $tail = Get-Content -LiteralPath $lp -Tail 2000
    $tail | Where-Object { $_ -match $errorPatterns -and $_ -notmatch $benignPatterns } | ForEach-Object {
        $errorHits += [PSCustomObject]@{ File = $lp; Line = $_ }
    }
}
if ($errorHits.Count -eq 0) {
    Write-Check 'OK' 'error scan' 'no ERROR/FATAL/CRITICAL lines in recent tails'
} else {
    Write-Check 'WARN' 'error scan' ("{0} error/fatal line(s) across runtime logs" -f $errorHits.Count)
    foreach ($h in $errorHits | Select-Object -Last 5) {
        $shortLine = if ($h.Line.Length -gt 110) { $h.Line.Substring(0, 107) + '...' } else { $h.Line }
        Write-Host ("        [{0}] {1}" -f (Split-Path -Leaf $h.File), $shortLine) -ForegroundColor DarkYellow
    }
}

Write-Host ("-" * 78)
if ($anomalies -gt 0) {
    Write-Host ("{0} anomaly signal(s). Investigate logs before next smoke." -f $anomalies) -ForegroundColor Yellow
    exit 1
} else {
    Write-Host 'Runtime clean.' -ForegroundColor Green
    exit 0
}
