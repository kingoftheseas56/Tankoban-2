# scripts/comics-fresh-start.ps1
#
# TANKOYOMI_VOLUME_PIVOT dirty-cleanup script.
#
# Phase 6's ComicsPrePivotMigrator wiped the NEW Phase-5-era manga_downloads_index.json
# + comics_library.json into <localappdata>/Tankoban/comics_pre_pivot_backup/, but
# left the LEGACY MangaDownloader files alone (manga_downloads.json + manga_history.json
# + progress.json). Those legacy files surface in Continue Reading as pre-pivot remnants
# (Berserk WeebCentral / Batman / etc).
#
# This script moves those legacy files into the same backup dir with a timestamped
# suffix so they're recoverable. Also clears empty staging dirs.
#
# Run from repo root: powershell -NoProfile -File scripts\comics-fresh-start.ps1
# Preserves: anilist_cache, manga_posters, all stream_* + video_* + books_* files.

$ErrorActionPreference = 'Stop'

$tankobanRoot = Join-Path $env:LOCALAPPDATA 'Tankoban'
$dataDir      = Join-Path $tankobanRoot 'data'
$backupDir    = Join-Path $tankobanRoot 'comics_pre_pivot_backup'

if (-not (Test-Path $dataDir)) {
    Write-Host "ERROR: Tankoban data dir not found at $dataDir" -ForegroundColor Red
    exit 1
}

Write-Host "Tankoban data dir: $dataDir"
Write-Host "Backup dir:        $backupDir"
Write-Host ""

# Step 1: kill running app (no exe lock during file moves)
$running = Get-Process -Name Tankoban -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "Killing Tankoban (PID $($running.Id))..." -ForegroundColor Yellow
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}
Get-Process -Name ffmpeg_sidecar -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# Step 2: ensure backup dir exists (migrator created it on first run; harmless if present)
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

# Step 3: move legacy MangaDownloader files + generic progress file
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$legacyFiles = @(
    'manga_downloads.json',
    'manga_history.json',
    'progress.json'
)

$movedCount = 0
foreach ($f in $legacyFiles) {
    $src = Join-Path $dataDir $f
    if (Test-Path $src) {
        $dst = Join-Path $backupDir "${f}.dirty_cleanup_${stamp}"
        Move-Item -Path $src -Destination $dst -Force
        $sizeKB = [math]::Round((Get-Item $dst).Length / 1024, 1)
        Write-Host "  moved $f ($sizeKB KB) -> backup/${f}.dirty_cleanup_${stamp}" -ForegroundColor Green
        $movedCount++
    } else {
        Write-Host "  skipped $f (not present)" -ForegroundColor DarkGray
    }
}

# Step 4: sweep .bak siblings of the legacy indices (housekeeping, not strictly needed)
$bakCount = 0
foreach ($pattern in @('manga_downloads.json.bak_*', 'manga_history.json.*-backup', 'manga_downloads_index.json.bak_*')) {
    Get-ChildItem -Path $dataDir -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object {
        $dst = Join-Path $backupDir $_.Name
        Move-Item -Path $_.FullName -Destination $dst -Force
        Write-Host "  swept $($_.Name) -> backup/" -ForegroundColor DarkCyan
        $bakCount++
    }
}

# Step 5: clear staging dirs (orphans from prior interrupted downloads)
$stagingCleared = 0
foreach ($dir in @('manga_premium_staging', 'manga_wc_staging')) {
    $stagingPath = Join-Path $dataDir $dir
    if (Test-Path $stagingPath) {
        $contents = @(Get-ChildItem $stagingPath -ErrorAction SilentlyContinue)
        if ($contents.Count -gt 0) {
            Write-Host "  clearing $dir ($($contents.Count) entries)" -ForegroundColor Yellow
            Remove-Item -Recurse -Force (Join-Path $stagingPath '*') -ErrorAction SilentlyContinue
            $stagingCleared++
        }
    }
}

Write-Host ""
Write-Host "Cleanup complete." -ForegroundColor Green
Write-Host "  legacy files moved : $movedCount"
Write-Host "  .bak files swept   : $bakCount"
Write-Host "  staging dirs cleared: $stagingCleared"
Write-Host ""
Write-Host "Next: launch via build_and_run.bat. Expected fresh-pivot state:" -ForegroundColor Cyan
Write-Host "  - Continue Reading empty"
Write-Host "  - DOWNLOADED section absent (MangaDownloadIndex starts empty)"
Write-Host "  - BOOKMARKED section absent (no AniListCache bookmarks yet)"
Write-Host "  - Comics landing renders the empty-state status label"
Write-Host ""
Write-Host "Recovery: backup/<file>.dirty_cleanup_${stamp} can be restored if needed."
