# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankoyomi search UX — cover cache, grid widget, view toggle, empty state, loading state
Reference spec: `C:\Users\Suprabha\Downloads\mihon-main\mihon-main\` (Mihon Kotlin/Android), specifically the browse-source stack and presentation-core screens
Objective: feature parity vs Mihon's browse/source search surface (per Agent 4's READY FOR REVIEW line chat.md:6415)
Files reviewed:
- `src/ui/pages/TankoyomiPage.h` (B1–B5 members, signal decl)
- `src/ui/pages/TankoyomiPage.cpp` (B1 ensureCover, B3 view toggle, B4 empty copy, B5 loading page, C1 toast wire)
- `src/ui/pages/tankoyomi/MangaResultsGrid.h` / `.cpp` (B2 grid widget)
- `CMakeLists.txt` additions (new grid source + header — checked additive only, not expanded here)

Date: 2026-04-14

### Scope

Compared batches B1–B5 of Agent 4's Tankoyomi search UX against Mihon's browse stack: `MangaCoverFetcher.kt` (data/coil), `BrowseSourceScreen.kt` + `BrowseSourceComfortableGrid.kt` + `CommonMangaItem.kt` (presentation/browse/components + presentation/library/components), `LibraryDisplayMode.kt` (domain/library/model), and the shared `EmptyScreen.kt` / `LoadingScreen.kt` in presentation-core. In scope: disk-caching semantics for result covers, grid/list view parity and persistence, empty-state copy and affordances, loading-state indicator. Out of scope: A-track (pause/resume/cancel — already PASSED), source/catalog management (no Tankoban analogue to Mihon's multi-extension sources), manga detail panel (Agent 4 flagged as C3, not shipped), category pickers, reader launch, any Tankostream code. C1 (toast/snackbar) is listed as "parallel, in progress" in the READY FOR REVIEW line but its wire-up already appears in `TankoyomiPage.cpp:109-122` and `:571-584`; I've included it as parity evidence where relevant but not gated on it. Platform-specific divergences (Compose Material3 → Qt widgets, moko-resources → hardcoded English, Coil disk cache → filesystem-backed `m_posterCacheDir`, paging APIs → one-shot scraper payloads) are accepted where Agent 4's batch notes disclosed them.

### Parity (Present)

- **Per-cover disk cache with in-flight dedupe and source-specific Referer** — reference: `MangaCoverFetcher.kt:107-150` (disk-cache snapshot → network fetch → write back) + `Downloader.kt:316-323`-style Referer pattern → Tankoban: `TankoyomiPage.cpp:669-724` (`ensureCover` checks cache, dedupes via `m_coversInFlight`, sets per-source Referer). Cache-hit emits via queued `QTimer::singleShot(0, ...)` so the signal is async — matches Coil's asynchronous semantics without the library.
- **Cover key = `{source}_{sanitized-id}.jpg`** — reference: Mihon keys by `diskCacheKey` derived from `thumbnail_url` (`MangaCoverFetcher.kt:60-61`); avoids slashy URLs by hashing under the hood → Tankoban: `TankoyomiPage.cpp:673-677` sanitizes `id` with `R"([<>:"/\\|?*\s])"` and prefixes source. Different key derivation, same defensive intent.
- **Grid item = cover + title + subtitle** — reference: `BrowseSourceComfortableGrid.kt:58-80` + `CommonMangaItem.kt` with `MangaComfortableGridItem(title, coverData, ...)` → Tankoban: `MangaResultsGrid.cpp:52-65` creates `TileCard(thumb, title, subtitle)` where subtitle = author ∥ source. Same three-field layout; subtitle fallback chain is a sensible Qt adaptation.
- **Grid populated eagerly; covers fill in asynchronously** — reference: Coil's `Fetcher` returns a `FetchResult` that Compose renders when ready → Tankoban: `MangaResultsGrid::onCoverReady` finds the matching tile by `mangaSource`/`mangaId` property and calls `setThumbPath` (`MangaResultsGrid.cpp:68-78`). Same fill-in-as-arrived pattern.
- **View-mode persisted to preferences** — reference: `LibraryDisplayMode.kt:10-42` serializes to a string token stored via `LibraryPreferences` → Tankoban: `TankoyomiPage.cpp:286-292` writes `"list"`/`"grid"` to `QSettings` key `tankoyomi/resultsView`; `:406-407` restores on startup. Schema differs (4 modes vs 2) but the persistence shape is equivalent.
- **List/grid switchable on a single toggle** — reference: `BrowseSourceScreen.kt:120-147` `when (displayMode) { ComfortableGrid → ... List → ... CompactGrid → ... }` → Tankoban: `QStackedWidget` with list + grid pages driven by `m_preferredDataView` (`TankoyomiPage.cpp:400-407`). Same stack-of-views architecture.
- **Empty-state distinct from loading-state** — reference: `BrowseSourceScreen.kt:76-118` — `LoadingScreen` on refresh-while-empty, `EmptyScreen` on done-but-zero → Tankoban: stack pages 2 (empty) and 3 (loading), picked by `updateResultsView()` based on `m_pendingSearches > 0` vs `m_displayedResults.isEmpty()` (`TankoyomiPage.cpp:644-667`). Same state machine.
- **Empty-state copy switches on "has the user searched yet"** — reference: Mihon uses `no_results_found` string (post-search) vs error actions (first load) → Tankoban: `m_lastQuery.isEmpty()` chooses `"Search manga & comics above"` vs `"No results for \"{query}\""` (`TankoyomiPage.cpp:658-663`). Pre-search vs post-search split matches the Mihon distinction.
- **Loading indicator is indeterminate** — reference: `LoadingScreen.kt:11-18` uses `CircularProgressIndicator` with no progress value → Tankoban: `QProgressBar` with `setRange(0, 0)` (`TankoyomiPage.cpp:388-396`). Same semantic; linear vs circular is a Qt/Compose idiom split, not a parity miss.
- **Loading copy personalised to the query** — reference: Mihon's `LoadingScreen` shows only a spinner → Tankoban: label reads `"Searching for \"{query}\"..."` when `m_lastQuery` is set (`TankoyomiPage.cpp:650-654`). This is *better* than Mihon; flagging only so the judgment is on the record.
- **Per-source failure surfaced via transient Toast with Retry** — reference: `BrowseSourceScreen.kt:62-73` uses `SnackbarHostState.showSnackbar(actionLabel = action_retry, duration = Indefinite)` on error-with-results-present → Tankoban: `TankoyomiPage.cpp:109-125` + `:571-588` show a `Toast` with "Retry" action on all-failed, or a silent toast on partial failure. Close behavioural match; Toast vs Snackbar is a widget-family swap.
- **Clean transition back to empty-state on all-sources-failed** — reference: implicit in Mihon's flow (empty itemCount + error → EmptyScreen with error message) → Tankoban: `TankoyomiPage.cpp:124, 586` explicitly re-enters `updateResultsView()` after each error to drop out of the loading page. Ships with the fix Agent 4 disclosed for the pre-existing "errors-never-cleared" bug.
- **Scrollable grid container** — reference: Compose `LazyVerticalGrid` (`BrowseSourceComfortableGrid.kt:29`) → Tankoban: `QScrollArea` wrapping `TileStrip` (`MangaResultsGrid.cpp:17-26`) with horizontal scrollbar disabled. Qt-idiomatic equivalent.

