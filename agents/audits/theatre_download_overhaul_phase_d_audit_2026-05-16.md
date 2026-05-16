# Phase D Audit - THEATRE_DOWNLOAD_OVERHAUL - 2026-05-16 - Agent 7 (Codex)

## Verdict

NEEDS_FIXES

## Findings

### F1 - Critical - Movie scope can start with every file priority set to 0

Location: `src/ui/pages/stream/TheatreDownloadPanel.cpp:597`

What: Movie mode renders the degenerate `(0,0)` tile, but `onDownloadClicked()` only computes file priorities by iterating `m_scopeEstimate.detectedSeasons` and calling `BulkPackVerifier::matchEpisodeFileForSeason()` (`TheatreDownloadPanel.cpp:439-452`). In movie mode there are no seasons to iterate. If `m_realFiles` is populated before the user clicks Download, the loop writes priority `0` for every file.

Why: A movie pack with metadata ready can emit `downloadRequested()` with `config.filePriorities` containing all zeroes, so `TorrentClient::startDownload()` applies a no-download priority vector before starting the torrent (`TorrentClient.cpp:2160-2173`). The already-have movie edge is also broken: the tile is intentionally re-checkable, but the movie-mode toggle lambda does not re-enable the button (`TheatreDownloadPanel.cpp:608-612`), so a user cannot choose to replace an existing movie.

Suggested fix: Add a movie branch in `onDownloadClicked()`. If tile `(0,0)` is checked, either leave `filePriorities` empty to preserve libtorrent's default all-files behavior, or select the largest whitelisted video file at priority `4` and set non-video extras to `0`. If tile `(0,0)` is unchecked, keep the button disabled and abort. Also update the movie-mode `EpisodeTile::toggled` lambda to enable/disable the button live.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:629`; `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:377`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2188-2210`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2400-2428`.

### F2 - Critical - Complete-series packs with no season tokens can render an empty, disabled scope picker

Location: `src/ui/pages/stream/TheatreDownloadPanel.cpp:622`

What: `TitleMetadataEstimator::estimate()` returns early for a complete-series title with no detected season tokens, leaving both `detectedSeasons` and `episodes` empty (`TitleMetadataEstimator.cpp:48-51`). `rerenderScopePicker()` groups only `m_scopeEstimate.episodes`, so it adds no season header, no tile, and then disables the Download button through `updateSeriesDownloadButton()` (`TheatreDownloadPanel.cpp:622-658`). When metadata later arrives, `onMetadataReady()` rerenders but does not derive episodes from `m_realFiles` (`TheatreDownloadPanel.cpp:376-389`), so the empty state persists.

Why: Full-show and complete-series torrents are a central reason for this arc. The brainstorm explicitly calls out older shows whose useful packs are often Complete Series torrents, and the UX vision says a user should be able to pick full-show packs and select seasons/episodes inside the Theatre-native panel. Current Phase D can leave exactly those packs impossible to start from the scope picker.

Suggested fix: When the title estimate has no episodes, do not render a dead empty picker. Show a metadata-loading placeholder while `m_pendingMetadataHash` is active, then derive `(season, episode, fileIndex, size)` rows from `m_realFiles` using the same filename parsing contract as `BulkPackVerifier::matchEpisodeFileForSeason()`. If metadata is unavailable, provide a deliberate fallback path, such as "Download entire pack", rather than a disabled blank panel.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:16`; `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:26`; `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:351-363`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2164-2265`.

### F3 - Important - Real metadata is captured but not used to refresh tile titles, sizes, or file associations

Location: `src/ui/pages/stream/TheatreDownloadPanel.cpp:376`

What: `onMetadataReady()` stores `m_realFiles` and calls `rerenderScopePicker()`, but `rerenderScopePicker()` still builds series tiles solely from `m_scopeEstimate.episodes` and never reads `m_realFiles` (`TheatreDownloadPanel.cpp:638-644`). As a result, series tiles keep `sizeBytes = 0`, so `EpisodeTile` shows the unknown-size fallback even after real metadata is available (`EpisodeTile.cpp:40-46`).

Why: The UX spec says tiles refresh in place with real filenames, real sizes, and file-index associations once metadata arrives. Phase D's priority driver does use `m_realFiles`, but the visible scope picker does not, so the user never gets the confidence signal that Theatre has mapped the selected pack correctly.

