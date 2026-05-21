# Tankorent ↔ Cinemata Integration — Design Spec

**Status:** brainstorm-locked 2026-05-21, awaiting writing-plans.

## Vision

Today, Tankorent search results live in a parallel universe from the Cinemata catalogue. A user can search Tankorent, download a torrent, and the files arrive on disk — but Theatre's show/movie detail views never light up with "downloaded" badges, never link the files to the right episodes, never reflect in-flight state on the right rows. The two systems have **never successfully connected end-to-end** (Hemanth, 2026-05-21).

This spec rebuilds that connection from the start, with two visible surfaces and one durable data path:

- **Theatre detail view** is the entry point: every download starts here, with show identity baked in at click time.
- **Downloads page** (sidebar-accessible) is the global view: every in-flight + recently-completed download from every show, grouped + collapsible.
- **The repository** (`TorrentRepository` SQLite, post-TORRENT_PERSISTENCE_COLLAPSE) is the single source of truth that connects the torrent layer to Cinemata identity.

The user must have **absolute clarity, at all times, about what's being downloaded and what has been downloaded.**

## User flows

### Flow 1 — Find sources for a season

1. User navigates Theatre → picks a show (Community) → detail view loads.
2. User selects Season 5 in the season picker.
3. User clicks **[⬇ Find sources for Season 5]** at the top-right of the season-picker row.
4. Tankoban auto-fires a Tankorent search pre-scoped to `Community S5` (with `imdbId=tt1439629 + season=5` baked into the search context, not just the query string).
5. Results come back; the source-ranker scores them: **season packs first**, then per-episode singles. Within each tier, ranked by seeders × trust-uploader × quality match × size sanity.
6. Top-ranked source **auto-picks** and download starts immediately (Stremio-style; user didn't need to click anything else).
7. Sources panel on the right shows: the auto-picked row + a **[Pick different source]** link below it.
8. If user clicks **[Pick different source]** → Sources panel expands to the full ranked list with manual ⬇ Download buttons per row.

### Flow 2 — Find a season pack specifically

User clicks the **▤ layers icon** next to "Find sources for Season 5". Same flow as Flow 1 but the ranker restricts to pack results only (no per-episode singles). If no good pack exists, falls back to per-episode results with a small note.

### Flow 3 — Find sources for a single episode

User clicks a not-yet-downloaded episode row directly (e.g. S5E3). The Sources panel populates for that specific episode (`imdbId + season + episode` all baked in). Auto-pick + manual override identical to Flow 1.

### Flow 4 — Pause / Resume

User clicks ⏸ on an in-flight download → torrent paused, partial bytes preserved on disk, libtorrent resume data saved. **Pause survives app restart.** User reopens Tankoban next day → download row still in Downloads page, still paused, still at the same %. Click ▶ resumes from where it left off.

### Flow 5 — Cancel

User clicks ✕ Cancel on an in-flight or queued download → torrent removed from libtorrent, **partial bytes deleted from disk**, row vanishes from Downloads page, episode chip in detail view returns to ⬇ Download state. Honors the standing invariant: cancel = delete files.

### Flow 6 — Remove from Library

User clicks **Remove from Library** on a show's detail view → show entry removed from Theatre, **all related downloads cancelled with files deleted**, all per-episode index entries dropped. Includes in-flight + queued + completed. Single contract, no confirmation dialog (the action name says it).

### Flow 7 — Resume an unfinished download next session

App was closed mid-download. On next launch: Tankoban's boot path re-adds the torrent from its resume data (`.fastresume` file or repo's `resume_data` BLOB), libtorrent picks up where it left off, the row reappears in Downloads page with same identity + same progress.

## UI surfaces

### Surface 1 — Theatre detail view

Layout: hero banner + title block + **season-picker row** + two-column body (episodes left, Sources panel right).

**Season-picker row:**
- Left: `Season: [S5 ▾]` dropdown
- Right: **[⬇ Find sources for Season 5]** primary button (purple) + **[▤]** secondary icon for pack-only search

**Episodes table** (left, 1.4× width):
- Columns: `#` (number), thumbnail, title + meta, action
- Action button per row by state:
  - **Downloaded** → green **▶ Play** button (highlighted row)
  - **In flight** → **✕ Cancel** button (mini progress bar on thumbnail)
  - **Queued** → "Queued · slot #N" text + **✕ Cancel** button
  - **Not downloaded** → dimmed **⬇ Download** button (dimmed row at ~55% opacity)

