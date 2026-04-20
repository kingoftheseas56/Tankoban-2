# Stream UX Parity TODO

**Owner:** Agent 4 (Stream & Sources). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/stream_mode_2026-04-15.md` as co-objective. Agent 3 cross-agent touches flagged per-phase.

**Created:** 2026-04-15 by Agent 0 after Agent 7's Stream mode behavioral audit.

## Context

This plan supersedes the earlier Tankostream basic-parity plan (which shipped as `STREAM_PARITY_TODO.md` Phases 1-6 + the picker UX rework, all in repo). That plan built the **plumbing** — addon protocol, aggregators, detail view, calendar, subtitles menu. This plan closes the **behavioral gaps** surfaced by Agent 7's audit at `agents/audits/stream_mode_2026-04-15.md` after Phases 1-6 shipped.

Agent 7's audit identified 2 P0s, 10 P1s, and 5 P2s. The dominant diagnosis: Tankostream's plumbing is correct-by-construction (addon protocol, aggregators), but moment-to-moment UX diverges from what a Stremio user expects at several specific touch points. The two P0s are both broken-flow bugs — catalog/search tiles can't reach play for non-library titles, and Continue Watching records progress but doesn't resume at the saved offset.

**Scope locked in by Hemanth (2026-04-15):**
- **Critical + Major tier only.** 5 phases, ~18 batches. P0s + continue-watching/binge + detail-view density + search polish + addon configurable support.
- **Calendar rework deferred.** Our 60-day forward tree works; it diverges from Stremio's month-grid UX but isn't broken. Revisit later.
- **Keyboard nav deferred.** Stream-side F1-F4 tab nav / arrow grid navigation / `s` focus-search not in scope. Player-side keybindings stay as-is.
- **Library management polish deferred.** Watch-state filter, type filter, custom collections — nice-to-have, not this TODO.
- **Error messaging polish deferred.** Current errors work; Stremio-style "no streams → install addons" nudge is polish.
- **Notifications still deferred** per original STREAM_PARITY_TODO.md.
- **Source picker resolution/language/HDR/seeds filters not included** — audit could not verify these as shipped Stremio picker behavior. If Hemanth confirms later, add as a follow-up.

**Delivery target:** the moment-to-moment Stream experience matches Stremio's "click anything, see detail, play it, keep watching, auto-advance episodes" flow without surprises.

## Objective

After this plan ships, a Stream-mode user can:
1. Click any home/catalog/search tile — including ones not in their library — and land on a detail view with real metadata.
2. Click Continue Watching and resume playback at the exact second they left off.
3. Finish an episode, see a countdown-to-next-episode overlay, and auto-advance without re-picking a source.
4. See the last-used source auto-selected (or auto-launched) on every new episode of a series — per Stremio's `last_used_stream` model.
5. Open detail view and see cast, director, trailer, runtime, genres, and per-episode descriptions/thumbnails, not just a title bar.
6. Type in the search bar and see results as they type, with history suggestions.
7. Install a configurable addon (one that requires setup, like a Real-Debrid-style addon) and complete its configure step from the UI.

## Non-Goals (explicitly out of scope for this plan)

- Calendar rework (our 60-day forward tree stays — note Stremio divergence in known-debt, revisit later)
- Keyboard navigation for Stream browse/detail/picker (player keybindings stay)
- Library watch-state/type filters + custom collections
- Stremio-style error-message polish ("no streams → install addons" nudges)
- Notifications / new-episode alerts (remain deferred per STREAM_PARITY_TODO.md)
- Source picker resolution/language/HDR/seeds filters (audit could not confirm as shipped Stremio behavior)
- Home row customization UI (reorder/hide catalog rows — polish)
- YouTube handler for trailer playback if non-trivial (defer to a separate YouTube-source batch if needed)
- Cloud sync / Trakt / multi-device (same as original plan)
- Chromecast, debrid backends — same as original plan
- Any work outside `src/core/stream/`, `src/ui/pages/stream*`, `src/ui/pages/StreamPage.*`, `src/ui/player/VideoPlayer.*` (minimal additions), plus the already-existing `src/ui/player/SubtitleMenu.*`

## Agent Ownership

All batches are **Agent 4's domain** (Stream & Sources). Phase 1 Batch 1.3 touches `src/ui/player/VideoPlayer.*` (Agent 3's domain) for an additive `startPositionSec` parameter — flagged below; coordinate with Agent 3 via chat.md before editing. Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — Core browse-to-play (the two P0s)

**Why:** Until these two ship, the normal Stremio flow is broken. Tiles from catalog browse and search either land on a metadata-empty detail view or toggle library instead of playing. Continue Watching records progress but starts at zero on resume.

### Batch 1.1 — Detail view accepts MetaItemPreview / MetaItem, not just StreamLibraryEntry

Audit P0 #1 citation: `src/ui/pages/stream/StreamDetailView.cpp:33-79` reads only `m_library->get(imdbId)`.

- Change `StreamDetailView::showEntry(imdbId, ...)` to `showEntry(MetaItemPreview preview, ...)` (or accept optional preview alongside the existing library-first path).
- When a catalog/home/search tile opens detail, pass its `MetaItemPreview` (title/year/type/poster/description are already in-hand).
- When a library tile opens detail, construct `MetaItemPreview` from `StreamLibraryEntry` on the fly — library path stays zero-cost.
- Kick off a `MetaAggregator::fetchMetaItem(imdbId, type)` on detail entry regardless of path, to populate the richer fields Phase 3 will render. Don't block the initial paint on it — it lights up additively.
- Files: `src/ui/pages/stream/StreamDetailView.h/.cpp`, `src/ui/pages/StreamPage.h/.cpp` (showDetail signature + all call sites from `StreamHomeBoard::itemActivated`, `CatalogBrowseScreen::tileOpened`, `StreamSearchWidget::result-click`, `StreamLibraryLayout::tile-click`).
- **Success:** open any catalog/home/search tile for a title NOT in library → detail view shows title/year/poster/description immediately from the tile's preview. No empty-detail crash. Library tiles unchanged.

### Batch 1.2 — Search result click → opens detail; Add/Remove library button in detail view

Audit P0 #1 citation: `src/ui/pages/stream/StreamSearchWidget.cpp:159-178` click-toggles-library.

- Change `StreamSearchWidget` tile click semantics from "toggle library" to "open detail" (via `StreamPage::showDetail`).
- In `StreamDetailView`, add an Add-to-Library / Remove-from-Library button in the header area — toggle state reflects `m_library->has(imdbId)` and persists via `StreamLibrary::add/remove`. Minimal styling in this batch (Phase 3 refines); functional is all that's needed here.
- Add a small "In Library" badge on search tiles (preserving the previous visual cue without the confusing click-to-toggle behavior).
- Files: `src/ui/pages/stream/StreamSearchWidget.h/.cpp` (click signal changes), `src/ui/pages/stream/StreamDetailView.h/.cpp` (add/remove button), `src/ui/pages/StreamPage.cpp` (wire search-click → showDetail).
- **Success:** click a search tile → opens detail (not a library toggle). Add-to-library button visible in detail, toggles state correctly, BooksPage-analog continue strip reflects change.

### Batch 1.3 — Stream resume seek (cross-agent: Agent 3)

Audit P0 #2 citation: `src/ui/pages/StreamPage.cpp:580-586` calls `setPersistenceMode(None)`; `src/ui/player/VideoPlayer.cpp:258-271` None-mode skips stored resume.

- Extend `VideoPlayer::openFile` (or add an overload) with `startPositionSec = 0.0` parameter. When non-zero, seek to that position once playback begins (after the first successful frame render), regardless of `PersistenceMode`. Keep `PersistenceMode::None` intact so stream progress still doesn't pollute Videos-mode continue strips.
- **Coordinate with Agent 3 before editing `VideoPlayer.*`.** Post a one-line heads-up in chat.md; edit is additive (new parameter with default), no existing-behavior regression.
- `StreamPage::onSourceActivated` (or the subtitle-fetch branch) reads saved progress for the pending episode/movie via `CoreBridge::progress("stream", episodeKey)`, extracts `positionSec`, passes it to `openFile(url, startPositionSec=positionSec)`.
- Files: `src/ui/player/VideoPlayer.h/.cpp` (Agent 3 territory, coordinate), `src/ui/pages/StreamPage.cpp` (read progress + pass start position), `src/ui/pages/stream/StreamPlayerController.h/.cpp` (thread the parameter through).
- **Success:** open a partially-watched stream from Continue Watching → playback starts at saved offset. Verify both magnet and direct-URL sources seek correctly. Stream progress continues to save correctly and doesn't leak into Videos continue strip.

### Phase 1 exit criteria
- Non-library catalog/search tiles reach detail + play without needing a library save first.
- Detail view has a working Add/Remove library button.
- Continue Watching resumes at the saved second.
- No regression: library-tile path, picker, buffering, stop/close flow all unchanged.
- `READY FOR REVIEW — [Agent 4, Phase 1]: Stream UX | Objective: Phase 1 per plan file radiant-foraging-flask.md + agents/audits/stream_mode_2026-04-15.md. Files: ...`. Agent 6 reviews against audit P0s #1 + #2.

---

## Phase 2 — Continue Watching + Binge + Source memory

**Why:** Stremio's signature feel is "keep watching" — press play on a show, the right next episode plays, with the right source, without friction. Our current Continue Watching only surfaces unfinished progress; it doesn't advance to the next unwatched episode after a completion, doesn't remember the source across episodes, and has no auto-next-episode flow. This phase closes that gap.

### Batch 2.1 — Next-unwatched-episode query helper

Audit P1 #1 citation: `src/ui/pages/stream/StreamContinueStrip.cpp:50-120` excludes finished, keeps latest-unfinished per show. No forward step.

- Add `QPair<int,int> StreamProgress::nextUnwatchedEpisode(seriesImdbId, episodeList)` returning `(season, episode)` or `(0,0)` if all watched. Input is the fetched episode list from `MetaAggregator::fetchSeriesMeta`. Walk in (season, episode) order; return the first episode whose `StreamProgress` record is either absent or `!finished`.
- Cache the result per series with short TTL (5 min) to avoid repeated scans — invalidate on any `progressUpdated` for that series.
- Files: `src/core/stream/StreamProgress.h` (new helper), `src/core/stream/StreamProgress.cpp` if split, or header-only inline.
- **Success:** given a series where episodes S1E1-S1E5 are all finished and S1E6 has no record, helper returns (1,6). All-finished series returns (0,0).

### Batch 2.2 — Continue Watching auto-advance

- Modify `StreamContinueStrip::refresh` logic: for each series with a most-recent finished episode (instead of excluding finished), check if there's a next-unwatched episode via Batch 2.1. If yes, display the card pointing at the next episode (not the finished one), with a "Continue with Episode N" hint.
- Keeps existing behavior for in-progress episodes: card points at the unfinished one with progress bar.
- When user clicks the card, `playRequested` fires with the next episode's (season, episode). StreamPage's existing flow takes over.
- Files: `src/ui/pages/stream/StreamContinueStrip.h/.cpp`.
- **Success:** finish S1E1 of a show → Continue Watching card now points at S1E2. Finishing S1E2 advances to S1E3. Series with all episodes watched drops out of the strip.

### Batch 2.3 — Series-level source memory

Audit P1 #2 citation: `StreamProgress.h:62-90` stores choices per exact episodeKey; `agents/chat.md:10480-10485` picker rework explicitly deferred cross-episode memory.

- Extend `StreamChoices` with a parallel `saveSeriesChoice(seriesImdbId, StreamPickerChoice)` / `loadSeriesChoice(seriesImdbId)` API that stores the last-used choice per series (in addition to per-episode). Movies use per-movie; series add a per-series layer.
- On `StreamPage::onSourceActivated`, write both the per-episode choice (existing) and the per-series choice (new) when the stream has `behaviorHints.bingeGroup` set — bingeGroup is the signal that "this source will work for other episodes too" per the addon protocol.
- Match-on-resume logic: when loading sources for a new episode, check per-episode first; if absent, check per-series; if the series-level choice's `bingeGroup` is present in the incoming stream list, highlight that card by default.
- Files: `src/core/stream/StreamProgress.h` (new namespace fns), `src/ui/pages/StreamPage.cpp` (dual-write on activate, dual-read on source load + highlight).
- **Success:** watch S1E1 with Torrentio's 1080p choice → open S1E2 → the matching 1080p card from the same release group is highlighted by default.

### Batch 2.4 — Source auto-launch on resume

Audit P1 #6 citation: `src/ui/pages/stream/StreamSourceList.cpp:88-142` only highlights saved choice; never auto-plays.

- When user navigates to detail and opens a series episode (or movie), if a saved choice (per-episode or per-series via Batch 2.3) exists AND the current stream list contains a matching source, start playback immediately without user click. Show a small "Resuming with last-used source" toast with a "Pick different" button that cancels auto-launch and falls back to picker.
- Auto-launch gates: must be within 10 minutes of last watch (otherwise user may want to re-pick), and must only apply when entering from Continue Watching or the same-series detail flow, not from a fresh catalog/search entry.
- Files: `src/ui/pages/StreamPage.cpp` (auto-launch branch in source-load callback), `src/ui/pages/stream/StreamSourceList.h/.cpp` (toast UI or defer toast to StreamPage).
- **Success:** click a Continue Watching card → playback starts without showing the picker. Click "Pick different" in the toast → picker reappears, auto-launch canceled.

### Batch 2.5 — End-of-playback next-episode overlay

Audit P1 #3 citation: `src/ui/pages/StreamPage.cpp:559-562` marks finished at 90%, no next-episode handling.

- On `VideoPlayer::progressUpdated` (or a new `playbackNearEnd`), when stream progress crosses a threshold (say, 95% OR within 60 seconds of duration, whichever hits first), StreamPlayerController checks if a next episode exists via Batch 2.1. If yes, pre-fetch its streams via `StreamAggregator::load(nextEpisodeRequest)` in the background.
- When playback ends (either player-ended signal or finished-at-90% crossing), show a "Next episode in 10s" overlay with a countdown, the next episode's title, and Play Now / Cancel / Back to Detail buttons. Ten-second default; Cancel resets to normal close flow.
- On countdown zero OR Play Now click: auto-open next episode using the pre-fetched streams + the per-series saved choice (Batch 2.3), via the same auto-launch path from Batch 2.4.
- Files: `src/ui/pages/stream/StreamPlayerController.h/.cpp` (pre-fetch + end-of-playback detection), `src/ui/pages/StreamPage.h/.cpp` (overlay widget — could be lightweight in-widget overlay, not a separate dialog).
- **Success:** finish an episode while watching a series → "Next episode in 10s" overlay appears → countdown fires → next episode plays automatically with the same source.

### Batch 2.6 — Shift+N player shortcut for manual next episode

Audit reference: Stremio Help Center documents `Shift + N` as "Play next episode" (`https://stremio.zendesk.com/hc/en-us/articles/360022892811`, lines 64-79).

