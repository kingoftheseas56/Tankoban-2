# Tankorent Download Panel — Full Audit (2026-05-27)

**Auditor:** Agent 4 (Stream + Tankorent)
**Trigger:** Hemanth's two screenshots (Torrentio Sources tab vs. Tankorent Download panel showing "One Piece" results on a Community Season 4 page) + the realization that the TANKORENT_QUALITY_AND_QUEUE brainstorm re-designed already-shipped functionality.
**Surface audited:** `TheatreDownloadPanel` (the screenshot-2 panel) + its feed chain `UnifiedPackSearchEngine` → `StreamAggregator::searchPacks` → Tankorent indexers.

## Context: what already exists (and works)

The download panel reached via the layers-toggle in `StreamDetailView` is `TheatreDownloadPanel` (THEATRE_DOWNLOAD_OVERHAUL, shipped 2026-05-16/17). It already has, and these all function:
- **Pack classification + badges** — `PackClassifier` runs in `UnifiedPackSearchEngine::normalizeAndEmit` (`classify(raw.title)`); each result carries `Season Pack` / `Complete Series` / `Multi-Season` / `Single Episode` classification. Badges render.
- **Type filter chips** (`All / Complete Series / Multi-Season / Season Pack`) — filter `m_filteredPacks` by `labelForType(classification.type)` (TheatreDownloadPanel.cpp:879-882). Work.
- **Source dropdown** (`All Sources / Nyaa / PirateBay / 1337x / YTS / EZTV / ExtraTorrents`) — feeds `sourceFilter` → `searchPacks` `wants()` gate (StreamAggregator.cpp). Gates which indexers run.
- **Score-descending sort** — `combinedScore` from `QualityScorer` (TheatreDownloadPanel.cpp:903-906). Works.
- **Per-show sequential queue** (Phase 1 this wake) — panel `downloadRequested` → StreamPage:891 → `startDownload(infoHash, dispatchConfig)` with `dispatchConfig.imdbId`+`season` populated (StreamPage.cpp:909-910, 998). Phase 1's lane gate engages correctly with a real lane key.

**Implication:** Phases 3 + 4 of the TANKORENT_QUALITY_AND_QUEUE plan were re-designs of shipped, working functionality. Only Phase 1 (sequential queue) was genuinely new and is correctly wired to this panel.

## Defects (real, open)

### DEFECT 1 — Stale results painted during a new search (the screenshot bug)
**Severity:** High (makes the panel look broken / wrong-show).
`TheatreDownloadPanel::openFor()` (cpp:209-246) clears the data models (`m_packs`, `m_filteredPacks`, `m_tileChecked`) but does NOT clear the visual `m_packList` widget — only `reset()` (cpp:274) does. So when you open a new show's panel, the previous search's rendered rows stay painted on screen through the "Searching sources…" phase until new results arrive and repaint. The data layer is correct (both `UnifiedPackSearchEngine::onTankorentPacksAvailable` cpp:81 and `TheatreDownloadPanel::onPackResults` cpp:548 guard on imdbId/season mismatch), so this is purely stale paint, not bad data.
**Repro:** search a heavy title (One Piece) in one show's panel, then open a different show (Community S4) → One Piece rows linger during the new search.
**Fix shape:** clear `m_packList` (and repaint empty `m_filteredPacks`) inside `openFor()`, mirroring `reset()`. ~1-2 lines.

### DEFECT 2 — Source dropdown change does not re-run the search
**Severity:** Medium (this is Hemanth's original complaint #3).
`TheatreDownloadPanel::onSourceComboChanged()` updates `m_sourceFilter` but deliberately does NOT re-fire the search — its own comment says the change "takes effect on the NEXT search() call (panel dismiss + reopen)." So changing the source picker appears to do nothing, and there's no Search button to force a re-run.
**Fix shape (decision needed):** either (a) re-fire `m_searchEngine->search(...)` at the end of `onSourceComboChanged` (the comment's stated concern about orphaning an in-flight fan-out is already handled — `UnifiedPackSearchEngine::search` force-emits the prior search's terminal `searchComplete` on a new call, cpp:54-55), or (b) add an explicit "Search" button next to the dropdown. (a) is cleaner and matches the spec's click-only-but-responsive intent; (b) matches the brainstorm's literal "search button" ask. Recommend (a).

## Findings (not defects, but decisions / gaps)

### FINDING 3 — This panel is the real "Nyaa parity" surface, capped at 25/indexer
`searchPacks` calls `indexer->search(query, kPackSearchPerIndexerLimit)` with `kPackSearchPerIndexerLimit = 25` (StreamAggregator.cpp:675). Aggregate raw ceiling = 6 indexers × 25 = 150, deduped. **Phase 2's limit fix (80→300) was on `TankorentPage` — the standalone tab — NOT this panel.** If Hemanth's "doesn't show what Nyaa shows" complaint was about this panel (likely), the lever is `kPackSearchPerIndexerLimit`, untouched. Note: this is *pack* search (season/complete-series shaped queries), so 25 pack-candidates/indexer is less starved than it sounds — but it's the surface to tune if depth is wanted here.

### FINDING 4 — TorrentsCsv excluded from pack search
`searchPacks` dispatches nyaa / piratebay / 1337x / yts / eztv / exttorrents — 6 of 7 indexers. `TorrentsCsvIndexer` is not dispatched. Possibly intentional (torrents-csv is weak for pack-shaped queries) but undocumented. Confirm intent.

### FINDING 5 — Western-title sparsity is expected, not a bug
Community on Nyaa returns ~0 (Nyaa is anime-focused), same as Daredevil. A real Community S4 search draws from PirateBay / 1337x / ExtraTorrents / EZTV. The 2 results in the screenshot were stale One Piece rows (Defect 1), not a real Community result set. Post-Defect-1, confirm Community S4 actually surfaces decent results from the Western-content indexers.

## Mapping to Hemanth's 4 original complaints

1. **"Cannot identify full-season / multi-season torrents"** → Already works in this panel (classification + badges + Type chips). Not broken. Possibly Hemanth didn't notice them, or wants them more prominent — needs his eyes.
2. **"Doesn't show actual results like Nyaa"** → Two parts: (a) Defect 1 made results LOOK wrong (One Piece on Community) — the headline; (b) Finding 3's 25-cap limits depth. Phase 2's fix missed this surface.
3. **"No search button on source change"** → Defect 2. Real, open.
4. **"Sequential downloads"** → Phase 1 built + correctly wired to this panel. Pending Hemanth smoke.

## Recommended next actions (smallest-first, no code until Hemanth greenlights)

1. **Defect 1** — clear `m_packList` in `openFor()`. 1-2 lines. Kills the wrong-show illusion. Highest value/effort ratio.
2. **Defect 2** — re-fire search in `onSourceComboChanged` (option a). Closes complaint #3.
3. **Finding 3** — if Hemanth confirms depth matters in this panel, bump `kPackSearchPerIndexerLimit` (mirror of the Phase 2 lever, on the correct surface this time).
4. **Finding 4** — confirm TorrentsCsv exclusion intent; add it to the fan-out if wanted.

**Net:** the arc shrinks to two small bug-fixes (Defects 1+2) on an already-built panel, plus an optional parity tune (Finding 3) on the correct surface. Phase 1 (sequential queue) stands as the only genuinely-new infrastructure and is correctly wired here.
