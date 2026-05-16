# THEATRE_DOWNLOAD_OVERHAUL — Brainstorm + Design

**Date:** 2026-05-16
**Vision owner:** Hemanth
**Brainstorm scribe:** Agent 4 (Stream mode)
**Status:** Brainstorm complete (5 batches × 4 questions × 20 ratified decisions). Pending Codex (Agent 7) UI/UX expansion per gov-v4 Rule 20 in-place co-authorship pattern before `/superpowers:writing-plans` fires.

---

## 1. Why this arc

The TANKORENT_STREAM_INTEGRATION arc (shipped 2026-05-15 across 4 Codex Trigger D dispatches + 3 Claude-side phase bundles) wired end-to-end "click pack → real torrent download → ✓ in show-view → local playback" flow. Functionally correct, smoke-verified. But the UX seam is loud: the moment a user picks a pack, the experience drops out of Theatre mode and lands in Tankorent's `AddTorrentDialog` — a power-user file tree + priority sliders + category combos that looks nothing like Theatre. Then the download progress lives on the Tankorent page, not the show-view.

Hemanth's vision: **the entire pick-and-download flow should be as contained to Theatre mode as possible.** No leaking out to Tankorent's UI. The pack picker, the file/scope selection, and the in-progress download surface all live INSIDE the show-view, styled like Theatre.

There's also a coverage gap. The current picker fans out per-season queries only. For older shows (Sopranos, Twin Peaks, Seinfeld, classic anime), season-specific packs don't exist — these properties only ship as Complete Series torrents. Without full-show pack support we're cutting off probably half the catalog.

And the current picker lists single-episode packs mixed with full-season packs with no visual distinction — user picks "the one with the most seeders" and ends up with one episode of a 10-episode season. Pack-type awareness needs to be a first-class concern.

This arc fixes all three: Theatre containment, full-show pack support, pack-type intelligence.

## 2. TL;DR (the whole thing in three paragraphs)

The season-header gets ONE button (`Download`) that replaces both the existing "Download Season" (Stremio bulk-cohort) AND "Download via Tankorent" (Phase D indexer picker). Click it → the Sources panel on the right slides out, the new Theatre-native download panel slides in. The panel queries Stremio addons + Tankorent indexers in parallel, normalizes results into one list, classifies each pack (Single Episode / Multi-Episode / Season Pack / Multi-Season / Complete Series), shows them with badges + filter chips + source labels.

User picks a pack → same panel transitions to a scope-picker state: episode tiles grouped by season, pre-checked except for episodes the user already has (smart skip). For full-show packs (Complete Series), user picks "S1-S3 only" or "the whole show" via per-season toggles. User clicks Download → torrent starts via libtorrent (uniform engine for all packs); non-episode files (samples, .nfo, behind-the-scenes) get priority 0 and are silently skipped. The panel closes, Sources panel slides back, and a season-header progress badge appears.

During download: per-episode action-icon morphs to a progress indicator (existing tick-as-action UX), season-header badge shows the aggregate. When an episode finishes, its ✓ icon lights up via the same `publishTankorentItemsForTorrent` → `StreamDownloadIndex::registerEpisode` path Phase A4 already wired. Click an episode → local file plays instantly via `onEpisodeActivated`'s `downloadIndexHit` branch (line 1085-1112 of StreamDetailView.cpp). Resume position persists identity-keyed via Phase F's UnifiedProgressStore. Movies get the same panel with a degenerate single-file scope picker.

## 3. Strategic decisions ratified (20/20)

### Batch 1 — Strategic gates

1. **AddTorrentDialog in Theatre flow** → hard-remove. Theatre flow never opens it. Tankorent page still uses it for its own direct-search downloads. Clean separation.
2. **Header buttons** → ONE canonical `Download` button replacing both existing buttons. Both Stremio and Tankorent results show in the same list.
3. **Movies in v1 scope** → yes. Same panel; scope picker is degenerate (single file pre-checked, no per-season grouping).
4. **Pack classification** → each pack gets a type badge (Single Ep / Season Pack / Multi-Season / Complete Series) + filter chip row at the top of the list.

### Batch 2 — UI shape

5. **Picker location** → slide-in panel from the right, replacing the Sources panel slot. Show-view content (hero, episode table) stays visible on the left.
6. **Scope picker shape** → pre-checked episode tiles with per-season toggle. Defaults are smart: pick pack, hit Download, get the obvious thing. Edit selection if you want partial.
7. **Source labeling** → small source chip next to the pack-type badge on each row (`[Complete Series][Stremio]` or `[Season Pack][Tankorent · 1337x]`). Filter chips include `[Stremio only]` / `[Indexers only]`.
8. **Quality-weight slider** → punted. Hardcode 0.6 quality × 0.4 seeders. Revisit if smoke surfaces a tuning need.

### Batch 3 — Mechanics + edge cases

9. **Metadata strategy** → hybrid. Title-derived estimate renders tiles instantly; real metadata (via `TorrentClient::resolveMetadata`) refreshes them when it arrives (~10-30s).
10. **Stremio integration scope** → SAME ARC. Stremio aggregator (currently powering "Download Season") gets migrated into the new unified picker in v1. No "two buttons in v1" awkwardness.
11. **Zero-results behavior** → empty state + automatic fallback. Status line says "No season packs found · showing whole-show packs instead", list silently widens to show Complete Series packs that include the requested season.
12. **Cancel semantics** → keep finished episodes (real files on disk, registered in StreamDownloadIndex, ✓ in show-view), evict + remove only the unfinished/queued episodes.

### Batch 4 — Visual treatment + progress

13. **Already-downloaded handling** → mark with a "Have" badge + pre-uncheck. User can re-check to force a re-download; Phase C's highest-quality-wins dedup handles storage replacement if the new file scores higher.
14. **Progress UX** → both per-episode action-icon progress AND a season-header summary badge ("Downloading S1: 4/10 · 12.3 GB / 41 GB · 75 KB/s").
15. **Sources panel behavior** → cleanly replaced by the download panel (slide swap). When panel dismisses or download starts, Sources panel slides back in for the current episode.
16. **Pack row style** → title line + chip row + meta line. Three-tier visual hierarchy matching existing Tankoban source-card aesthetic.

### Batch 5 — Engine + queuing + file handling

17. **Download engine** → libtorrent (via `TorrentClient`) for ALL packs, Stremio-sourced or Tankorent-sourced. Uniform engine, uniform mental model. Stremio's stream-server stays for the Stream-playback button only.
18. **Multi-pack queuing** → parallel. libtorrent handles concurrent torrents natively; new picks queue immediately. Tankorent transfers tab already shows multiple active torrents this way.
19. **Non-episode files in packs** → skip preemptively via libtorrent `filePriority=0`. Only files matching `BulkPackVerifier`'s SxxExx regex get priority 1. Samples / .nfo / behind-the-scenes never hit disk.
20. **Vision completeness check** → Hemanth: "we can ask agent 7 to expand on your brainstorm.md or spec.md in terms of how the UI/UX experience will be with all these features and we can ask it to always keep user-friendly-ness and simplicity when expanding on details". This document is queued for that Codex pass before `/superpowers:writing-plans`.

## 4. Architecture

### What gets retired

- `TorrentPackPicker` as a `QDialog`. The class survives but transforms into a `QWidget` panel that lives in the right pane. The D2 indexer fan-out, D3 regex helpers (EnrichedResult + isCompleteSeriesName + detectSeasonsFromTitle + detectEpisodeCountFromTitle), and D4 combined-score sort logic all survive intact — they get rehomed into the new panel class.
- `AddTorrentDialog` reachability from Theatre. The class itself stays (Tankorent page still uses it for direct-search downloads). It just never opens from any Theatre flow.
- The two-button season header. `m_downloadSeasonBtn` ("Download Season", Stremio bulk-cohort) + `seasonTankorentBtn` ("Download via Tankorent", Phase D picker) collapse into one `m_downloadBtn`.

### What gets reused (don't rewrite what works)

- `BulkPackVerifier::matchEpisodeFileForSeason` — filename regex that decides which file is which episode. Drives both season-specific matching and the multi-season probe loop (1..50) for full-show packs.
- `QualityScorer` (Phase B, 7 GREEN tests) — 0.6 quality × 0.4 health combined-score computation. Untouched.
- `StreamDownloadIndex` highest-quality-wins dedup (Phase C, 5 GREEN tests including the H1 movie variants).
- `publishTankorentItemsForTorrent` (Phase A4) — already handles both per-season binding (when `record.season > 0`) and multi-season probe (when `record.season == 0` sentinel). Full-show packs land cleanly because of this.
- `TorrentClient::startDownload(infoHash, AddTorrentConfig)` (Phase A1, A2) — the contract that captures `imdbId` + `season` + `selectedIndices/filePriorities` in the record. We just drive it programmatically without opening AddTorrentDialog.
- `StreamAggregator` Stremio-addon fan-out — currently feeds the soon-to-be-retired "Download Season" path. Gets re-pointed to feed the new unified panel instead.
- Phase F `UnifiedProgressStore` (Option A via CoreBridge) — resume position lookups for downloaded episodes are identity-keyed; no change needed.
- `StreamDetailView::onEpisodeActivated` `downloadIndexHit` branch (line 1085-1112) — local-file playback dispatch on episode click. No change needed.
- `resolveRowState` `RowState::Published` action-icon morphing (line 82-89 + `actionIconForState`) — ✓ icon lights up automatically on `entriesChanged`. No change needed.
- Right-click "Show alternate streams" menu override — preserved.

### What's new