- Add a Shift+N keybinding in the player context (KeyBindings.cpp or an equivalent stream-mode extension). When pressed during stream playback AND a next episode exists, triggers the same next-episode flow as Batch 2.5 (skipping the countdown).
- Files: `src/ui/player/KeyBindings.cpp` (additive shortcut), `src/ui/player/VideoPlayer.cpp` (dispatch the shortcut to a signal StreamPlayerController subscribes to, or route via StreamPage).
- **Coordinate with Agent 3 before editing `KeyBindings.cpp` or `VideoPlayer.cpp`.** Additive shortcut; no existing-behavior regression.
- **Success:** during stream playback of a series episode, press Shift+N → next episode plays. No-op if no next episode.

### Phase 2 exit criteria
- Continue Watching auto-advances to next unwatched after a finished episode.
- Opening a series episode from Continue Watching resumes at saved offset AND auto-launches with last-used source (Phase 1 Batch 1.3 + Phase 2 Batches 2.3+2.4 compose).
- End-of-episode overlay with countdown to next episode works.
- Shift+N manual next-episode works.
- Agent 6 review against audit P1s #1, #2, #3, #6.

---

## Phase 3 — Detail view density

**Why:** Our detail view shows title, year/type/rating, and a short description. Stremio shows cast, director, trailer, background art, runtime, genres, per-episode descriptions and thumbnails. Addon `MetaItem` already carries all of this — we just don't render it. This phase lights up the metadata we already have.

