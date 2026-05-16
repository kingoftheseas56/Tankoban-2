# Tankoyomi Volume Pivot - 7-Case Smoke Matrix

**Date authored**: 2026-05-16
**Arc**: TANKOYOMI_VOLUME_PIVOT (13 phases shipped this session)
**Spec**: docs/superpowers/specs/2026-05-16-tankoyomi-volume-pivot-design.md
**Plan**: docs/superpowers/plans/2026-05-16-tankoyomi-volume-pivot.md
**Predecessor close pattern**: agents/audits/comics_tankoyomi_stream_merger_impl_audit_2026-05-14.md + agents/audits/tankorent_stream_integration_smoke_2026-05-15.md

## Why this doc exists

Phase 13 of the 13-phase arc is a 7-case smoke matrix that requires (a) real network downloads, (b) manual filesystem edits, and (c) visual-quality judgments. The 13 CODE phases shipped end-to-end this session; the smoke execution is deferred to a dedicated Hemanth-driven session that has the time + bandwidth + visual-verify lane to do the matrix justice.

This doc is the recipe for that session.

## Preconditions

1. Tankoban built clean via `build_and_run.bat` (current master after this session's Phase 1-12 commits + the eventual /commit-sweep landing the 13 RTC lines).
2. resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json present (Death Note worked-example from the 11-phase predecessor arc).
3. resources/manga_uploader_trust.json present (Phase 4 ship; deployed to out/resources/ via POST_BUILD copy).
4. Network connectivity to graphql.anilist.co (free, no auth, 90 req/min) + nyaa.si + WeebCentral (the actual WC scraper endpoint configured in MangaSourceRegistry).
5. tankoctl.exe at out/ with the Tankoban app launched in dev-control mode (build_and_run.bat auto-sets --dev-control).

## Smoke matrix (7 cases)

### Smoke 1 - Search renders single ranked list (no section split)

**Steps**:
1. Cold-launch via `build_and_run.bat`.
2. Click Comics tab.
3. Type "death note" into the search input.
4. Wait for AniList GraphQL response (~1-3 sec).
5. Screenshot the results.

**Expected**:
- Single ranked tile strip under a "RESULTS" header.
- No PREMIUM / MANGA / COMICS section splits visible.
- Death Note tile present (AniList anilistId=30005, English title "Death Note").
- Cover thumb loaded from AniList coverThumbUrl via the search-widget NAM-direct cache.

**Pass criteria**: single-list shape + Death Note tile rendered with cover.

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s1_search.png`

### Smoke 2 - Series view opens with volume list

**Steps**:
1. Click the Death Note tile from Smoke 1.
2. Wait for ComicsSeriesView to open (cache-hit if previously fetched; ~1-3 sec AniList background fetch on first open).
3. Screenshot.

**Expected**:
- ComicsSeriesView visible with banner pane (AniList bannerImage or coverFullUrl fallback).
- Title "Death Note" + meta line (year + status humanized "Completed" + format "Manga").
- Synopsis text below title.
- m_volumesTable with 12 rows (Death Note has 12 bound volumes).
- Vol X row NOT visible (Death Note is FINISHED).
- Per-vol art column populated from AniList volume art when available; otherwise series-cover fallback.

**Pass criteria**: 12 volume rows visible, banner painted, meta+synopsis rendered.

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s2_seriesview.png`

### Smoke 3 - Vol click populates Sources panel

**Steps**:
1. From Smoke 2's open series view, click Vol 1 row (col 0 / col 2 anywhere on the row).
2. Watch the right-pane Sources panel transition from "Select a volume to see sources" to the populated list.
3. Wait for nyaa response (~2-5 sec).
4. Screenshot.

**Expected**:
- Sources panel populated with at least 2 rows:
  - Catalog row: tier 1, "[Catalog] VIZ Digital - Volume 1" (or "1r0n - Volume 1" depending on the catalog's releaseEdition for Death Note), uploaderHint "1r0n" or "VIZ Digital", real fileSizeBytes from the Phase 11 catalog volume entry, magnetUri populated from PremiumCatalog::entryForAnilistIdAndVolume(30005, 1)
  - WeebCentral row: tier 99, "[WeebCentral] WeebCentral (synthesized)" at the bottom
- nyaa rows may or may not appear depending on what nyaa.si returns for "Death Note v1 (1r0n | Hox | VIZ Digital | KG Manga | DKThias)"; if returned, they sort between catalog (tier 1) and WeebCentral (tier 99).
- Source rows sorted: tier ascending, seeders descending within tier.

**Pass criteria**: Sources panel populated with catalog hit at top + WC packer at bottom.

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s3_sources.png`

### Smoke 4 - Catalog source download completes (REAL DOWNLOAD, ~5-10 min)

**Steps**:
1. From Smoke 3's populated panel, click the Catalog row's Download cell.
2. Wait for the real torrent download (libtorrent + Phase 4 finalize lifecycle).
3. Watch the volume row's Status cell update.
4. After completion, watch the cover thumbnail get replaced (Phase 12 cover extractor fires on volumeCompleted; ComicsPage routes to ComicsSeriesView::setVolumeCoverFromDisk).
5. Screenshot post-completion.

**Expected**:
- Real torrent activity visible in tankoctl logs (peer connect, piece-fetched events).
- Volume row Status transitions: "Not downloaded" -> (progress %) -> "Downloaded".
- .tankoban-part file appears in staging dir, validates via PremiumArchiveValidator, atomic-renames to .cbz at canonical path `<comics-root>/<series-folder>/<Vol-01-filename>`.
- Cover thumb in col 1 replaces the AniList pre-download art with the cbz-extracted art (PremiumCoverExtractor output).
- MangaDownloadIndex picks up the new entry on next refresh.

**Pass criteria**: cbz lands on disk + row flips Downloaded + cover replaced.

**Visual-quality verification** (Hemanth's lane): does the cbz open cleanly in the comic reader? Are the pages high-quality? Does the cover-thumb look correct?

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s4_download.png` (post-completion screenshot)

### Smoke 5 - WeebCentral fallback download completes (REAL HTTP FETCH + ZIP)

**Steps**:
1. Search a non-catalog series (e.g. "Chainsaw Man" or "Jujutsu Kaisen" - any popular series NOT in the Phase 11 Death Note worked-example catalog).
2. Click the series tile -> ComicsSeriesView opens.
3. Click a Vol row that has no catalog hit.
4. Sources panel shows ONLY the WeebCentral row (no Catalog, no nyaa if uploaders don't have it).
5. Click WeebCentral Download.
6. Watch ComicsPage's WeebCentralVolumePacker fire requestVolume with the AniListVolumeMapper-computed chapterIds.
7. Wait for HTTP fetch of each chapter's images + zip-on-the-fly (~3-7 min for a 30-page x 8-chapter volume).
8. Screenshot post-completion.

**Expected**:
- volumeProgress signal emissions visible in tankoctl logs (one per chapter completion).
- Staging dir at `<appData>/manga_premium_staging/wc_anilist_<id>_v<NN>/` filled with `<chapterIdx-4-zero-pad>_<pageIdx-4-zero-pad>.jpg` files.
- .tankoban-part assembled via QZipWriter, validated, atomic-renamed to .cbz at `<comics-root>/<series-slug>/Volume <NN>.cbz`.
- Cover thumb replaced post-completion (PremiumCoverExtractor on the WC-packed cbz).
- Row flips Downloaded.

**Pass criteria**: cbz from WC fetches lands on disk + visual fidelity acceptable per Hemanth's quality verdict.

**Visual-quality verification** (Hemanth's lane): WeebCentral is known to downscale to ~71% linear / ~50% pixel count of master (per `feedback_weebcentral_71pct_downscale_confirmed.md`). The fetched volume should look acceptable for a reader but visibly lower-fidelity than a 1r0n catalog source. The synthesized vol order + page sequencing should be correct.

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s5_wc_download.png`

### Smoke 6 - Bookmark + AniList offline survives

**Steps**:
1. From any open series view (e.g. Death Note from Smoke 2), click the Add-to-Library / Bookmark affordance (specific affordance TBD: may be a star icon, may be the existing Library button - Phase 7 stub doesn't expose this; flag if absent and add as Phase 13.1 inline polish OR confirm via Phase 10 landing-page bookmark store wiring).
2. Verify bookmark added: navigate back to Comics landing; bookmark should appear in the BOOKMARKED section.
3. Close Tankoban entirely (`taskkill /F /IM Tankoban.exe`).
4. Disconnect network (disable WiFi or unplug ethernet).
5. Relaunch Tankoban via `build_and_run.bat`.
6. Click Comics tab. Verify BOOKMARKED section still shows Death Note tile (rendered from AniListCache cached MediaPreview).
7. Click Death Note tile. ComicsSeriesView opens; renders from cache without crashing.
8. Screenshot.

**Expected**:
- Bookmark persists across restart (AniListCache::addBookmark writes `_bookmarks.json` to disk).
- Offline launch renders the bookmarked tile via AniListCache::bookmarkedPreviews().
- Series view background-refetch fails silently (AniListClient::seriesFailed); cached MediaDetail (from prior online open) renders the volume list cleanly.

**Pass criteria**: tile appears offline + series view opens without crash + UI doesn't show "loading forever" spinner.

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s6_offline.png`

**Known scope note**: if the Add-to-Library affordance isn't surfaced on the Phase 7 ComicsSeriesView stub yet, this smoke is partially blocked - the bookmark can be added manually by editing `_bookmarks.json` directly for the smoke. Phase 13.1 inline polish OR a tiny Phase 14 affordance is the right home for surfacing the bookmark toggle in the series view.

### Smoke 7 - Vol X auto-shrink on AniList refresh

**Steps**:
1. Pick an ongoing series with a Vol X (One Piece is the canonical example: anilistId=30013 if you have it cached, OR a fresh search adds it to cache).
2. Open One Piece series view. Confirm Vol X row exists at the bottom with N residual chapters.
3. Close the app.
4. Manually edit `<appData>/anilist_cache/series_30013.json`:
   - Increment `totalVolumes` from current value to current+1 (e.g. 111 -> 112).
   - Optionally bump `totalChapters` to a value that absorbs more chapters into bound vols.
5. Relaunch Tankoban.
6. Re-open One Piece series view.
7. Verify Vol X has shrunk (fewer chapters in it) AND a new Vol 112 row appears.
8. Screenshot.

**Expected**:
- AniListVolumeMapper::map() called on the freshly-loaded MediaDetail produces 112 bound vols + a smaller Vol X (if the total chapter count was bumped enough to absorb residual; otherwise just a smaller Vol X with the same chapter set redistributed).
- UI re-renders the volume table from the new mapper output.

**Pass criteria**: Vol X shrunk + new bound vol appears.

**Visual-quality verification** (Hemanth's lane): does the row transition look reasonable? Does the "Vol X" label correctly stay on the synthetic row not the new bound vol? Are the chapter-range subtitles updated?

**Evidence path**: `agents/audits/smoke_evidence/02XX_volume_pivot_s7_volx_shrink.png`

## MCP-driven UI flow vs. Hemanth-driven download verification

**Agent can drive** (Phase 13.1 dedicated session if budgeted):
- Smokes 1, 2, 3, 6 (UI-only or filesystem-edit-only)
- Click sequence + screenshot for Smoke 7 (the JSON edit is filesystem only)

**Hemanth-required**:
- Smoke 4 (real torrent download, ~5-10 min wall-clock + visual quality verdict on the resulting cbz)
- Smoke 5 (real WeebCentral HTTP fetch, ~3-7 min wall-clock + visual fidelity verdict on the downscaled-by-WC cbz)
- Visual-quality judgments on covers + page rendering across all smokes

## Rule 17 cleanup (post-smoke)

After every smoke session:
```cmd
taskkill /F /IM Tankoban.exe
taskkill /F /IM ffmpeg_sidecar.exe
```

Or use `scripts/stop-tankoban.ps1` which handles both + cleans up stranded stremio-runtime instances.

## MCP LOCK protocol

Claim at start:
```
MCP LOCK - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 13 7-case smoke]: ~30 min expected.
```

Release at end:
```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_VOLUME_PIVOT Phase 13 smoke]: <N>/7 PASS.
```

## Carry-forward to follow-up phase / arc

Any smoke that fails:
- Document the failure mode at `agents/audits/smoke_evidence/02XX_volume_pivot_s<N>_FAIL.png` + a short note explaining what broke.
- File a follow-up TODO at the relevant phase site OR open a new fix-TODO doc if the issue is cross-phase.

Most likely candidates for failure (per the in-arc concerns flagged in each phase RTC):
- **C4 from Phase 9**: NyaaRuntime fileIndex=-1 fallback unverified against TorrentVolumeProvider; Smoke 4 + 5 will exercise this for the first time on a real multi-vol nyaa pack.
- **C1 from Phase 5**: per-image NAM concurrent fan-out has no cap; Smoke 5 may exercise rate-limit behavior on WeebCentral CDN.
- **Phase 7 PHASE 9 visual check**: m_volumesTable col 6 Download QPushButton sizing under ResizeToContents may render oddly; Smoke 2 will reveal.
- **Phase 6 partial-migration state**: the burn-it-down migrator can leave a partial-backup state if interrupted; not a Phase 13 smoke target but worth watching across multiple smoke runs.

## Arc close criteria

The 13-phase TANKOYOMI_VOLUME_PIVOT arc is considered SHIPPED when:
1. All 13 code phases close with build_check.bat BUILD OK (DONE; all 13 RTC lines in chat.md pending Agent 0 sweep).
2. At least Smokes 1+2+3 (UI render path) pass via agent-driven MCP OR Hemanth visual confirmation.
3. Smokes 4+5 pass with Hemanth's visual-quality verdict on at least one real download per source kind (catalog + WC).
4. Smoke 6 confirms offline survives.
5. Smoke 7 confirms Vol X auto-shrink.

Smokes 4-7 are the deferred-to-dedicated-Hemanth-session lane.

## Lineage

- Predecessor arc TANKOYOMI_PREMIUM_MVP 11-phase shipped 2026-05-15; post-arc Hemanth-driven MCP smoke verified the rendered UI but surfaced the wrong-UI-shape feedback that prompted this 13-phase pivot.
- This pivot arc's CODE shipped 2026-05-16 in one session (Phases 1-12 + Phase 13 smoke recipe authored).
- Phase 13 smoke execution deferred to a follow-up Hemanth-driven session per the agent-vs-Hemanth-lane split documented above.
