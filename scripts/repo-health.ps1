# scripts/repo-health.ps1
# Tankoban 2 repo-health audit. Reports on drift surfaces Agent 0 normally
# checks by hand at session boundaries. Closes Codex audit item #5 (2026-04-18).
#
# Invocation:
#   powershell -NoProfile -File scripts/repo-health.ps1
#   powershell -NoProfile -File scripts/repo-health.ps1 -Verbose
#
# Designed to run in PowerShell 5.1 (default Windows) or 7+. No external deps.
# Agent-invokable from bash: powershell -NoProfile -File scripts/repo-health.ps1
#
# Exit codes:
#   0 - all checks green or only INFO items
#   1 - one or more WARN findings
#   2 - one or more FAIL findings
#
# Checks (in order):
#   1. chat.md line count vs rotation threshold
#   2. Uncommitted READY TO COMMIT lines
#   3. CONGRESS.md open + stale-archive detection
#   4. HELP.md open requests
#   5. STATUS.md vs CLAUDE.md freshness drift
#   6. Large source files (>=80 KB OR >=2000 lines)
#   7. Tracked generated files (build output leaked into git index)
#
# NOTE: this file is pure ASCII by convention. PowerShell 5.1 reads scripts
# in the OEM codepage by default and mangles UTF-8 em-dashes / section-signs
# absent a BOM. Keep edits ASCII-only to preserve bash-invocation safety.
# (This is the exact rule GOVERNANCE Rule 16 codifies.)

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# cd to repo root (script lives at <repo>/scripts/)
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
Set-Location -LiteralPath $RepoRoot

$warnings = 0
$failures = 0

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
    if ($Status -eq 'WARN') { $script:warnings++ }
    if ($Status -eq 'FAIL') { $script:failures++ }
}

Write-Host "Tankoban 2 repo-health audit  $(Get-Date -Format 'yyyy-MM-dd HH:mm')" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot" -ForegroundColor DarkGray
Write-Host ("-" * 78)

# 1. chat.md line count
$chatPath = 'agents/chat.md'
if (Test-Path -LiteralPath $chatPath) {
    $lineCount = (Get-Content -LiteralPath $chatPath | Measure-Object -Line).Lines
    $byteSize  = (Get-Item -LiteralPath $chatPath).Length
    $kb        = [math]::Round($byteSize / 1024, 0)
    if ($lineCount -gt 3000 -or $byteSize -gt (300 * 1024)) {
        Write-Check 'FAIL' 'chat.md rotation' "${lineCount} lines / ${kb}KB - past 3000-line / 300KB trigger. Run /rotate-chat."
    } elseif ($lineCount -gt 2000) {
        Write-Check 'WARN' 'chat.md rotation' "${lineCount} lines / ${kb}KB - approaching rotation threshold."
    } else {
        Write-Check 'OK' 'chat.md rotation' "${lineCount} lines / ${kb}KB (threshold 3000/300KB)"
    }
} else {
    Write-Check 'WARN' 'chat.md rotation' "agents/chat.md not found"
}

# 2. Pending READY TO COMMIT
$pending = $null
$scanScript = '.claude/scripts/scan-pending-commits.sh'
if (Test-Path -LiteralPath $scanScript) {
    try {
        $pending = [int](bash $scanScript 2>$null)
    } catch {
        $pending = $null
    }
}
if ($null -eq $pending) {
    Write-Check 'INFO' 'pending RTC lines' "unable to run scan-pending-commits.sh (bash missing?)"
} elseif ($pending -eq 0) {
    Write-Check 'OK' 'pending RTC lines' "no uncommitted READY TO COMMIT lines"
} elseif ($pending -le 3) {
    Write-Check 'INFO' 'pending RTC lines' "${pending} pending - batch soon via /commit-sweep"
} else {
    Write-Check 'WARN' 'pending RTC lines' "${pending} pending - /commit-sweep overdue"
}