### Batch 3.1 — Background art + metadata badge row

Audit P1 #5 citation: `src/core/stream/addon/MetaItem.h:33-77` exposes `background`, `runtime`, `genres`, `links`, but `StreamDetailView.cpp:83-216` ignores them.

- Render `MetaItem.background` as a full-width hero image at the top of the detail view, with dark-gradient overlay fading into the existing content. Fallback to blurred poster if background is missing.
- Below the title row, add a metadata chip strip: year · runtime (from `MetaItem.runtime`) · genres (first 3, from `MetaItem.genres`) · IMDb rating chip · type badge.
- Files: `src/ui/pages/stream/StreamDetailView.h/.cpp`.
- **Success:** detail view has a visual hero + dense metadata row. No new data fetches — all fields come from the `fetchMetaItem` already kicked off in Phase 1 Batch 1.1.

### Batch 3.2 — Cast + director row — **DEFERRED 2026-04-15 → STREAM_UX_PARITY_ADD_LATER.md**

Moved to polish sweep after all phases close. See [STREAM_UX_PARITY_ADD_LATER.md](STREAM_UX_PARITY_ADD_LATER.md). Do not work on this batch until explicitly re-opened.

### Batch 3.3 — Description block with show-more

- Current description is a short single-line. Switch to a 3-line clamped paragraph with a "Show more" affordance that expands to full `MetaItem.description`. Small details — but Stremio's detail view gets this right and ours feels cramped.
- Files: `src/ui/pages/stream/StreamDetailView.cpp`.
- **Success:** long descriptions don't overflow the layout; users can read the full text when they want.