**Sources panel** (right, 1× width):
- Header shows context: "Sources · for Episode 3 · Basic Intergluteal" (or "for Season 5" when season-level)
- Default state (post-auto-pick): single row showing the auto-picked source + `[Pick different source]` link
- Expanded state (after user clicks the link): full ranked list with per-row ⬇ Download buttons + seeder counts + trusted-uploader badges
- States: empty (pre-search), loading (spinner during search), populated, no-results (with retry option), error

**Top-right of hero banner:** `Remove from Library` link (red on hover).

### Surface 2 — Downloads page (sidebar-accessible)

Layout: Chrome-style download-history list, grouped by show.

**Top bar:**
- Title: "Downloads"
- Search field (filters across all rows)
- `Collapse all` / `Clear completed` actions

**Per-show section:**
- Section header is clickable to expand/collapse (▾/▸ chevron)
- Header content: show poster (40×56) + show name + season summary + per-show stats (`2 downloading · 1 completed · started 2 min ago`) + right-aligned total bytes + per-show progress %
- Default state: shows with active downloads expanded, completed-only collapsed

**Within a show section (when expanded):**
- For multi-season shows: inline `Season N` subdividers separate the row groups
- Episode rows: file icon + episode title + filename + progress bar + speed/ETA + per-row actions
- Row states + actions: downloading (⏸/✕), queued (✕ only), completed (📁 show-in-folder, ▶ play)
- Overflow: when a season has 10+ rows, show first 3-5 and a `…and N more in this season` collapse line

**Movie sections:** single-row, no season nesting, actions inline on the right.

