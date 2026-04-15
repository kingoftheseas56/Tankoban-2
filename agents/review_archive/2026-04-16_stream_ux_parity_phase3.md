# Archive: STREAM_UX_PARITY Phase 3 — Detail view density

**Agent:** 4 (Stream & Sources)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (triple):**
- **Primary (plan):** `STREAM_UX_PARITY_TODO.md:167-207` — Phase 3 (Batches 3.1 + 3.3 + 3.4)
- **Secondary (audit):** `agents/audits/stream_mode_2026-04-15.md` P1 #5 (detail view ignores MetaItem fields + episode thumbnails)
- **Defer-tracker:** `STREAM_UX_PARITY_ADD_LATER.md` — Batches 3.2 + 3.5 tracked deferrals with implementation notes

**Outcome:** REVIEW PASSED 2026-04-16 first-read pass. 0 P0, 0 P1, 8 non-blocking P2 + 3 optional Qs. Audit P1 #5 closed.

**Shape:** 3 batches shipped (3.1 hero + chips, 3.3 description clamp, 3.4 episode thumbnails/overview) + 2 tracked deferrals (3.2 cast+director, 3.5 trailer). Phase 3 exit criteria at TODO:203-207 explicitly accept the defers.

---

## Parity (Present)

### Batch 3.1 — Hero + chips
- Root layout restructure to `VBox(topBar, hero, HBox(leftCol, rightCol))` at StreamDetailView.cpp:175-244.
- `m_heroLabel` 240px fixed height, hidden by default, `#101010` placeholder.
- `renderHeroPixmap` at :833-869: KeepAspectRatioByExpanding + center-crop + QLinearGradient baked into ARGB32_Premultiplied canvas (not QGraphicsEffect — zero-cost per frame).
- `applyHeroImage` at :757-797: disk-cache → async download → poster fallback → hidden. "Absent is better than broken."
- Disk cache at `{AppData}/Tankoban/data/stream_backgrounds/{imdb}.jpg`.
- `downloadBackgroundArt` at :799-830: shared NAM + QPointer guard + 10s timeout + UA header.
- Stale-callback guards (imdb match at :708 + QPointer).
- Idempotency via `bgSource` property at :721-726.
- `applyChips` at :871-908: year/runtime/genres(first 3 joined by middle-dot)/rating/type. `m_infoLabel` hidden iff any chip shows.

### Batch 3.3 — Description clamp + Show more/less
- `setDescription` centralization at :917-932 for both showEntry (preview) + onMetaItemReady (full-meta) paths.
- Dynamic clamp via QFontMetrics::boundingRect + lineSpacing*3 at :934-962.
- Toggle auto-hidden when text fits.
- Per-title reset at :927-930 prevents stale expanded state.
- Meta enrichment at :739-741 only overwrites on non-empty diff.
- Qt::TextSelectableByMouse for copy UX.

### Batch 3.4 — Episode thumbnails + overview
- StreamEpisode struct extended with overview + thumbnail; parseSeriesEpisodes populates.
- 5-column layout at :347-369 (# | Thumb 64×36 | Title+Overview stacked | Progress | Status), 64px row height.
- `setSortingEnabled(false)` — binge navigation intent.
- On-disk cache `{AppData}/stream_episode_thumbnails/{imdb}_{s}_{e}.jpg` follows poster pattern.
- Dual-QPointer + imdb stale-callback guards on async download.

### Deferred batches (tracked)
- Batch 3.2 (cast+director) + Batch 3.5 (trailer) marked `DEFERRED 2026-04-15 → STREAM_UX_PARITY_ADD_LATER.md`.
- Defer doc has full ask + success criteria + implementation notes (specific file lines + already-parsed data pointers).
- Rationale: "nice to have" enrichments that don't block Phase 4/5, don't break layout if missing, can be picked up in isolation post-parity.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (non-blocking):

1. `updateDescriptionClamp` doesn't re-run on resize — description clamp stale at new window widths.
2. Poster-cache path literal duplicated at :776-779 + :789-792 — future consolidation opportunity.
3. `downloadBackgroundArt::usePosterFallback` parameter plumbed but unused.
4. `applyChips` type labels ("Series"/"Movie") hardcoded English (no `tr()`).
5. Hero gradient alpha hardcoded — dark-backgrounds-over-dark-gradient could look washed.
6. Hero target width fallback 800px floor on first paint before layout.
7. `setSortingEnabled(false)` on episode table — deliberate, noting.
8. `setScaledContents(false) + AlignCenter` — defense-in-depth for sub-width pixmaps.

---

## Agent 6 verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 4, STREAM_UX_PARITY Phase 3]** 2026-04-16 first-read pass.

Audit P1 #5 closed. Three-batch delivery with two tracked deferrals. Tracked-defer doc pattern (full ask + criteria + pickup notes) is exemplary scope management.

**Optional questions:**
1. Resize-aware description clamp — add resizeEvent hook or accept as polish debt?
2. Shared cache-path helper for poster / background / thumbnails namespaces?
3. `downloadBackgroundArt::usePosterFallback` — honor or drop?
