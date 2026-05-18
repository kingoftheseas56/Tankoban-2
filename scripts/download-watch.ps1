# scripts/download-watch.ps1 — Stream download pipeline watch.
#
# Polls four state files Tankoban writes to %LOCALAPPDATA%\Tankoban\data and
# emits one line per interesting delta. Designed for Claude's Monitor tool:
# each stdout line becomes a notification.
#
# Files watched:
#   torrents.json            - m_records: per-torrent state (added/state)
#   stream_bulk_groups.json  - cohort organization for series packs
#   stream_downloads.json    - on-disk file-to-(imdb,season,ep) index
#   stream_library.json      - library entries (movie/series added)
#
# Each event line: HH:mm:ss | EVENT_TYPE | key=value pairs
#
# Run via Monitor or `pwsh -NoProfile -File scripts\download-watch.ps1`.

param(
    [int] $PollSeconds = 5,
    [string] $DataDir  = "$env:LOCALAPPDATA\Tankoban\data"
)

function Now { (Get-Date).ToString("HH:mm:ss") }

function Read-Json([string] $path) {
    if (-not (Test-Path $path)) { return $null }
    try { Get-Content $path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop }
    catch { return $null }  # mid-write or malformed; retry next tick
}

function Short([string] $s, [int] $n = 8) {
    if (-not $s) { return '' }
    if ($s.Length -le $n) { return $s } else { return $s.Substring(0, $n) }
}

function Item-Episode([object] $item) {
    if ($item.itemKey -match 'S(\d+)E(\d+)') {
        return ("S{0:d2}E{1:d2}" -f [int]$Matches[1], [int]$Matches[2])
    }
    return $item.itemKey
}

# ── baseline snapshot ────────────────────────────────────────────────────────
$prevTorrents = @{}   # hash -> state
$prevTorrentNames = @{}  # hash -> name (for log line)
$prevItems = @{}      # "$groupId|$itemKey" -> itemState
$prevGroups = @{}     # groupId -> $true
$prevIndex = @{}      # canonicalPath -> $true
$prevLibrary = @{}    # imdb -> $true

$flushLog = "$PSScriptRoot\..\out\download-watch.log"
$logDir = Split-Path $flushLog -Parent
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }

function Emit([string] $line) {
    Write-Output $line
    Add-Content -Path $flushLog -Value "$(Now) | $line" -Encoding UTF8
}

Write-Output "$(Now) | WATCH_START datadir=$DataDir poll=${PollSeconds}s"
Add-Content -Path $flushLog -Value "$(Now) | WATCH_START datadir=$DataDir poll=${PollSeconds}s" -Encoding UTF8

# Prime baseline silently — first tick after this emits only TRUE deltas.
$first = Read-Json "$DataDir\torrents.json"
if ($first -and $first.active) {
    foreach ($p in $first.active.PSObject.Properties) {
        $prevTorrents[$p.Name] = $p.Value.state
        $prevTorrentNames[$p.Name] = $p.Value.name
    }
}
$first = Read-Json "$DataDir\stream_bulk_groups.json"
if ($first -and $first.groups) {
    foreach ($gp in $first.groups.PSObject.Properties) {
        $prevGroups[$gp.Name] = $true
        foreach ($it in $gp.Value.items) {
            $prevItems["$($gp.Name)|$($it.itemKey)"] = $it.itemState
        }
    }
}
$first = Read-Json "$DataDir\stream_downloads.json"
if ($first -and $first.byPath) {
    foreach ($p in $first.byPath.PSObject.Properties) {
        $prevIndex[$p.Name] = $true
    }
}
$first = Read-Json "$DataDir\stream_library.json"
if ($first) {
    foreach ($p in $first.PSObject.Properties) {
        if ($p.Name -like 'tt*') { $prevLibrary[$p.Name] = $true }
    }
}

Emit "BASELINE torrents=$($prevTorrents.Count) groups=$($prevGroups.Count) items=$($prevItems.Count) index=$($prevIndex.Count) library=$($prevLibrary.Count)"

