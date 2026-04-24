<#
  scripts/compare-mpv-tanko.ps1

  Compare mpv baseline vs Tankoban sidecar playback on the same file.
  v1 (this): log-parser only. Feed it two already-recorded logs and get a
             one-line diff + verdict. Proof-of-shape for the regression
             harness proposed 2026-04-24 (Agent 3, chat.md evening block).

  What "drops" means on each side:
    mpv:     count of log lines matching "dropping frame" (vd debug) or
             "[cplayer] Dropped:" (session summary). 0 is consistent with a
             clean run at --msg-level=all=v since drops only appear there
             when they actually occurred.
    sidecar: count of "VideoDecoder: dropped late frame" lines in the log.
             One line per dropped frame event. We do NOT read the running
             `(total=N)` tail because that counter resets at each sidecar
             session boundary — a log file spanning multiple open/close
             cycles would report only the max per-session peak, which
             under-reports total drops. Line-counting is unambiguous.
             For fresh smokes, rotate sidecar_debug_live.log aside BEFORE
             launch so each file-under-test has exactly one session in its
             captured log (project convention already — Rule 17 + the
             rotation pattern used by existing smoke scripts).

  What "stalls" means on each side:
    mpv:     0 on local-file playback. mpv doesn't cache-stall on disk I/O.
             (The harness does NOT attempt to parse mpv network stalls.)
    sidecar: count of `handle_stall_pause` IPC dispatch lines + count of
             `handle_stall_resume` ditto. Each pair is one stall cycle.

  Verdict rule:
    CONVERGED       if tanko_drops <= threshold * mpv_drops (default 2.0)
                    AND tanko_stall_pauses <= mpv_stalls + 1 (tolerance)
                    AND (mpv_drops > 0 OR tanko_drops == 0)  -- 0→N is hard fail
    DIVERGED-WORSE  otherwise

  Exit codes:
    0 = CONVERGED
    1 = DIVERGED-WORSE
    2 = parse error / missing input

  Output:
    stdout: one CSV-ish line, key=value tokens, easy to grep from automation
    stderr: human-readable block (counts, session durations, verdict)

  Example:
    pwsh -NoProfile -File scripts/compare-mpv-tanko.ps1 `
      -MpvLog     agents/audits/evidence_mpv_baseline_20260424_132114.log `
      -SidecarLog agents/audits/evidence_sidecar_debug_dip_smoke_20260424_132114.log `
      -File       "Edgbaston cricket clip"

  Future work (v2, separate ship):
    - Orchestrate fresh runs: mpv with "--no-terminal --end=120 --msg-level=all=v,vd=debug"
      + MCP-driven Tankoban playback of the same file, then diff.
    - Batch mode: feed a directory of files, emit per-file rows + a
      summary table (CONVERGED / DIVERGED-WORSE counts).
    - Wire into pre-RTC checklist for sidecar/player changes (any
      DIVERGED-WORSE on the reference set fails the RTC).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$MpvLog,

    [Parameter(Mandatory=$true)]
    [string]$SidecarLog,

    [string]$File = "<unspecified>",

    [int]$DurationSec = 0,

    [double]$DropRatioThreshold = 2.0,

    [int]$StallTolerance = 1
)

$ErrorActionPreference = "Stop"

function Get-LastTimestampSec {
    param([string[]]$lines)
    # mpv lines look like "[   0.004][v][cplayer] ..."
    # Walk backward from the last line until we find a timestamp prefix.
    for ($i = $lines.Length - 1; $i -ge 0 -and $i -ge $lines.Length - 50; $i--) {
        if ($lines[$i] -match '^\[\s*([0-9]+\.[0-9]+)\]') {
            return [double]$matches[1]
        }
    }
    return 0.0
}

function Parse-MpvLog {
    param([string]$path)
    if (-not (Test-Path -LiteralPath $path)) {
        return @{ parsed=$false; reason="mpv log not found: $path" }
    }
    $dropA = @(Select-String -LiteralPath $path -Pattern 'dropping frame'      -SimpleMatch)
    $dropB = @(Select-String -LiteralPath $path -Pattern '\[cplayer\] Dropped:')
    $dropCount = $dropA.Count + $dropB.Count

    # Session duration from last timestamped line
    $lines = Get-Content -LiteralPath $path -Tail 50
    $sessionSec = Get-LastTimestampSec -lines $lines

    # Clean exit detection
    $cleanExit = $false
    foreach ($l in $lines) {
        if ($l -match '\[cplayer\] End of file|\[cplayer\] Exiting') {
            $cleanExit = $true
            break
        }
    }

    return @{
        parsed      = $true
        drops       = $dropCount
        stalls      = 0  # local-file playback; harness v1 doesn't parse mpv cache stalls
        sessionSec  = $sessionSec
        cleanExit   = $cleanExit
    }
}

function Parse-SidecarLog {
    param([string]$path)
    if (-not (Test-Path -LiteralPath $path)) {
        return @{ parsed=$false; reason="sidecar log not found: $path" }
    }
    # One "dropped late frame" line per dropped frame event. Session-safe.
    $dropCount    = @(Select-String -LiteralPath $path -Pattern 'dropped late frame' -SimpleMatch).Count
    $stallPauses  = @(Select-String -LiteralPath $path -Pattern 'handle_stall_pause'  -SimpleMatch).Count
    $stallResumes = @(Select-String -LiteralPath $path -Pattern 'handle_stall_resume' -SimpleMatch).Count

    # [PERF] lines in this sidecar don't carry wall-clock timestamps, so
    # session duration from sidecar log is not directly recoverable. Leave at 0.
    return @{
        parsed        = $true
        drops         = $dropCount
        stallPauses   = $stallPauses
        stallResumes  = $stallResumes
        sessionSec    = 0.0
    }
}

# ─── Parse both sides ───────────────────────────────────────────────────────
$mpv  = Parse-MpvLog     -path $MpvLog
if (-not $mpv.parsed) {
    [Console]::Error.WriteLine("ERROR: {0}" -f $mpv.reason)
    exit 2
}

$tank = Parse-SidecarLog -path $SidecarLog
if (-not $tank.parsed) {
    [Console]::Error.WriteLine("ERROR: {0}" -f $tank.reason)
    exit 2
}

# ─── Verdict ────────────────────────────────────────────────────────────────
$verdict = "CONVERGED"
$reasons = @()

# Drops: tolerate up to threshold x mpv drops. 0 → N is hard fail.
if ($mpv.drops -eq 0 -and $tank.drops -gt 0) {
    $verdict = "DIVERGED-WORSE"
    $reasons += "tanko drops $($tank.drops) vs mpv 0 (hard fail)"
} elseif ($mpv.drops -gt 0 -and $tank.drops -gt ($mpv.drops * $DropRatioThreshold)) {
    $verdict = "DIVERGED-WORSE"
    $reasons += ("tanko drops {0} exceeds {1}x mpv drops {2}" -f `
                 $tank.drops, $DropRatioThreshold, $mpv.drops)
}

