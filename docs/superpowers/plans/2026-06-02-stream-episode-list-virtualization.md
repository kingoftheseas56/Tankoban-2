# STREAM_EPISODE_LIST_VIRTUALIZATION + click-bug — plan (2026-06-02)

**Owner:** Agent 4 (Stream mode). **Authorized by Hemanth** ("go straight for the rewrite", ultracode).
**Source:** 8-agent investigation workflow `agents/workflows/stream-episode-rewrite-investigate.js`
(run `wf_d3365a7b-07b`) — dispatch/render/perf/reference/API census + 2 adversarial verdicts + synthesis.

## Problem (three independent causes; #1 owned separately under TORRENT_DB_DURABILITY)
1. **Corrupt torrents.db** (Agent 0; dominant app-wide hang) — see `2026-06-02-torrent-db-durability.md`.
2. **Episode-list freeze/jank** — `populateEpisodeTable` (StreamDetailView.cpp:1043) builds ~8–9 live
   QWidgets/row via `setCellWidget` (≈9–12k widgets for One Piece's ~1000 eps), defeating QTableView
   viewport virtualization; **and** a 1Hz timer (cpp:114-120) + every `StreamDownloadIndex` change
   (cpp:1475) run `refreshAllEpisodeRows()` (cpp:1444) with **no visibility guard**, each row doing
   `QFileInfo::exists` (cpp:1296) + a full `listActive()` scan (cpp:1312) + `streamBulkSnapshotForImdbSeason`
   (cpp:1340) → O(rows×torrents) UI-thread work/second.
3. **Silent dead-click** — the click IS wired correctly (`onActionIconClicked`→`singleEpisodeDownloadRequested`
   →`StreamPage::startAutoDownload`) and works for normal shows (Invincible: full chain in events.jsonl).
   For One Piece the async `m_streamAggregator->load(req)` returns **neither** `streamsReady` nor
   `streamError`, so the one-shot callbacks never fire → click dies with **zero feedback** (no timeout).
   Plus `startAutoDownload` has **no in-flight guard** — rapid clicks overwrite a single `m_pendingAuto`
   and orphan prior one-shot handlers, silently dropping all but the last. The kitsu/anime absolute-episode
   request-id mapping is a **strong-but-UNPROVEN** suspect (one verdict attributed the captured failure to
   app shutdown ~3s post-click) — instrument before chasing it.

## Target architecture
The codebase's **first** true model/view (negative finding: no existing QAbstractItemModel/QTableView+setModel
anywhere). `EpisodeListModel : QAbstractTableModel` feeding a `QTableView`, with per-column
`QStyledItemDelegate` paint + delegate hit-testing on `clicked()`. Keep the 7-column geometry verbatim
(checkbox/episode/thumb/title/progress/status/action; Fixed/Stretch; 84px rows). True virtualization →
1000 eps cost ~1000 POD rows + zero widgets; only ~12–15 visible rows paint. Heavy state derivation moves
behind a **cache** in the model, recomputed only for visible rows (1Hz) or the single
(imdb,season,episode) row that `entryStateChanged` names; snapshot fetched **once** per pass, not per row.
**External contract (constructor, all 18 signals, all public methods, m_seasons source-of-truth) stays
byte-stable** — rewrite is internal to episode-list rendering. Behavioral contract preserved: **row click
never downloads** (play/source-pick); **action glyph** downloads/pauses/resumes/plays.

In-repo paint exemplars to lift: `TankorentPage::TitleCellDelegate` (TankorentPage.cpp:425-507, segmented
text + hardcoded sizeHint + drawControl re-init-trap fix), `ShowView`/`SeriesView` progress delegates
(recolor green→gray per no-color rule), `EpisodeTile::paintEvent` (EpisodeTile.cpp:190-255 chip/progress,
already grayscale), `BookResultsGrid::setCoverPixmap` (scale-once discipline). **EpisodeTile stays** (used
by TheatreDownloadPanel) but is the WRONG model for 1000+ rows — lift its paint only.

## Tasks (each BUILD-OK-gated, contract-preserving, TDD where logic exists)

- **T1 — Lock the state seam under test.** Extend GoogleTest on the existing pure unit
  `deriveEpisodeDisplayState` (src/core/stream/EpisodeDisplayState.*) for the One-Piece Published-but-no-index
  case + pre-alloc-partial case. *Test-only; no src change.* `ctest -R episode_display_state` green.

- **T2 — PERF RELIEF FIRST (on the existing QTableWidget).** Rewrite `refreshAllEpisodeRows`
  (cpp:1444) to compute the visible row range via `indexAt(viewport top/bottom)` and loop only those;
  fetch `streamBulkSnapshotForImdbSeason` **once** before the loop and pass into a snapshot-taking
  `refreshEpisodeRow` overload (removes per-row :1340 and the second :1384 fetch); add an imdbId guard to
  the `entriesChanged` handler (:1475). *Low risk, behavior-preserving.* Smoke: One Piece scroll no longer
  "Not Responding"; events.jsonl no per-second full-season churn.