### Batch 3.4 — Episode rows with overview + thumbnail

Audit P1 #5 citation: `src/ui/pages/stream/StreamDetailView.cpp:299-359` shows number/title/progress only. `MetaAggregator` parses `MetaItem.videos[]` which includes overview + thumbnail.

- Extend the episode table rows to include thumbnail (64x36px, left side) + a second line with episode overview (italic gray text, truncated at 2 lines). Progress bar row stays.
- Pre-cache episode thumbnails in `{AppData}/stream_episode_thumbnails/{imdb}_{season}_{episode}.jpg` (reuses existing poster-cache pattern).
- Files: `src/ui/pages/stream/StreamDetailView.cpp` (row widget restructure), `src/core/stream/MetaAggregator.cpp` (pass through `thumbnail` + `overview` in the `StreamEpisode` struct), episode-thumbnail cache writer (new small helper).
- **Success:** each episode row is 64px tall with a thumbnail + 2-line overview. Missing thumbnails fall back to a placeholder chip.

### Batch 3.5 — Trailer playback — **DEFERRED 2026-04-15 → STREAM_UX_PARITY_ADD_LATER.md**

Moved to polish sweep after all phases close. See [STREAM_UX_PARITY_ADD_LATER.md](STREAM_UX_PARITY_ADD_LATER.md). Do not work on this batch until explicitly re-opened.