# Stalls: tolerate up to mpv.stalls + StallTolerance.
if ($tank.stallPauses -gt ($mpv.stalls + $StallTolerance)) {
    if ($verdict -eq "CONVERGED") { $verdict = "DIVERGED-WORSE" }
    $reasons += ("tanko stall pauses {0} exceeds mpv stalls {1} + tolerance {2}" -f `
                 $tank.stallPauses, $mpv.stalls, $StallTolerance)
}

# ─── Output ─────────────────────────────────────────────────────────────────
$oneLine = "file={0} mpv_drops={1} mpv_stalls={2} mpv_sec={3:F1} tanko_drops={4} tanko_stall_pauses={5} tanko_stall_resumes={6} verdict={7}" -f `
    ($File -replace '\s','_'), $mpv.drops, $mpv.stalls, $mpv.sessionSec, `
    $tank.drops, $tank.stallPauses, $tank.stallResumes, $verdict

Write-Output $oneLine

# Human-readable block to stderr
$humanBlock = @"

-- compare-mpv-tanko ---------------------------------------------
File:          $File
Duration hint: $DurationSec s

mpv baseline
  log:        $MpvLog
  drops:      $($mpv.drops)
  stalls:     $($mpv.stalls)
  sessionSec: $($mpv.sessionSec)
  cleanExit:  $($mpv.cleanExit)

tankoban sidecar
  log:           $SidecarLog
  drops:         $($tank.drops)
  stall_pauses:  $($tank.stallPauses)
  stall_resumes: $($tank.stallResumes)

verdict: $verdict
"@
[Console]::Error.WriteLine($humanBlock)

if ($reasons.Count -gt 0) {
    [Console]::Error.WriteLine("reasons:")
    foreach ($r in $reasons) {
        [Console]::Error.WriteLine("  - $r")
    }
}
[Console]::Error.WriteLine("")

# Exit codes: 0 = CONVERGED, 1 = DIVERGED-WORSE, 2 = parse error
if ($verdict -eq "CONVERGED") { exit 0 } else { exit 1 }
