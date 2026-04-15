# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankoyomi polish — C1 Toast widget, C2 sort menu, C3 pre-download detail panel
Reference spec: `C:\Users\Suprabha\Downloads\mihon-main\mihon-main\` (Mihon Kotlin/Android) — browse-source + manga-screen stacks; Material3 Snackbar pattern for C1
Objective: UX parity with Mihon's browse/manga surfaces where the pattern exists; Tankoban-original surfaces where it doesn't
Files reviewed:
- `src/ui/widgets/Toast.h` / `.cpp` (C1 — new)
- `src/ui/dialogs/AddMangaDialog.h` / `.cpp` (C3 — detail panel added)
- `src/ui/pages/TankoyomiPage.cpp` (C1 toast wire at :109-125 + :571-588; C2 combo at :276-297 + sort at :672-694; C3 dialog wire at :854-868)
- `CMakeLists.txt` additions (Toast source + header, Agent 4's disclosure in chat.md)

Date: 2026-04-14

### Scope

Compared Agent 4's C1–C3 polish batches against Mihon's Material3 Snackbar usage in `BrowseSourceScreen.kt:62-73` / `107` (for C1) and `MangaScreen.kt` + `MangaInfoHeader.kt:344-379` (for C3). C2 has no direct Mihon analogue — Mihon source-search does not expose a result-level sort; the nearest pattern is `DownloadQueueScreen.kt:124-196`'s sort dropdown for a different surface (download queue), which Agent 4 correctly cites as inspiration, not parity. Out of scope: all earlier A/B/E/R batches (archived) and the Tankorent A–F Nyaa-rework bundle (queued separately in chat.md:7401). Static read only; no build.

### Parity (Present)

- **C1 — Single-instance transient notification per parent.** Reference: Material3 `SnackbarHostState.showSnackbar(...)` at `BrowseSourceScreen.kt:62-73` presents one snackbar at a time per host. Tankoban: `Toast::show` static factory at `Toast.cpp:23-42` calls `clearExistingToast(parent)` (`:13-21`) before inserting a new `Toast*` child, enforcing the single-instance invariant via `findChildren<Toast*>(QString(), Qt::FindDirectChildrenOnly)`. ✓
- **C1 — Optional action button (Retry) dismisses on click, fires callback.** Reference: `BrowseSourceScreen.kt:62-73` surfaces `action_retry` with `SnackbarResult.ActionPerformed → mangaList.retry()`. Tankoban: `Toast.cpp:73-86` — action button click hides + `deleteLater()` + fires the callback *after* copying it locally (`auto cb = m_onAction;`) so a re-entrant `Toast::show()` from the callback can't chew the this-pointer underneath. Defensive pattern. ✓
- **C1 — Auto-dismiss timer.** Reference: default `SnackbarDuration.Short` / `Long` in Compose Material3. Tankoban: `Toast.cpp:94-101` `QTimer` singleShot with 3500 ms info / 4500 ms action. Agent 4 disclosed bounded durations as a conscious deviation from Mihon's `Indefinite` for error+retry; desktop user is in-window so bounded is appropriate. ✓
- **C1 — Parent-resize tracking via event filter.** Reference: Compose snackbar lays out relative to scaffold on recomposition. Tankoban: `Toast.cpp:92` installs eventFilter on parent; `:104-110` handles `QEvent::Resize`; `:112-121` recenters to bottom with 32 px offset. Direct-child layout, parent-clipped. ✓
- **C1 — Noir styling consistent with rest of app.** `Toast.cpp:56-63` uses `rgba(20,20,20,0.95)` bg, `rgba(255,255,255,0.10)` border, `#eee` text, `#60a5fa` action link. Gray/black/white palette per `feedback_no_color_no_emoji`; action uses the same `#60a5fa` blue the status-icon delegate uses (CONTRACTS.md progress-icon convention). ✓
- **C1 — Wire-up in TankoyomiPage scraper error paths.** `TankoyomiPage.cpp:109-125` (primary search path) + `:571-588` (post-cancel reconnect path): all-sources-failed branch shows action-toast with Retry that calls `startSearch()` with the preserved `m_lastQuery`; any-source-partial-failed branch shows info-toast. Replaces the inline "Search Failed: {err}" status text — status label now shows "Search failed" (short) and the detail moves to the toast. Matches the `BrowseSourceScreen.kt:62-73` split (snackbar for error, empty state for no-results). ✓
- **C1 — Toast anchored to top-level window, not the tab.** `TankoyomiPage.cpp:113, 575` — `QWidget* anchor = window() ? window() : this;` routes the toast to the MainWindow. Matches the expected placement for a global-ish notification. ✓
- **C2 — Client-side `std::stable_sort` over deduplicated result list.** `TankoyomiPage.cpp:672-694` applies `title_asc` / `title_desc` / `source`-then-title sort modes using `stable_sort` with case-insensitive `QString::compare`. "Relevance" falls through — scraper order preserved, which matches the zero-alteration-if-user-doesn't-ask-for-sort invariant. ✓
- **C2 — QSettings persistence** at `TankoyomiPage.cpp:288-291, 293-296`. Key: `tankoyomi/sortKey`; default `"relevance"`. Matches Mihon's `LibraryPreferences` persistence pattern for display/sort state. ✓
- **C2 — Four-entry combo labels match Mihon's sort DropdownMenu pattern** (for the download-queue surface Agent 4 cites). `TankoyomiPage.cpp:284-287`: Relevance / Title A–Z / Title Z–A / Source. Clean, flat, no nested menu. ✓
- **C3 — Two-column dialog body: 260 px left detail + flexible right chapter picker.** Reference: `MangaScreen.kt` + `MangaInfoHeader.kt:344-379` (`MangaAndSourceTitlesLarge`) — cover on top, metadata below. Tankoban: `AddMangaDialog.cpp:40-82` left panel = cover (240×360) + title + author + source + status; right panel = existing chapter-picker layout (status line, controls row, chapter table, destination row, format combo). Dialog minimum expanded to 920×560, default 1120×700 (`:22-23`) to accommodate. ✓
- **C3 — Cover sourced from B1 cache, late-arriving covers via `coverReady`.** Reference: `MangaCover.Book` with Coil `ImageRequest` at `MangaInfoHeader.kt:358-366`. Tankoban: `TankoyomiPage.cpp:857-868` calls `ensureCover(...)` (primes + returns cached path), sets immediately if already on disk, connects `coverReady` to the dialog with `rid`/`rsrc` closure-captured so only the right cover triggers `setCoverPath`. Disconnect on dialog destroyed (`:865-867`) prevents use-after-free on stack-allocated dialog. ✓
- **C3 — Cover placeholder when no image available.** `AddMangaDialog.cpp:51-58` "No cover" label in a subtle panel; `setCoverPath` at `:319-328` ignores empty/missing paths so the placeholder stays. ✓
- **C3 — Title/author/source/status setters are idempotent and defensive.** `AddMangaDialog.cpp:296-317` — empty strings don't overwrite; optional fields (author, mangaStatus) start hidden and show only when populated. Safe to call setMangaMetadata before or after populateChapters. ✓
- **C3 — Cover aspect ratio.** 240×360 = 2:3 (≈0.667), matching Mihon's `MangaCover.Book` convention. Consistent with the library thumbnail contract (CONTRACTS.md §Thumbnail Cache — 240×369 = 0.65). ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **C1 — Toast doesn't pause auto-dismiss on hover.** Reference: Material3 snackbar pauses its timer while the cursor is over it so the user can read long messages. Tankoban's `Toast::m_dismissTimer` (`Toast.cpp:94-101`) fires unconditionally. On info-toast (3.5 s) a user still mid-read loses it. Minor UX polish; not a blocker — add a `QEvent::Enter` / `QEvent::Leave` check in the existing eventFilter when you next touch the widget.
- **C1 — Long error messages can overflow the parent width.** `Toast.cpp:70` sets `setWordWrap(false)`. `adjustSize()` + `repositionToBottomCenter()` resize to fit the message; if the scraper error string is longer than the window width, the toast exceeds it and `move(qMax(0, x), ...)` clamps X to 0 but the right edge still overhangs. Real scraper errors are short ("network error", "parse failed", etc.) so the exposure is low, but a future error message with a URL or stack snippet would hit this. Cheap fix: set a max width (e.g. `setMaximumWidth(parentWidget()->width() - 64)`) + enable wordWrap when message is long.
- **C3 — Source label shows raw scraper id ("weebcentral") instead of display name ("WeebCentral").** `AddMangaDialog.cpp:72, 308-309` sets `"Source: " + m_source` / `"Source: " + result.source`. Same issue I flagged in the B-review (listed there as P2 / Q). Carries over because `MangaResult::source` is still the id. One small helper (`sourceDisplayName(QString)`) covers both surfaces when someone feels like touching them.
- **C3 — No artist field separate from author.** Reference: `MangaInfoHeader.kt:413-418` shows `author` and `artist` as distinct fields. Tankoban: `MangaResult` has only `author`. Acknowledged by Agent 4 as a `MangaResult`-schema limitation; cost is scraper-side parsing changes. Flagging for completeness. Not a defect of C3 itself.
- **C3 — No synopsis, no genre chips.** Reference: `ExpandableMangaDescription` + genre chip rows on `MangaScreen.kt`. Tankoban: explicitly disclosed as out-of-scope by Agent 4 because the search payloads don't carry those fields. Same schema-limitation reason; bolt-on if `MangaResult` ever grows `description`/`tags`. Not a defect.
- **C2 — "Sort by Source" has subtle semantics under title dedup.** `renderResults` deduplicates by lowercased title *before* applying the sort (`TankoyomiPage.cpp:661-669` → sort at `:672-694`). A title that appears on both WeebCentral and ReadComicsOnline collapses to whichever scraper's payload arrived first; sort-by-source then partitions the winners only. Users expecting "show me all hits grouped by source" will see only one representative per title. Not a defect per the current dedup contract; flagging so the behaviour is documented.
- **C3 — Cover lookup runs a disk stat on the main thread.** `TankoyomiPage.cpp:857-859` — `ensureCover` calls `QFileInfo::exists(path) && QFileInfo(path).size() > 0` synchronously on open. One stat per dialog-open is negligible; flagging only because the pattern repeats the perf-trap shape of R3 (the manga-scale stat storm that hotfix-fixed). Single-shot here is safe.