- **`TheatreDownloadPanel`** (`src/ui/pages/stream/TheatreDownloadPanel.{h,cpp}`) — the new `QWidget` that lives in the right pane. Internal states: `PackList` (default) and `ScopePicker` (after a pack is selected). Owns the source-merge fan-out, classification, episode-tile rendering, file-priority computation.
- **Source-merge layer** — one query (show name + season) fans out in parallel to:
  - Stremio addons via `StreamAggregator::searchPacks(imdbId, season)` (new method extracted from the existing "Download Season" path)
  - Tankorent indexers via the D2 owned-QNAM pattern Codex copied from `TankorentPage`
  Both produce `TorrentResult` records, which the panel normalizes into a single `EnrichedResult` list with an added `source` field (`Stremio | Tankorent`).
- **Pack-type classifier** — extension of D3 regex helpers. New method `classifyPack(EnrichedResult&)` returns one of:
  - `SingleEpisode` (title matches `SxxExx` exactly, no range / no Complete / no multi-season pattern)
  - `MultiEpisode` (title matches `Exx-Exx` or `EXX.EXX` patterns within one season)
  - `SeasonPack` (single-season `Sxx` token + filename / size / completeness heuristic)
  - `MultiSeason` (explicit `Sxx-Sxx` range)
  - `CompleteSeries` (literal "Complete Series" / "Complete Box Set" / "Complete Collection")
  Used by badges, filter chips, and auto-fallback widening logic.
- **Auto-fallback widening** — when the season-specific fan-out returns zero real packs (filtered: empty magnet/infoHash records dropped, cross-season pollution filtered by classifier), the panel silently re-issues a show-wide query (drops the season constraint) and shows those results below an empty-state line. Closes the F1 finding from the prior smoke.
- **Title-derived metadata estimator** — `estimateContentsFromTitle(title) → ScopeEstimate { detectedSeasons, episodesPerSeason, totalEpisodes, isCompleteSeries }`. Drives the scope-picker tile grid BEFORE libtorrent fetches real metadata.
- **Real-metadata refresh hook** — when `TorrentEngine::metadataReady` fires for the picked torrent's hash, the panel iterates real files, runs `matchEpisodeFileForSeason` per file, swaps each tile to show real filename + real size + real file-index association.
- **Episode-tile widget** — small reusable component (`src/ui/pages/stream/EpisodeTile.{h,cpp}` or inline within TheatreDownloadPanel). Shows S·E label, title (when known), size, checkbox, "Have" badge if already in StreamDownloadIndex.
- **File-priority driver** — when user clicks final Download, computes `filePriorities[fileIndex] = (matched && checked) ? 1 : 0` per torrent file in the engine's file list. Non-episode files (regex miss) get priority 0 preemptively. Result is stuffed into `AddTorrentConfig.filePriorities` for `startDownload`.
- **Season-header progress badge** — small widget in `StreamDetailView`'s season row. Aggregates downloading state across all this-season episodes (`TorrentClient::streamBulkSnapshotForImdbSeason` + StreamDownloadIndex queries) into a one-line summary. Hidden when no this-season activity. Has a `[× Stop]` affordance.

### Engine + state