**Order:** most-recently-actioned show at top (sort by `max(addedAt)` over the show's rows). Within a show, downloading rows above completed.

## Data model

### Repository writes (TorrentRepository)

**On `[Find sources for Season 5]` click + auto-pick:**
- `m_repo.upsertTorrent(TorrentRow{ hash, state=PendingEngineAdd, imdb_id="tt1439629", season=5, magnet_uri=<picked>, stream_group_id=<see below>, ... })`

**`stream_group_id` shape:**
- Season pack: `tankorent:tt1439629:s05:<timestamp>` (one group per season-level user action)
- Per-episode single: empty string (no group; episode mapping happens post-completion)

**On `onTorrentFinished`:**
- Existing publish path fires: `publishStreamBulkItemsForTorrent` for bulk-cohorts, `publishTankorentItemsForTorrent` for non-bulk
- BOTH paths now write to `stream_downloads_index` table (per-episode entries with `canonical_path + imdb_id + season + episode + state="complete" + info_hash`)
- Filename → episode S/E inference runs here for season packs (see Inference rules below)

**On `deleteTorrent(hash, deleteFiles=true)`:**
- `m_engine->removeTorrent(infoHash, true)` — libtorrent drops files
- `m_repo.removeTorrent(infoHash)` — repo row gone
- `m_streamDownloadIndex->evictBySourceGroup(stream_group_id)` if grouped, else `evictByImdb(imdb_id)` for the matching season

### Reads driving the UI

**Theatre detail view episode rows** read from:
- `m_streamDownloadIndex->lookupByImdbSeasonEpisode(imdb, season, ep)` → returns `state=complete | downloading | queued | absent`
- For `downloading`: also pulls progress % from `m_engine->status(infoHash).progress`
- Caches the lookup per detail-view paint; refreshes on `streamDownloadIndexChanged()` signal

**Downloads page rows** read from:
- `m_repo.listTorrents()` filtered to `state IN (PendingEngineAdd, Queued, Active, Paused, Completed)` where `Queued` is the new enum value introduced by this arc (see Behavior contracts → Concurrent cap)
- Joined with `m_engine->allStatuses()` for live progress on Active rows
- Joined with `m_streamDownloadIndex.listByImdb(imdb)` for the per-episode breakdown within each show
- Sort: descending by `MAX(rows.added_at) GROUP BY imdb_id` for show order

### Cinemata identity capture invariant

Every torrent row created via the Find sources flow **MUST** have:
- `imdb_id` non-empty (the show/movie's IMDB ID)
- `season` set (0 for movies; 1–N for series)
- `stream_group_id` non-empty if a season pack, empty otherwise
- `magnet_uri` non-empty (for resume across restart)
- `category` = `"videos"` (existing convention)

If any of these are missing at upsert time, **fail loudly** (log + reject the dispatch). No silent identity loss.

## Source ranking heuristic (for auto-pick)

Score = `0.45 × seeder_score + 0.25 × trust_score + 0.20 × quality_score + 0.10 × size_score`

- **seeder_score** = `min(1.0, log10(seeders + 1) / 3)` — flattens above ~1000 seeders
- **trust_score** = 1.0 if uploader in `TrustedUploaders` set (NTb, Joy, danke-empire, 1r0n, antiherogold), 0.5 otherwise; -0.5 for known-untrusted set; 0.0 default
- **quality_score** = 1.0 for `1080p`, 0.7 for `720p`, 0.3 for `2160p` (too-big penalty), 0.0 for unknown
- **size_score** = 1.0 if size in expected band for `quality × duration × episode_count`; falls off linearly outside the band

For **season packs**, a `pack_bonus = +0.15` to the final score (one click vs N clicks beats raw quality at the margin).

**Tiebreaker:** prefer newer `publishDate` (fresher upload usually = better source).

**Confidence gate:** if top score < 0.30, treat as "no good source found" — fall back to manual-pick mode (sources panel populates fully expanded; no auto-pick happens).

## Filename → episode S/E inference (for season-pack post-completion publish)

When a season pack completes, the torrent contains N files. Each file needs to map to a Cinemata episode in this show+season. Strategy:

**Pass 1 — Regex on each filename:**
- `[Ss](\d{1,2})[._\- ]?[Ee](\d{1,3})` matches `S05E03`, `s5e3`, `S05.E03`, etc.
- `(\d{1,2})x(\d{1,3})` matches `5x03`, `05x03`
- `Season\s*\d+.*Episode\s*(\d+)` matches `Season 5 Episode 3`
- First match wins.

**Pass 2 — Filename-order fallback:**
- If Pass 1 yields N matches and the torrent has N video files, register each in alphabetical-order × episode-order alignment.
- Only triggered when Pass 1's match count equals the torrent's video-file count.

**Pass 3 — Episode count from Cinemata:**
- Query Cinemata for the expected episode count for this season.
- If pack has exactly that count and Pass 1+2 disagreed, prefer Pass 2 (alphabetical order).
- If pack has wrong count → log warning + don't auto-publish; user sees "Episode mapping failed, please verify" notice in the Downloads page.

Pure-logic primitives (the regex matchers + count comparisons) live in `tankoban_tests`. Integration test uses a frozen Community S5 pack fixture.

## Behavior contracts

### Pause / Resume
- ⏸ on in-flight → libtorrent `pauseTorrent(hash)` → torrent state = `Paused` in repo + libtorrent
- Resume data saved automatically (libtorrent's `save_resume_data` alert path)
- App close → no extra action (paused torrent stays paused)
- App reopen → boot reconcile loop re-adds the torrent via `addFromResume`, libtorrent sees paused state in resume blob, torrent stays paused
- ▶ Resume → `resumeTorrent(hash)` → state = `Active`, libtorrent resumes from saved position

### Concurrent download cap = 1
- At most 1 torrent in `Active` state at any moment
- New downloads with no `Active` peer → start immediately (state `Active`)
- New downloads when there's already an `Active` peer → state `Queued` (new enum value, distinct from `PendingEngineAdd` which is engine-confirmation-pending and `Paused` which is user intent). Slot # in the UI = position in `Queued`-state list, 1-indexed (`Queued · slot #1` is the next-to-promote)
- On `Active` completion → queue picker selects the oldest `Queued` row by `added_at`, transitions to `Active`
- User pause of `Active` → next `Queued` does NOT auto-promote (pause is user intent, not "skip me"); only one row in `Active OR Paused` state across the system, but a `Paused` Active row holds the slot
- User cancel of `Active` → next `Queued` auto-promotes immediately
- User cancel of `Queued` → row gone, no promotion needed

### Cancel
- ✕ Cancel → `deleteTorrent(hash, deleteFiles=true)`
- Files deleted from disk via libtorrent's `delete_files` flag on remove
- Repo row removed via `m_repo.removeTorrent(hash)` (P5.4-wired call)
- `stream_downloads_index` entries removed via `evictBySourceGroup` or `evictByImdb` as appropriate
- Episode chip in detail view returns to `⬇ Download` state
- Queue slot freed; next `Pending` row promoted to `Active`

### Remove from Library
- Walks every torrent with `imdb_id` matching the show + every `stream_group_id` matching `tankorent:<imdb>:*`
- Calls `deleteTorrent(hash, true)` on each (cascade)
- Removes show entry from `StreamLibrary`
- All cascading deletes single-transaction at the repo layer (atomic — no half-removed state)

## Out of scope (for this arc)

- **Library scanner** — scanning `Desktop/Media/TV/<folders>/` for pre-existing media and linking it to Cinemata identities. Future Phase 2 arc.
- **Per-show download preferences** — preferred quality, preferred uploaders, audio/subtitle prefs per show. Future polish.
- **Multi-machine sync** — Tankoban on a second machine doesn't see the first's downloads. Out of scope.
- **Stream-server streaming integration** — Tankorent downloads are ALWAYS full-file (no progressive streaming of the in-flight bytes). Streaming is the stream-server's job.
- **Audiobook/Comics/Books mode integration** — this spec is Theatre-only. Books has its own (`BOOKS_STREMIO_PIVOT`, Agent 2) and Comics has its own (`COMICS_TANKOYOMI_STREAM_MERGER`, Agent 1).

## Acceptance criteria

1. User clicks `[Find sources for Season 5]` on Community detail view → top-ranked source auto-picks → download starts → episode rows in Community detail view reflect in-flight state → on completion, episode rows show ▶ Play → clicking ▶ plays the local file.

2. User clicks ▶ on a downloaded episode → file plays via the existing player. No "file not found" errors. No need to navigate elsewhere.

3. User clicks ⏸ on an in-flight download → close Tankoban → reopen → download row still present, still paused at same %. Click ▶ → resumes from saved position.

4. User clicks ✕ Cancel on an in-flight download → partial files removed from disk → episode chip in detail view returns to ⬇ Download. Verifiable via file-system inspection.

5. User clicks Remove from Library on Community → all S5 episode files deleted from disk → all S5 torrent rows removed from `torrents` table → all S5 entries removed from `stream_downloads_index` → Community card removed from Theatre library list.

6. The Downloads page in the sidebar reflects all of the above in real time: rows appear when downloads start, update with progress, transition to completed state, vanish on cancel, collapse to summary when show has only completed downloads.

7. Force-quitting Tankoban mid-download then reopening: paused state preserved, partial bytes preserved, download resumes on user click.

8. Search returns < 0.30 confidence score on top result → no auto-pick; Sources panel populates expanded with manual-pick option; no silent failure.

## Skills + tools used

- `superpowers:brainstorming` (this doc's authoring)
- `superpowers:writing-plans` (next step)
- `superpowers:executing-plans` (after plan lands)
- `superpowers:test-driven-development` (filename-inference primitives + source-ranker primitives)
- `superpowers:verification-before-completion` (per-task gates)
- `superpowers:systematic-debugging` (when bugs surface)
- `/build-verify`, `/simplify`, `/hemanth-language`

## Memory references

- `project_tankorent_as_foundation_vision.md` — indexer engine is permanent infra
- `project_stream_server_pivot.md` — streaming layer (out of scope, but coexists)
- `feedback_hemanth_picks_longer_path.md` — informed the architectural choice over patching
- `project_dev_control_bridge.md` — dev-bridge surface for smoke verification

## Codebase entry points

- `src/ui/pages/ShowView.cpp` — Theatre detail view; this arc adds the season-picker row + Sources panel changes
- `src/ui/widgets/SidebarDrawer.cpp` — sidebar entries; this arc adds a Downloads entry
- `src/ui/pages/` — new file `DownloadsPage.{h,cpp}` for the sidebar surface
- `src/core/TankorentSearchService.{h,cpp}` — existing headless dispatcher; this arc adds the season-pack ranker + auto-pick
- `src/core/stream/StreamDownloadIndex.{h,cpp}` — already SQLite-backed (Phase 3.4); this arc just consumes its existing API
- `src/core/stream/StreamLibrary.cpp` — `remove()` extended per Flow 6
- `src/core/torrent/TorrentClient.cpp` — `deleteTorrent` path extended per Flow 5; concurrency cap enforcement
- `src/core/manga/TrustedUploaders.{h,cpp}` — extends to a video-side uploader trust set (or new `VideoTrustedUploaders` parallel)
- New: `src/core/stream/SourceRanker.{h,cpp}` — pure-logic ranker per the heuristic above
- New: `src/core/stream/PackEpisodeInferrer.{h,cpp}` — pure-logic filename → S/E parser per the inference rules

## Risk register

- **Concurrency cap = 1 means slow user experience.** A 10-episode queue feels slow if the user's pipe is fat. Mitigation: log avg wait times after 4 weeks of use; revisit if Hemanth flags. Not a blocker; pace-pick respects Hemanth's explicit answer.
- **Auto-pick can pick wrong source occasionally.** Confidence gate (< 0.30 → manual) + [Pick different source] link both safety-net this. Worst case: user cancels + re-picks; not catastrophic.
- **Filename inference can mis-map episodes for weird packs.** Three-pass strategy + load-loudly-fail handling for ambiguous cases. Pure-logic tests cover known patterns; unknown patterns fail gracefully (no silent mis-mapping).
- **Season-pack and per-episode racing.** If user clicks Find sources for Season 5 (pack), then clicks Episode 3 manually before pack search completes — what happens? Spec answer: per-episode click cancels the pending pack search; latest user action wins.
