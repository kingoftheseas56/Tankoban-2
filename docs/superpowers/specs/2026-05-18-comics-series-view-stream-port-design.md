# ComicsSeriesView Stream-blueprint port -- design spec

- **Date:** 2026-05-18
- **Author:** Agent 1 (Comic Reader + Tankoyomi-as-source)
- **Arc tag:** `COMICS_TANKOYOMI_STREAM_MERGER` (series-view-layout-port piece)
- **Status:** Brainstorm closed 2026-05-18 ~2:00pm IST after 8-question 2-batch session + visual-companion mockup approval ("THAT IS SO FUCKING PERFECT OMG OMG THAT IS PRESTINE AS FUCK. YES WE ARE SETTLED ON IT."). Pending Hemanth spec scan -> `/superpowers:writing-plans`.
- **Umbrella brainstorm:** `docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md` (Phase 1 of the broader merger arc). This spec is a SCOPED PIECE of that arc.
- **Mockup artifact:** `.superpowers/brainstorm/1608-1779095122/content/proposed-layout.html` (single-page end-to-end mockup, Death Note as the worked example).
- **Skills invoked (this phase):** `/brief`, `/superpowers:brainstorming` (with visual companion), `/superpowers:verification-before-completion` (pre-spec).
- **Codex review:** NOT a gate. Per Hemanth's correction 2026-05-18 mid-brainstorm, Codex review is by-his-explicit-call only on rare occasions, not a standing pipeline. Default flow: brainstorm -> writing-plans -> execute -> smoke -> ship.

---

## 1. Goal -- one paragraph

Port `src/ui/pages/comics/ComicsSeriesView.{cpp,h}`'s layout to mirror `src/ui/pages/stream/StreamDetailView.{cpp,h}`. Same widget shape (both keep `QTableWidget`), different styling + row shape + hero treatment + library button + selection style + Sources panel positioning. The "gold bar visual glitch" Hemanth flagged dies as a side-effect of the selection-style change. F1 (keyboard-arrow-nav populates Sources panel, shipped 2026-05-18 ~1:50pm IST) and the COMICS_SOURCES_SIDEBAR v1 Stremio-style cards (shipped 2026-05-17) survive unchanged in their internal logic; only the Sources panel's vertical positioning changes.

Out of scope for THIS spec (separate merger-arc pieces, separate specs): search-bar rename to "Search Tankoyomi", Stream-style search-result tiles for Comics, Netflix-style in-library download workflow, Tankoyomi badge on Tankoyomi-sourced series.

---

## 2. The 8 locked decisions

From the 2-batch 8-question brainstorm 2026-05-18 ~1:55pm IST:

| # | Decision | Locked answer |
|---|----------|---------------|
| 1 | Hero backdrop fate | **Strict Stream parity: 140px banner block + text below.** No overlay text. Manga-art becomes ambient banner image. |
| 2 | Volume row shape | **Strict Stream parity: Cover -> Thumb (48x64 portrait keeping manga aspect), Volume label + chapter range stacked title-overview style.** Drop separate Chapters column. |
| 3 | Library button | **Stream verbatim:** `Remove from Library` when in library, `Add to Library` when not. Active button, not passive pill. |
| 4 | Description shape | **Stream parity: 3-line clamp + Show more / Show less toggle.** Same `QFontMetrics`-driven dynamic clamp as `StreamDetailView::m_descLabel` + `m_descShowMoreBtn` at lines 454-472. |
| 5 | Multi-select checkbox col-0 | **Yes -- port Stream's checkbox col-0 + "Download Selected (N)" bulk path.** Supports bulk-download multi-volume use case (e.g. "download One Piece vols 1-10"). |
| 6 | Action col-6 fate | **Drop entirely.** No col-6. Row click handles open-cbz (if downloaded) or populate-Sources (if not). Cleanest. |
| 7 | Season-selector slot above volume list | **Skip entirely for v1.** Future story-arcs slot per Hemanth: *"in the future when we keep making this app better and better, we can story arcs there."* |
| 8 | Next-unread highlight | **Yes -- subtle highlight on next-unread volume row.** Series view auto-scrolls to + softly distinguishes the next vol the user hasn't read. |

Plus pre-locked from conversation (settled before the 4-question batches):

