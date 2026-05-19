# -- smoke-cinemeta-pack.ps1 -------------------------------------------------
# TANKORENT_CINEMETA_PACK_MAPPING Phase 2 smoke evidence-capture harness
# (Agent 4, 2026-05-19).
#
# Polls `tankoctl get-downloads` every 5s, filters StreamDownloadIndex entries
# for the target (imdbId, season) pair, logs state distribution + per-entry
# state transitions on a single timeline, and writes timestamped artifacts
# under agents/audits/smoke_evidence/p2_smoke_<imdb>_s<NN>_<HHMMSS>.{log,json}.
#
# Usage from repo root:
#   powershell -NoProfile -File scripts\smoke-cinemeta-pack.ps1 `
#       -Imdb tt3322312 -Season 2 [-MaxSeconds 300] [-PollSeconds 5]
#
# Designed for use with v1.3 stream-side bridge expansion (stream-open-detail
# + stream-get-sources + stream-direct-download). Caller is expected to dispatch
# the pack via tankoctl BEFORE invoking this harness; the harness only watches
# the StreamDownloadIndex evolution + does not itself dispatch anything.
#
# Exit codes:
#   0 -- happy path: all entries observed reaching Complete OR max-duration reached
#       with at least one observed Pending->Downloading transition
#   1 -- never saw any Pending entry register (substrate didn't fire)
#   2 -- Tankoban / tankoctl ping failure pre-flight
#   3 -- never observed any state transition over the polling window
# ----------------------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Imdb,
    [Parameter(Mandatory=$true)][int]$Season,
    [int]$MaxSeconds = 300,
    [int]$PollSeconds = 5,
    [string]$EvidenceDir = "$PSScriptRoot\..\agents\audits\smoke_evidence"
)

$ErrorActionPreference = "Stop"
$tankoctl = Join-Path $PSScriptRoot "..\out\tankoctl.exe"

if (-not (Test-Path $tankoctl)) {
    Write-Host "ERROR: tankoctl.exe not found at $tankoctl"
    exit 2
}

# Pre-flight: confirm Tankoban is up + ping clean
$pingRaw = & $tankoctl ping 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: tankoctl ping failed -- is Tankoban running with --dev-control?"
    Write-Host $pingRaw
    exit 2
}
try {
    $ping = $pingRaw | ConvertFrom-Json
} catch {
    Write-Host "ERROR: tankoctl ping returned malformed JSON: $pingRaw"
    exit 2
}
if ($ping.schema -notmatch "tankoban\.dev\.v1\.[0-9]+") {
    Write-Host "ERROR: schema $($ping.schema) doesn't look like a tankoban dev bridge"
    exit 2
}
Write-Host "Pre-flight OK -- schema=$($ping.schema)"

# Ensure evidence dir
if (-not (Test-Path $EvidenceDir)) {
    New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
}

$timestamp = Get-Date -Format "HHmmss"
$safeImdb = $Imdb -replace "[^A-Za-z0-9]", "_"
$seasonTag = "s{0:D2}" -f $Season
$basename = "p2_smoke_${safeImdb}_${seasonTag}_${timestamp}"
$logPath = Join-Path $EvidenceDir "$basename.log"
$jsonPath = Join-Path $EvidenceDir "$basename.json"

# Run-state
$startedAt = Get-Date
$ticks = @()
$priorEntryStates = @{}      # itemKey -> state(int) from prior tick
$transitionEvents = @()      # list of {ts, itemKey, fromState, toState, progressPct}
$everSawPending = $false
$allCompleteSeen = $false

function Format-State($n) {
    switch ($n) {
        0 { "Complete" }
        1 { "Pending" }
        2 { "Downloading" }
        3 { "MissingSource" }
        4 { "Cancelled" }
        default { "State$n" }
    }
}

Write-Host "Watching $Imdb season $Season for up to ${MaxSeconds}s (poll every ${PollSeconds}s)"
Write-Host "Evidence: $logPath + $jsonPath"
Write-Host "----------------------------------------------------------------"
"# TANKORENT_CINEMETA_PACK_MAPPING Phase 2 smoke timeline" | Out-File $logPath -Encoding utf8
"# Imdb=$Imdb Season=$Season Started=$($startedAt.ToString('o'))" | Add-Content $logPath -Encoding utf8
"" | Add-Content $logPath -Encoding utf8

