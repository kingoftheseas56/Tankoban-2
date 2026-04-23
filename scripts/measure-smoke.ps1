# scripts/measure-smoke.ps1
# Extracts metrics from one smoke's log files.
# Usage: .\measure-smoke.ps1 -PlayerLog <path> -TelemetryLog <path> -Treatment <ON|OFF> -Label <label>
#
# Outputs one CSV row to stdout in header-free format:
#   label,treatment,stalls,cold_open_ms,first_piece_ms,p50_wait_ms,p99_wait_ms,playback_s

param(
    [Parameter(Mandatory=$true)][string]$PlayerLog,
    [Parameter(Mandatory=$true)][string]$TelemetryLog,
    [Parameter(Mandatory=$true)][ValidateSet("ON","OFF")][string]$Treatment,
    [Parameter(Mandatory=$true)][string]$Label
)

if (-not (Test-Path $PlayerLog))    { throw "PlayerLog not found: $PlayerLog" }
if (-not (Test-Path $TelemetryLog)) { throw "TelemetryLog not found: $TelemetryLog" }

# Helper: parse "HH:mm:ss.fff" timestamp prefix from a log line
function Parse-LineTime($line) {
    if (-not $line) { return $null }
    $head = ($line -split '\s+')[0]   # e.g. "10:44:39.873"
    $base = ($head -split '\.')[0]    # "10:44:39"
    try {
        return [datetime]::ParseExact($base, 'HH:mm:ss', $null)
    } catch {
        return $null
    }
}

# Count stall_detected events for today's date prefix in telemetry
$today = Get-Date -Format 'yyyy-MM-dd'
$stallCount = @(Select-String -Path $TelemetryLog -Pattern "^\[${today}T.*event=stall_detected").Count

# Cold-open = time from [Sidecar] SEND {"name":"open"... -> [Sidecar] RECV: first_frame in player log
# Use regex match, take LAST occurrence for SEND open (most recent session) and the first first_frame AFTER it
$lines = Get-Content $PlayerLog
$sendOpenLines = @($lines | Where-Object { $_ -match '\[Sidecar\] SEND: \{"name":"open"' })
$coldOpenMs = -1
if ($sendOpenLines.Count -gt 0) {
    $lastSendOpen = $sendOpenLines[-1]
    $t1 = Parse-LineTime $lastSendOpen
    # Find first first_frame line occurring AFTER the last SEND open
    $foundSend = $false
    $firstFrameAfter = $null
    foreach ($line in $lines) {
        if (-not $foundSend) {
            if ($line -eq $lastSendOpen) { $foundSend = $true }
            continue
        }
        if ($line -match '\[Sidecar\] RECV: first_frame') {
            $firstFrameAfter = $line
            break
        }
    }
    if ($t1 -and $firstFrameAfter) {
        $t2 = Parse-LineTime $firstFrameAfter
        if ($t2) {
            $coldOpenMs = [int]($t2 - $t1).TotalMilliseconds
        }
    }
}

# First-piece latency from telemetry first_piece event (today only)
$firstPieceMatch = Select-String -Path $TelemetryLog -Pattern "^\[${today}T.*event=first_piece.*deltaMs=(\d+)" | Select-Object -First 1
$firstPieceMs = if ($firstPieceMatch) { [int]$firstPieceMatch.Matches[0].Groups[1].Value } else { -1 }

# Piece wait distribution from stall_detected events (today only)
$waitMs = @()
Select-String -Path $TelemetryLog -Pattern "^\[${today}T.*event=stall_detected.*wait_ms=(\d+)" |
    ForEach-Object { $waitMs += [int]$_.Matches[0].Groups[1].Value }

$p50Wait = -1
$p99Wait = -1
if ($waitMs.Count -gt 0) {
    $sorted = $waitMs | Sort-Object
    $p50Idx = [math]::Max(0, [math]::Min($sorted.Count - 1, [int][math]::Floor($sorted.Count * 0.5)))
    $p99Idx = [math]::Max(0, [math]::Min($sorted.Count - 1, [int][math]::Floor($sorted.Count * 0.99)))
    $p50Wait = [int]$sorted[$p50Idx]
    $p99Wait = [int]$sorted[$p99Idx]
}

# Playback duration = time from first PERF tick to last PERF tick in the log
$perfLines = @($lines | Where-Object { $_ -match '\[PERF\] frames=' })
$playbackS = -1
if ($perfLines.Count -ge 2) {
    $t1 = Parse-LineTime $perfLines[0]
    $t2 = Parse-LineTime $perfLines[-1]
    if ($t1 -and $t2) {
        $playbackS = [int]($t2 - $t1).TotalSeconds
        # Handle midnight wrap defensively (if negative, add 86400)
        if ($playbackS -lt 0) { $playbackS += 86400 }
    }
}

"$Label,$Treatment,$stallCount,$coldOpenMs,$firstPieceMs,$p50Wait,$p99Wait,$playbackS"