# ── poll loop ────────────────────────────────────────────────────────────────
while ($true) {
    Start-Sleep -Seconds $PollSeconds

    # ── torrents.json ────────────────────────────────────────────────────────
    $t = Read-Json "$DataDir\torrents.json"
    if ($t -and $t.active) {
        $seen = @{}
        foreach ($p in $t.active.PSObject.Properties) {
            $hash = $p.Name
            $rec = $p.Value
            $seen[$hash] = $true
            if (-not $prevTorrents.ContainsKey($hash)) {
                $nm = if ($rec.name.Length -gt 50) { $rec.name.Substring(0,50)+'...' } else { $rec.name }
                $grp = ''
                if ($rec.streamGroupId) { $grp = Short $rec.streamGroupId 32 }
                Emit "TORRENT_ADDED hash=$(Short $hash) state=$($rec.state) grp=$grp name='$nm'"
                $prevTorrents[$hash] = $rec.state
                $prevTorrentNames[$hash] = $rec.name
            }
            elseif ($prevTorrents[$hash] -ne $rec.state) {
                Emit "TORRENT_STATE hash=$(Short $hash) $($prevTorrents[$hash]) -> $($rec.state) name='$($prevTorrentNames[$hash])'"
                $prevTorrents[$hash] = $rec.state
            }
        }
        foreach ($hash in @($prevTorrents.Keys)) {
            if (-not $seen.ContainsKey($hash)) {
                Emit "TORRENT_REMOVED hash=$(Short $hash) name='$($prevTorrentNames[$hash])'"
                $prevTorrents.Remove($hash) | Out-Null
                $prevTorrentNames.Remove($hash) | Out-Null
            }
        }
    }

    # ── stream_bulk_groups.json ──────────────────────────────────────────────
    $bg = Read-Json "$DataDir\stream_bulk_groups.json"
    if ($bg -and $bg.groups) {
        foreach ($gp in $bg.groups.PSObject.Properties) {
            $groupId = $gp.Name
            $group = $gp.Value
            if (-not $prevGroups.ContainsKey($groupId)) {
                $imdb = ''
                if ($groupId -match 'tt\d+') { $imdb = $Matches[0] }
                $dest = if ($group.destinationRoot.Length -gt 40) { '...'+$group.destinationRoot.Substring($group.destinationRoot.Length-40) } else { $group.destinationRoot }
                Emit "BULK_GROUP_NEW groupId=$(Short $groupId 32) imdb=$imdb kind=$($group.groupKind) items=$($group.items.Count) dest='$dest'"
                $prevGroups[$groupId] = $true
            }
            foreach ($it in $group.items) {
                $key = "$groupId|$($it.itemKey)"
                if (-not $prevItems.ContainsKey($key)) {
                    Emit "COHORT_ITEM_NEW grp=$(Short $groupId 32) ep=$(Item-Episode $it) state=$($it.itemState) file='$($it.canonicalFilename)'"
                    $prevItems[$key] = $it.itemState
                }
                elseif ($prevItems[$key] -ne $it.itemState) {
                    Emit "COHORT_ITEM_STATE grp=$(Short $groupId 32) ep=$(Item-Episode $it) $($prevItems[$key]) -> $($it.itemState) hash=$(Short $it.infoHash)"
                    $prevItems[$key] = $it.itemState
                }
            }
        }
    }

    # ── stream_downloads.json (THE critical linkage file) ────────────────────
    $sd = Read-Json "$DataDir\stream_downloads.json"
    if ($sd -and $sd.byPath) {
        foreach ($p in $sd.byPath.PSObject.Properties) {
            if (-not $prevIndex.ContainsKey($p.Name)) {
                $r = $p.Value
                $basename = Split-Path $r.canonicalPath -Leaf
                $sizeMB = [math]::Round($r.fileSizeBytes/1048576, 0)
                $grpShort = if ($r.sourceGroupId) { Short $r.sourceGroupId 32 } else { '<movie>' }
                Emit "INDEX_ADD imdb=$($r.imdbId) S$($r.season)E$($r.episode) sizeMB=$sizeMB grp=$grpShort file='$basename'"
                $prevIndex[$p.Name] = $true
            }
        }
    }

    # ── stream_library.json ──────────────────────────────────────────────────
    $lib = Read-Json "$DataDir\stream_library.json"
    if ($lib) {
        foreach ($p in $lib.PSObject.Properties) {
            if ($p.Name -like 'tt*' -and -not $prevLibrary.ContainsKey($p.Name)) {
                $r = $p.Value
                Emit "LIBRARY_ADD imdb=$($r.imdb) type=$($r.type) name='$($r.name)' year='$($r.year)'"
                $prevLibrary[$p.Name] = $true
            }
        }
    }
}