while (((Get-Date) - $startedAt).TotalSeconds -lt $MaxSeconds) {
    $tickTs = Get-Date
    $tElapsed = [int](($tickTs - $startedAt).TotalSeconds)

    try {
        $rawJson = & $tankoctl get-downloads 2>&1
        if ($LASTEXITCODE -ne 0) {
            "T+${tElapsed}s -- tankoctl get-downloads exit=$LASTEXITCODE (skip tick)" | Tee-Object -FilePath $logPath -Append
            Start-Sleep -Seconds $PollSeconds
            continue
        }
        $payload = $rawJson | ConvertFrom-Json
    } catch {
        "T+${tElapsed}s -- JSON parse failed (skip tick): $_" | Tee-Object -FilePath $logPath -Append
        Start-Sleep -Seconds $PollSeconds
        continue
    }

    $allEntries = if ($payload.entries) { $payload.entries } else { @() }
    # NOTE: get-downloads payload uses field name `imdb` not `imdbId` (verified
    # 2026-05-19 Task 11 Phase 2 validation smoke). DO NOT rename without
    # re-confirming via raw tankoctl get-downloads output.
    $matched = $allEntries | Where-Object {
        $_.imdb -eq $Imdb -and $_.season -eq $Season
    }
    $matchedCount = ($matched | Measure-Object).Count

    # State distribution
    $dist = @{}
    foreach ($e in $matched) {
        $stateName = Format-State $e.state
        if (-not $dist.ContainsKey($stateName)) { $dist[$stateName] = 0 }
        $dist[$stateName] += 1
    }
    $distStr = ($dist.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Value)$($_.Name[0])" }) -join " "
    if ([string]::IsNullOrEmpty($distStr)) { $distStr = "(none)" }

    $line = "T+{0,4:D}s -- entries={1,2:D}   {2}" -f $tElapsed, $matchedCount, $distStr
    $line | Tee-Object -FilePath $logPath -Append

    # Per-entry transitions
    foreach ($e in $matched) {
        $key = "$($e.imdbId):S$($e.season)E$($e.episode)"
        $prior = $priorEntryStates[$key]
        if ($null -ne $prior -and $prior -ne $e.state) {
            $fromName = Format-State $prior
            $toName = Format-State $e.state
            $progressPct = if ($e.progressPct) { $e.progressPct } else { 0 }
            $tline = "       transition: $key  $fromName -> $toName  ($progressPct%)"
            $tline | Tee-Object -FilePath $logPath -Append
            $transitionEvents += [pscustomobject]@{
                ts = $tickTs.ToString("o")
                tElapsed = $tElapsed
                itemKey = $key
                fromState = $prior
                fromStateName = $fromName
                toState = $e.state
                toStateName = $toName
                progressPct = $progressPct
            }
        }
        if ($e.state -eq 1) { $everSawPending = $true }  # Pending = 1
        $priorEntryStates[$key] = $e.state
    }

    # Capture this tick
    $ticks += [pscustomobject]@{
        ts = $tickTs.ToString("o")
        tElapsed = $tElapsed
        matchedCount = $matchedCount
        stateDistribution = $dist
        entries = $matched
    }

    # Exit-early: all entries Complete (state=0)
    if ($matchedCount -gt 0) {
        $completeCount = ($matched | Where-Object { $_.state -eq 0 } | Measure-Object).Count
        if ($completeCount -eq $matchedCount) {
            $allCompleteSeen = $true
            "T+${tElapsed}s -- ALL ${matchedCount} ENTRIES COMPLETE -- exiting early" | Tee-Object -FilePath $logPath -Append
            break
        }
    }

    Start-Sleep -Seconds $PollSeconds
}

# Summary
$endedAt = Get-Date
$elapsedTotal = [int]($endedAt - $startedAt).TotalSeconds
"" | Add-Content $logPath -Encoding utf8
"# Summary -- ended=$($endedAt.ToString('o')) elapsedSec=$elapsedTotal" | Add-Content $logPath -Encoding utf8
"# Total ticks: $($ticks.Count); transitions observed: $($transitionEvents.Count)" | Add-Content $logPath -Encoding utf8
"# Ever saw Pending: $everSawPending; all-complete: $allCompleteSeen" | Add-Content $logPath -Encoding utf8

$summary = [pscustomobject]@{
    imdb = $Imdb
    season = $Season
    startedAt = $startedAt.ToString("o")
    endedAt = $endedAt.ToString("o")
    elapsedSec = $elapsedTotal
    pollSec = $PollSeconds
    maxSec = $MaxSeconds
    tickCount = $ticks.Count
    transitionCount = $transitionEvents.Count
    everSawPending = $everSawPending
    allCompleteSeen = $allCompleteSeen
    transitions = $transitionEvents
    ticks = $ticks
}
$summary | ConvertTo-Json -Depth 6 | Out-File $jsonPath -Encoding utf8

Write-Host "----------------------------------------------------------------"
Write-Host "Evidence saved:"
Write-Host "  log:  $logPath"
Write-Host "  json: $jsonPath"
Write-Host "Saw Pending: $everSawPending  AllComplete: $allCompleteSeen  Transitions: $($transitionEvents.Count)"

# Exit code logic
if (-not $everSawPending -and $transitionEvents.Count -eq 0 -and $ticks.Count -gt 0 -and ($ticks[0].matchedCount -eq 0)) {
    Write-Host "FAIL -- never saw any matching entry register in StreamDownloadIndex (Phase 2 substrate did not fire)"
    exit 1
}
if ($transitionEvents.Count -eq 0) {
    Write-Host "PARTIAL -- entries registered but no state transition observed within window"
    exit 3
}
Write-Host "OK"
exit 0