### Phase 3 exit criteria
- Detail view matches Stremio's density for runtime/genres/background (3.1) + description show-more (3.3).
- Episode rows show thumbnail + overview (3.4).
- Batches 3.2 (cast + director) + 3.5 (trailer) deferred to STREAM_UX_PARITY_ADD_LATER.md — not required for Phase 3 exit.
- Agent 6 review against audit P1 #5 scoped to the non-deferred batches.

---

## Phase 4 — Search polish

**Why:** Our search bar runs only on Enter/click, shows one flat strip, has no history, no live typing, no URL/magnet handling. Stremio's search is live with history and accepts pasted URLs for instant play or addon install.

### Batch 4.1 — Live search with debounce

Audit P1 #4 citation: `src/ui/pages/StreamPage.cpp:208-247` runs on Return/button only.

- Add a 300ms `QTimer` debounce on the search field's `textChanged` signal. After 300ms of no keystrokes with >=2 characters typed, fire `MetaAggregator::searchCatalog(query)` automatically.
- While searching, show a subtle spinner in the search bar (not a full-page `Searching...` state — Stremio's is unobtrusive).
- Clearing the field (zero characters) restores the home board state.
- Files: `src/ui/pages/StreamPage.h/.cpp` (debounce timer), `src/ui/pages/stream/StreamSearchWidget.h/.cpp` (inline spinner).
- **Success:** type "Brea" in the search bar → 300ms later, results for "Brea" appear. Keep typing → results update with each pause.

### Batch 4.2 — Search history

Audit P1 #4 reference: Stremio's guide screenshot at `https://howtomediacenter.com/en/stremio-guide/` lines 97-103 calls out an extended search bar with history.

- Persist the last 20 search queries to QSettings under `stream_search_history` (chronological, dedup on insert).
- On search field focus OR empty-query-plus-focused state, show a small dropdown below the field listing the most-recent 10 queries. Click one → populates the field and triggers search (same debounce path).
- Small "×" on each history row to delete individual queries. No "clear all" in this batch — simple is fine.
- Files: `src/ui/pages/StreamPage.h/.cpp` (dropdown widget, persistence helpers).
- **Success:** search for "Breaking Bad", close, reopen, focus search field → history dropdown shows "Breaking Bad" at top. Click → re-runs search.

### Batch 4.3 — Search bar URL/magnet handling

Audit P1 #4 reference: `https://howtomediacenter.com/en/stremio-guide/` lines 97-103 — Stremio's search accepts HTTP/magnet/addon URLs.

- Detect input patterns on Enter (or on debounced live-search, but only if the full value is parseable as a URL — regex guarded):
  - `magnet:?xt=urn:btih:...` → treat as an ad-hoc direct stream. Open player with the magnet via `StreamEngine::startStream({source: Magnet(...)})`. Skip detail view.
  - `http(s)://...mp4` / `...mkv` / `...m3u8` etc. (common video extensions) → treat as direct-URL play, launch player via the direct-URL path from existing Phase 4.3 (of the original plan).
  - `https://.../manifest.json` OR `stremio://...` → treat as an addon install request, opens `AddAddonDialog` pre-populated with the URL.
  - Anything else → normal text search.
- Show a small inline prompt when a URL is detected: "Play this stream" / "Install this addon" instead of letting the search go through as a text query.
- Files: `src/ui/pages/StreamPage.h/.cpp` (URL pattern detection + routing).
- **Success:** paste a magnet link → player launches. Paste a Stremio addon manifest URL → install dialog opens pre-filled. Paste an arbitrary text string → normal search runs.

### Phase 4 exit criteria
- Live search works with debounce.
- History dropdown on focus.
- URL/magnet pasting routes correctly.
- Agent 6 review against audit P1 #4.

---

## Phase 5 — Addon configurable support

**Why:** Manifest hints for `behaviorHints.configurable` and `configurationRequired` are already parsed by `AddonRegistry` but the UI doesn't surface them. Users installing configurable addons (Real-Debrid-style, API-key-requiring, etc.) have no way to complete the setup step. Stremio's install flow handles this via a `/configure` redirect.

### Batch 5.1 — Configure button on addon detail panel

Audit P1 #8 citation: `src/core/stream/addon/Manifest.h:11-17` + `AddonRegistry.cpp:213-218` parse the hints; `AddonDetailPanel.cpp:63-301` doesn't surface them.

- When `AddonDetailPanel` shows a selected addon whose `manifest.behaviorHints.configurable == true`, add a "Configure" button (gear icon or text button, follows `feedback_no_color_no_emoji` — gray/black/white only).
- Click opens `{addon.transportUrl}/configure` in the default browser via `QDesktopServices::openUrl`. Stremio's convention is that `/configure` is an HTML form served by the addon; the user completes it, the addon updates its internal state or issues a new manifest URL with configuration embedded.
- No in-app webview for configure — opening in the system browser is the standard Stremio desktop pattern and avoids hosting config UIs we don't control.
- Files: `src/ui/pages/stream/AddonDetailPanel.h/.cpp`.
- **Success:** install a configurable addon (e.g. a test addon with `behaviorHints.configurable=true`), select it in AddonManagerScreen → Configure button visible. Click → default browser opens to its configure page.

### Batch 5.2 — Configuration-required install flow

Audit P1 #8 reference: `https://stremio.github.io/stremio-addon-sdk/advanced.html` lines 320-326 — addons with `configurationRequired=true` cannot be used until configured. GitHub issue `Stremio/stremio-web#174` documents configure-as-install flow.

- In `AddAddonDialog`, after `AddonTransport::fetchManifest` returns successfully, check `manifest.behaviorHints.configurationRequired`. If true, the dialog changes its messaging from "Install" to "Configure & Install" and the button redirects to `{transportUrl}/configure` in the default browser instead of completing the install directly.
- Configure page typically ends by returning the user to a new manifest URL (with config embedded as base64 in the path, Stremio-standard pattern) or a "click to re-add in Stremio" link. We can't intercept that webflow. So: after opening the configure page, the dialog shows a "Paste configured manifest URL below" field where the user pastes the new URL from their browser; the dialog then retries install with that URL.
- This matches Stremio's manual-flow fallback for desktop apps that can't intercept deep links.
- Files: `src/ui/dialogs/AddAddonDialog.h/.cpp`.
- **Success:** install a `configurationRequired=true` addon → dialog shows configure flow → user completes in browser → pastes new manifest URL → addon installs correctly.

### Phase 5 exit criteria
- Configurable addons surface a Configure button in their detail panel.
- Configuration-required addons route through the configure-first install flow.
- Agent 6 review against audit P1 #8.

---

## Cross-Cutting Concerns

### Ordering
Phase 1 is the critical path — ship first, no parallelism. Once Phase 1 is green and drift-checked, Phases 2-5 can sequence: 2 → 3 → 4 → 5. Phase 2 depends on Phase 1 Batch 1.3's `startPositionSec` mechanism. Phase 3 depends on Phase 1 Batch 1.1's `fetchMetaItem` kick-off. Phases 4 and 5 are mostly independent.

### Agent roles per phase
| Phase | Agent | Cross-agent touches |
|-------|-------|---------------------|
| 1 | Agent 4 | Batch 1.3 touches `src/ui/player/VideoPlayer.*` (Agent 3). Coordinate via chat.md heads-up; additive `startPositionSec` parameter, no regression risk. |
| 2 | Agent 4 | Batch 2.6 adds Shift+N to `src/ui/player/KeyBindings.cpp` (Agent 3). Coordinate; additive shortcut. |
| 3 | Agent 4 | None |
| 4 | Agent 4 | None |
| 5 | Agent 4 | None |

### Agent 6 review gates
At each phase exit: `READY FOR REVIEW — [Agent 4, Phase X]: Stream UX | Objective: Phase X per plan file radiant-foraging-flask.md + agents/audits/stream_mode_2026-04-15.md. Files: ...`. Agent 6 reviews against this plan + the audit as co-objective. No phase moves to commit (Rule 11) until `REVIEW PASSED`.

### Rule 11 commit gates
Per-batch `READY TO COMMIT` in chat.md. Agent 0 batches commits at phase boundaries, not per-batch, unless a batch is particularly risky (Phase 1 Batch 1.3 qualifies — cross-agent VideoPlayer change).

### What's retained from prior plans (do NOT re-open)
- STREAM_PARITY_TODO.md Phases 1-6 shipped work: addon protocol, aggregators, calendar, subtitles menu, picker rework. Not touched by this plan.
- All the deferred items from the original plan remain deferred (notifications, Trakt, Chromecast, debrid, HTML subtitle overlay, etc.).

---

## Critical Files Modified

**New (to be created):**
- `src/ui/pages/stream/` — no new files (all Phase 3 density changes restructure existing StreamDetailView widget).
- Possibly a new small helper for episode-thumbnail caching (Batch 3.4) if not folded into MetaAggregator directly.

**Modified (by phase):**
- Phase 1: `src/ui/pages/stream/StreamDetailView.h/.cpp`, `src/ui/pages/StreamPage.h/.cpp`, `src/ui/pages/stream/StreamSearchWidget.h/.cpp`, `src/ui/pages/stream/StreamPlayerController.h/.cpp`, `src/ui/player/VideoPlayer.h/.cpp` (Agent 3 — additive).
- Phase 2: `src/core/stream/StreamProgress.h` (+ `.cpp` if needed), `src/ui/pages/stream/StreamContinueStrip.h/.cpp`, `src/ui/pages/StreamPage.h/.cpp`, `src/ui/pages/stream/StreamPlayerController.h/.cpp`, `src/ui/pages/stream/StreamSourceList.h/.cpp`, `src/ui/player/KeyBindings.cpp` (Agent 3 — additive Shift+N), `src/ui/player/VideoPlayer.cpp` (Agent 3 — additive signal).
- Phase 3: `src/ui/pages/stream/StreamDetailView.h/.cpp`, `src/core/stream/MetaAggregator.cpp`.
- Phase 4: `src/ui/pages/StreamPage.h/.cpp`, `src/ui/pages/stream/StreamSearchWidget.h/.cpp`.
- Phase 5: `src/ui/pages/stream/AddonDetailPanel.h/.cpp`, `src/ui/dialogs/AddAddonDialog.h/.cpp`.

**Reference (read-only during implementation):**
- `agents/audits/stream_mode_2026-04-15.md` — the audit. Phase-by-phase objective source.
- `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/` — especially `meta_details.rs` (last_used_stream, guess_stream, next_video/next_streams), `player.rs` (binge matching), `continue_watching_preview.rs`.
- `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/meta_item.rs` — canonical shape for the metadata fields Phase 3 renders.
- Existing `STREAM_PARITY_TODO.md` at repo root — shipped Phase 1-6 surface Agent 4 builds on.

---

## Risks & Mitigations

1. **Phase 1 Batch 1.3 touches Agent 3's `VideoPlayer.*`.** Mitigation: additive `startPositionSec` parameter with default value 0.0 preserves all existing call sites. Coordinate via a one-line chat.md heads-up before editing; Agent 3 reviews the additive change if desired.

2. **Phase 2 auto-launch could surprise users who want to re-pick a source.** Mitigation: the "Pick different" toast in Batch 2.4 gives immediate escape. Gate auto-launch to "within 10 minutes of last watch" so an overnight return goes through the picker normally.

3. **Phase 2 end-of-episode overlay timing is subjective.** Mitigation: 10s countdown is Stremio-standard. Cancel button always available. Pre-fetch of next-episode streams runs in the background during the last minute of playback to minimize latency when the countdown fires.

4. **Phase 3 episode thumbnails can balloon cache size.** Mitigation: cap per-series to the visible episodes count; purge on library remove (same hook that already clears stream_posters). LRU eviction at a reasonable total size threshold.

5. **Phase 4 URL pattern detection can mis-classify borderline strings.** Mitigation: require explicit `magnet:` / `http://` / `https://` / `stremio://` scheme prefix. Text without a URL scheme is always a search query. User can always prepend a scheme to force URL handling.

6. **Phase 5 Batch 5.2 depends on Stremio's /configure convention being honored by 3rd-party addons.** Mitigation: many addons implement it imperfectly. If a particular addon doesn't return a re-addable URL, the user can manually copy it from their browser into the dialog's paste field — documented behavior, not a bug.

7. **Scope creep.** Explicitly enumerated non-goals above. Calendar rework, library polish, keyboard nav, error-message polish, and home row customization UI all deliberately deferred. Agent 6 will flag any batch that drifts.

---

## Verification

End-to-end test plan after each phase:

**Phase 1:** (1) Open a catalog tile for a title NOT in library → detail shows real metadata, not empty. (2) Click a search tile → detail opens (not library toggle). (3) Start a stream, watch 2 minutes, close. Open from Continue Watching → playback resumes at the 2-minute mark.

**Phase 2:** (1) Finish S1E1 of a tracked series. Continue Watching card now points at S1E2. (2) Click the card → stream auto-launches with the S1E1 source (if bingeGroup matches). (3) Watch to 95% → next-episode overlay appears with countdown. (4) Let it fire → S1E2 plays. (5) Press Shift+N during S1E2 → S1E3 plays.

**Phase 3:** Detail view shows background art, cast, director, genres, runtime, description with show-more, episode rows with thumbnails + overviews, trailer button (when available).

**Phase 4:** (1) Type "brea" in search → results appear 300ms later. (2) Focus empty search field → history dropdown appears. (3) Paste a magnet link → player launches. (4) Paste a Stremio addon manifest URL → install dialog opens pre-filled.

**Phase 5:** (1) Install a `configurable=true` addon → Configure button visible on its detail panel → opens configure URL in browser. (2) Install a `configurationRequired=true` addon → Configure & Install flow runs, user pastes back configured URL, install succeeds.

**Build verification per Rule 6:** every batch ends with `build_and_run.bat` → exit 0 → feature smoke-tested in the running app → Agent 4 pastes build exit code + one-line smoke result in chat.md before posting `READY TO COMMIT`. No batch declared done on compile alone.