### Answers to Agent 4's intentional deviations (re-flagging for the record)

Agent 4 disclosed three conscious deviations in the READY FOR REVIEW line; confirming each is acceptable:

1. **Toast bounded duration vs Mihon's `Indefinite`.** Accepted — desktop user in front of window makes indefinite-until-dismiss unnecessary.
2. **Flat `QComboBox` sort vs Mihon nested DropdownMenu.** Accepted — four entries, no nesting needed; Qt combo is the idiomatic surface.
3. **Detail panel without genre chips / synopsis.** Accepted — schema limitation; not a design choice.

### Questions for Agent 4

1. **Hover-pause on Toast auto-dismiss.** Not shipping in C-track, or simply not implemented yet? One eventFilter branch away.
2. **Source-id display-name helper.** Carried over from B-review. Still deferred to "next batch that touches MangaResultsGrid::setResults" (B P2 response), or ready to fold into the next file-touch that hits either surface (since both now show it)?
3. **C2 relevance-mode sort as a user-visible option.** "Relevance" preserves scraper order. With multiple sources enabled (All Sources), "relevance" is actually "source-interleave order by arrival", not a quality ranking. Is that label the right one for users to understand the behaviour? (Not a bug — just asking if a re-label like "Default order" or "As returned" would be clearer.)

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 4, C1–C3], 2026-04-14.** Seven P2 observations, none blocking; three Qs for polish discussion. Agent 4 clear for Rule 11 commit alongside any other closed batches.

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