### Gaps (Missing or Simplified)

Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**

None. The search-UX pipeline renders, fetches covers, toggles modes, persists preference, handles empty/loading/error — the core flow is intact.

**P1:**

- **No "already in library / already downloaded" indicator on grid tiles.** Reference: `BrowseSourceComfortableGrid.kt:73-76` dims the cover alpha to `CommonMangaItemDefaults.BrowseFavoriteCoverAlpha = 0.34f` (`CommonMangaItem.kt:54`) AND overlays `InLibraryBadge(enabled = manga.favorite)` (`components/BrowseBadges.kt:9`). Applied consistently across Compact/Comfortable/List — all three call `InLibraryBadge` (grep: `BrowseSourceCompactGrid.kt:75`, `BrowseSourceList.kt:68`). Tankoban: `MangaResultsGrid::setResults` (`MangaResultsGrid.cpp:47-66`) stamps every tile identically — no check against `m_bridge->rootFolders("comics")` for an existing `{title}` directory, no check against `MangaDownloader::listActive()`/`listHistory()` for an existing record. Impact: a user searches "one piece", sees 30 identical tiles, double-clicks one they already downloaded yesterday — the AddMangaDialog is the first signal that it's already on disk. The "already got it" state is a first-class affordance in Mihon's browse grid precisely because manga search results overlap heavily with library state; losing it means every search feels like a fresh discovery even for content the user owns. With R3 (skip-at-enqueue) now landed in the A-track, the data to drive this badge already exists — it's purely a UI gap.
- **Grid tile right-click is wired but unused.** Reference: `CommonMangaItem.kt` via `MangaComfortableGridItem` supports `onLongClick` (Compose long-press → context-aware action) and `BrowseSourceScreen.kt:51, 127, 134, 144` threads `onMangaLongClick` to every display mode — Mihon's long-press opens a preview/add-to-library affordance. Tankoban: `MangaResultsGrid.h:32` declares `resultRightClicked(int row, const QPoint& globalPos)` and `MangaResultsGrid.cpp:33-37` emits it on `TileStrip::tileRightClicked` — but `TankoyomiPage.cpp:73-77` only connects `resultActivated`. Grep confirms `resultRightClicked` has no `connect(...)` anywhere in TankoyomiPage.cpp. Impact: right-clicking a grid tile does nothing. The table view (list mode) *has* a right-click menu with Download + Copy Title (`TankoyomiPage.cpp:135-150`). Grid-mode users lose that affordance for the same data. This is a within-Tankoban inconsistency that would be caught the first time a user switches modes.
- **Empty-state on failure has no action buttons.** Reference: `BrowseSourceScreen.kt:81-118` — when `itemCount == 0`, `EmptyScreen` renders with `EmptyScreenAction`s: `action_retry` + `action_open_in_web_view` + `label_help` (or, for LocalSource, the help guide). These are tangible buttons wired to `mangaList::refresh`, `onWebViewClick`, `onHelpClick`. Tankoban: `m_emptyLabel` is a pure `QLabel` with text only (`TankoyomiPage.cpp:368-373`); no button row on either the pre-search or the zero-results page. Retry exists on the Toast for the error-after-pending case (C1), but a user who typed "xyzabc" and got zero results has no Retry button in front of them — they have to click back into the search field. Impact: lower than P2 polish; higher than nothing because "zero results" is the exact moment the user needs the most help.

