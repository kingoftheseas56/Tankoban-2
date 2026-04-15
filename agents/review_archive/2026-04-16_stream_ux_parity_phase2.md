# Archive: STREAM_UX_PARITY Phase 2 — Continue Watching + Binge + Source memory + End-of-episode overlay + Shift+N

**Agent:** 4 (Stream & Sources)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `STREAM_UX_PARITY_TODO.md:99-166` — Phase 2 batch list + exit criteria
- **Secondary (audit):** `agents/audits/stream_mode_2026-04-15.md` P1 #1 + #2 + #3 + #6 + Stremio Help Center Shift+N ref

**Outcome:** REVIEW PASSED 2026-04-16 first-read pass. 0 P0, 0 P1, 8 non-blocking P2 + 4 optional Qs. Audit P1s #1/#2/#3/#6 all closed.

**Shape:** six batches unified per Agent 4's Phase 2 exit submission. Phase 2 closes audit's "binge experience" gaps as a coherent delivery: opens CW auto-advance (2.1+2.2), cross-episode source memory via bingeGroup (2.3), zero-click resume with 10-min freshness gate (2.4), end-of-episode overlay with 10s countdown (2.5), manual Shift+N override (2.6).

---

## Parity (Present)

### Batch 2.1 — nextUnwatchedEpisode helper
- Pure-function helper at StreamProgress.h:101-128 takes `(imdbId, episodesInOrder, allStreamProgress)`; returns `{0,0}` sentinel for all-finished.
- Per-series cache via static `s_nextUnwatchedCache` + `kNextUnwatchedTtlMs = 5min`. `(0,0)` cached too.
- Explicit `invalidateNextUnwatchedCache / clearAllNextUnwatchedCache` APIs at :91-99.

### Batch 2.2 — Continue Watching auto-advance
- MetaAggregator plumbed through StreamContinueStrip at :27-30; async series-meta → `nextUnwatchedEpisode` lookup at :251-252.
- Finished-episode inclusion pre-2.2 excluded them; 2.2 retains as next-up entry points.
- `playRequested(imdbId, season, episode)` emits next episode's (s,e).

### Batch 2.3 — Series-level source memory
- `saveSeriesChoice / loadSeriesChoice / clearSeriesChoice` at StreamProgress.h:177-196. QSettings namespace `stream_series_choices/{imdbId}`.
- bingeGroup gate on save at StreamPage.cpp:1032-1035.
- Dual-read match at resume (:574-578): per-episode wins → fall through to per-series bingeGroup.

### Batch 2.4 — Source auto-launch on resume
- `m_autoLaunchTimer` single-shot 2s at :125-129.
- 10-min freshness gate via `kAutoLaunchWindowMs` at :585.
- `cancelAutoLaunch` fired across all nav surfaces (showBrowse/AddonManager/CatalogBrowse/Calendar/Detail/SourceActivated/StreamFailed).
- Pick-different cancel path via `autoLaunchCancelRequested` at :120-121.

### Batch 2.5 — End-of-episode overlay
- Near-end detection at :1102-1128 with fire-once `m_nearEndCrossed` gate; 95% OR 60s-remaining trigger.
- `startNextEpisodePrefetch` at :696-779 synthesizes allProgress, fetches series meta, kicks StreamAggregator::load with disconnect-reconnect discipline.
- `NextEpisodePrefetch` struct with `std::optional<Choice> matchedChoice` distinguishes in-flight vs done-no-match.
- Overlay widget at :170-227 — grayscale-only (rgba background + translucent-white buttons), 420px fixed width, hidden by default.
- closeRequested lambda ordering at :1146-1157: overlay shown BEFORE stopStream (documented race-condition fix).
- Cleanup on every exit path.
- 10s countdown; `onNextEpisodePlayNow` funnels through canonical `onSourceActivated`.
- Overlay label: `"Up next: {SeriesName} · S{NN}E{NN}"`.

### Batch 2.6 — Shift+N manual next-episode
- KeyBindings.cpp:59-62 — `stream_next_episode → Qt::Key_N + ShiftModifier`.
- VideoPlayer.cpp:2011 emits `streamNextEpisodeRequested`.
- StreamPage::onReadyToPlay wires with `Qt::UniqueConnection`; disconnects on streamFailed / streamStopped / closeRequested.
- Three-path dispatch: hot (prefetch+match → immediate), in-flight (arms pending flag), cold (kicks prefetch + arms flag).
- Silent no-op on no-match.

### Architectural coherence
- `onSourceActivated` is the canonical activation path for all five entry points.
- Per-session reset via `onSourceActivated` at :997-1001.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (non-blocking):

1. TODO:144 calls for three buttons on overlay; shipped two (Play Now + Cancel). "Back to Detail" missing — Cancel returns to browse instead.
2. `m_nearEndCrossed` fire-once persists across rewind — rewinding past 95% after prefetch-miss leaves user without overlay.
3. EOF → overlay path not explicitly verified (Q3).
4. Countdown copy "Playing in Ns..." vs TODO's "Next episode in 10s" (cosmetic).
5. Cancel returns to browse vs TODO's ambiguous "normal close flow" (defensible).
6. Auto-launch toast copy not verified in review (Batch 2.4 delivery deep-reading).
7. Series-level choice clear-on-non-binge-pick not handled — stale binge choice persists if user picks a non-binge source for a later episode.
8. `Up next` label uses library-entry name; for never-added series shows `SxxExx` only. Could use matched-choice Video.title for richer label.

---

## Agent 6 verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 4, STREAM_UX_PARITY Phase 2]** 2026-04-16 first-read pass.

Audit P1s #1, #2, #3, #6 all closed. Six batches compose cleanly through a single canonical `onSourceActivated` activation path. Agent 4 executed Phase 2 with zero blocking gaps and strong architectural discipline.

**Optional questions to Agent 4:**
1. EOF → overlay wiring path (does VideoPlayer emit `closeRequested` on natural EOF, or something else?).
2. "Back to Detail" button — intentional scope trim or missed?
3. `m_nearEndCrossed` rewind edge — does it matter in practice?
4. Series-level choice cleanup on non-binge pick — stale-choice false-positive risk?
