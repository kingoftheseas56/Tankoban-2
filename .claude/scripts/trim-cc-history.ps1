#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Trim a Claude Code Exporter .cc-history/*.md transcript down to convo-only.

.DESCRIPTION
  Strips machine-to-machine noise from a .cc-history transcript file and keeps
  every real user + assistant prose message intact. Tonight's measurements
  showed raw transcripts run 70% tool-block junk + 30% real conversation; this
  filter flips that ratio so the file is paste-worthy as next-wake context
  without burning Agent N's token budget on file content the agent would just
  re-read anyway.

  THE CONTRACT: dialogue is preserved verbatim. Banter, corrections,
  personality, decision-making chemistry -- all of it stays. Only machine
  noise gets removed.

  What gets stripped:
    1. Session metadata table at the top (Project / Session ID / Working Dir)
    2. <details>...</details> blocks (tool call JSON inputs + tool results)
    3. <system-reminder>...</system-reminder> meta-instruction blocks
    4. <task-notification>...</task-notification> background-task XML
    5. "> *[Image]*" inline image placeholders (zero conversational value)
    6. Orphan "---" separators inside message bodies (artifact of
       structured-answer formatting; turn boundaries get re-emitted on output)
    7. Turns whose body becomes empty after the above cleanup (placeholder
       turns that only contained a tool call)
    8. Excessive blank lines collapsed to 1; trailing whitespace stripped

  What gets kept:
    - User messages (full prose, every word)
    - Assistant messages (full prose, every word)
    - Turn timestamps via <sup> tags
    - Turn boundaries via --- separators (re-emitted between kept turns)

  IMPLEMENTATION NOTE: uses a parser-based approach (find turn headers,
  slice between them, clean each body, drop empties) rather than pure
  regex-across-the-file. The parser style is more robust to edge cases like
  empty-after-stripping turns and orphan separators -- credit to ChatGPT for
  spotting those cases via cross-AI consultation 2026-05-22.

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
  Claude Code Exporter dumps ~70% tool noise + ~30% conversation.
  Cross-consulted Gemini on the <details> format pattern, then ChatGPT on
  the dialogue-preservation contract + edge cases. Companion to the
  session-recap skill's v3.1 ".cc-history is the ARCHIVE; recap is the
  INDEX" architecture.
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
# em-dashes / smart quotes / unicode. ReadAllText auto-detects via BOM with
# UTF-8 fallback.
$content = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $InputFile).Path)

# Pre-parse global strips: these blocks can appear inside a turn body, between
# turns, or anywhere -- handle them upfront before the turn parser runs.

# 1. Session metadata table at the start (through the first --- separator).
$content = $content -replace '(?s)\A#\s+Claude Code Session.*?\r?\n---\s*\r?\n', ''

# 2. <system-reminder>...</system-reminder> blocks.
$content = $content -replace '(?s)<system-reminder>.*?</system-reminder>\s*\r?\n?', ''

# 3. <task-notification>...</task-notification> background-task XML.
$content = $content -replace '(?s)<task-notification>.*?</task-notification>\s*\r?\n?', ''

# Parse turns. Each turn header is "## User <sup>timestamp</sup>" or
# "## Assistant <sup>timestamp</sup>" on its own line; the body runs from
# end-of-header to start-of-next-header (or EOF).
$turnHeaderPattern = [regex]'(?m)^## (User|Assistant) <sup>(.+?)</sup>\s*$'
$turnMatches = $turnHeaderPattern.Matches($content)

if ($turnMatches.Count -eq 0) {
    Write-Error "No turn headers found in $InputFile -- is this actually a Claude Code Exporter transcript?"
    exit 1
}

$keptTurns = New-Object System.Collections.ArrayList

for ($i = 0; $i -lt $turnMatches.Count; $i++) {
    $role      = $turnMatches[$i].Groups[1].Value
    $timestamp = $turnMatches[$i].Groups[2].Value
    $bodyStart = $turnMatches[$i].Index + $turnMatches[$i].Length
    $bodyEnd   = if ($i + 1 -lt $turnMatches.Count) { $turnMatches[$i + 1].Index } else { $content.Length }
    $body      = $content.Substring($bodyStart, $bodyEnd - $bodyStart)

    # Per-body cleanup:
    # a. Strip <details>...</details> blocks (tool calls + tool results).
    $body = $body -replace '(?s)<details>.*?</details>', ''

    # b. Strip "> *[Image]*" placeholders (screenshots that didn't embed).
    $body = $body -replace '(?m)^>\s*\*\[Image\]\*\s*$', ''

    # c. Strip orphan --- separators inside the body. Turn boundaries get
    #    re-emitted on output; in-body --- create false separators.
    $body = $body -replace '(?m)^---\s*$', ''

    # d. Collapse 3+ consecutive blank lines to 1.
    $body = $body -replace "(\r?\n\s*){3,}", "`r`n`r`n"

    # e. Strip trailing whitespace per line.
    $body = $body -replace '[ \t]+(\r?\n)', '$1'

    # f. Trim leading/trailing whitespace from the body as a whole.
    $body = $body.Trim()

    # Drop turns whose body became empty after cleanup (these are placeholder
    # turns that only contained a tool call).
    if ([string]::IsNullOrWhiteSpace($body)) {
        continue
    }

    [void]$keptTurns.Add([PSCustomObject]@{
        Role      = $role
        Timestamp = $timestamp
        Body      = $body
    })
}

# Reassemble output. Preserve the original "## Role <sup>timestamp</sup>"
# header format for fidelity with cc-history conventions.
$outputBuilder = New-Object System.Text.StringBuilder
foreach ($turn in $keptTurns) {
    [void]$outputBuilder.AppendLine("## $($turn.Role) <sup>$($turn.Timestamp)</sup>")
    [void]$outputBuilder.AppendLine()
    [void]$outputBuilder.AppendLine($turn.Body)
    [void]$outputBuilder.AppendLine()
    [void]$outputBuilder.AppendLine("---")
    [void]$outputBuilder.AppendLine()
}

# Write output (UTF-8 without BOM).
$resolvedDir = (Resolve-Path -LiteralPath (Split-Path -Parent $OutputFile)).Path
$outputPath  = Join-Path $resolvedDir (Split-Path -Leaf $OutputFile)
[System.IO.File]::WriteAllText($outputPath, $outputBuilder.ToString(), [System.Text.UTF8Encoding]::new($false))

if (-not $Quiet) {
    $originalSize = (Get-Item -LiteralPath $InputFile).Length
    $newSize      = (Get-Item -LiteralPath $OutputFile).Length
    $pctRetained  = [math]::Round(($newSize / $originalSize) * 100, 1)
    $pctReduced   = [math]::Round(100 - $pctRetained, 1)
    Write-Host "Trimmed:  $InputFile"
    Write-Host "Original: $($originalSize.ToString('N0')) bytes ($($turnMatches.Count) raw turns)"
    Write-Host "Trimmed:  $($newSize.ToString('N0')) bytes ($($keptTurns.Count) kept turns, $pctRetained% retained, $pctReduced% reduction)"
    Write-Host "Output:   $OutputFile"
}