**P2:**

- **Three-way display mode compressed to two.** Reference: `LibraryDisplayMode.kt:4-8` — `CompactGrid`, `ComfortableGrid`, `List`, `CoverOnlyGrid` (four). Tankoban: binary list/grid. Explicitly disclosed in Agent 4's READY FOR REVIEW line (chat.md:6439) — "future refinement fits in the same QSettings key." Accepted as deferred.
- **Default display mode differs.** Reference: `LibraryDisplayMode.kt:22` — `default = CompactGrid`. Tankoban: `"grid"` (comfortable with subtitle) is default on first launch (`TankoyomiPage.cpp:406`, `"grid"` fallback, resolves to `m_preferredDataView = 1`). Minor preference divergence; not clearly wrong given Tankoban lacks a CompactGrid equivalent today.
- **No multi-line title overflow indicator.** Reference: `CommonMangaItem.kt` relies on `TextOverflow.Ellipsis` inside MaterialTheme typography. Tankoban: delegated to `TileCard`'s existing behaviour (out of Agent 4's scope, in Agent 5's). Flagging only so it doesn't fall between domains.
- **Pre-search state lacks Mihon's kaomoji flourish.** Reference: `EmptyScreen.kt:57, 67-72, 102-115` picks a random face from `ErrorFaces` for both error and empty states. Tankoban: plain label. Per `feedback_no_color_no_emoji` in memory, emoji are off-limits — Tankoban's plainer take is consistent with that guidance. Flagging for the record, not as a gap.
- **No cover-lastModified cache invalidation.** Reference: `MangaCoverFetcher` accepts `MangaCover.lastModified` as part of the fetch key (via `BrowseSourceComfortableGridItem.kt:71`) so a server-side cover update replaces the cached version. Tankoban: cache key is `{source}_{id}.jpg` only; if a scraper updates a cover URL the old file is returned forever until manually purged. Impact: stale covers. Minor on these two scrapers where covers rarely change.
- **Loading page does not dim the prior results.** Reference: Mihon's paging shows a small `BrowseSourceLoadingItem` at the top of the existing grid (`BrowseSourceComfortableGrid.kt:35-39`), preserving context during incremental loads. Tankoban: a new search swaps to a full-page loader, losing the previous result context. Different architecture (one-shot payload vs paged), lower priority.
- **Subtitle falls back to raw source ID.** `MangaResultsGrid.cpp:55` uses `r.source` (e.g. `"weebcentral"`) when author is empty, instead of the human-readable name used elsewhere in the UI (e.g. `"WeebCentral"` from `m_sourceCombo->addItem`). Impact: minor visual inconsistency. One-liner to fix with a small source-id → display-name helper.

### Questions for Agent 4

1. **"Already downloaded" badge — B6 or C-track?** Is the in-library badge explicitly deferred (and where — chat.md reference please), or does it belong in a near-term batch? The data pipeline is already there (`MangaDownloader::listActive/listHistory` + `CoreBridge::rootFolders("comics")`), so the ask is "which batch/track owns the UI."
2. **Grid right-click intent.** Is the connect-miss at `TankoyomiPage.cpp:73-77` an oversight, or was `MangaResultsGrid::resultRightClicked` added speculatively for a C-track consumer? If the former, wiring it to the same Download/Copy-Title menu the table has is a one-block change.
3. **Empty-state actions.** Do you want a Retry / Clear-search action button on the zero-results empty state, or is Toast-on-error + re-focus-search-box considered sufficient UX?
4. **Toast lifecycle around view switches.** If a user clicks Grid/List mid-search, `updateResultsView()` fires but the toast from a prior error is not dismissed. Intended, or a cleanup oversight? (Not gating on this — just want it documented.)
5. **Cover cache invalidation strategy.** Is a cover TTL or lastModified-aware key on the roadmap, or do we accept "stale until manual purge" for the lifetime of this batch?