Suggested fix: Build a metadata-derived episode map during `onMetadataReady()` or `rerenderScopePicker()`, then feed `EpisodeTileData::title` and `sizeBytes` from matched files. Preserve the existing `m_tileChecked` state across the rerender so a late metadata refresh does not undo user choices.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:284`; `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:361`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2336-2358`.

### F4 - Important - Per-season scope controls are below the Agent 7 UI spec

Location: `src/ui/pages/stream/TheatreDownloadPanel.cpp:627`

What: Season groups are plain labels with `"Season N - X episodes"` text. The implementation does not include the chevron, selected-count summary, per-season `All` toggle, collapse behavior, panel-wide `Select all`, or panel-wide `None` controls from the brainstorm expansion.

Why: The shipped D2 path is functionally usable for small season packs, but it falls short for multi-season and complete-series packs where bulk selection is the difference between a simple Theatre-native picker and a long manual checklist. This is especially important because the arc's main catalog gap is whole-show packs.

Suggested fix: Add panel-wide `Select all` / `None` text controls above the first group, and convert each season header from a `QLabel` into a compact header row with selected count plus tri-state `All` behavior. Collapse animation can be deferred if needed, but the bulk actions should land before this flow is considered UX-complete.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:359-364`; `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:391-395`.

### F5 - Minor - EpisodeTile visual fidelity is much tighter than the pixel spec

Location: `src/ui/pages/stream/EpisodeTile.cpp:12`

What: `EpisodeTile` is a 36px minimum-height row with transparent background and a bottom border. The expansion spec calls for 58px compact tiles, 72px when title plus filename-size display, 10px padding, 6px radius, visible resting background, and checked/unchecked visual states. The `Have` badge is present and grayscale, but it is inline, lacks the specified tooltip, and uses weaker contrast than the spec.

Why: This does not break the workflow, but it makes the scope picker read like a dense settings list instead of a Theatre-native selection surface. The mismatch will become more visible once full-show packs produce many rows.

Suggested fix: Keep the current widget class, but restyle it toward the specified tile card: stable 58px height, rounded border/background, secondary line for size/source text, and a badge tooltip reading `Already downloaded - leave unchecked to skip`.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:359-377`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2000-2141`.

### F6 - Minor - Download confirmation feedback is not yet wired

Location: `src/ui/pages/stream/TheatreDownloadPanel.cpp:469`

What: After emitting `downloadRequested()`, `onDownloadClicked()` immediately calls `reset()`. There is no 120ms `Starting...` button state, no toast, and no durable season-header progress badge in this phase.

Why: This is not a Phase D blocker by itself because Phase E owns the show-view integration and Phase F owns progress surfaces, but it is a carry-forward that must not be lost. Without confirmation, a multi-GB download click can feel like the panel simply disappeared.

Suggested fix: Carry this into Phase E/F: the host-side `downloadRequested` handler should perform the 180ms panel swap and show a compact confirmation once `TorrentClient::startDownload()` is invoked. The season-header progress badge should appear within the same second when Phase F lands.

Brainstorm/plan ref: `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md:457-463`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2600-2612`; `docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md:2670-2682`.

## Phase D summary

D1: Mostly matches the plan's compact widget contract, but visual treatment is below the Agent 7 expansion spec and the size fallback remains unknown for series rows.

D2: Basic per-season rendering and smart-skip state exist, but movie toggle handling and empty complete-series state are not safe enough to build Phase E on top.

D3: The `Qt::QueuedConnection` is correct because `TorrentEngine::metadataReady` is emitted from the alert worker path (`TorrentEngine.cpp:144`), and the hash guard filters stale cross-pack replies. The captured metadata is not yet used by the visible tile renderer.

D4: The normal series-season priority path is directionally correct, including priority `4`, reset cleanup, and stale metadata clearing. Movie mode and no-detected-season complete-series mode need fixes before integration.

## Carry-forwards for Phase E / F / G

- Stop before Phase E until F1 and F2 are fixed; both are critical because they can make user-visible download starts fail or become impossible.
- Preserve the D3 hash guard and queued connection shape.
- Keep the empty-`m_realFiles` fallback behavior documented at `TheatreDownloadPanel.cpp:424-429`; it gracefully lets libtorrent use default all-file priorities when metadata is unavailable.
- When Phase E wires `downloadRequested`, ensure the host actually calls `m_torrentClient->startDownload(infoHash, config)` and adds the confirmation moment from brainstorm 5.6.A.
- Phase F should treat persistent progress feedback as a required companion to the transient toast, not a polish-only item.
