#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Trim a Claude Code Exporter .cc-history/*.md transcript down to convo-only.

.DESCRIPTION
  Strips machine-to-machine noise from a .cc-history transcript file and keeps
  only the actual user <-> assistant dialog. Tonight's measurements showed raw
  transcripts run 70% tool-block junk + 30% real conversation; this filter
  flips that ratio so the file is paste-worthy as next-wake context without
  burning Agent N's token budget on file content the agent would just re-read
  anyway.

  What gets stripped:
    1. Session metadata table at the top (Project / Session ID / Working Dir)
    2. All <details>...</details> blocks (tool call JSON inputs + tool results)
    3. <system-reminder>...</system-reminder> meta-instruction blocks
    4. <task-notification>...</task-notification> background-task XML
    5. Empty "## Assistant <sup>timestamp</sup>" headers that have no prose
       under them (they're markers that precede tool-only turns)
    6. Trailing whitespace + collapsed 3+ blank lines

  What gets kept:
    - User messages (full prose)
    - Assistant messages (prose only — tool calls dropped, text retained)
    - Turn timestamps via <sup> tags (useful for reconstructing the timeline)
    - Turn separator --- lines

.PARAMETER InputFile
  Path to the .cc-history/*.md file to trim. Required.

.PARAMETER OutputFile
  Optional. Defaults to <input>.trimmed.md alongside the input.

.PARAMETER Quiet
  Suppress the size-reduction summary line.

.EXAMPLE
  .\trim-cc-history.ps1 -InputFile .cc-history\2026-05-21_231657_...md
  -> writes .cc-history\2026-05-21_231657_....trimmed.md, prints size report

.EXAMPLE
  .\trim-cc-history.ps1 -InputFile foo.md -OutputFile bar.md -Quiet

.NOTES
  Authored 2026-05-22 by Agent 0 (Coordinator) after Hemanth noted the
  Claude Code Exporter dumps ~70% tool noise + ~30% conversation. Gemini
  was consulted on the format pattern (see chat.md for the cross-AI
  consultation thread). Companion to the session-recap skill's v3.1
  ".cc-history is the ARCHIVE; recap is the INDEX" architecture.
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$InputFile,

    [string]$OutputFile,

    [switch]$Quiet
)

if (-not (Test-Path -LiteralPath $InputFile)) {
    Write-Error "Input file not found: $InputFile"
    exit 1
}

if (-not $OutputFile) {
    $dir = Split-Path -Parent $InputFile
    if (-not $dir) { $dir = "." }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($InputFile)
    $OutputFile = Join-Path $dir "$base.trimmed.md"
}

# Read as UTF-8 explicitly. Get-Content -Raw can default to Windows-1252 on
# Windows PowerShell 5.1 when the source file has no BOM, which mojibakes
# em-dashes / smart quotes / unicode. ReadAllText auto-detects encoding via
# BOM and falls back to UTF-8 cleanly.
$content = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $InputFile).Path)

# 1. Strip session metadata table at the start (through the first --- separator)
$content = $content -replace '(?s)\A#\s+Claude Code Session.*?\r?\n---\s*\r?\n', ''

# 2. Strip all <details>...</details> blocks (tool calls + tool results)
$content = $content -replace '(?s)<details>.*?</details>\s*\r?\n?', ''

# 3. Strip <system-reminder>...</system-reminder> blocks
$content = $content -replace '(?s)<system-reminder>.*?</system-reminder>\s*\r?\n?', ''

# 4. Strip <task-notification>...</task-notification> XML blocks
$content = $content -replace '(?s)<task-notification>.*?</task-notification>\s*\r?\n?', ''

# 5. Strip empty "## Assistant <sup>...</sup>" turns. Pattern: the header line
#    followed only by whitespace then ---. These are placeholders for turns
#    whose only content was a tool call (already stripped in step 2).
$content = $content -replace '(?m)^## Assistant <sup>[^<]+</sup>\s*\r?\n\s*\r?\n---\s*\r?\n', ''

# 6. Same for empty User turns (rare but possible — Tool Result -only turns).
$content = $content -replace '(?m)^## User <sup>[^<]+</sup>\s*\r?\n\s*\r?\n---\s*\r?\n', ''

# 7. Collapse 3+ consecutive blank lines to 1
$content = $content -replace "(\r?\n\s*){3,}", "`r`n`r`n"

# 8. Strip trailing whitespace per line
$content = $content -replace '[ \t]+(\r?\n)', '$1'

# Write output (UTF-8 without BOM)
[System.IO.File]::WriteAllText((Resolve-Path -LiteralPath (Split-Path -Parent $OutputFile)).Path + "\" + (Split-Path -Leaf $OutputFile), $content, [System.Text.UTF8Encoding]::new($false))

if (-not $Quiet) {
    $originalSize = (Get-Item -LiteralPath $InputFile).Length
    $newSize      = (Get-Item -LiteralPath $OutputFile).Length
    $pctRetained  = [math]::Round(($newSize / $originalSize) * 100, 1)
    $pctReduced   = [math]::Round(100 - $pctRetained, 1)
    Write-Host "Trimmed:  $InputFile"
    Write-Host "Original: $($originalSize.ToString('N0')) bytes"
    Write-Host "Trimmed:  $($newSize.ToString('N0')) bytes  ($pctRetained% retained, $pctReduced% reduction)"
    Write-Host "Output:   $OutputFile"
}
