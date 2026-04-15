# Archive: Stream UX Parity Phase 1 (Batches 1.1 + 1.2 + 1.3)

**Agent:** 4 (Stream & Sources)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `STREAM_UX_PARITY_TODO.md:55-95` — Phase 1 batch list + exit criteria
- **Secondary (audit):** `agents/audits/stream_mode_2026-04-15.md:114-118` — audit P0 #1 (browse/search-to-play broken for non-library titles) + P0 #2 (Continue Watching no-resume)

**Outcome:** REVIEW PASSED 2026-04-15 first-read pass. 0 P0, 0 P1, 6 non-blocking P2 observations. One optional clarification question.

**Shape:** three batches, thirteen files, closes both audit P0s in one phase. Cross-agent coordination for Batch 1.3's additive `VideoPlayer::openFile` parameter landed cleanly.

---

## Scope

**Batch 1.1** reshapes `StreamDetailView::showEntry` to accept an optional `MetaItemPreview` hint so catalog/home/search tiles can paint immediately without library lookup. **Batch 1.2** flips search-tile click semantics from toggle-library to detail-open + adds Add/Remove library button in detail header. **Batch 1.3** extends `VideoPlayer::openFile` with a caller-supplied `startPositionSec` parameter honored regardless of `PersistenceMode`, threading through `StreamPage::onReadyToPlay` which reads the saved stream-progress offset.

Out of scope: Phase 2+ (Continue Watching auto-advance + binge + end-of-episode overlay + Shift+N); functional verification (Hemanth's smoke).

---

## Parity (Present)

### Batch 1.1
- `showEntry` signature extended with `std::optional<MetaItemPreview> previewHint` at StreamDetailView.h:46-49.
- Preview-hint branch paints immediately from hint fields at StreamDetailView.cpp:58-65 (name, releaseInfo, type, imdbRating, description).
- Library-tile fallback preserved at :67-85 (`m_library->get(imdbId)`).
- `fetchMetaItem(imdbId, displayType)` fires regardless at :105-107 — additive rich-metadata fetch.
- StreamPage dual `showDetail` overloads at :398 (imdbId-only) + :414 (MetaItemPreview).
- All four entry surfaces route correctly: CatalogBrowseScreen::metaActivated (:172-174), StreamHomeBoard::metaActivated (:311-314), StreamSearchWidget::metaActivated (:339-342), StreamLibraryLayout::showClicked via explicit lambda (:121-122).
- Idempotency guard on `showDetail(QString)` at StreamPage.cpp:405-409 dedupes Qt's single-click+double-click double-fire.

### Batch 1.2
- Search tile click flipped from toggle-library to emit `metaActivated` at StreamPage.cpp:339-342.
- `m_libraryBtn` Add/Remove toggle in detail header at StreamDetailView.cpp:162-177. Grayscale-only per `feedback_no_color_no_emoji`.
- `[inLibrary="true"]` property-driven styling at :172-173.
- External library-change reconciliation via `libraryChanged → refreshLibraryButton` at :185-188.
- `m_lastPreviewHint` stashed for constructing `StreamLibraryEntry` on Add-to-Library click.
- Button visibility gated on valid showEntry at :176.

### Batch 1.3
- `VideoPlayer::openFile` extended with `startPositionSec = 0.0` parameter at :224-226. Default zero preserves all existing call-sites.
- Three-way priority at VideoPlayer.cpp:259-280:
  1. Caller-supplied `startPositionSec > 0.0` → honored regardless of PersistenceMode.
  2. `PersistenceMode::LibraryVideos` → read "videos" domain progress.
  3. `PersistenceMode::None` → zero.
- Comment block at :260-269 documents the Stream-mode semantic explicitly.
- `StreamPage::onReadyToPlay` reads "stream" domain progress at :622-631.
- Sane resume-gate thresholds at :628-630: `savedPos > 2.0 && savedDur > 0 && savedPos < savedDur * 0.95`.
- `player->openFile(httpUrl, {}, 0, streamResumeSec)` threads position through at :632.
- `PersistenceMode::None` preserved at :613 (no "videos" domain pollution).
- Defensive persistence-mode reset on `onStreamFailed` at :648-652.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (all non-blocking):

1. `_currentEpKey` stored as Qt dynamic property vs typed member — stringly-typed key invites future typos across three call sites.
2. `fetchMetaItem` fires on every `showEntry` call including repeat opens — depends on MetaAggregator internal caching (Q1 asked).
3. `[inLibrary="true"]` property selector may need `style()->unpolish/polish` cycle on property change under some Qt builds.
4. `m_lastPreviewHint` never cleared on library-path showEntry — theoretical cross-title stale-hint edge if Add-to-Library handler doesn't guard on imdbId consistency.
5. Resume threshold `0.95` matches 90% finished-flag with 5% buffer (Stremio parity); could be a named constant for tunability.
6. StreamPage::showDetail dual-overload + explicit-lambda pattern is clean; documentation is good.

---

## Agent 6 verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 4, Stream UX Parity Phase 1]** 2026-04-15 first-read pass.

Both audit P0s closed. Phase 2 (Continue Watching + binge + end-of-episode overlay + Shift+N) stands by for review when Agent 4 ships READY FOR REVIEW.

**Optional clarification (Q1):** Does `MetaAggregator::fetchMetaItem` cache by imdbId? If yes, P2 #2 is closed by design.
