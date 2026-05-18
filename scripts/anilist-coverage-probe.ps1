# scripts/anilist-coverage-probe.ps1
#
# Probe AniList's GraphQL API directly for a list of long-running manga series
# and print a coverage table. Answers the question: "does AniList have
# totalChapters + totalVolumes data for series X, or does it return null?"
#
# Used by TANKOYOMI_VOLUME_PIVOT to decide whether One Piece's null-totals
# response is a one-off vs a systematic gap.
#
# Run from repo root: powershell -NoProfile -File scripts\anilist-coverage-probe.ps1
#
# Rate-limited: 90 req/min cap on AniList. Script sleeps 700ms between
# requests to stay well under the ceiling.

$ErrorActionPreference = 'Stop'

$endpoint = 'https://graphql.anilist.co'
$series = @(
    'Death Note',
    'Naruto',
    'Berserk',
    'One Piece',
    'Bleach',
    'Vagabond',
    'Vinland Saga',
    'Chainsaw Man',
    'Demon Slayer',
    'Attack on Titan',
    'Hunter x Hunter',
    'JoJo''s Bizarre Adventure',
    'Slam Dunk',
    '20th Century Boys',
    'Tokyo Ghoul',
    'Spy x Family',
    'Jujutsu Kaisen',
    'Fullmetal Alchemist',
    'Dragon Ball',
    'Monster'
)

$query = @'
query ($search: String) {
  Media(search: $search, type: MANGA) {
    id
    title { english romaji }
    status
    chapters
    volumes
    startDate { year }
  }
}
'@

Write-Host "Probing AniList GraphQL for $($series.Count) series..." -ForegroundColor Cyan
Write-Host "Endpoint: $endpoint" -ForegroundColor DarkGray
Write-Host ""

$results = New-Object System.Collections.ArrayList

foreach ($name in $series) {
    $body = @{
        query     = $query
        variables = @{ search = $name }
    } | ConvertTo-Json -Depth 5 -Compress

    try {
        $resp = Invoke-RestMethod -Uri $endpoint -Method Post -Body $body `
                                  -ContentType 'application/json' `
                                  -TimeoutSec 10
        $m = $resp.data.Media
        if ($null -ne $m) {
            $picked = if ($m.title.english) { $m.title.english } else { $m.title.romaji }
            $ch = if ($null -ne $m.chapters) { [int]$m.chapters } else { 0 }
            $vol = if ($null -ne $m.volumes) { [int]$m.volumes } else { 0 }
            $year = if ($null -ne $m.startDate.year) { [int]$m.startDate.year } else { 0 }
            $data = if (($ch -gt 0) -and ($vol -gt 0)) { 'FULL' }
                    elseif (($ch -gt 0) -or ($vol -gt 0)) { 'PARTIAL' }
                    else { 'NULL' }
            [void]$results.Add([PSCustomObject]@{
                Series   = $name
                Matched  = $picked
                Id       = [int]$m.id
                Year     = $year
                Status   = $m.status
                Chapters = $ch
                Volumes  = $vol
                Data     = $data
            })
        } else {
            [void]$results.Add([PSCustomObject]@{
                Series=$name; Matched='<no match>'; Id=0; Year=0; Status='-'
                Chapters=0; Volumes=0; Data='NOT FOUND'
            })
        }
    } catch {
        Write-Host "  ERROR for '$name': $($_.Exception.Message)" -ForegroundColor Red
        [void]$results.Add([PSCustomObject]@{
            Series=$name; Matched='<error>'; Id=0; Year=0; Status='-'
            Chapters=0; Volumes=0; Data='ERROR'
        })
    }
    Start-Sleep -Milliseconds 700
}

Write-Host ""
Write-Host "AniList coverage results:" -ForegroundColor Cyan
$results | Format-Table -AutoSize

# Summary buckets
$full    = ($results | Where-Object { $_.Data -eq 'FULL' }).Count
$partial = ($results | Where-Object { $_.Data -eq 'PARTIAL' }).Count
$null_   = ($results | Where-Object { $_.Data -eq 'NULL' }).Count
$miss    = ($results | Where-Object { $_.Data -eq 'NOT FOUND' -or $_.Data -eq 'ERROR' }).Count

Write-Host ""
Write-Host "Summary:" -ForegroundColor Cyan
Write-Host "  FULL data (both chapters + volumes) : $full / $($series.Count)"
Write-Host "  PARTIAL (only one of the two)        : $partial"
Write-Host "  NULL (both zero)                     : $null_"
Write-Host "  NOT FOUND / ERROR                    : $miss"
Write-Host ""
Write-Host "FULL means AniListVolumeMapper can produce real Vol 1..Vol N rows."
Write-Host "NULL / PARTIAL means we fall back to the placeholder Vol X."