- **T3 — CLICK-BUG FIX (independent of the rewrite).** In `StreamPage::startAutoDownload` (StreamPage.cpp:3243):
  (a) per-request monotonic **token** captured in both one-shot lambdas (stale token = ignore);
  (b) **in-flight guard** so a second click re-arms cleanly instead of orphaning; (c) single-shot
  **guaranteed-terminal QTimer** (~20s, env-tunable) that, if neither `streamsReady` nor `streamError`
  arrives, calls `m_detailView->setStreamSourcesError("No sources found — try again")` and clears
  `m_pendingAuto`; (d) `qInfo` the constructed `req.id` (kitsu vs imdb:season:episode) so the anime path is
  diagnosable. *Medium risk.* Smoke: One Piece click ALWAYS reaches a terminal; Invincible still completes.
  (Run against a **healthy** DB so a TORRENT_DB failure isn't mis-attributed.)

- **T4 — Model skeleton (additive, BUILD-OK, unwired).** `EpisodeListModel : QAbstractTableModel`
  (rowCount/columnCount/data/flags/headerData, `setSeason(QList<StreamEpisode>)`, custom roles
  Episode/Season/State/Progress/Thumb/Overview/Checked, injected `stateProvider` std::function,
  `refreshRows(QList<int>)`, `setChecked`/`checkedEpisodes`, `setThumbnail`). grep-verify CMakeLists.

- **T5 — 5 column delegates (additive, BUILD-OK, unwired).** `EpisodeDelegates.{h,cpp}`:
  Checkbox (SVG, NOT Qt CheckStateRole — Win11 clip bug at cpp:1065-1074), Thumb (scale-once),
  Title (port TankorentPage TitleCellDelegate), ProgressStatus (lift EpisodeTile paint, grayscale),
  Action (state glyph / "Play" pill + static `actionGlyphRect(option.rect)` shared with hit-test). Reuse the
  exact `:/icons/*.svg` from refreshEpisodeRow (:1416-1439, :1086-1094).

- **T6 — Cut over rendering.** buildUI: replace `m_episodeTable` (QTableWidget) with `m_episodeView`
  (QTableView) + `m_episodeModel` + `setItemDelegateForColumn`×5; identical resize modes/widths/84px/QSS.
  `populateEpisodeTable` → `m_episodeModel->setSeason(...)`. Delete per-row widget construction +
  refreshEpisodeRow widget mutation (state now flows model→dataChanged→delegate). *High risk — the core swap.*

- **T7 — Re-wire gestures (contract preservation).** `clicked(QModelIndex)`: action col → hit-test
  `actionGlyphRect` → `onActionIconClicked` (unchanged switch, still emits singleEpisodeDownloadRequested);
  checkbox col → toggle CheckedRole + sync `m_selectedEpisodes` + updateDownloadSelectedButton; else →
  `onEpisodeActivated` (unchanged play/source-pick). Re-point context menu, `rowForEpisode`,
  uncheck-all, onSeriesMetaReady preselect, `devSnapshot` to model indices/roles. mouseTracking+entered for
  hover cursor. *High risk — contract lives here.* Verify: row click ≠ download; glyph = download; selection;
  preselect; devSnapshot intact. (PMF connects fail to compile = safety net.)

- **T8 — Visible-range async thumbnail loader.** Replace per-row populate-time fetch (:1146, up to ~1000
  concurrent GETs) with a visible-range fetcher capped to a small in-flight pool; reply →
  `setData(index, scaled QPixmap, ThumbRole)` → repaint one row. Keep `episodeThumbPath` disk-cache key.

- **T9 — Stop the 1Hz timer when no active transfers + final verification.** Gate
  `m_progressRefreshTimer` (started showEvent / stopped hideEvent) to also stop when zero visible rows are
  Downloading/Paused, restarting on entryStateChanged/streamBulkGroupsChanged. Full smoke: One Piece open +
  scroll + click→download with terminal feedback; Invincible regression.

## Risks
- First true model/view in repo (no model to copy) — T4/T5 additive skeletons; delegate paint lifts proven
  TankorentPage/EpisodeTile code; T1 locks the seam first.
- Delegate hit-test geometry must match paint geometry — one shared static `actionGlyphRect` helper.
- CMakeLists.txt collision (flat-on-master) — grep-verify each source add; one focused edit/task.
- Contract regression on cutover — PMF compile safety net + explicit T7 verify checklist.
- T3 timeout value is a guess — log req.id + observed round-trips (Invincible ~1–4s) and tune; guaranteed
  terminal is strictly better than the current silent hang regardless.

## Open questions (recommendations; not blocking)
- T3 timeout 20s — make env-tunable; confirm acceptable.
- Chase the kitsu absolute-episode mapping now, or ship T3 terminal-guard + instrument first? **Recommend**
  instrument first; only fix the mapping if the log confirms a never-resolving kitsu fetch.
- Keep the 3-column progress/status/action geometry (recommend yes, minimize drift).