# 3. CONGRESS.md state
$congressPath = 'agents/CONGRESS.md'
if (Test-Path -LiteralPath $congressPath) {
    $head = Get-Content -LiteralPath $congressPath -TotalCount 30
    $statusLine = $head | Where-Object { $_ -match 'STATUS:\s*([A-Z ]+)' } | Select-Object -First 1
    if ($statusLine -match 'STATUS:\s*(NO ACTIVE MOTION)') {
        Write-Check 'OK' 'CONGRESS status' "no active motion"
    } elseif ($statusLine -match 'STATUS:\s*(OPEN)') {
        $fullText = Get-Content -LiteralPath $congressPath -Raw
        if ($fullText -match '\b(ratified|APPROVES|Final Word|Execute)\b') {
            Write-Check 'FAIL' 'CONGRESS status' "OPEN but ratification keyword present - Agent 0 owes same-session archive (GOVERNANCE Rule + auto-close)"
        } else {
            Write-Check 'WARN' 'CONGRESS status' "motion OPEN - pending positions / synthesis / ratification"
        }
    } else {
        Write-Check 'INFO' 'CONGRESS status' "status line not detected (non-standard template?)"
    }
} else {
    Write-Check 'INFO' 'CONGRESS status' "agents/CONGRESS.md not found"
}

# 4. HELP.md state
$helpPath = 'agents/HELP.md'
if (Test-Path -LiteralPath $helpPath) {
    $head = Get-Content -LiteralPath $helpPath -TotalCount 15
    $statusLine = $head | Where-Object { $_ -match 'STATUS:\s*([A-Z ]+)' } | Select-Object -First 1
    if ($statusLine -match 'STATUS:\s*(OPEN)') {
        Write-Check 'WARN' 'HELP state' "one or more cross-agent asks OPEN"
    } elseif ($statusLine -match 'STATUS:\s*(CLOSED|NONE|NO\s+OPEN)') {
        Write-Check 'OK' 'HELP state' "no open cross-agent asks"
    } else {
        Write-Check 'INFO' 'HELP state' "status line not detected"
    }
} else {
    Write-Check 'OK' 'HELP state' "no HELP.md present (no open asks by construction)"
}

# 5. STATUS.md vs CLAUDE.md freshness
$statusPath = 'agents/STATUS.md'
$claudePath = 'CLAUDE.md'
if ((Test-Path -LiteralPath $statusPath) -and (Test-Path -LiteralPath $claudePath)) {
    $statusHead = Get-Content -LiteralPath $statusPath -TotalCount 10
    $claudeHead = Get-Content -LiteralPath $claudePath -TotalCount 20

    $statusDate = $null
    foreach ($line in $statusHead) {
        if ($line -match 'Last header touch:\s*([0-9]{4}-[0-9]{2}-[0-9]{2})') {
            $statusDate = [datetime]::ParseExact($matches[1], 'yyyy-MM-dd', $null)
            break
        }
    }
    $claudeDate = $null
    foreach ($line in $claudeHead) {
        if ($line -match '\*\*As of:\*\*\s*([0-9]{4}-[0-9]{2}-[0-9]{2})') {
            $claudeDate = [datetime]::ParseExact($matches[1], 'yyyy-MM-dd', $null)
            break
        }
    }
    if ($null -ne $statusDate -and $null -ne $claudeDate) {
        $drift = ($statusDate - $claudeDate).TotalDays
        if ($drift -gt 1) {
            Write-Check 'WARN' 'STATUS vs CLAUDE' ("STATUS.md ahead of CLAUDE.md by {0:N0}d - dashboard drift. Agent 0 refresh CLAUDE.md." -f $drift)
        } elseif ($drift -lt -7) {
            Write-Check 'INFO' 'STATUS vs CLAUDE' ("CLAUDE.md ahead by {0:N0}d (harmless)" -f [math]::Abs($drift))
        } else {
            Write-Check 'OK' 'STATUS vs CLAUDE' ("in sync (drift {0:N0}d)" -f $drift)
        }
    } else {
        Write-Check 'INFO' 'STATUS vs CLAUDE' "date headers not parseable"
    }
} else {
    Write-Check 'INFO' 'STATUS vs CLAUDE' "STATUS.md or CLAUDE.md missing"
}

