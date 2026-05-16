# TANKOYOMI_VOLUME_PIVOT - Pre-Smoke Fix TODO

**Owner**: Agent 1 (Comic Reader) - may dispatch Trigger D Codex for specific scopes
**Authored**: 2026-05-16, post Agent 7 Trigger C audit
**Source audit**: [agents/audits/tankoyomi_volume_pivot_arc_audit_2026-05-16.md](agents/audits/tankoyomi_volume_pivot_arc_audit_2026-05-16.md)
**Validate-pass**: completed 2026-05-16 by Agent 1 - all 3 P0s + both action-bearing P1s confirmed against the actual code at the cited file:line refs
**Status**: queued, awaiting Hemanth ratification + executor assignment
**Gate**: must close BEFORE the 7-case smoke matrix runs ([docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md](docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md))

---

## Why this TODO exists

The 13-phase TANKOYOMI_VOLUME_PIVOT arc shipped code-clean (every phase build_check.bat BUILD OK) but Agent 7's end-to-end audit caught three blockers per-phase reviewers couldn't see, plus two action-bearing P1s. The audit traced each finding to specific lines; the validate pass confirmed all 5 against the live working tree.

If we run the smoke matrix as-is:
- Smoke 2 dies (zero volume rows render on a live AniList fetch)
- Smoke 4 dies for nyaa-runtime rows (priorities-off, never downloads)
- Smoke 5 dies for WC packer (cbz lands on disk but UI never reflects it + not in landing's DOWNLOADED section)
- Smoke 6 blocked (no bookmark button affordance in the series view)

This fix-TODO closes those gaps surgically. Total estimated lift: ~250-350 LOC across 5-6 files. Single executor session if dispatched cleanly.

---

## Phase A - P0 blockers (must close before any smoke)

### Phase A.1 - AniListVolumeMapper synthesizes chapters when list is empty

**Finding**: P0-1. AniList GraphQL query asks for scalar `chapters` + `volumes` counts, NOT the per-chapter list. `MediaDetail.chapters` is therefore always empty after a live fetch. `AniListVolumeMapper::map` exits empty at the `if (detail.chapters.isEmpty()) return out;` guard. Result: zero volume rows render in ComicsSeriesView.

**Validated locations**:
- Query: [src/core/manga/anilist/AniListClient.cpp:54-55](src/core/manga/anilist/AniListClient.cpp#L54-L55) - scalar `chapters` + `volumes`
- Handler: [AniListClient.cpp:274-278](src/core/manga/anilist/AniListClient.cpp#L274-L278) - stores `totalChapters`/`totalVolumes`, comment "populated by callers"
- Mapper empty-exit: [AniListVolumeMapper.cpp:22](src/core/manga/anilist/AniListVolumeMapper.cpp#L22)
- Caller using empty result: [src/ui/pages/comics/ComicsSeriesView.cpp:361](src/ui/pages/comics/ComicsSeriesView.cpp#L361)

**Fix shape**: extend `AniListVolumeMapper::map` to synthesize numbered chapters when `detail.chapters` is empty AND `detail.totalChapters > 0`. Generate N `AniListChapter` entries with `number = QString::number(i)` for i in 1..totalChapters, leave `title` empty + `boundVolume = -1`. The existing bucket logic then distributes them into `totalVolumes` bound vols via the chapters/volumes-floor heuristic.

Place the synthesis at the top of `map()`, right BEFORE the current empty-chapters guard. Keep the guard for the "no data at all" case (`totalChapters == 0 AND chapters.isEmpty()`).

**Test surface**:
- Add a 7th test case to `tests/core/manga/AniListVolumeMapperTest.cpp`: `LiveFetchShapeProducesVolumesFromTotalsOnly` - Death Note shape (12 vols, 108 chapters, FINISHED, empty chapters list) -> 12 rows of 9 chapters each.
- Existing 6 tests still pass (they all populate the chapters list explicitly).
- Run `cmake --build out --target tankoban_tests; cd out && ctest -R AniListVolumeMapperTest --output-on-failure`. Expect 7/7 PASS.

**Estimated lift**: ~15 LOC in mapper + ~15 LOC test case.

---

### Phase A.2 - TorrentVolumeProvider tolerates fileIndex=-1 via metadata-time match

**Finding**: P0-2. The Phase 9 nyaa-runtime dispatch synthesizes a `PremiumCatalogEntry` with `fileIndex=-1`, expecting "pick the only cbz" fallback. The provider does not have that fallback. It skips negative file indices in priority assignment and piece-range checks, starving the download.

**Validated locations**:
- Synth: [src/ui/pages/ComicsPage.cpp:1540-1558](src/ui/pages/ComicsPage.cpp#L1540-L1558)
- Files-array discarded: [src/core/manga/TorrentVolumeProvider.cpp:183-190](src/core/manga/TorrentVolumeProvider.cpp#L183-L190) - `Q_UNUSED(files)`
- applyUnionPriorities skips negatives: [TorrentVolumeProvider.cpp:251-260](src/core/manga/TorrentVolumeProvider.cpp#L251-L260)
- Piece-range filter rejects -1: [TorrentVolumeProvider.cpp:297-298](src/core/manga/TorrentVolumeProvider.cpp#L297-L298)

**Fix shape**: insert a `resolveUnresolvedFileIndices(infoHash, files)` step in `TorrentVolumeProvider::onMetadataReady` BEFORE `applyUnionPriorities`. For each Inflight with `fileIndex < 0`:

1. Walk the `files` QJsonArray (which Phase 9 ignored) looking for cbz candidates. Match strategy:
   - If exactly one `.cbz` file in the torrent: use that fileIndex + fileSizeBytes.
   - If multiple `.cbz` files: match by `iff.cbzFileName` (the synth path sets this to `<title> v<NN>.cbz`); failing that, match by volume-number tokens in the filename (v01, Vol 1, Volume 1 - same regex shape as Phase 10's continue-strip extraction).
   - If still no match: emit `volumeFailed(seriesId, vol, "metadata_file_match_failed", <files-array-debug>)` + remove from bucket. Do NOT call `applyUnionPriorities` for this Inflight.

2. Once `fileIndex` is set, compute `pieceStart`/`pieceEnd`/`fileSizeBytes` from libtorrent. Look at how the engine exposes per-file piece ranges - probably `TorrentEngine::fileByteRangesOfHavePieces` or a sibling helper. Grep `src/core/torrent/TorrentEngine.h` for the relevant accessor. If no accessor exists for "give me the piece range covering file N", add a small one.

3. Update the Inflight in-place + call applyUnionPriorities.

**Constraint**: the existing catalog path (where `fileIndex` is pre-known) MUST be untouched. The resolution only fires for `fileIndex < 0`.

**Test surface**: integration-shape, no unit test slot in current `tankoban_tests` target (this lives in the provider's libtorrent-driven path). Defer to the smoke matrix's Smoke 4 nyaa run for end-to-end verification. Add a debug log line at resolve-success and resolve-fail so smoke evidence can grep for the path-taken.

**Estimated lift**: ~50-80 LOC in TorrentVolumeProvider + possibly ~20 LOC in TorrentEngine if a new piece-range accessor is needed.

---

### Phase A.3 - Wire provider volumeCompleted into ComicsPage + MangaDownloadIndex + ComicsSeriesView

**Finding**: P0-3. ComicsPage only listens for `volumeCoverReady` from the providers. There is no listener for `volumeCompleted`, `volumeProgress`, or `volumeFailed`. WC packer's volumeCompleted is not registered into MangaDownloadIndex. Nyaa synth has empty chapterIds so the provider's internal `registerVolume` block skips. Even if a cbz lands on disk, the table row still says "Not downloaded" and the landing's DOWNLOADED section won't surface it.

**Validated locations**:
- Cover-only connects: [src/ui/pages/ComicsPage.cpp:203-210](src/ui/pages/ComicsPage.cpp#L203-L210), [ComicsPage.cpp:322-329](src/ui/pages/ComicsPage.cpp#L322-L329)
- Grep result: `volumeCompleted|volumeProgress|volumeFailed` in ComicsPage.cpp returns only one stale comment at line 176, zero `connect(...)` matches
- Provider registerVolume gated on non-empty chapters: [TorrentVolumeProvider.cpp:501-508](src/core/manga/TorrentVolumeProvider.cpp#L501-L508)
- Row hardcoded "Not downloaded": [src/ui/pages/comics/ComicsSeriesView.cpp:421-424](src/ui/pages/comics/ComicsSeriesView.cpp#L421-L424)

**Fix shape**: introduce a `ComicsPage::onProviderVolumeCompleted(seriesId, volumeNumber, cbzPath, sourceKind, anilistId, chapterIds)` adapter slot. Connect to:
- `TorrentVolumeProvider::volumeCompleted` - capture `sourceKind = Catalog OR NyaaRuntime` via the Inflight context (the panel's UnifiedSourceRow.kind was lost by the time the provider emits; either stash it on the Inflight at requestVolume time, OR have ComicsPage maintain a sidecar map keyed by (seriesId, vol)).
- `WeebCentralVolumePacker::volumeCompleted` - sourceKind = WeebCentralPacker always.

The slot:
1. If sourceKind in {NyaaRuntime, WeebCentralPacker}: explicitly call `m_mangaDownloadIndex->registerVolume(...)` with the chapterIds + anilistId-derived series fields. (Catalog already self-registers; skip the double-register.)
2. Resolve anilistId from seriesId via the existing `anilistIdForDownloadEntry` helper (handles both real catalog seriesIds + the `anilist_<N>` slug).
3. If the active ComicsSeriesView is currently rendering this anilistId, call a new public slot `ComicsSeriesView::setVolumeDownloadState(int volNumber, const QString& cbzPath, bool downloaded)` that updates the row's Progress + Status cells. Otherwise no-op (Comics landing's `refreshLibraryStrips` will pick it up on next tick via the index's `entriesChanged` signal).

Also: connect `volumeFailed` to a similar adapter slot that surfaces the failure as a status-bar message OR a row-level error indicator. `volumeProgress` connects to a row-progress-cell updater (optional for v1 close; can defer to v1.1 if scope tight, just leave the Status column on "Downloading...").

**ComicsSeriesView changes**:
- Add `void setVolumeDownloadState(int volNumber, const QString& cbzPath, bool downloaded)` public slot.
- The slot walks `m_volumesTable` rows finding the one with matching volume number (col 0 text), sets col 5 Status cell text to "Downloaded" + stores cbzPath in col 0's UserRole+1 for the future row-click "open reader" branch.
- Also: re-render the row's Download button into a chevron-style "Open" icon (P1-3 ties in here - see Phase B.3).

**Estimated lift**: ~40-60 LOC in ComicsPage (the adapter slot + 2 new connects + sourceKind sidecar map) + ~25 LOC in ComicsSeriesView (the new slot).

---

## Phase B - P1 (must close before smoke 6, recommended before smoke 4/5)

### Phase B.1 - Bookmark Add/Remove button on ComicsSeriesView

**Finding**: P1-1. AniListCache exposes addBookmark/removeBookmark/isBookmarked but no UI in the new series view calls them. Smoke 6 (offline bookmark survives) blocked unless the smoke manually edits `_bookmarks.json`.

**Validated locations**:
- Cache API present: [src/core/manga/anilist/AniListCache.h:36-39](src/core/manga/anilist/AniListCache.h#L36-L39)
- No call sites: grep `addBookmark|removeBookmark|isBookmarked` in `src/ui/pages/comics/` returned zero matches.

**Fix shape**: add a top-right "Add to library / In library" toggle button on ComicsSeriesView's hero pane. Mirrors StreamDetailView's library button shape ([src/ui/pages/stream/StreamDetailView.cpp:1805-1916](src/ui/pages/stream/StreamDetailView.cpp#L1805-L1916) for the reference). On click: if `m_cache->isBookmarked(m_currentAnilistId)`, call `removeBookmark`, else `addBookmark`. Refresh button text/icon on `m_cache->bookmarksChanged` signal + on `showSeries` entry.

**Estimated lift**: ~30 LOC in ComicsSeriesView.

### Phase B.2 - Fix the orphaned col-6 Download button on the volume table

**Finding**: P1-2. The visible "Download" button in each volume row emits `openVolume(volNum, "")` which ComicsPage doesn't listen to. Click does nothing.

**Validated locations**:
- Button connect emits openVolume: [src/ui/pages/comics/ComicsSeriesView.cpp:432-436](src/ui/pages/comics/ComicsSeriesView.cpp#L432-L436)
- ComicsPage doesn't connect openVolume: grep `ComicsSeriesView::openVolume` in ComicsPage.cpp returns zero hits

**Fix shape**: rip out the Download button entirely from col 6. The row-click handler (`onVolumeCellClicked`) already populates the sources panel - that's the canonical action. Keep col 6 as a state-icon column (empty for "Not downloaded", a chevron-arrow "Open" icon for "Downloaded") so the user gets visual feedback on row state. Click on the chevron triggers `openVolume(volNum, cbzPath)` with the populated cbzPath; ComicsPage connects that to the comic reader.

Couples with Phase A.3 (the row's downloaded state) and Phase B.3 (the reader open path).

**Estimated lift**: ~20 LOC in ComicsSeriesView + ~10 LOC ComicsPage connect.

### Phase B.3 - Downloaded-state row paint + open-local-volume path

**Finding**: P1-3. ComicsSeriesView has no MangaDownloadIndex pointer, no row-state resolver, no open-local signal. Even when Phase A.3 wires the post-download row-state flip, the COLD path (user opens a series whose volumes are already downloaded from a prior session) shows them as "Not downloaded".

**Validated locations**:
- StreamDetailView's reference patterns: [src/ui/pages/stream/StreamDetailView.h:97-102](src/ui/pages/stream/StreamDetailView.h#L97-L102), [StreamDetailView.cpp:1423-1438](src/ui/pages/stream/StreamDetailView.cpp#L1423-L1438)
- ComicsSeriesView Progress/Status hardcoded empty + "Not downloaded": [ComicsSeriesView.cpp:417-424](src/ui/pages/comics/ComicsSeriesView.cpp#L417-L424)

**Fix shape**:
1. Add a `MangaDownloadIndex*` ctor arg to ComicsSeriesView (non-owning, like the existing 4 pointers).
2. In `renderDetail`, for each VolumeRow, query the index for "is vol N of this series downloaded? if so, what's the cbz path?". The index's existing `entriesForAllSeries` walks all entries; we'd add a tighter `entryForSeriesAndVolume(seriesId, vol)` (the catalog already has the same-named helper; mirror it on the index).
3. Set the row's col 5 Status cell to "Downloaded" / "Not downloaded" based on the query.
4. Set col 6 to the chevron-arrow icon (Downloaded) or empty (Not downloaded).
5. Click on the chevron OR on a Downloaded row anywhere -> emit `openVolume(volNumber, cbzPath)` which ComicsPage routes to the comic reader.

**Estimated lift**: ~50 LOC across ComicsSeriesView + ~10 LOC index helper.

---

## Phase C - P2/P3 (defer to v1.1 unless smoke surfaces them)

Per Agent 7's audit:
- P2-1: stale catalog cover repaint guard weak for non-prefixed seriesIds. Tighten in v1.1.
- P2-2: AniList throttle blocks UI thread. Already inline TODO from Phase 1 polish. Replace with QTimer pump in v1.x.
- P3-1: ASCII drift in 2 files. Sweep in a janitorial pass.

Not gating the smoke matrix.

---

## Execution options

**Option A (recommended): Agent 1 ships Phase A + B in one session**

Single-session sequential: A.1 (mapper) -> A.2 (provider resolve) -> A.3 (completion wiring) -> B.1 (bookmark button) -> B.2 (button rip-out) -> B.3 (downloaded-state). ~250-350 LOC total. build_check.bat after each phase. Single RTC at fix-TODO close. ~2-3 hours of focused work.

**Option B: Trigger D Codex for A.1 + A.2 (pure-backend), Agent 1 ships A.3 + B.1 + B.2 + B.3 (UI-shaped)**

Codex Trigger D dispatches the two pure-logic backend fixes (mapper synthesis + provider metadata-time resolve) in parallel with Agent 1's UI work. Tighter total wall-clock but requires Trigger D coordination overhead. Recommend if Hemanth wants Codex to share the load.

**Option C: Each phase as its own RTC**

If the fix shape grows beyond estimate, break out per-phase RTCs for cleaner /commit-sweep batching.

---

## Verification gate (pre-smoke checklist)

After Phase A + B close:
1. `build_check.bat` BUILD OK
2. `ctest -R AniListVolumeMapperTest` PASS 7/7 (P0-1's new test case)
3. Cold launch via `build_and_run.bat`. Open a series (Death Note from the worked-example catalog OR a fresh AniList search). Verify volume table renders N rows where N = totalVolumes from AniList. This validates P0-1 visually.
4. Verify the row's Download button is gone (or replaced by a state icon per Phase B.2).
5. Verify the bookmark Add/Remove button appears on the series view hero (Phase B.1).
6. Verify a prior-session downloaded volume (if any exist post-burn-it-down + previous smoke run) shows as "Downloaded" with the open-reader icon (Phase B.3).

P0-2 + P0-3 verification depends on the smoke matrix Smoke 4 + 5 themselves; can't verify pre-smoke without running a real download.

---

## Forward references

Once Phase A + B close, the smoke matrix at [docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md](docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md) is unblocked. Hemanth runs the 7-case matrix in a dedicated session; results feed back into either an arc-close confirmation OR a small Phase D if the smoke surfaces new gaps.

---

## Lineage

- Code shipped: 13-phase TANKOYOMI_VOLUME_PIVOT arc (2026-05-16, this session). All 14 RTC lines in chat.md tail awaiting Agent 0 sweep.
- Audit: Agent 7 Trigger C, 2026-05-16, written to [agents/audits/tankoyomi_volume_pivot_arc_audit_2026-05-16.md](agents/audits/tankoyomi_volume_pivot_arc_audit_2026-05-16.md).
- Validate pass: Agent 1, 2026-05-16, this TODO is the output. All 5 findings (3 P0 + 2 P1) confirmed against the live working tree at the cited file:line refs.
- Smoke recipe: [docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md](docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot-smoke.md), authored 2026-05-16, gates the arc close.