- libtorrent via `TorrentClient` downloads everything. No hybrid.
- Stremio's `stream-server.exe` keeps its role: streams that get played-not-downloaded via the existing "Stream" button + `playRequested` flow. Untouched by this arc.
- Multi-pack parallel queuing by default (libtorrent's native behavior).
- Cancel = stop torrent, evict UNFINISHED episode entries from `StreamDownloadIndex`, remove their part-files. FINISHED entries stay registered + on disk + showing ✓.

## 5. UX flow (end-to-end user journey)

Each step is what the user sees + does, in order.

### Step 1 — Show-view, before download
Season header has ONE button: `[ ↓ Download ]`. Sources panel is on the right showing playable streams for the currently selected episode (existing behavior unchanged).

<!-- AGENT_7_EXPAND: 5.1.A — what's the exact visual treatment of the new `Download` button? Position in the season-row layout. Icon choice. Hover state. Resting state. Active state. Disabled state when no season selected (movies skip this). Reference existing Tankoban button vocabulary (DetailDownloadSeasonBtn / DetailMovieTankorentBtn styles in StreamDetailView.cpp lines 538-543 + 481-487). -->

<!-- AGENT_7 EXPAND START -->
**5.1.A - Canonical Download button treatment.**

Use one button labeled `Download`, not `Download Season`, because the new flow may download one episode, a season, multiple seasons, a complete series, or a movie. Keep the existing Tankoban button vocabulary from `DetailDownloadSeasonBtn` and `DetailMovieTankorentBtn`: 30px fixed height, 6px radius, `download-arrow.svg`, 12px text, 12px horizontal padding, resting `rgba(255,255,255,0.08)` background, 1px `rgba(255,255,255,0.14)` border, hover `rgba(255,255,255,0.12)` with border `rgba(255,255,255,0.22)`. Do not introduce a new accent-filled primary button; the Theatre surface already reads as dark/glass, and the existing download buttons are quiet enough to repeat weekly without feeling like an ad.

Position: in the season row, keep the season combo on the left, then stretch, then the progress badge if present, then `Download` at the far right. This preserves the current muscle memory where download actions live at the right edge of the season control row. For movies, reuse the same visual treatment inside `movieActionRow`, after any library/trailer actions and before the LOCAL chip.

States: resting is the existing outlined-gray button. Hover uses the existing +4% white overlay and pointing cursor. Pressed should drop to `rgba(255,255,255,0.16)` for one frame cycle, then either open the panel or return to resting. Active/open state should look selected, not brighter: background `rgba(255,255,255,0.12)`, border `rgba(255,255,255,0.28)`, text `#f3f4f6`. Disabled state for series with no selected season: opacity 0.45, cursor arrow, tooltip `Choose a season first`; movies never enter this state.

Reference pattern: Spotify and Netflix both make offline availability a direct action on the album/show surface rather than sending users into a separate manager first. Plex similarly starts downloads from the media detail screen and puts the item into a queue. Tankoban should follow that obvious mental model: button on the thing, panel explains the scope.

References:
- Tankoban existing `DetailDownloadSeasonBtn` / `DetailMovieTankorentBtn` styles in `src/ui/pages/stream/StreamDetailView.cpp`.
- Netflix Help, `How to use Smart Downloads` - https://help.netflix.com/en/node/122916
- Spotify Support, `Listen offline` - https://support.spotify.com/in-en/article/listen-offline/
- Plex Support, `Downloads Overview` - https://support.plex.tv/articles/downloads-overview/
<!-- AGENT_7 EXPAND END -->

### Step 2 — Click Download
Sources panel slides out to the right. `TheatreDownloadPanel` slides in to take its place. Heading: "Download · {Show Name} · Season N" (or "Download · {Show Name}" for movies). Below: a horizontal row of filter chips (`[All] [Complete Series] [Multi-Season] [Season Pack] [Single Episode] [Stremio] [Indexers]`). Below that: a status line that morphs ("Searching 5 indexers + 3 addons..." → "11 packs found").

<!-- AGENT_7_EXPAND: 5.2.A — slide-in transition timing, easing curve, duration. Anchor point for the slide (right edge? slot edge?). How does the Sources panel slide-out coordinate with the download panel slide-in (sequential? cross-fade? simultaneous)? -->

<!-- AGENT_7 EXPAND START -->
**5.2.A - Right-panel slide swap.**

Treat the download panel as a side-sheet occupying the same right-column slot as Sources, not as a modal over the whole show. This follows Notion's `Side peek` pattern: the detail surface opens on the right while the main database/detail context remains visible on the left. It also matches Figma's right properties panel pattern, where controls related to the selected object live in a persistent right sidebar.

Motion: simultaneous slide + fade, 180ms total, `QEasingCurve::OutCubic` for entry and `QEasingCurve::InCubic` for exit. Sources moves from x=0 to x=+24px inside its clipped slot while opacity fades 1.0 to 0.0; TheatreDownloadPanel starts at x=+32px and opacity 0.0, ending at x=0 and opacity 1.0. Do not slide from the full window edge; anchor to the right-pane slot edge so the left show content does not feel displaced.

The two panels should overlap for the full 180ms rather than run sequentially. Sequential motion makes the app feel like it is navigating away; simultaneous cross-slide reads as swapping tools in the same side area. If Qt opacity effects become too expensive, keep the positional slide and replace fade with immediate show/hide after 90ms; motion should never add more than about 200ms before the user can read pack results.

References:
- Notion Help, `Open pages in: Side peek` - https://www.notion.com/help/views-filters-and-sorts
- Figma Help, right sidebar properties panel - https://help.figma.com/hc/en-us/articles/360039832014-Design-prototype-and-explore-layer-properties-in-the-right-sidebar
- Material Design 3 side sheets overview - https://m3.material.io/components/side-sheets/overview
- Apple HIG Popovers, smooth transition when changing size/context - https://developer.apple.com/design/human-interface-guidelines/popovers/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.2.B — filter chip row visual treatment. Are chips toggleable (multi-select) or single-select (radio-style)? Active vs inactive visual states. Spacing. What's the keyboard navigation through the chips? -->

<!-- AGENT_7 EXPAND START -->
**5.2.B - Filter chip row.**

Use single-select chips for pack type and source, not arbitrary multi-select. The default is `All`. Clicking `Complete Series`, `Multi-Season`, `Season Pack`, or `Single Episode` replaces the active type filter. Clicking `Stremio` or `Indexers` replaces the active source filter. If both dimensions are needed, show them as two short groups separated by 10px: `All  Complete Series  Multi-Season  Season Pack  Single Episode` then `All sources  Stremio  Indexers`. This keeps first-time use obvious and avoids Linear/Notion-style advanced boolean filtering, which is powerful but unnecessary for a media picker.

Visuals: chips are 26px high, 6px horizontal padding, 10px font, 600 weight only when active. Inactive: transparent background, 1px `rgba(255,255,255,0.10)` border, text `rgba(255,255,255,0.62)`. Active: `rgba(255,255,255,0.12)` background, `rgba(255,255,255,0.26)` border, text `#f3f4f6`. Hover inactive: background `rgba(255,255,255,0.06)`. Keep 6px chip gap and 8px top/bottom row padding.

Keyboard: Tab focuses the chip row as one group, Left/Right moves between chips, Space or Enter activates the focused chip, Escape returns focus to the pack list. Backspace clears only when a non-`All` chip is focused, mirroring Linear's filter-panel clear behavior without requiring users to learn command-palette syntax.

References:
- Linear Docs, filters and keyboard support - https://linear.app/docs/filters
- Notion Help, database filters - https://www.notion.com/help/views-filters-and-sorts
- Material Design 3 chips overview - https://m3.material.io/components/chips/overview
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.2.C — status line treatment during loading. Shimmer? Spinner? Progress bar? Text-only? What's the transition from "Searching..." to "N packs found" — instant text swap, or fade-cross? -->

<!-- AGENT_7 EXPAND START -->
**5.2.C - Loading status line.**

Use text plus a thin indeterminate line, not a spinner and not a full skeleton-only state. The status line sits directly under the filter chips at 11px, text `rgba(255,255,255,0.56)`, height 22px. While searching, show `Searching 5 indexers + 3 addons...` and a 2px indeterminate bar pinned to the bottom of that status row. The bar should be grayscale: track `rgba(255,255,255,0.08)`, moving segment `rgba(255,255,255,0.34)`.

Why: NN/g's visibility-of-system-status guidance is especially relevant because indexer fan-out may take several seconds and users will otherwise click again. Material progress guidance says a single indicator should represent a single operation; here the operation is `find packs`, so one compact line is enough. Do not use shimmer here; reserve skeletons for the rows below so the panel does not have two competing loading metaphors.

Transition to result text with a 120ms opacity crossfade, not a height change. Examples: `11 packs found`, `11 packs found - 2 sources still searching`, or `No season packs found - showing whole-show packs`. Keep the line stable even after rows arrive so users can understand partial results.

References:
- NN/g, `Visibility of System Status` - https://www.nngroup.com/articles/visibility-system-status/
- NN/g, `Response Times: The 3 Important Limits` - https://www.nngroup.com/articles/response-times-3-important-limits/
- Material Design progress/activity guidance - https://m1.material.io/components/progress-activity.html
- Apple `NSProgressIndicator` docs - https://developer.apple.com/documentation/appkit/nsprogressindicator
<!-- AGENT_7 EXPAND END -->

### Step 3 — Pack list populates
Each row uses the title-line + chip-row + meta-line layout. Title in bold; chips include `[Pack Type]` + `[Source]`; meta line shows `N seeders · X.X GB · score N`. Sorted by combined score descending. Filter chips at the top live-filter the list. If zero real results, status line says "No season packs found · showing whole-show packs instead" and the list silently widens.

<!-- AGENT_7_EXPAND: 5.3.A — row spacing, line-height, font sizes, color hierarchy on the three lines. Hover state on a row. Selected state. Loading/skeleton state when results are streaming in but not yet ready. -->

<!-- AGENT_7 EXPAND START -->
**5.3.A - Pack-list row visual treatment.**

Reuse the `StreamSourceCard` hierarchy, with only density adjusted for torrent-pack metadata. Row minimum height 82px, margins 12px left/right and 10px top/bottom, vertical text spacing 4px. Title line: 13px, weight 600, `#f3f4f6`, single line with right elide and tooltip for the full release name. Chip row: 10px, weight 600, 4px chip radius, 1px grayscale border only on type/source chips; use 6px chip gap. Meta line: 11px, `rgba(255,255,255,0.48)`, format `62 seeders - 17.5 GB - score 71`. Avoid symbolic peer glyphs for this panel; plain words are clearer for non-technical users.

Resting row: `rgba(255,255,255,0.04)` background, 1px `rgba(255,255,255,0.08)` border, 8px radius. Hover: background `rgba(255,255,255,0.08)`, border `rgba(255,255,255,0.14)`, transition 120ms ease-out. Selected: background `rgba(255,255,255,0.12)`, border `rgba(255,255,255,0.24)`, plus a 2px left inset line `rgba(255,255,255,0.38)`; no color-coded pack types.

Loading rows: render 5 skeleton rows at the same 82px height so the layout does not jump. Each skeleton has three gray bars: 70% width title bar at 13px height, 3 chip bars at 18px x 64px, and a 45% width meta bar at 11px height. Pulse opacity from 0.35 to 0.65 over 900ms with a sine/ease-in-out loop. Stop skeletons as soon as the first real row arrives; streaming results should append below without clearing already-visible rows.

References:
- Tankoban `StreamSourceCard.cpp` current hierarchy and state treatment.
- Material lists with multi-line list items - https://m3.material.io/components/lists/overview
- NN/g, status feedback prevents repeated uncertain actions - https://www.nngroup.com/articles/visibility-system-status/
- Plex mobile downloads queue uses rows with progress/status - https://support.plex.tv/articles/downloads-for-ios/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.3.B — list scroll affordances. Virtualization for >50 result lists. Scrollbar visibility/styling. -->

<!-- AGENT_7 EXPAND START -->
**5.3.B - Pack-list scroll behavior.**

Use a normal vertical list up to 50 rows; above that, virtualize/recycle row widgets. Torrent result bursts can spike, but the common case is under 20, and the panel should optimize for clarity rather than exposing a dense transfer-client table. Keep 8px vertical gap between rows. The scroll container should preserve current scroll position when filters are toggled only if the selected row remains visible; otherwise scroll to top so users see the best-ranked matching packs first.

Scrollbar: hidden until hover or keyboard focus inside the list, then show a slim 6px grayscale thumb with no arrow buttons. Thumb `rgba(255,255,255,0.24)`, hover `rgba(255,255,255,0.36)`, track transparent. Add top/bottom 16px gradient fades only if implementation can do it cheaply; the scrollbar is the primary affordance, not the fade.

Keyboard: Up/Down moves row focus, Enter opens the focused pack, Home/End jump to first/last, PageUp/PageDown scroll one viewport. This mirrors Linear's list-first keyboard behavior while staying simple enough for a media UI.

References:
- Linear Docs, keyboard-driven list/filter workflow - https://linear.app/docs/filters
- Linear Docs, peek navigation with arrows/J/K - https://linear.app/docs/peek
- Material lists overview - https://m3.material.io/components/lists/overview
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.3.C — auto-fallback transition. When the season-specific query yields zero and we widen to show-wide, how is the transition communicated visually? Inline message? Banner? Animation? The user shouldn't feel the system "switched modes" silently. -->

<!-- AGENT_7 EXPAND START -->
**5.3.C - Auto-fallback transition.**

Pushback on Section 3 decision 11 wording: the widening can be automatic, but it should not be visually silent. Users need to know why a Complete Series pack is appearing after they asked for Season 2. Preserve the ratified behavior by removing the extra choice, but show a persistent inline explanation.

Treatment: when season-specific results finish with zero qualifying packs, keep the panel in place, fade the status line over 120ms to `No season packs found - showing whole-show packs that include Season N`, then insert a compact notice row above the first fallback result. Notice row height 44px, background `rgba(255,255,255,0.05)`, border `rgba(255,255,255,0.10)`, text 11px `rgba(255,255,255,0.68)`. Include a small neutral icon only if one already fits; no warning icon because this is successful recovery, not an error.

Do not animate rows out and back in. If season-specific rows existed but were filtered out by user chips, changing chips should not trigger fallback. Fallback triggers only when the underlying season query has no usable season packs.

References:
- NN/g, system status changes should be explicit enough to preserve user control - https://www.nngroup.com/articles/visibility-system-status/
- Material empty states guidance: prevent confusion when content cannot be shown - https://m1.material.io/patterns/empty-states.html
- Stack Overflow Design System empty states: explain result and next step - https://stackoverflow.design/system/components/empty-states/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.3.D — empty state when even the show-wide fallback yields nothing. What does the user see? Suggested next actions? -->

<!-- AGENT_7 EXPAND START -->
**5.3.D - True empty state.**

If show-wide fallback also returns nothing, show an empty state inside the list area, not a modal. Keep it practical and small: centered within the panel's remaining height, max width 280px, top margin 48px. Use `magnet.svg` or `download-arrow.svg` as a 28px monochrome icon at `rgba(255,255,255,0.34)`, heading `No packs found` at 14px/600, body at 12px `rgba(255,255,255,0.58)`.

Copy: `No packs found for this show right now. You can still stream the episode from Sources, or try again later.` If filters are active, replace with `No packs match these filters.` and show a small `Clear filters` text button. Do not suggest opening Tankorent or AddTorrentDialog; that would reintroduce the seam this arc is removing.

Empty state should not hide the filter chips or status line. Users should be able to clear filters or close the panel without losing context.

References:
- Stack Overflow Design System empty states - https://stackoverflow.design/system/components/empty-states/
- Material empty states pattern - https://m1.material.io/patterns/empty-states.html
- AIA Design empty state guidance: distinguish empty from error and give next steps - https://design.aia.com/component/empty-state
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.3.E — error state when indexers are unreachable / Stremio addons time out / network is offline. Partial-failure case (1 of 5 indexers responded). How does the panel communicate degraded state? -->

<!-- AGENT_7 EXPAND START -->
**5.3.E - Error and partial-failure states.**

Separate partial degradation from total failure. Partial failure is common with torrent indexers and should not feel alarming if useful packs are already available. Keep the pack list visible, and change the status line to examples like `11 packs found - 2 indexers timed out` or `Stremio unavailable - showing indexer results`. Add a right-aligned `Details` text button in the status row; clicking expands a 72px diagnostic drawer listing failed sources in plain language. The drawer is for transparency, not for normal use.

Total failure gets an inline error state in the list area: icon `error.svg` at 28px monochrome, heading `Search did not finish`, body `Check your connection, then try again. Sources for streaming are still available.` Primary action `Try again`, secondary `Close`. Since no-color is mandatory, severity comes from wording, icon shape, and border weight, not red.

Timeout policy: show partial results as soon as any source returns; never block the list waiting for slow sources. After 8 seconds, mark remaining sources as timed out and let `Try again` re-query only failed providers. This follows the response-time principle: after long variable waits, users need feedback and a way to continue.

References:
- NN/g, `Response Times: The 3 Important Limits` - https://www.nngroup.com/articles/response-times-3-important-limits/
- NN/g, `Visibility of System Status` - https://www.nngroup.com/articles/visibility-system-status/
- Plex downloads errors expose failed downloads with retry/remove options - https://support.plex.tv/articles/downloads-for-ios/
- Stremio Help notes offline downloads are cache-limited, so Tankoban should be explicit when provider behavior degrades - https://stremio.zendesk.com/hc/en-us/articles/360021228252-Download-videos
<!-- AGENT_7 EXPAND END -->

### Step 4 — Click a pack
The panel transitions inline (no new dialog) to the scope-picker state. Top: a back arrow + the pack's title + chips. Below: estimated episode tiles render instantly from title-derived metadata. A subtle loading indicator marks "real metadata loading". When real metadata arrives, tiles refresh in-place with real filenames + real sizes + file-index associations.

<!-- AGENT_7_EXPAND: 5.4.A — back-arrow / pack-summary header treatment. Sticky? Scrolls with content? Visual hierarchy between title, chips, and the rest of the panel. -->

<!-- AGENT_7 EXPAND START -->
**5.4.A - Scope-picker header.**

Make the header sticky at the top of the panel. It should stay visible while the user scrolls through 86 Sopranos episodes, because it carries the chosen pack identity and the way back. Height 64px minimum, bottom border `rgba(255,255,255,0.08)`, background matching the panel surface at about `rgba(15,15,15,0.96)` so scrolling content does not bleed through.

Layout: 28px square back button on the left using `chevron_left.svg`, then a text column. Title line 13px/600 `#f3f4f6`, one line, right-elided, tooltip full release name. Chip row below: pack type + source + size, 10px font, 4px radius, same chip style as pack list. If the pack title is extremely long, never wrap it; wrapping would push the tile grid down and make the panel feel unstable.

Back button hover: `rgba(255,255,255,0.08)` background, 4px radius. Tooltip `Back to packs`. Escape should trigger this same back behavior unless focus is inside the final large-download confirmation.

References:
- Figma right sidebar: properties remain tied to selected canvas object - https://help.figma.com/hc/en-us/articles/360039832014-Design-prototype-and-explore-layer-properties-in-the-right-sidebar
- Notion side peek keeps database context on the left while details open right - https://www.notion.com/help/views-filters-and-sorts
- Apple HIG Popovers: panel appearance should preserve context - https://developer.apple.com/design/human-interface-guidelines/popovers/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.4.B — pack-list → scope-picker transition. Same-panel state-swap vs slide-within vs page-flip? Should it feel like the same surface or two different surfaces? -->

<!-- AGENT_7 EXPAND START -->
**5.4.B - Pack-list to scope-picker transition.**

Use same-panel state swap with a shallow horizontal slide, not a page flip and not a new dialog. The pack list should feel like step 1 of the same download flow; a page flip implies leaving the current surface, and a dialog would recreate the AddTorrentDialog seam.

Motion: selected row briefly holds selected state for 80ms, then pack list content slides left by 20px while fading to 0 over 140ms. Scope-picker content enters from right +20px to 0 and opacity 0 to 1 over 160ms, same `OutCubic`. Header can appear with the scope content; do not animate the whole panel width. Keep total transition under 200ms.

If metadata is still loading, the scope picker still opens immediately with estimated tiles. This matters more than visual flourish: Hemanth's expected path is pick pack, hit Download, watch later; delays between intent steps should be minimal.

References:
- Notion `Side peek` / `Center peek` distinction supports detail-in-context rather than modal interruption - https://www.notion.com/help/views-filters-and-sorts
- Material side sheets overview - https://m3.material.io/components/side-sheets/overview
- NN/g response-time limits: preserve flow and provide feedback when operations are not immediate - https://www.nngroup.com/articles/response-times-3-important-limits/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.4.C — the "estimated vs real" data transition. When real metadata replaces estimated tiles, what's the user-visible difference? Subtle highlight? Toast? Or silent in-place refresh? -->

<!-- AGENT_7 EXPAND START -->
**5.4.C - Estimated-to-real metadata refresh.**

Default to quiet in-place refinement. Estimated tiles are good enough for immediate selection; real metadata adds confidence. When real metadata arrives, update filename and size labels in place and run a 700ms border shimmer on changed tiles only: border from `rgba(255,255,255,0.12)` to `rgba(255,255,255,0.24)` and back. Do not show a toast; the user did not ask for an interruption.

If metadata changes the selectable scope materially, escalate. Examples: estimated S1-S6 but real files only match S1-S5, or some checked tiles cannot be mapped to file indices. In that case, keep the user in the scope picker, show a compact notice below the header: `Metadata updated - 8 selected episodes are unavailable in this pack.` Then uncheck unavailable tiles and disable them with a tooltip. This is status visibility, not an error dialog.

Real filenames should appear as secondary text only on hover or as a two-line tile expansion if space allows; normal users care about episode identity and size, not scene-release filenames.

References:
- NN/g, visibility of system status and communicating changes that affect users - https://www.nngroup.com/articles/visibility-system-status/
- Apple HIG Popovers: animate size/content changes to avoid feeling replaced - https://developer.apple.com/design/human-interface-guidelines/popovers/
- qBittorrent exposes raw file selection, but this arc intentionally abstracts that into episode tiles - https://github.com/qbittorrent/qBittorrent/wiki/Explanation-of-Options-in-qBittorrent
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.4.D — metadata-loading indicator. Where does it sit? How prominent? Does it block interaction (no, scope picker should be interactive on estimated data)? -->

<!-- AGENT_7 EXPAND START -->
**5.4.D - Metadata loading indicator.**

Place the indicator in the sticky header's second line, after chips: `Checking torrent contents...` at 11px `rgba(255,255,255,0.50)` with a 12px indeterminate spinner or a 2px inline bar. Use only one indicator. Prefer the 2px inline bar if it can be clipped to the text column width; it is quieter and reads better in a narrow side panel.

Do not block tile interaction. Estimated tiles are selectable immediately, and the bottom Download button remains enabled as long as at least one estimated tile is checked. If the user clicks Download before real metadata arrives, change the button label to `Start when contents are ready` and show a 1-line explanation in the bottom bar. That prevents a dead click while avoiding a modal wait.

If metadata takes longer than 10 seconds, change the line to `Still checking contents - you can keep choosing episodes` and keep the panel usable. This follows NN/g's threshold for long waits: after about 10 seconds, the user needs more explicit feedback.

References:
- NN/g response-time limits - https://www.nngroup.com/articles/response-times-3-important-limits/
- Material progress/activity guidance - https://m1.material.io/components/progress-activity.html
- Apple `NSProgressIndicator` determinate/indeterminate distinction - https://developer.apple.com/documentation/appkit/nsprogressindicator
<!-- AGENT_7 EXPAND END -->

### Step 5 — Scope picker
Episode tiles grouped under collapsible per-season headers. Each tile shows S·E label + title (when known) + size + a checkbox. Tiles for episodes already in `StreamDownloadIndex` show a small "Have" badge + pre-UNCHECKED state. Per-season header has a `[Select all S1]` toggle. Bottom of panel: `[Cancel]` + `[Download N episodes · X.X GB]` (label updates live as tiles toggle).

<!-- AGENT_7_EXPAND: 5.5.A — tile dimensions, grid columns (1? 2? auto-fit?), per-season header style + toggle interaction. What does collapse/expand of a season feel like? -->

<!-- AGENT_7 EXPAND START -->
**5.5.A - Episode tile grid and season groups.**

Use one column for narrow right panels below 360px, two columns for 360px and above. Do not use three columns in the right pane; episode titles and sizes will truncate too aggressively. Tile height 58px in compact one-line mode, 72px if title + filename-size both display. Tile padding 10px, radius 6px, resting background `rgba(255,255,255,0.04)`, border `rgba(255,255,255,0.08)`. Checked tile: background `rgba(255,255,255,0.08)`, border `rgba(255,255,255,0.20)`; unchecked tile stays visually present, not faded out.

Tile layout: 18px checkbox icon left (`checkbox-checked.svg` / `checkbox-empty.svg`), text column middle, optional `Have` badge right/top. Label line: `S1 E03` or `S1E03 - Episode title`, 12px/600. Secondary line: `estimated - 1.4 GB` or `1.4 GB`, 11px `rgba(255,255,255,0.50)`. Avoid raw file names by default.

Season header: 34px high. Header has chevron, `Season 1`, count summary `8 selected / 10`, and right-aligned `All` toggle. Collapse/expand should animate height over 140ms only when the group has more than 6 tiles; otherwise immediate is fine. Collapsed headers still show the selected count.

References:
- Material lists with controls: checkbox is state indicator and primary action for selectable items - https://m1.material.io/components/lists-controls.html
- Spotify offline flow uses album/playlist-level download with track availability handled inside the collection - https://support.spotify.com/in-en/article/listen-offline/
- qBittorrent file-priority selection is the power-user layer Tankoban is abstracting away - https://github.com/qbittorrent/qBittorrent/wiki/Explanation-of-Options-in-qBittorrent
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.5.B — the "Have" badge visual. Tankoban no-color rule applies; what's the grayscale treatment that's still unmistakable? Position on tile (corner badge? inline label?). -->

<!-- AGENT_7 EXPAND START -->
**5.5.B - `Have` badge treatment.**

Use a small grayscale badge in the tile's top-right corner, not a colored chip and not a checkmark-only icon. Text `Have`, 9px, weight 700, letter spacing 0, height 18px, padding 1px 6px, radius 3px. Background `rgba(255,255,255,0.12)`, border `rgba(255,255,255,0.22)`, text `#eeeeee`. This matches the existing `DetailMovieLocalChip` / downloaded-chip family without bringing in color.

Behavior: already-have tiles are pre-unchecked but not disabled. Tile opacity remains 1.0 so users understand the episode exists in the pack. The checkbox starts unchecked; clicking it checks the tile and changes secondary text to `Will replace if better quality`. Tooltip on the badge: `Already downloaded - leave unchecked to skip`.

Do not call the badge `LOCAL` inside the picker. `LOCAL` is the show-view playback state; `Have` is the scope-picker decision state. This distinction avoids confusing storage state with action scope.

References:
- Tankoban `DetailMovieLocalChip` in `StreamDetailView.cpp`.
- Spotify Support: downloaded album/playlist state is managed at the collection surface - https://support.spotify.com/in-en/article/listen-offline/
- NN/g, communicating current status improves control and trust - https://www.nngroup.com/articles/visibility-system-status/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.5.C — bulk-toggle UX. Per-season "Select all S1" is one. Is there a panel-wide "Select all / None" too? What about "Select only first 3 episodes of every season" or other power patterns — needed in v1 or future? -->

<!-- AGENT_7 EXPAND START -->
**5.5.C - Bulk-toggle scope.**

Ship v1 with three bulk controls only: per-season `All`, panel-wide `Select all`, and panel-wide `None`. Put panel-wide controls in a compact row directly above the first season group: `Select all` and `None` as text buttons, 11px, no boxed chips. Do not add `first 3 episodes of every season` or other power selectors in v1; they solve rare cases and would make the primary path harder to understand.

Per-season `All` is a tri-state text toggle in behavior, not necessarily a custom tri-state widget. If zero selected: label `All`. If some selected: label `All` plus summary `3/10`. If all selected: label `None`. The header's count summary makes the state clear without introducing another icon language.

When user toggles a group containing `Have` tiles, default `All` selects only not-have tiles. If the user explicitly wants re-downloads, they can click those tiles individually. This follows the Spotify-style `download remaining` mental model: do the obvious missing work, do not duplicate existing local content unless asked.

References:
- Spotify Support, offline downloads and downloaded collection removal - https://support.spotify.com/in-en/article/listen-offline/
- Linear filters: quick filters first, advanced combinations later - https://linear.app/docs/filters
- Notion filters can become complex, but v1 should avoid exposing that complexity - https://www.notion.com/help/views-filters-and-sorts
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.5.D — bottom-pinned action bar. Cancel + Download arrangement. Disabled state when zero episodes selected. The "Download N episodes · X.X GB" live-updating label is high-feedback; what's the typography? -->

<!-- AGENT_7 EXPAND START -->
**5.5.D - Bottom-pinned action bar.**

Pin the action bar to the bottom of the panel with a top border `rgba(255,255,255,0.08)` and background `rgba(15,15,15,0.96)`. Height 58px, margins 12px, spacing 8px. Left side: small summary text `8 episodes selected - 12.3 GB` at 11px `rgba(255,255,255,0.58)`. Right side: `Cancel` secondary button then `Download 8 episodes - 12.3 GB` primary-action-styled gray button.

Buttons: `Cancel` is transparent/outlined, 30px high, 12px text. Download button reuses `DetailDownloadSeasonBtn` visual style but with slightly stronger border `rgba(255,255,255,0.20)` because it is the committing action. Disabled when zero episodes selected: opacity 0.45, text `Select episodes to download`, no hover change, tooltip `Choose at least one episode`.

Live label updates should be immediate, with no animation except text width change; do not bounce or pulse. If size is estimated, append `est.` in the small summary, not in the button: `8 episodes selected - est. 12.3 GB`. The button should remain readable and action-oriented.

References:
- Stripe checkout submit button pattern: the final button restates the committed amount/action - https://docs.stripe.com/payments/accept-a-payment?payment-ui=elements
- Material side sheets often include confirm/cancel actions in the sheet surface - https://m3.material.io/components/side-sheets/overview
- NN/g visibility: users need to know the action was registered and what it will do - https://www.nngroup.com/articles/visibility-system-status/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.5.E — Movie mode degenerate state. The whole season-grouping infrastructure collapses to a single tile + the Download button. What's the visual treatment so the panel doesn't feel half-empty? -->

<!-- AGENT_7 EXPAND START -->
**5.5.E - Movie mode degenerate state.**

Do not show a lonely season-grid shell for movies. Use a single `Selected file` card at the top, then a compact `Pack details` section underneath. The selected file card is 78px high, full width, with checkbox, movie title, size, and `Have` badge if already local. Under it, show 3-5 detail rows if available: `Quality`, `Size`, `Source`, `Seeders`, `Release`. Each row is 28px high, label 11px `rgba(255,255,255,0.46)`, value 12px `rgba(255,255,255,0.78)`.

This uses the extra space for reassurance, not controls. Do not add file-tree access or priority controls; Section 6 explicitly punts that. If metadata is not yet real, show `Estimated from title` as the detail value and update in place when real metadata arrives.

Bottom button label: `Download movie - X.X GB` or `Download movie` if size unknown. This mirrors Stripe's amount-in-CTA pattern without making the panel feel like a payment screen.

References:
- Stripe checkout submit button includes the concrete commitment - https://docs.stripe.com/payments/accept-a-payment?payment-ui=elements
- Plex starts movie downloads from item detail and queues them - https://support.plex.tv/articles/downloads-for-ios/
- qBittorrent raw file-priority UI is intentionally out of scope - https://github.com/qbittorrent/qBittorrent/wiki/Explanation-of-Options-in-qBittorrent
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.5.F — keyboard navigation through tiles. Tab order. Arrow-key navigation through the grid. Space to toggle a tile. Enter to confirm download. Escape to cancel back to pack list. -->

<!-- AGENT_7 EXPAND START -->
**5.5.F - Keyboard navigation through scope tiles.**

Tab order: header back button, filter/source controls if visible, first season header toggle, tile grid, bottom `Cancel`, bottom `Download`. Inside the tile grid, use roving focus: only one tile has tab focus; Arrow keys move between tiles without tabbing through every checkbox. Left/Right move columns, Up/Down move rows, Home/End move to first/last tile in the current season, Ctrl+Home/Ctrl+End jump across all seasons.

Space toggles the focused tile. Enter activates the bottom Download action if the current selection is valid; if zero selected, Enter does nothing and the disabled button tooltip/status explains why. Escape first returns from scope picker to pack list; if already on pack list, Escape closes the panel and restores Sources. Backspace on a season header clears that season only.

Focus visuals: 1px border `rgba(255,255,255,0.34)` plus background `rgba(255,255,255,0.08)`. Do not rely on color alone; the border and checkbox icon state carry the information.

References:
- Linear Docs, keyboard-first filters/list navigation - https://linear.app/docs/filters
- Linear Docs, arrow navigation with peek - https://linear.app/docs/peek
- Material list controls: checkbox state belongs to the list item action - https://m1.material.io/components/lists-controls.html
<!-- AGENT_7 EXPAND END -->

### Step 6 — Click Download (final)
Panel slides out, Sources panel slides back in for the current episode. Behind the scenes: `startDownload(infoHash, config)` fires with `config.filePriorities` driven by the user's tile selections. Non-episode files (samples / .nfo / extras) get priority 0 and are never downloaded. libtorrent starts pulling.

<!-- AGENT_7_EXPAND: 5.6.A — the "Download started" feedback moment. Does the panel just slide away? Toast notification? Brief success animation? The user has just committed to a multi-GB download; they want confirmation it's happening. -->

<!-- AGENT_7 EXPAND START -->
**5.6.A - Download-started feedback.**

Do not simply slide away with no confirmation. The user just committed disk space and bandwidth, so give a compact confirmation while returning them to Theatre. Sequence: final Download button enters pressed/loading state for 120ms, label changes to `Starting...`, panel slides out and Sources slides back in over 180ms, then a toast appears bottom-center for 4 seconds: `Download started - S1, 8 episodes`. Action button: `View progress` if there is a Theatre progress surface to focus; otherwise omit the action.

Use the existing `Toast` component shape but fix its color for this feature: current `Toast` uses blue action text, which violates the no-color rule. For this flow, action text should be `#e6e6e6`, hover `#ffffff`, with underline or weight change rather than color shift.

The season-header badge must appear within the same second as the toast. NN/g's point is direct: feedback tells users the click worked and prevents repeated clicks. Toast alone is not enough; persistent badge is the durable proof.

References:
- NN/g, `Visibility of System Status` - https://www.nngroup.com/articles/visibility-system-status/
- Plex downloads: selected item enters an in-progress queue - https://support.plex.tv/articles/downloads-for-ios/
- Tankoban `Toast.cpp` existing component shape; adjust no-color action styling for this flow.
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.6.B — confirmation pattern for very large downloads (e.g., 145 GB Sopranos Complete Series). Soft confirmation ("This will use 145 GB. Continue?") or hard-confirmation gate (separate dialog)? What's the threshold? -->

<!-- AGENT_7 EXPAND START -->
**5.6.B - Very-large download confirmation.**

Use a soft in-panel confirmation, not a separate dialog, for large downloads. Threshold: show confirmation when selected size is known and >= 50 GB, or selected episode count >= 40 with unknown size. For especially large packs (>= 100 GB), require a second click on the same bottom button. This borrows the transaction-confirmation clarity of checkout flows without forcing confirmation on normal season downloads.

Treatment: the bottom action bar expands upward by 64px. Message: `This download will use about 145 GB.` Secondary line: `Finished episodes will stay if you stop later.` Buttons: `Cancel` and `Download 86 episodes - 145 GB`. The primary button remains grayscale, but stronger border `rgba(255,255,255,0.30)`. Default keyboard focus stays on Cancel for the confirmation state; Enter should not accidentally start a 145 GB transfer unless the user has moved focus to the Download button.

Do not ask `Are you sure?` for normal downloads. Confirmation fatigue would kill the feature. The gate exists only when the cost is high enough that a reasonable user might regret a misclick.

References:
- Stripe checkout: final action labels include the concrete payment/commitment - https://docs.stripe.com/payments/accept-a-payment?payment-ui=elements
- Microsoft confirmation guidance: confirmations are for actions with relevant risk/choice - https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-confirm
- Apollo UX writing confirmation guidance: state consequence and use action-specific button labels - https://www.apollographql.com/docs/ux-writing-style-guide
<!-- AGENT_7 EXPAND END -->

### Step 7 — While downloading
Season header gains a small badge: `[ ● Downloading S1: 4/10 · 12.3 GB / 41 GB · 75 KB/s ]`. Each downloading episode's action-column icon morphs to a per-row progress indicator (`Queued` / `42%` / `Done` / `Failed`). When an episode finishes, its action icon flips to ✓ ("Downloaded — options" tooltip) via `publishTankorentItemsForTorrent` → `registerEpisode` → `entriesChanged` → `refreshEpisodeMarkers`.

<!-- AGENT_7_EXPAND: 5.7.A — season-header progress badge visual treatment. Position relative to season-combo + Download button. Animation when state changes (pulse on each completed episode? steady fill bar?). What's the resting visual at 4/10 vs 1/10 vs 9/10? -->

<!-- AGENT_7 EXPAND START -->
**5.7.A - Season-header progress badge.**

Place the progress badge between the season combo and the Download button cluster, after the stretch only if horizontal space is tight. Preferred order on wide detail view: `Season:` combo, progress badge, stretch, Download. The badge should be visible near the season selector because it describes that season, not the whole page.

Visual: height 24px, radius 4px, background `rgba(255,255,255,0.08)`, border `rgba(255,255,255,0.14)`, text 10.5px `rgba(255,255,255,0.76)`. Text examples: `Downloading S1: 4/10 - 12.3 / 41 GB - 75 KB/s`, `Queued S1: 0/10`, `Finishing S1: 9/10`. Add a 2px bottom fill line inside the badge for aggregate percent: at 1/10 it is 10% width, at 4/10 40%, at 9/10 90%. Fill uses `rgba(255,255,255,0.38)`; track transparent.

Animation: no constant pulse. On each episode completion, run one 180ms brighten of the badge border to `rgba(255,255,255,0.28)` then settle. Constant pulsing makes downloads feel unstable; progress should feel steady and background-capable.

References:
- Plex in-progress queue shows current downloading, queued items, progress bars, and remaining space - https://support.plex.tv/articles/downloads-for-ios/
- Material progress/activity guidance: determinate indicators show completion and should not decrease - https://m1.material.io/components/progress-activity.html
- NN/g response-time/progress guidance - https://www.nngroup.com/articles/response-times-3-important-limits/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.7.B — per-episode action-icon progress states. Existing icons: download-arrow.svg, pause-circle.svg, check.svg, play-circle.svg, retry-arrow.svg (per actionIconForState in StreamDetailView.cpp:99-110). For 0%/queued, the icon is download-arrow; for 42%, do we morph the icon itself or add a progress ring around it? What's the visual at 99% vs 100%? -->

<!-- AGENT_7 EXPAND START -->
**5.7.B - Per-episode action-icon progress.**

Keep the existing `actionIconForState` icon vocabulary and add a progress ring around the button for determinate download percent. Do not morph the SVG itself; changing `pause-circle` into custom percent artwork would create a new icon language. Button stays 24px, icon 16px. Draw a 1.5px circular ring around the icon holder: track `rgba(255,255,255,0.10)`, fill `rgba(255,255,255,0.42)`.

States: queued uses `download-arrow.svg` with a dotted/indeterminate ring or no fill and tooltip `Queued`. Downloading uses `pause-circle.svg` with ring fill and tooltip `Downloading - 42%`. 99% remains downloading until publish completes; do not show the check icon early. Publishing uses `pause-circle.svg` or a small indeterminate ring with tooltip `Publishing`. 100%/Published flips to `check.svg` only after `StreamDownloadIndex` registration fires.

If percent is unknown, show text only in tooltip/status, not inside the icon cell. The action column should remain visually calm; detailed speed belongs in the season badge tooltip or transfers view.

References:
- Tankoban `actionIconForState` in `StreamDetailView.cpp`.
- Apple `ProgressView`: determinate vs indeterminate progress - https://developer.apple.com/documentation/swiftui/progressview
- Apple `NSProgressIndicator`: determinate vs indeterminate progress - https://developer.apple.com/documentation/appkit/nsprogressindicator
- Material progress indicators overview - https://m3.material.io/components/progress-indicators/overview
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.7.C — bandwidth + ETA display. Where? Hover tooltip on the season-header badge? Tankorent transfers tab is one screen away; should the user need to leave Theatre to see speed/ETA? -->

<!-- AGENT_7 EXPAND START -->
**5.7.C - Bandwidth and ETA display.**

Show current speed in the season-header badge because it is the one number users expect to see without leaving Theatre. Keep ETA in the badge tooltip or an expanded hover card, not in the row text by default. Badge text has limited width; speed is more trustworthy than ETA for torrents, which fluctuates heavily.

Tooltip/hover card content: `Downloading S1`, `4 of 10 episodes`, `12.3 GB of 41 GB`, `75 KB/s`, `About 6h remaining`, `2 packs active`. Use plain stacked text, max width 260px, no charts. If speed is 0 for more than 30 seconds, badge text can become `Waiting for peers` instead of `0 KB/s` because that is more user-readable.

Users should not need to leave Theatre for basic progress. The Tankorent transfers tab remains the power-user detail screen for per-torrent peers, trackers, and queue management.

References:
- Plex downloads in-progress queue exposes progress, queued state, errors, and remaining space - https://support.plex.tv/articles/downloads-for-ios/
- NN/g visibility of system status - https://www.nngroup.com/articles/visibility-system-status/
- qBittorrent options/content UI represents the power-user transfer-management layer - https://github.com/qbittorrent/qBittorrent/wiki/Explanation-of-Options-in-qBittorrent
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.7.D — multi-pack concurrent UX. User starts Sopranos S1, then Daredevil S2 — show-view shows season-header badge per-show when navigating. Theatre library landing (the entry point) might benefit from a "downloads happening" hint? Or stay quiet? -->

<!-- AGENT_7 EXPAND START -->
**5.7.D - Multi-pack concurrent UX.**

Keep show-view progress local to the show/season the user is viewing. If Sopranos S1 and Daredevil S2 are both downloading, Sopranos detail shows only Sopranos badges; Daredevil detail shows only Daredevil badges. This avoids turning each show page into a global transfer dashboard.

Add one quiet global hint on the Theatre library landing: a small `Downloads` pill in the top-right control cluster or near the Theatre header, visible only while any Theatre downloads are active. Text: `2 downloads` or `Downloads active`; height 24px, same badge styling as season progress, no animation except one 180ms border brighten when a new download starts. Clicking it can open Tankorent transfers or a future Theatre downloads overview, but v1 can route to the existing transfers page if no Theatre overview exists.

Do not show toast spam for every background progress change. Toast is for start, failure, and completion if the user is not on the relevant show.

References:
- Plex separates in-progress downloads into a downloads queue while allowing item-level starts - https://support.plex.tv/articles/downloads-for-ios/
- Spotify downloads play automatically offline but collection state remains visible in-library - https://support.spotify.com/in-en/article/listen-offline/
- NN/g system status: expose backstage state when it affects user decisions - https://www.nngroup.com/articles/visibility-system-status/
<!-- AGENT_7 EXPAND END -->

### Step 8 — After completion
Season-header badge disappears. Every downloaded episode shows ✓ in its row. Left-click an episode → instant local playback via `onEpisodeActivated` line 1085-1112. Right-click → "Show alternate streams" override available. Phase F `UnifiedProgressStore` resume position works identity-keyed.

<!-- AGENT_7_EXPAND: 5.8.A — completion celebration moment. The user just successfully downloaded a full season; does the show-view acknowledge that achievement? Or is silence the right answer (no confetti, no toast, just the ✓ icons lighting up)? -->

<!-- AGENT_7 EXPAND START -->
**5.8.A - Completion moment.**

Use quiet acknowledgement, not celebration. No confetti, no oversized modal, no sound. When the final selected episode publishes, the season badge does a 200ms brighten, changes to `Downloaded S1: 10/10` for 3 seconds, then fades out over 160ms. Episode rows flip to the existing check icon as each publish lands; that is the main satisfaction moment.

If the user is not currently on that show/season, show one toast: `Sopranos S1 downloaded` with optional action `Open`. Duration 5 seconds. If the user is already watching the relevant table, no toast is necessary; the check icons and disappearing badge are enough.

This matches the product's cinema feel: downloads become part of the library, not achievements. It also respects the anti-pattern list: the user will do this repeatedly, so completion feedback should be informative and non-intrusive.

References:
- NN/g visibility of system status: feedback should confirm outcomes without removing control - https://www.nngroup.com/articles/visibility-system-status/
- Plex completed downloads show completed items and space summary rather than celebration - https://support.plex.tv/articles/downloads-for-ios/
- Spotify downloaded state is represented as durable offline availability - https://support.spotify.com/in-en/article/listen-offline/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.8.B — discoverability of right-click "Show alternate streams". Hidden in a context menu means most users won't find it. Is the right-click pattern enough? Long-press? Subtle hover affordance? -->

<!-- AGENT_7 EXPAND START -->
**5.8.B - Alternate-stream discoverability.**

Right-click alone is too hidden for most media users. Keep the right-click menu, but add a hover affordance to downloaded rows: when a row with `check.svg` is hovered, the action icon can show a tiny adjacent `kebab-menu.svg` hit target or change tooltip to `Downloaded - click for options`. Left-clicking the check icon should open the same options menu currently reached by right-click, while left-clicking the row/title still plays the local file.

For touch/long-press parity later, long-press on the row can open the same menu, but v1 desktop can ship with hover + click-on-check. Do not add a persistent `Alternate streams` button column; it clutters the primary table for a secondary action.

Menu copy: `Play local file`, `Show alternate streams`, `Open file location` if already available elsewhere. The first item reinforces that the check icon is actionable and not just a status badge.

References:
- Apple HIG Popovers: contextual controls should be small, anchored, and not stacked - https://developer.apple.com/design/human-interface-guidelines/popovers/
- Linear Docs, multiple action paths for list items via keyboard/contextual controls - https://linear.app/docs/conceptual-model
- NN/g recognition over recall / visibility principle - https://www.nngroup.com/articles/ten-usability-heuristics/
<!-- AGENT_7 EXPAND END -->

### Step 9 — Cancel mid-download
A small `[ × Stop ]` affordance next to the season-header progress badge. Click → libtorrent stops the torrent, in-flight + queued episodes get evicted from `StreamDownloadIndex` and their part-files removed, FINISHED episodes stay registered + on disk + showing ✓.

<!-- AGENT_7_EXPAND: 5.9.A — Stop confirmation. Soft "Stop downloading? Files in progress will be discarded" prompt, or click-immediate destructive? -->

<!-- AGENT_7 EXPAND START -->
**5.9.A - Stop confirmation.**

Stopping is destructive for unfinished part-files, so use a lightweight confirmation. Do not make the first click immediate. The prompt should be anchored near the badge or appear as a small in-panel popover, not a full-window modal. Copy: `Stop downloading S1?` Body: `Finished episodes stay. In-progress files will be discarded.` Buttons: `Keep downloading` and `Stop download`. Default focus on `Keep downloading`.

No red styling. The destructive choice can be visually secondary in grayscale: outline button with stronger border, text `#f3f4f6`, while `Keep downloading` uses the normal primary gray button. This prevents accidental Enter from stopping the transfer and satisfies the no-color rule.

If zero bytes have completed for all selected episodes, simplify body to `Queued and in-progress files will be removed.` If at least one episode is complete, include count: `3 finished episodes will stay.` Concrete consequences are better than generic `Are you sure?`.

References:
- Microsoft confirmation guidance: confirmations suit actions with risk and a relevant choice - https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-confirm
- Apollo UX writing style guide: state consequences and use specific action labels - https://www.apollographql.com/docs/ux-writing-style-guide
- Plex downloads can be canceled from the in-progress queue - https://support.plex.tv/articles/downloads-for-ios/
<!-- AGENT_7 EXPAND END -->
<!-- AGENT_7_EXPAND: 5.9.B — post-Stop visual. Does the season-header badge animate out gracefully? What does the user see in the table — formerly-downloading rows just snap back to their "stream this" state? -->

<!-- AGENT_7 EXPAND START -->
**5.9.B - Post-stop visual state.**

After confirmed Stop, keep the badge visible for 2 seconds as `Stopped S1: 3 kept, 7 removed`, then fade out over 160ms. This avoids a jarring snap and tells the user the preservation semantics worked. If no episodes finished, text becomes `Stopped S1`.

Rows: finished episodes stay as check icons. Previously queued/downloading rows return to idle `download-arrow.svg`, but animate the icon opacity 0.5 to 1.0 over 120ms so the reset reads as intentional. If a row had an in-progress ring, ring fades out first, then icon returns. Do not leave failed-looking states after an intentional stop.

Toast only if the user navigates away during the stop: `Download stopped - 3 episodes kept`. If the user is on the table, badge + row state is enough.

References:
- NN/g visibility of system status: explain state changes caused by user action - https://www.nngroup.com/articles/visibility-system-status/
- Plex cancel semantics live in the in-progress queue and completed downloads remain separate - https://support.plex.tv/articles/downloads-for-ios/
- Material progress guidance: progress indicators should appear/disappear with the operation they represent - https://m1.material.io/components/progress-activity.html
<!-- AGENT_7 EXPAND END -->

### Step 10 — Movie show-views
Same panel, but scope-picker is degenerate: a single tile representing the movie file, pre-checked. No per-season collapsing. `[Cancel] [Download · X.X GB]`. After completion: movie-row LOCAL chip in the H2 movieActionRow lights up.

<!-- AGENT_7_EXPAND: 5.10.A — movie scope-picker visual treatment when there's only one episode tile. The whole panel feels almost empty. Should we use the extra space for pack details (resolution, encoding, audio language)? -->

<!-- AGENT_7 EXPAND START -->
**5.10.A - Movie one-tile scope picker.**

For movies, make the scope picker feel like a confirmation/detail surface, not an empty episode selector. Top card: poster thumbnail if already available from the show/movie detail cache, otherwise a 64x36 dark placeholder using `file.svg`. Text: movie title, year if known, selected release title elided below. Checkbox is still visible and pre-checked so the interaction model stays consistent with episodes.

Below the card, show a `Pack details` block with compact rows: `Resolution`, `Size`, `Seeders`, `Source`, `Score`, `Audio` if parseable. Use two-column label/value rows; no chips beyond type/source in the header. If details are unknown until metadata arrives, show `Checking...` for values and update quietly.

Bottom bar: `Cancel` + `Download movie - X.X GB`. If the movie is already local, show `Have` badge and pre-uncheck only if the selected pack appears lower or equal quality; if Phase C quality score says higher quality, pre-check and secondary text `Higher-quality copy available`. That is the one place where smart default beats strict skip.

References:
- Stripe final-button commitment pattern - https://docs.stripe.com/payments/accept-a-payment?payment-ui=elements
- Plex item-level downloads from the media detail screen - https://support.plex.tv/articles/downloads-for-ios/
- Figma right sidebar pattern: details for the selected object fill the right panel - https://help.figma.com/hc/en-us/articles/360039832014-Design-prototype-and-explore-layer-properties-in-the-right-sidebar
<!-- AGENT_7 EXPAND END -->

<!-- AGENT_7 EXPAND START -->
**Section 5 Cross-cutting review pass.**

The main UX risk in this arc is not torrent complexity; it is mode confusion. The user starts in Theatre, wants local playback later, and should never need to learn Tankorent's file-tree vocabulary. Every Section 5 surface should therefore use media-language nouns first (`episodes`, `season`, `movie`, `packs`) and torrent-language nouns only in secondary tooltips or details (`seeders`, `metadata`, `file priorities`). qBittorrent and Deluge prove that raw file-priority control is powerful, but that is explicitly the layer this Theatre panel is replacing.

One wording pushback on Section 3: decision 11 says fallback should `silently` widen to whole-show packs. I recommend preserving automatic fallback but rejecting actual silence. A compact inline explanation is necessary because a user who asked for S2 and sees `Complete Series` needs to know the app is helping, not changing scope behind their back. This is a UX wording correction, not an architecture reversal.

Use one component vocabulary across the flow. Pack rows should visually inherit `StreamSourceCard`. Episode/movie scope tiles should inherit `TileCard` density and the existing checkbox SVG discipline from the Netflix downloads overhaul. Buttons should inherit `DetailDownloadSeasonBtn` / `DetailMovieTankorentBtn`. Toasts should inherit `Toast`, but any blue action text must be neutralized for this arc because the no-color rule is explicit.

Progress needs two layers and no more: persistent local progress in the season header + per-row action icon/ring; optional global hint on the Theatre landing while any download is active. Do not build a mini transfers tab inside every show page. The existing Tankorent transfers tab remains the expert dashboard.

Confirmation policy should be consistent: no confirmation for normal downloads, soft in-panel confirmation for very large downloads, confirmation for Stop because unfinished files are discarded. This keeps weekly downloads fast while still protecting high-regret actions.

Accessibility and keyboard behavior need to be designed up front, not patched later: roving focus in pack list and tile grid, visible grayscale focus borders, Space toggles, Enter confirms, Escape backs out one step. The panel is complex enough that mouse-only design will age badly.

External references used across Section 5:
- Netflix Help, `How to use Smart Downloads` - https://help.netflix.com/en/node/122916
- Spotify Support, `Listen offline` - https://support.spotify.com/in-en/article/listen-offline/
- Plex Support, `Downloads for iOS and Android Mobile` - https://support.plex.tv/articles/downloads-for-ios/
- Plex Support, `Downloads Overview` - https://support.plex.tv/articles/downloads-overview/
- Stremio Help, `Download videos` - https://stremio.zendesk.com/hc/en-us/articles/360021228252-Download-videos
- Material Design 3 Lists - https://m3.material.io/components/lists/overview
- Material Design 3 Side sheets - https://m3.material.io/components/side-sheets/overview
- Material Design progress/activity - https://m1.material.io/components/progress-activity.html
- Material Design empty states - https://m1.material.io/patterns/empty-states.html
- Apple HIG Popovers - https://developer.apple.com/design/human-interface-guidelines/popovers/
- Apple NSProgressIndicator - https://developer.apple.com/documentation/appkit/nsprogressindicator
- NN/g `Visibility of System Status` - https://www.nngroup.com/articles/visibility-system-status/
- NN/g `Response Times: The 3 Important Limits` - https://www.nngroup.com/articles/response-times-3-important-limits/
- Linear Docs, `Filters` - https://linear.app/docs/filters
- Linear Docs, `Peek preview` - https://linear.app/docs/peek
- Notion Help, `Views, filters, sorts and groups` - https://www.notion.com/help/views-filters-and-sorts
- Figma Help, right sidebar properties panel - https://help.figma.com/hc/en-us/articles/360039832014-Design-prototype-and-explore-layer-properties-in-the-right-sidebar
- qBittorrent Wiki, `Explanation of Options` - https://github.com/qbittorrent/qBittorrent/wiki/Explanation-of-Options-in-qBittorrent
- Stripe Docs, `Accept a payment` - https://docs.stripe.com/payments/accept-a-payment?payment-ui=elements
- Microsoft Learn, `Confirmations` - https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-confirm
- Apollo GraphQL Docs, UX writing confirmation guidance - https://www.apollographql.com/docs/ux-writing-style-guide
<!-- AGENT_7 EXPAND END -->

## 6. Out of scope (explicit punts)

- **Per-show quality preference learning.** "User prefers 1080p WEB-DL for Daredevil; default to that next time." Possible later feature; not v1.
- **Power-user file-tree access.** The hard-remove of AddTorrentDialog means there's no "show me the raw torrent file tree" escape hatch in Theatre. If this turns out to be a hole, add it in v1.x.
- **Settings → Theatre → Quality slider.** The 0.6 quality / 0.4 health weights are fixed in v1.
- **Pre-flight verification.** We don't independently validate that a pack's contents actually match its claimed scope before download. If a "Complete Series" pack turns out to be missing S03, the user discovers that on completion (registered episode count vs expected). Future work.
- **Cross-mode unification with Books / Comics download flows.** This arc is Theatre-only. The TANKOYOMI_PREMIUM arc has its own picker pattern; we don't unify with it.
- **Per-pack trust signals beyond source chip.** Stremio addons have addon-side filtering (Torrentio trust scores, language filters, debrid integration); Tankorent indexer hits are raw. We surface source-of-origin via chip but don't compute or display a trust score. Future work.
- **Bandwidth + global download priority controls.** Speed limits, scheduled downloads, per-show throttles — all defer. libtorrent handles concurrent torrents with its built-in bandwidth pool; user can pause via the Stop button per-pack but not tune the pool.

## 7. Success criteria (how we know it shipped)

1. **End-to-end smoke:** open Theatre → pick a popular show (Sopranos / Daredevil) → click Download → pick a Complete Series pack from the unified list → confirm scope (default = all episodes) → Download → libtorrent downloads → on completion, every episode in show-view shows ✓ + clicking plays local file. Zero seconds of UI spent in Tankorent page or AddTorrentDialog.
2. **Older shows work:** open The Sopranos → Download → list shows Complete Series packs (S1-S6 multi-season) → pick one → scope-picker pre-checks all 86 episodes → Download → all episodes register against `(tt_sopranos, season_N, episode_N)` keys via the multi-season probe.
3. **Empty case handles cleanly:** Star Wars: Maul - Shadow Lord (2026 pilot, zero indexer results) → search runs → status says "No season packs found · showing whole-show packs instead" OR "No packs available; try streaming this episode."
4. **Movie path:** open Inception detail → Download → list shows movie packs → pick one → scope picker shows single tile pre-checked → Download → after completion, movie-row LOCAL chip in movieActionRow lights up.
5. **Resume across restart:** download a season, watch S1E3 for 5 minutes, close app, reopen → S1E3 row shows resume position (Phase F UnifiedProgressStore identity-keyed retrieval).
6. **Cancel-and-preserve:** start downloading S1 (10 episodes), wait until 3 finish, click Stop → 3 finished episodes show ✓ in show-view + play locally; 7 remaining episodes drop back to "stream this" state cleanly.

## 8. Prior arc state we're building on

- **TANKORENT_STREAM_INTEGRATION** (shipped 2026-05-15) — Phases A-G. Plan at `docs/superpowers/plans/2026-05-15-tankorent-stream-integration.md`. Spec at `docs/superpowers/specs/2026-05-15-tankorent-stream-integration-design.md`. Smoke + findings at `agents/audits/tankorent_stream_integration_smoke_2026-05-15.md`.
- **STREAM_DOWNLOADS_NETFLIX_OVERHAUL** (shipped 2026-05-12, 10 phases) — established the tick-as-action UX (kColAction ✓ icon as the downloaded-state indicator) + the morphing "Download Season" button. Referenced inline at StreamDetailView.cpp:1133-1140.
- **STREAM_DOWNLOADED_LIBRARY Phase 2-4** (shipped 2026-05-10) — the `StreamDownloadIndex` wiring + `onPlayLocalFileFromStreamRequested` signal + `MainWindow::onPlayLocalFileFromStreamRequested` handler (the local-file playback path).
- **STREAM_SERVER_PIVOT** (shipped 2026-05-05) — Stremio's `stream-server.exe` replaced libtorrent for streaming-only. Confirms that streaming path stays separate from the download path this arc unifies.
- **TANKOYOMI_PREMIUM** (Agent 1, shipped 2026-05-15, 9 phases) — parallel arc with a different picker pattern (manga volume catalog + TorrentVolumeProvider). Not directly merged with this arc but the architecture lessons (manga premium catalog + selective single-volume libtorrent file-priority download) inform our scope-picker thinking.

## 9. OPEN — for Codex (Agent 7) UI/UX expansion

This section is intentionally underspecified. Per gov-v4 Rule 20 in-place co-authorship pattern, Codex (Agent 7) will review and expand this brainstorm-md, focusing on the `AGENT_7_EXPAND` HTML-comment markers embedded throughout Section 5. The brief Codex will receive emphasizes:

1. **Comprehensive scope analysis.** Before expanding, Codex should read the full vision section (1) + locked-in decisions (3) + architecture (4) + UX flow (5) end-to-end and articulate what's actually being asked for in plain language. No expanding-in-isolation.
2. **User-friendliness + simplicity as the priority axis.** Hemanth's verbatim direction: *"keep user-friendliness and simplicity when expanding on details about the UI and UX"*. Clever-but-confusing patterns lose to obvious-but-friendly patterns every time.
3. **Internet research authorized.** Codex is encouraged to search for current best-practice UI/UX patterns: Netflix's download flow, Plex's request/download patterns, Stremio's native UX, Spotify's per-album download toggles, Linear/Notion/Stripe interaction patterns, Material Design 3 + Apple HIG + Nielsen Norman Group reference principles.
4. **Concrete + specific.** Each `AGENT_7_EXPAND` marker should resolve to actual visual treatment + interaction details, not platitudes. Specific timings, specific affordances, specific transitions. If Codex proposes a pattern, name the reference it's drawing from.
5. **Tankoban constraints respected.** No color (`feedback_no_color_no_emoji.md`), grayscale UI, dark theme, glassy/cinema aesthetic, existing `TileStrip` / `TileCard` / source-card primitive vocabulary should inform the new components.
6. **Pushback welcome.** If a locked-in decision from Section 3 looks suboptimal once Codex thinks through the user flow end-to-end, flag it explicitly. The decisions in Section 3 are ratified for the structural shape of the arc; the UI/UX details on top of them are open.
7. **Out-of-scope items stay out-of-scope.** Section 6's punts are not up for re-litigation.

Codex's expansion lands inline as HTML-comment-bracketed insertions (`<!-- AGENT_7 EXPAND START / AGENT_7 EXPAND END -->`) immediately after each `AGENT_7_EXPAND` marker, leaving Agent 4's original prose visible alongside Codex's expansion. Co-authorship attribution stays clear.

After Codex's pass, Hemanth reviews the expanded document. Then `/superpowers:writing-plans` fires off the consolidated brainstorm + expansion.