| # | Pre-lock | Lock |
|---|----------|------|
| P1 | Sources panel positioning | **Full-height right column.** Moves out of the hero-row top-right corner; runs from below banner all the way down beside the volume list. |
| P2 | Selection style | **Subtle 8% white tint** (`background: rgba(255,255,255,0.08)`), Stream parity. Gold left-stripe (the rgba(212,165,116,0.80) from yesterday's Sources Sidebar Decision 12) is REMOVED. |
| P3 | Keep `QTableWidget` | Stream `StreamDetailView::m_episodeTable` also uses `QTableWidget` (line 661). Not a widget swap; only styling + row shape + selection-style + column-content changes. |
| P4 | Preserve F1 (2026-05-18) | `cellClicked` -> `onVolumeCellClicked` mouse-tap-to-open; `currentCellChanged` -> `onVolumeCurrentChanged` populates Sources for mouse + keyboard; `populateSourcesForRow` shared helper. Survives the port unchanged in logic; only signal-wiring may need to be re-pointed if the QTableWidget object is rebuilt. |
| P5 | Preserve COMICS_SOURCES_SIDEBAR v1 | `ComicsSourcesPanel` + `ComicsSourceCard` shipped 2026-05-17 stay. Only the OUTER positioning changes; internal card-rendering, populate(), 3-skeleton-card-pulse, auto-pick 300ms beat, Stremio-style card shape all preserved. |
| P6 | Mode-pill reset contract | Standing brotherhood contract (`feedback_mode_pill_resets_to_root.md`): the new view must still expose `resetToRoot()` + emit `navigationRequested`. ComicsSeriesView currently does this via PHASE 8/B.2 wiring; port preserves it. |

---

## 3. Architecture / file scope

### 3.1 Files touched (the entire change set)

- `src/ui/pages/comics/ComicsSeriesView.cpp` -- primary target. `buildUi()`, `renderDetail()`, `populateVolumeRows()`, slot bodies. ~400-600 LOC across edits.
- `src/ui/pages/comics/ComicsSeriesView.h` -- header for new slots / new widget members / new helpers.
- `src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}` -- minor: the panel itself is reused, but its container layout may need a min-width / size-policy tweak to render properly in the new full-vertical right-column slot.
- `CMakeLists.txt` -- already lists `ComicsSeriesView.{h,cpp}` + `ComicsSourcesPanel.{h,cpp}` + `ComicsSourceCard.{h,cpp}`. No new files in v1, so no CMake additions.

### 3.2 Files NOT touched

- `src/ui/pages/stream/StreamDetailView.{cpp,h}` -- READ-ONLY blueprint reference per `feedback_reference_during_implementation.md`. Cite line numbers; do not edit.
- `src/ui/pages/ComicsPage.cpp` -- ComicsSeriesView's host. Should not need changes (host plumbing already wires `navigationRequested` / `downloadDispatchRequested` / `openVolume` from ComicsSeriesView).
- `src/ui/pages/comics/ComicsSourceCard.{h,cpp}` -- internal card widget shipped 2026-05-17. Card shape and rendering stay. No reason to touch.
- `src/core/manga/anilist/AniListCache.{h,cpp}` -- bookmarks (library-state) storage. New library button reads/writes via the existing API; no schema changes.
- Anything in `src/core/`, `native_sidecar/`, `tests/`, `resources/` -- not in scope.

### 3.3 New widget members (added to ComicsSeriesView.h)

- `QLabel*    m_heroBannerLabel`  -- 140px solid block at top, replaces the full-bleed backdrop logic.
- `QPushButton* m_descShowMoreBtn` -- toggle for 3-line clamp. Stream parity at `StreamDetailView.cpp:460-472`.
- `bool         m_descExpanded`   -- toggle state.
- `int          m_descClampLines` -- clamp count (3, computed from `QFontMetrics::lineSpacing` on first paint).
- `QPushButton* m_downloadSelectedBtn` -- "Download Selected (N)" button. Hidden when zero rows checked, shown when N >= 1. Mirrors `StreamDetailView::m_downloadSelectedBtn` at line 599.
- `int          m_nextUnreadRow`  -- cached row index of the next-unread volume, computed from per-row `chaptersRead` state + bookmark state. -1 if none / all read.

### 3.4 Removed widget members / state

- The full-bleed wallpaper-backdrop QSS + the wallpaper image-load path -- delete from `applyStyleSheet`. Banner-image-load replaces it; same source image, smaller display surface.
- `m_libraryButton`'s "In library" passive-label state -- the button now shows "Add to Library" / "Remove from Library" based on bookmark state. Same QPushButton, new label binding.

---

## 4. Layout structure (Section A from the brainstorm)

### 4.1 The shell

```
+--------------------------------------------------------------+
|  TopBar (Comics / Books / Theatre pills -- unchanged host)   |
+--------------------------------------------------------------+
|  [<- Back]                            [Add/Remove Library]   |  action row
+--------------------------------------------------------------+
|                                                              |
|  [ Hero banner image  -- 140px solid block, no overlay ]     |
|                                                              |
+--------------------------------------------------------------+
|  Title (Death Note)                       |                  |
|  Meta line                                |                  |
|  Description (3-line clamp) [Show more]   |     Sources      |
|  --------------                           |     panel        |
|  [Volume list table -- scrollable]        |     (full        |
|  () # [Thmb] Vol 1                        |     vertical     |
|         Chs 1-9       --- Progress ----   |     height       |
|  () # [Thmb] Vol 2                        |     stack of     |
|         Chs 10-18     --- Progress ----   |     Stremio      |
|  ...                                      |     cards)       |
+--------------------------------------------------------------+
       <----- left col, stretch ~3 ----->    <-- right col, ~1 -->
```

### 4.2 Q-layout tree

- `outer` = QVBoxLayout on the view itself.
- `outer` adds:
  1. **Action row** (QHBoxLayout) -- `m_backButton` (left), stretch, `m_libraryButton` (right).
  2. **Hero banner** -- `m_heroBannerLabel` with `setFixedHeight(140)` + `setObjectName("ComicsSeriesHeroBanner")` + QSS `background: #101010; border-radius: 8px;` (Stream parity at StreamDetailView.cpp:402-404). Banner image loaded via the existing wallpaper-load pipeline but painted at fixed 140px height + `Qt::KeepAspectRatioByExpanding` to crop a horizontal band.
  3. **Two-column content row** (QHBoxLayout, spacing 16-20) -- `leftCol` (stretch 3) + `rightCol` (stretch 1).

- `leftCol` = QVBoxLayout (spacing 8) on a wrapper widget:
  - `m_title` (existing member). Font-size shrinks from current 24pt bold to **~18-22px bold** (mockup uses 22px; Stream's `m_titleLabel` is 16px bold at `StreamDetailView.cpp:418`). Implementation-time pick within that range to match the mockup's visual hierarchy -- small enough that the volume table is the focal point, large enough to read at a glance.
  - `m_metaLine` (12pt, inline single-line, rgba(255,255,255,0.62)).
  - `m_synopsis` (existing member, keep name; functionally maps to Stream's `m_descLabel` role) -- with 3-line clamp via `setMaximumHeight(QFontMetrics::lineSpacing * 3)`.
  - `m_descShowMoreBtn` (flat, hidden until `applyChips` / first description paint determines clamp is active).
  - **Future story-arcs slot** -- currently nothing; reserved comment-block placeholder for v1.x story-arc widget per Decision 7.
  - `m_volumesTable` (QTableWidget, scrollable via Qt default vertical scrollbar).
  - `m_downloadSelectedBtn` (hidden until at least one row checked).

- `rightCol` = the existing `m_sourcesPanel` widget (a `ComicsSourcesPanel*`), but added as a SECOND child of the two-column content row (currently it's nested INSIDE the hero row). `setMinimumWidth(240)` + `setSizePolicy(Preferred, MinimumExpanding)`.

### 4.3 Banner image source

The existing wallpaper-image-load path resolves a series-level artwork URL via the AniList cache (banner image or cover, whichever is set). Re-use the same source; render at 140px height instead of full-bleed. The `Qt::KeepAspectRatioByExpanding` + a centered painter ensures a horizontal slice fills the 140px band without distortion. If no banner is found, fall back to a flat `#101010` block (Stream behavior at StreamDetailView.cpp:404).

---

## 5. Volume table redesign (Section B from the brainstorm)

### 5.1 Column layout (final)

| Col | Width | Header | Content | Notes |
|-----|-------|--------|---------|-------|
| 0 | 32 px | (empty) | Checkbox | Multi-select. Per Decision 5. |
| 1 | 36 px | `#` | Volume index (1, 2, 3, ...) | Centered, rgba(255,255,255,0.6). |
| 2 | 76 px | (empty) | Thumbnail | 48x64 portrait + 14px padding total. Manga-aspect, NOT Stream's 64x36 landscape (which would distort a portrait cover). Per Decision 2. |
| 3 | stretch | Title | **Stacked** -- `Volume N` on top line (12px, e5e7eb, 500wt); `Chs A-B - N chapters` below (11px, rgba(255,255,255,0.55)). Single column holds both. Per Decision 2. |
| 4 | 80 px | Progress | Per-row progress text | **v1 ships "--" (em-dash)** for all rows. Per-chapter read-state wiring is OUT OF SCOPE for this spec; future v1.x slot. Stream parity preserved (column reserved, default value is the same "--" Stream uses for not-yet-watched episodes). |
| 5 | 60 px | Status | Text or icon -- "Downloaded" / "Not downloaded" / "Downloading" | Stream parity at StreamDetailView.cpp:1093-1112. |

**Total: 6 columns** (down from 7). Action col-6 deleted per Decision 6.

### 5.2 Selection style

```cpp
"QTableWidget#ComicsSeriesVolumesTable::item:selected {"
"  background: rgba(255,255,255,0.08);"   // Stream parity, NOT the gold stripe
"}"
```

Replaces the current `border-left: 3px solid rgba(212,165,116,0.80)`. The gold-bar visual glitch dies as a side-effect.

### 5.3 Multi-select + bulk-download wiring

- Each row's col-0 holds a `QCheckBox` (mounted via `QTableWidget::setCellWidget(row, 0, checkbox)`).
- A new private slot `onVolumeCheckboxToggled(int row, bool checked)` updates a `QSet<int> m_selectedRows`.
- `m_downloadSelectedBtn->setText(tr("Download Selected (%1)").arg(m_selectedRows.size()))` + `m_downloadSelectedBtn->setVisible(!m_selectedRows.isEmpty())`.
- `onDownloadSelectedClicked` slot: for each row in `m_selectedRows`, emit `downloadDispatchRequested(volRow, chapterIds)` via the same signal the Sources panel uses today. ComicsPage routes each emission to the active provider.
- "Select-all" / "Clear" not in v1; Stream doesn't have them either at this layer.

### 5.4 Next-unread highlight (Decision 8)

- `m_nextUnreadRow` computed in `populateVolumeRows()`: iterate rows top-down, first row whose `chaptersReadCount < total` OR whose `cbzPath isEmpty` (proxy for "not started") is the next-unread.
- Visual: a subtle left-side accent (NOT the gold stripe -- something quieter, e.g. 2px solid rgba(255,255,255,0.30)) OR an italic tone on the title text. Final treatment determined at implementation; the brainstorm-locked answer is "yes, subtle".
- Scroll: `m_volumesTable->scrollToItem(m_volumesTable->item(m_nextUnreadRow, 0), QAbstractItemView::PositionAtCenter)` once on first paint.
- If all volumes read OR no chapters-read state available, `m_nextUnreadRow = -1` and no highlight fires; series view scrolls to top normally.

### 5.5 Click handlers (F1 preserved)

- `m_volumesTable` connects:
  - `cellClicked(int row, int col)` -> `onVolumeCellClicked(row, col)` -- mouse-tap-to-open path. Unchanged from F1 ship.
  - `currentCellChanged(int curRow, int curCol, int prevRow, int prevCol)` -> `onVolumeCurrentChanged(...)` -- always populates Sources. Unchanged from F1 ship.
- New: col-0 checkbox click is intercepted before row-selection-change (Qt default behavior; cellClicked on col 0 toggles the checkbox without selecting the row). If selection logic interferes, gate `onVolumeCellClicked` to ignore col-0 explicitly.

---

## 6. Library button (Section C from the brainstorm)

### 6.1 Label + state

```cpp
void ComicsSeriesView::refreshLibraryButton() {
    const bool inLibrary = m_cache && m_cache->isBookmarked(m_currentAnilistId);
    m_libraryButton->setText(inLibrary ? tr("Remove from Library") : tr("Add to Library"));
    // Existing accessible-description + accessible-name preserve.
}
```

### 6.2 QSS (Stream parity)

```cpp
"QPushButton#ComicsSeriesLibraryButton {"
"  background: rgba(255,255,255,0.08);"
"  border: 1px solid rgba(255,255,255,0.18);"
"  border-radius: 6px;"
"  color: #ddd;"
"  padding: 6px 14px;"   // up from 4px 12px for Stream parity
"  font-size: 12px;"
"  font-weight: 500;"     // Stream uses 500-weight
"}"
"QPushButton#ComicsSeriesLibraryButton:hover {"
"  background: rgba(255,255,255,0.12);"
"  border-color: rgba(255,255,255,0.28);"
"}"
```

Existing F3 narrow fix (event-filter accepting release-without-press) stays. The button is now an action affordance (was passive pill); click toggles the bookmark + refreshes the label.

### 6.3 Click handler

`onLibraryButtonClicked()` already exists and routes through `AniListCache::toggleBookmark(anilistId)`. Behavior unchanged. The visual is the change.

---

## 7. Description with 3-line clamp (Section C cont'd, Decision 4)

### 7.1 Clamp computation

On first description paint OR on resize:
```cpp
const QFontMetrics fm(m_descLabel->font());
const int lineHeight = fm.lineSpacing();
const int clampHeight = lineHeight * m_descClampLines;  // 3 by default
m_descLabel->setMaximumHeight(clampHeight);
```

### 7.2 Show-more toggle

```cpp
void ComicsSeriesView::onDescShowMoreClicked() {
    m_descExpanded = !m_descExpanded;
    if (m_descExpanded) {
        m_descLabel->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->setText(tr("Show less"));
    } else {
        const QFontMetrics fm(m_descLabel->font());
        m_descLabel->setMaximumHeight(fm.lineSpacing() * m_descClampLines);
        m_descShowMoreBtn->setText(tr("Show more"));
    }
}
```

Mirror StreamDetailView.cpp:460-472 + onDescShowMoreClicked logic. Button is hidden if the full description fits within the clamp (i.e. clamping is a no-op).

---

## 8. Sources panel relocation (Pre-lock P1)

### 8.1 Move from hero-row-right to full-height right column

Today's tree:
```
outer
+- heroRow (QHBoxLayout)
|  +- heroLeftWrap (title/meta/synopsis stack)
|  +- m_sourcesPanel               <-- HERE in v1
+- m_volumesTable
```

New tree:
```
outer
+- actionRow (back + library button)
+- m_heroBannerLabel
+- contentRow (QHBoxLayout)
|  +- leftColWrap (title/meta/desc/downloadSelected/m_volumesTable)
|  +- m_sourcesPanel               <-- MOVED HERE, full vertical height
```

The panel object itself is the same `ComicsSourcesPanel` instance shipped 2026-05-17. Only its parent layout slot changes.

### 8.2 Size policy

- `m_sourcesPanel->setMinimumWidth(240)` -- enough horizontal room for one Stremio card without text-elision.
- `m_sourcesPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding)` -- expand vertically to match the left column's height.
- `contentRow->addWidget(leftColWrap, 3); contentRow->addWidget(m_sourcesPanel, 1);` -- 3:1 stretch.

### 8.3 What survives unchanged

- `ComicsSourcesPanel::populate(...)` API + signal-out `downloadRequested(...)` + skeleton-card-pulse + auto-pick 300ms beat + Stremio-style cards + the 3-card vertical layout. All untouched.

---

## 9. What's preserved (full list)

- **F1 (2026-05-18, ~1:50pm IST):** `cellClicked` / `currentCellChanged` split + `populateSourcesForRow` helper. Confirmed unchanged in logic; signal connects are re-wired to the rebuilt QTableWidget but the SLOT bodies are untouched.
- **COMICS_SOURCES_SIDEBAR v1 (2026-05-17, `a9557e6`):** ComicsSourcesPanel + ComicsSourceCard + 12 brainstorm decisions + auto-pick 300ms beat + skeleton-card pulse. Entirely preserved internally; only outer positioning moves.
- **MangaUpdates fallback v1 (2026-05-17):** vol-count resolution from AniList-null -> MangaUpdates chain. Unaffected; ComicsSeriesView reads vol counts from the same source.
- **Mode-pill reset contract (`feedback_mode_pill_resets_to_root.md`):** the view's `resetToRoot()` + `emit navigationRequested` survive. ComicsPage's existing wiring keeps working.
- **F3 narrow fix on `m_libraryButton`:** event-filter accepting release-without-press stays. The button's label/state binding is new; its event-filter is old code.
- **`openVolume(int volumeNumber, const QString& cbzPath)` signal:** still emitted by `onVolumeCellClicked` when the clicked row has a stashed cbz. ComicsPage continues to route to ComicReader.
- **`downloadDispatchRequested(VolumeRow, QStringList chapterIds)` signal:** still emitted by ComicsSourcesPanel (downloads). New `m_downloadSelectedBtn` emits the same signal once per selected row.

---

## 10. Implementation order (proposed task boundaries)

The plan-md (next step, `/superpowers:writing-plans`) will split this into concrete tasks. My recommendation for the plan:

- **Task 1 -- Layout shell skeleton.** Strip the old hero-overlay layout. Add action row, hero banner block (140px), two-column contentRow with 3:1 stretch. Move m_sourcesPanel into the right slot. Build + visual check empty state. ~80-120 LOC.
- **Task 2 -- Volume table column-shape refactor.** New 6-column layout. Drop col-6 chevron. Stack Volume + Chs A-B in the title col. Add 32px col-0 checkbox column (placeholder, no wiring yet). Selection style -> 8% white tint. ~80-100 LOC.
- **Task 3 -- Description clamp + Show more.** Port StreamDetailView's clamp logic. Add m_descShowMoreBtn. ~40-60 LOC.
- **Task 4 -- Library button: passive pill -> action button.** Re-label dynamically. New QSS (Stream parity). Connect to AniListCache::toggleBookmark. ~30-40 LOC.
- **Task 5 -- Multi-select wiring + Download Selected button.** Add `QSet<int> m_selectedRows`, `onVolumeCheckboxToggled`, `m_downloadSelectedBtn` + show/hide + label-update, `onDownloadSelectedClicked` emitting per-row downloadDispatchRequested. ~80-120 LOC.
- **Task 6 -- Next-unread highlight + auto-scroll.** Compute `m_nextUnreadRow` in populateVolumeRows. Apply subtle accent. scrollToItem on first paint. ~50-70 LOC.
- **Task 7 (optional) -- Polish + smoke.** Banner-image crop tuning, padding tweaks, hero-image fallback verification, Hemanth visual-quality verdict. ~20-40 LOC.

Each task its own build_check.bat BUILD OK + RTC in chat.md per `feedback_one_fix_per_rebuild.md` + `feedback_commit_protocol.md`. ~6 RTCs (or ~5 if Task 7 folds in).

Estimated total: **~400-550 LOC across 4-6 commits.**

---

## 11. Risk / verification

### 11.1 Things that could break

- **F1 keyboard nav regression** -- the QTableWidget gets rebuilt in Task 2. The two signal connects (cellClicked + currentCellChanged) MUST be re-wired post-rebuild or arrow-nav stops populating Sources. Mitigation: explicit smoke after Task 2 verifying arrow-key sources-update still works.
- **F2 agent-pixel-click coord mystery** -- my MCP smoke can't reliably click table rows post-port any more reliably than before. Mitigation: Hemanth-driven smoke on the new view. Same as today.
- **Hero banner image cropping** -- if the source banner is too tall/wide, `KeepAspectRatioByExpanding` may crop the focal subject. Mitigation: test with 3-4 series (Death Note tall cover, One Piece wide banner, Berserk square art) during Task 1 smoke; adjust crop alignment if needed.
- **Sources panel size in new slot** -- the panel may render too narrow or too wide. Mitigation: explicit min-width 240px + visual smoke against Death Note (3 cards) + One Piece (many cards, scroll).
- **Selection-style change breaks Sources Sidebar Decision 12** -- yesterday locked the gold stripe; today removes it. Document the override in the RTC + memory file.
- **Multi-select checkbox click vs row-selection-change interaction** -- Qt's default cellClicked behavior may toggle the checkbox AND change the current row, which could fire onVolumeCurrentChanged unwanted-ly. Mitigation: smoke Task 5 specifically for "click checkbox while different row is selected" path.

### 11.2 Verification gates

- Per-task `build_check.bat BUILD OK` (`/build-verify` skill).
- ASCII sweep on diff per `feedback_evidence_before_analysis.md`.
- Hemanth-driven UI smoke per `feedback_hemanth_role_open_and_click.md` -- I can MCP-smoke shape/UIA but Hemanth has final visual-quality verdict per `feedback_subjective_over_trace.md`.
- F1 regression smoke after Task 2 + after Task 5 (the two tasks most likely to disturb the signal wiring).
- COMICS_SOURCES_SIDEBAR v1 cards still render correctly in their new vertical-right slot (Task 1 smoke).

### 11.3 Rollback

Each task is its own commit. If a task breaks something, `git revert <commit>` rolls back that task only without losing earlier tasks. No shared state across tasks (each task's diff is independent of the others' diffs).

---

## 12. What's NOT in this spec

Other COMICS_TANKOYOMI_STREAM_MERGER arc pieces, all separate specs / plans / wakes:

- Search-bar "Search Tankoyomi" + Stream-style search-result tiles for Comics.
- Netflix-style in-library download workflow (downloading a chapter auto-adds the series to library).
- Tankoyomi badge on Tankoyomi-sourced series + the badge driving "use the new series view vs the old folder-import view" branch.
- The full merger umbrella reuse-vs-fork-vs-replace map in `2026-05-14-comics-tankoyomi-merger-brainstorm.md` -- this spec only resolves the SERIES VIEW LAYOUT piece of that umbrella.

Future v1.x polish (not in this spec):

- Per-volume cover thumbnails fetched from AniList instead of placeholder rectangles.
- Animated next-unread highlight (fade-in on focus).
- Story-arc widget for the season-selector slot.
- Hero banner subtle parallax / blur-on-scroll.

---

## 13. References

- Mockup: `.superpowers/brainstorm/1608-1779095122/content/proposed-layout.html`.
- Umbrella brainstorm: `docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md`.
- Blueprint source: `src/ui/pages/stream/StreamDetailView.{cpp,h}` (READ-ONLY).
- Today's target: `src/ui/pages/comics/ComicsSeriesView.{cpp,h}`.
- F1 ship RTC: `agents/chat.md` 2026-05-18 ~1:50pm IST.
- COMICS_SOURCES_SIDEBAR v1 ship: `agents/chat.md` 2026-05-17 + memory `project_comics_sources_sidebar_shipped_2026-05-17.md`.
- Brotherhood contracts: `agents/GOVERNANCE.md` (Rule 11 RTC, Rule 14 decision-authority, Rule 15 self-service execution, Rule 17 smoke cleanup, Rule 18 plan-execute-smoke-verify), `feedback_one_fix_per_rebuild.md`, `feedback_mode_pill_resets_to_root.md`, `feedback_hemanth_role_open_and_click.md`, `feedback_reference_during_implementation.md`, `feedback_no_color_no_emoji.md`, `feedback_css_scoping.md`.

---

## CLOSING

This spec is a SCOPED PIECE of the COMICS_TANKOYOMI_STREAM_MERGER arc. It resolves the series-view layout port end-to-end and leaves the other merger-arc pieces (search, in-library downloads, badges) for their own specs in future wakes. Hemanth-approved mockup at `.superpowers/brainstorm/1608-1779095122/content/proposed-layout.html` is the ground truth. Build the visual to match the mockup, preserve F1 + COMICS_SOURCES_SIDEBAR v1, ship in 4-6 tasks.

Next step: Hemanth scans this spec. If green, fire `/superpowers:writing-plans` against this file -> plan-md at `docs/superpowers/plans/2026-05-18-comics-series-view-stream-port.md` -> `/superpowers:subagent-driven-development` execution per `feedback_plan_first_zero_errors.md`.
