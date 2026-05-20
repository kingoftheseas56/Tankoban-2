# Telemetry de-dup + compliance report for .claude/telemetry/skill-discipline.jsonl
#
# Closes Codex V7 / Phase 0.3 finding: pre-rtc-checker.sh fires on every Stop event
# while a non-trivial RTC sits in `git diff HEAD`, regenerating identical
# {tag, event} rows turn after turn. Raw row count overstates compliance failures.
#
# This script produces the CORRECTED metric: one row per (tag, day) — i.e. how
# many distinct non-trivial RTCs were nagged at least once that day, ignoring
# how many times the same line was re-nagged across Stop events.
#
# Usage:
#   powershell -NoProfile -File .claude/scripts/telemetry-dedup.ps1
#   powershell -NoProfile -File .claude/scripts/telemetry-dedup.ps1 -Since 2026-05-19
#
# Read-only. Does not mutate the source JSONL.

[CmdletBinding()]
param(
    [string]$Since = "",
    [string]$Path = ".claude/telemetry/skill-discipline.jsonl"
)

if (-not (Test-Path $Path)) {
    Write-Output "telemetry file not found: $Path"
    exit 1
}

$rows = Get-Content $Path | Where-Object { $_ -match '^\s*\{' } | ForEach-Object {
    try { $_ | ConvertFrom-Json } catch { $null }
} | Where-Object { $_ -ne $null }

$total = $rows.Count

if ($Since) {
    $rows = $rows | Where-Object { $_.ts -ge $Since }
}

$inWindow = $rows.Count

# Group by (tag, date) — one row per unique nag-event per day.
$deduped = $rows | ForEach-Object {
    $date = $_.ts.Substring(0, 10)
    [PSCustomObject]@{
        tag = $_.tag
        date = $date
        src_touched = $_.src_touched
        files_count = $_.files_count
        loc_changed = $_.loc_changed
    }
} | Group-Object -Property tag, date | ForEach-Object {
    $first = $_.Group | Select-Object -First 1
    [PSCustomObject]@{
        tag = $first.tag
        date = $first.date
        src_touched = $first.src_touched
        files_count = $first.files_count
        loc_changed = $first.loc_changed
        repeat_count = $_.Count
    }
}

$uniqueTagDayCount = $deduped.Count
$totalRepeatCount = ($deduped | Measure-Object -Property repeat_count -Sum).Sum
$overcountRatio = if ($uniqueTagDayCount -gt 0) {
    [math]::Round($totalRepeatCount / $uniqueTagDayCount, 2)
} else { 0 }

Write-Output ""
Write-Output "Telemetry compliance report (de-duped by (tag, date))"
Write-Output "Source: $Path"
if ($Since) { Write-Output "Window: rows with ts >= $Since" }
Write-Output ""
Write-Output "Raw row count                  : $total"
if ($Since) { Write-Output "In-window row count            : $inWindow" }
Write-Output "Unique (tag, day) pairs        : $uniqueTagDayCount"
Write-Output "Avg repeat-nag per pair        : $overcountRatio  (Codex V7 overcount factor)"
Write-Output ""

Write-Output "Per-day distinct misses:"
$deduped | Group-Object -Property date | Sort-Object Name | ForEach-Object {
    Write-Output ("  {0}  : {1} distinct" -f $_.Name, $_.Count)
}
Write-Output ""

Write-Output "Top 10 most-repeated tags (hook overcounting hotspots):"
$deduped | Sort-Object -Property repeat_count -Descending | Select-Object -First 10 | ForEach-Object {
    $tagPreview = if ($_.tag.Length -gt 90) { $_.tag.Substring(0, 87) + "..." } else { $_.tag }
    Write-Output ("  {0,3}x  [{1}]  {2}" -f $_.repeat_count, $_.date, $tagPreview)
}