### Verdict
- [x] All P0 closed — none found.
- [x] All P1 closed or justified — Agent 4 shipped E1 (in-library badge + dim via setInLibraryKeys, computed from listActive + listHistory + comics rootFolders walk), E2 (showResultContextMenu helper factored from table; grid resultRightClicked now wired), E3 (empty page wraps Retry + Clear search buttons, post-search-only). Q1–Q3 answered with code; Q4 (toast lifecycle) and Q5 (cover invalidation) deferred with reasoning. P2s deferred with reasoning. Code spot-checked by Agent 6: TankoyomiPage.cpp:140-146, :409-431, :723-748, :909-924, MangaResultsGrid.cpp:87-131.
- [x] Ready for commit (Rule 11).

**Verdict: REVIEW PASSED — [Agent 4, B1–B5], 2026-04-14.**

Agent 4: address or defer each P1 with technical justification. P2s are advisory. Q1–Q5 in parallel. Once P1s are settled I'll post `REVIEW PASSED — [Agent 4, B1–B5]` in chat.md and archive this file.

---

## Agent 4 — Response (E-track, 2026-04-14)

All three P1s closed. Q1–Q5 answered. P2s addressed selectively (see below).

### P1 resolution

| P1 | Status | Batch | Files |
|----|--------|-------|-------|
| #1 In-library badge on grid tiles | Closed | **E1** | `MangaResultsGrid.h/.cpp` (new `setInLibraryKeys` + overlay) + `TankoyomiPage.cpp` (compute set in `renderResults` from `listActive` + `listHistory` + `rootFolders("comics")` directory walk) |
| #2 Grid right-click is wired but unused | Closed | **E2** | `TankoyomiPage.h/.cpp` — factored the table's Download/Copy-Title menu into `showResultContextMenu(row, globalPos)`, connected `MangaResultsGrid::resultRightClicked` to it |
| #3 Empty-state has no action buttons | Closed | **E3** | `TankoyomiPage.h/.cpp` — empty page now wraps label + Retry + Clear search buttons; buttons visible only post-search |

### Question answers

- **Q1 (badge ownership).** Not deferring. Shipped in **E1** this same review cycle. Heuristic combines `MangaDownloader::listActive()` + `listHistory()` titles with a directory walk over `m_bridge->rootFolders("comics")`. Lowercase + trim on both sides for casing tolerance.
- **Q2 (grid right-click).** Pure oversight from B2 — the signal was emitted with no consumer wired. **E2** wires it to the same menu the table uses, factored into a `showResultContextMenu` helper so the two surfaces can't drift again.
- **Q3 (empty-state actions).** Shipping Retry + Clear search in **E3**. No web-view or help button (no analogues in Tankoban). Buttons are post-search-only — the pre-search state stays text-only because there's nothing meaningful to retry or clear.
- **Q4 (toast lifecycle on view switch).** Accepting as-is. The toast has its own dismissal timer (3.5 s info / 4.5 s with action) and only one toast per parent — switching grid/list mid-error doesn't extend or stack it. The error message is independent of which view shows the data, so dismissing it on view switch would lose information mid-read. If users complain, easy follow-up.
- **Q5 (cover cache invalidation).** Deferred. Both scrapers serve covers off CDNs that almost never update; the cost of ignoring `lastModified` is occasionally-stale art for a tiny window between user searches. If that becomes a complaint, add a cache-key suffix from a future `MangaResult::coverLastModified` field. Not a code-now item.

### P2 disposition

Not shipping in this review cycle (deferred):
- **Three-way display mode (Compact/Comfortable/CoverOnly/List).** Same QSettings key supports it; future batch.
- **Default mode CompactGrid vs Comfortable.** Coupled with the above.
- **Multi-line title overflow.** Agent 5 territory (`TileCard`).
- **Pre-search kaomoji.** Blocked by `feedback_no_color_no_emoji`.
- **Cover lastModified key.** Q5 above.
- **Loading dim instead of full-page swap.** Architecturally tied to paged-vs-one-shot; deferred.
- **Subtitle source-id readability** ("weebcentral" → "WeebCentral"). One-line fix; will fold into the next batch that touches `MangaResultsGrid::setResults` to avoid a cosmetic-only rebuild.

Ready for verdict.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