# 6. Large source files (refactor candidates).
# PS 5.1 Get-ChildItem doesn't recurse with ** glob; use -Recurse explicitly
# on each root dir with -Include on the extension list.
$largeFiles = @()
$sourceRoots = @('src', 'native_sidecar/src')
foreach ($root in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    Get-ChildItem -Path $root -Recurse -Include '*.cpp','*.h' -File -ErrorAction SilentlyContinue | ForEach-Object {
        $sizeKb  = [math]::Round($_.Length / 1024, 0)
        $lineCnt = (Get-Content -LiteralPath $_.FullName | Measure-Object -Line).Lines
        if ($sizeKb -ge 80 -or $lineCnt -ge 2000) {
            $largeFiles += [PSCustomObject]@{
                Path    = $_.FullName.Substring($RepoRoot.Length + 1) -replace '\\', '/'
                SizeKb  = $sizeKb
                Lines   = $lineCnt
            }
        }
    }
}
if ($largeFiles.Count -eq 0) {
    Write-Check 'OK' 'large source files' "no files >=80KB or >=2000 lines"
} elseif ($largeFiles.Count -le 6) {
    Write-Check 'WARN' 'large source files' ("{0} refactor candidates" -f $largeFiles.Count)
    foreach ($f in $largeFiles | Sort-Object -Property SizeKb -Descending) {
        Write-Host ("        {0,4}KB  {1,5}L  {2}" -f $f.SizeKb, $f.Lines, $f.Path) -ForegroundColor DarkYellow
    }
} else {
    Write-Check 'FAIL' 'large source files' ("{0} refactor candidates (over threshold 6)" -f $largeFiles.Count)
    foreach ($f in $largeFiles | Sort-Object -Property SizeKb -Descending | Select-Object -First 10) {
        Write-Host ("        {0,4}KB  {1,5}L  {2}" -f $f.SizeKb, $f.Lines, $f.Path) -ForegroundColor DarkRed
    }
}

# 7. Tracked generated files (build output leaked into git index)
$generatedPatterns = @(
    '\.ninja_deps$',
    '\.ninja_log',
    '^CMakeCache\.txt$',
    '/CMakeCache\.txt$',
    '/CMakeFiles/',
    '_autogen/',
    '\.obj$',
    '/CMakeLists\.txt\.rule$',
    'compile_commands\.json$'
)
$trackedGenerated = @()
$gitListOk = $true
try {
    $allTracked = git ls-files 2>$null
    foreach ($pattern in $generatedPatterns) {
        $patternMatches = $allTracked | Where-Object { $_ -match $pattern }
        if ($patternMatches) { $trackedGenerated += $patternMatches }
    }
    $trackedGenerated = $trackedGenerated | Sort-Object -Unique
} catch {
    $gitListOk = $false
}
if (-not $gitListOk) {
    Write-Check 'INFO' 'tracked generated' "git ls-files failed"
} elseif ($trackedGenerated.Count -eq 0) {
    Write-Check 'OK' 'tracked generated' "no build output in git index"
} else {
    Write-Check 'FAIL' 'tracked generated' ("{0} generated files tracked - likely build output leak" -f $trackedGenerated.Count)
    foreach ($f in $trackedGenerated | Select-Object -First 8) {
        Write-Host ("        $f") -ForegroundColor DarkRed
    }
    if ($trackedGenerated.Count -gt 8) {
        Write-Host ("        ... plus {0} more" -f ($trackedGenerated.Count - 8)) -ForegroundColor DarkRed
    }
}

# Summary
Write-Host ("-" * 78)
if ($failures -gt 0) {
    Write-Host ("{0} failure(s) + {1} warning(s). Fix failures before shipping." -f $failures, $warnings) -ForegroundColor Red
    exit 2
} elseif ($warnings -gt 0) {
    Write-Host ("{0} warning(s). No failures." -f $warnings) -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "All checks green." -ForegroundColor Green
    exit 0
}
