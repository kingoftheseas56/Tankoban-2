# Second Congress Pre-Brief: Library UI/UX
## Master Checklist — synthesized from assistant1_report.md + assistant2_report.md
## Agent 0 synthesis — 2026-03-25

This document is the pre-processed groundwork map for the Second Congress of the Brotherhood.
Topic: Library UI/UX parity and improvement — Comics, Books, Videos.
Scope: exclude Sources (Tankorent/Tankoyomi).

Congress agents read this document before filing positions.

---

# SECTION 1: ARCHITECTURE

## 1A. Sidebar (MISSING — P0)

Groundwork has a **252px fixed-width sidebar** on the left of every library page.

Contents per library:
- "LIBRARIES" label
- Root folder list (clickable, filters grid to that root)
- "+ Add comics/books/video root..." button (objectName "SidebarAction")
- "FOLDERS" label
- Expandable folder tree (QTreeWidget) showing the folder hierarchy
- Context menu on tree items: Open, Reveal, Copy, Remove (with DANGER styling)
- Context menu on "All" item: "Restore all hidden series/shows"
- Books only: additional "AUDIOBOOKS" section with "+ Add audiobook root..."

We have: no sidebar. Root folders are managed through the MainWindow "+" button only.
Gap: Sidebar is how the groundwork lets users filter by root folder and navigate the folder tree. Without it, users can't drill into specific root folders or see the folder hierarchy.

Implementation path: `QFrame` objectName `"LibrarySidebar"`, fixed width 252px, left column. QTreeWidget for folder tree. Context menus on tree items. Signals for folder selection filtering the grid.

---

## 1B. FadingStackedWidget (MISSING — P1)

Groundwork uses `FadingStackedWidget` (a custom QStackedWidget with fade transitions) between:
1. library_layer — grid home view
2. detail_view — FolderDetailView (SeriesView/ShowView equivalent)
3. reader_layer or player_layer — embedded reader/player

We use: plain QStackedWidget. No fade transition.

Implementation: see `app_qt/ui/widgets/fading_stacked_widget.py`. Cross-fade opacity animation on page change.

---

## 1C. List View Mode (MISSING — P1)

Groundwork has a **V-key toggle** between grid and list mode.

List view is a `QTreeWidget` with columns:
- Name (stretch)
- Items (fixed 80px)
- Last Modified (fixed 140px)
- Download (fixed 92px, shows torrent %)

Grid/list mode persisted: `"library_view_mode_comics"` / `"library_view_mode_books"` / `"library_view_mode_videos"`.
View toggle button: 28×28px, objectName `"ViewToggle"`. Icon: hamburger (grid mode), dotted square (list mode).
Density slider and cover_slider hidden in list mode.
Keyboard shortcut: `V` (no-op if search bar focused).

We have: grid only.

---

# SECTION 2: TILE DIMENSIONS AND DENSITY

## 2A. Exact Tile Widths (PARTIALLY WRONG — P0)

Groundwork values from `parity_dimensions.py`:

| Density | Comics | Books | Videos |
|---------|--------|-------|--------|
| Small   | 150px  | 150px | 155px  |
| Medium  | 200px  | 200px | 210px  |
| Large   | 240px  | 240px | 250px  |

Gap between tiles:

| Density | Gap |
|---------|-----|
| Small   | 10px |
| Medium  | 16px |
| Large   | 20px |

Cover aspect ratio: `0.65` (width / height = 0.65, so height = width / 0.65).
- Medium comics: 200px wide × 307px tall.
- `card_padding = 0` — card width equals cover width exactly.

Card width expansion: groundwork expands card widths to fill available space evenly:
```
avail = widget.width() - 4
cols = max(1, (avail + gap) // (card_w + gap))
actual_card_w = max(card_w, (avail - (cols - 1) * gap) // cols)
actual_cover_h = int(actual_card_w / 0.65)
```
Cards stretch to fill the row — no orphan whitespace on the right edge.

We have: 150/200/240px widths, correct cover ratio (Agent 5 fixed this). Missing: card width expansion, videos gets +5/10px wider than comics/books.

---

## 2B. TileCard Missing Features (MULTIPLE GAPS — P0/P1)

### Cover Image Rendering
- Corner radius: **8px** rounded via `_round_pixmap(pm, radius=8)` — applied to the cached pixmap
- Scale mode: `KeepAspectRatioByExpanding` + `SmoothTransformation` (fill, not fit)
- Async loading via QThreadPool (max 2 threads), module-level LRU cache (120 entries)
- We have: async loading exists. Corner radius: unknown. LRU cache: unknown.

### Placeholder (when no cover)
- Background: `QColor(0, 0, 0, 89)` semi-transparent black
- First alphabetic character of title, centered, uppercased
- Font: point size 28, Bold
- Color: `#9ca3af`
- We have: unknown — need to compare.

### Progress Bar (P0)
- 3px bar at bottom edge of cover image (`pm.height() - 3`)
- Track: `QColor(0, 0, 0, 77)` dark semi-transparent
- Fill (in-progress): palette Highlight color, fallback `#94a3b8`
- Fill (finished): `#4CAF50` (green)
- We have: none — no progress bar on tile covers.

### Badge Pill Bottom-Right (P0)
- Shows volume/episode count (or whatever badge string is passed)
- Font: pixelSize 11, DemiBold
- Background: `QColor(0, 0, 0, 115)` semi-transparent black
- Text: `#f0f0f0`
- Padding: 6px each side, height = font_height + 4, fully rounded capsule
- Margin: 6px from edges
- Hidden when `status == "finished"`
- We have: partial — need to verify exact values.

### Page Badge Pill Bottom-Left (P0)
- Same visual as badge pill but positioned bottom-left (6px from left, 6px from bottom)
- Content: current page / total pages (e.g., "P42" or "42/200")
- We have: none — not implemented.

### "New" Dot (P1)
- Color: palette Highlight, fallback `#94a3b8`
- 10×10 ellipse
- Position: top-left area (offset right of folder icon if folder icon present)
- Shown when file was modified within 7 days of now
- We have: none.

### Folder Icon Overlay (P1)
- Top-left corner, semi-transparent white `QColor(255, 255, 255, 140)`
- Two rounded rects forming an open-folder shape
- We have: none (but we have folder.svg — different approach).

### Status Indicators (P0)
- `status = "reading"` — shown as in-progress (progress bar fills partially, page badge shown)
- `status = "finished"` — badge pill hidden, progress bar full green
- `status = ""` — unread, no progress shown
- We have: unknown — need to compare.

### Hover / Click / Selection (P1)
- Hover: `"QFrame#TileImageWrap { border: 2px solid {accent}; border-radius: 8px; }"` — 2px accent border
- Selected: same border, persistent
- 120ms click flash (accent border, then `QTimer(400ms)` restores)
- Focus ring (keyboard): `"QFrame#TileCard[focused=true] { border: 1px dotted rgba(255,255,255,0.5); }"`
- We have: some hover behavior, but exact CSS and flash behavior unknown.

### Drag-and-Drop Cover Replace (P2)
- Accepted: `.png .jpg .jpeg .bmp .webp .gif .tiff`
- Emits `posterDropped(tile_key, file_path)` on drop
- We have: none.

---

## 2C. TileStrip Missing Features (MULTIPLE GAPS — P0/P1)

### Grid Layout (P0)
Groundwork uses `QGridLayout` with computed columns, NOT a fixed-column layout.
Column count recalculated on every resize:
```
avail = widget.width() - 4
cols = max(1, (avail + gap) // (card_w + gap))
```
After placing tiles, first empty column gets `setColumnStretch(used_cols, 1)` — tiles push left, no right-side orphan stretch.
Card width expanded to fill available space (see 2A).

We have: `QWidget` with tiles as direct children using some layout. Likely fixed columns or wrapping flow. Need to verify.

### Selection Model (P1)
Groundwork has `GridSelectionModel`:
- Left click: select only this tile (deselect all others)
- Ctrl+click: toggle (add/remove from multi-selection)
- Shift+click: select contiguous range
- Escape: clear all selection
- `selectionChanged` signal
- Multi-selection enables batch context menu actions (mark all, remove N items, etc.)

We have: single selection only. No Ctrl+click, no Shift+click, no range.

### Keyboard Navigation (P1)
- Arrow keys move focus between tiles (Left/Right ±1, Up/Down ±cols)
- Enter/Return: emit `tileDoubleClicked`
- Escape: clear selection, reset focus
- Focused tile shows dotted focus ring

We have: unknown — likely no keyboard navigation in tile grid.

### Signals (P1)
Groundwork TileStrip signals: `tileClicked`, `tileDoubleClicked`, `tileRightClicked`, `tilePosterDropped`, `selectionChanged`, `focusChanged`.
We have: some of these. Need to verify completeness.

### Continue Mode (P1)
Continue strip uses `mode="continue"`:
- Fixed height: `cover_height + 48`
- Horizontal scroll (single row)
- Left-aligned

`ContinueSpec` dimensions:
- panel_height: 430px
- min_h_comfortable: 240px, max_h_comfortable: 323px
- min_h_compact: 210px, max_h_compact: 261px
- cover_ratio: 0.65 (same as grid)
- fallback_cover: 160×240px

We have: continue strip exists (Agent 5 built it). Exact heights may differ from spec.

---

# SECTION 3: CONTINUE READING/WATCHING

## 3A. Comics Continue — Progress Data Shape (P0)

Groundwork fields per progress entry (key = book_id):
- `"finished"`: bool — if True, EXCLUDED from strip
- `"updatedAt"`: int — Unix timestamp seconds, used for sort and dedup
- `"page"`: int/float — current page (1-indexed; 0 = not started)
- `"pageCount"`: int — total pages

TileData for continue tiles:
- `key`: book_id
- `badge`: percentage string e.g., `"45%"`
- `page_badge`: `"{page}/{totalPages}"` or `"P{page}"` or `""`
- `status`: `"reading"` if pct > 0 and not finished, else `""`
- `progress_fraction`: float 0.0-1.0
- `is_new`: bool (file modified within 7 days)

Strip population:
- Series deduplication: one tile per seriesId, highest updatedAt wins. Fallback key = book_id if no series.
- Sort: descending updatedAt (most recently read first)
- Limit: 40 tiles
- Finished items (finished=True) excluded entirely

We have: continue strip exists. Agent 5 built infrastructure. Agent 1 was asked to add `path` field. The exact dedup logic and 40-tile limit need verification.

---

## 3B. Books Continue — Differences (P0)

Same as Comics with additions:
- Mixed strip: BOTH books AND audiobooks in one continue strip
- Progress from `get_all_progress()` (books) AND `get_all_audiobook_progress()` (audiobooks)
- Books search is scored (token ranking + individual book hits) vs Comics simple AND matching

---

## 3C. Videos Continue — Full Feature (P0)

Key differences from Comics/Books:

Continue tile key = **show_id** (not episode_id). One tile per show.

Progress per episode:
- `"finished"`: bool
- `"positionSec"`: float — resume time in seconds (no page field)

Resume episode selection (`_pick_resume_episode`): finds the most appropriate unfinished episode to resume. If all episodes finished, show excluded.

Single-click behavior: 250ms delay (`QTimer` single-shot). On expiry, opens player.
Double-click: cancels timer, opens player immediately.
This prevents accidental navigation when double-clicking.

Partial progress update path: `_apply_progress_update(payload)` refreshes ONLY continue section — no full grid rebuild. Used during active playback.

What clicks do: Comics/Books → open reader. Videos → start player directly.

We have: Agent 5 built continue watching. Agent 3 saved progress. Path field was requested. 250ms single-click delay: not implemented. Partial progress updates: not implemented.

---

## 3D. Continue Bar in Detail View (MISSING — P1)

Inside FolderDetailView (SeriesView/ShowView), there is a compact **40px "Continue" bar** between the nav bar and the content:

Height: 40px fixed
Layout: horizontal, margins (16, 4, 16, 4), spacing 10
Contents:
- "Continue reading" label (objectName "ContinueBarTitle")
- Current item name (objectName "ContinueBarItem", min width 60px)
- QProgressBar 100×6px, no text
- Percentage label (objectName "Subtle", width 36px)
- "Open" / "Read" / "Watch" button (objectName "ContinueBarBtn", 60×26px)

Visibility: only shown when there is in-progress content for this series/show.
Right-click on bar: shows a mini context menu (action, reset progress, reveal, copy path).

We have: none — our SeriesView/ShowView has no continue bar.

---

# SECTION 4: CONTEXT MENUS

## 4A. Comics Grid Context Menu — Single Selection (P0)

Full groundwork menu order:
1. "Open"
2. *(separator)*
3. "Mark all as unread" OR "Mark all as read" (toggles based on all-finished state)
4. *(separator)*
5. "Rename series..." — QInputDialog, renames folder on disk, triggers rescan
6. "Hide series" — hidden flag persisted to QSettings
7. "Reveal in File Explorer"
8. "Copy path"
9. *(separator)*
10. "Remove from library..." — DANGER (red text QWidgetAction)

Confirmation text for Remove: `"Remove this series from the library?\n{path}\nFiles will not be deleted from disk."`
Buttons: Yes | No

We have: this is mostly correct (Agent 5 built it). Need to verify: exact label text, separator positions, confirmation text wording, "Open in new window" (groundwork has it in CONTINUE menu but not grid menu).

---

## 4B. Comics Grid Context Menu — Multi-Selection (MISSING — P0)

Shown when 2+ tiles are selected (requires multi-select from 2C).

Items:
1. "Open first selected"
2. *(separator)*
3. "Mark all as read"
4. "Mark all as unread"
5. *(separator)*
6. `"Remove {N} items"` — DANGER

Confirmation: `"Remove {N} items from library?\nFiles will not be deleted from disk."`
Buttons: Cancel | Yes, default Cancel.

We have: none — no multi-selection, so no multi-select menu.

---

## 4C. Comics Continue-Tile Context Menu (MISSING — P0)

Right-click on a tile in the continue strip:

1. "Continue reading"
2. "Open series" — *visible only if book has seriesId*
3. "Open in new window"
4. *(separator)*
5. "Mark as unread" / "Mark as read" (toggles finished flag)
6. "Clear from Continue Reading" — removes all progress for this book
7. *(separator)*
8. "Reveal in File Explorer" — *enabled if path non-empty*
9. "Copy path" — *enabled if path non-empty*
10. *(separator)*
11. "Remove from library..." — DANGER — *enabled if series_path*

We have: probably none — continue strip tiles likely have no right-click menu.

---

## 4D. Books Context Menu Differences (P0)

Books continue-tile menu vs Comics:
- ABSENT: "Open in new window"
- PRESENT: "Rename..." (calls rename on the book/series)
- "Clear from Continue Reading" passes BOTH sid AND bid (not just bid like Comics)

Books grid menu vs Comics:
- First item: "Open series" (not just "Open")
- Additional item: "Continue reading" — conditionally enabled if has_in_progress
- Remove label: "Remove series folder..." (not "Remove from library...")

Books sidebar has extra "Rename..." on both series items and file items.
Books sidebar has a "File Item" context menu (individual book files) — not present in Comics.

---

## 4E. Videos Context Menu Differences (P0)

Videos menus have unique items not in Comics/Books:

Continue-tile menu:
- "Play / Continue"
- "Play from beginning"
- "Open show" (*visible if sid*)
- "Mark as unwatched" / "Mark as watched"
- "Clear from Continue Watching"
- "Auto-rename" (*visible if sid*)
- "Reveal in File Explorer" (uses EPISODE path, not show path)
- "Copy path"
- "Set poster..." / "Remove poster" / "Paste image as poster" (*visible if sid*)
- "Remove from library..." — DANGER

Grid menu (single):
- "Play / Continue" (*enabled if has_episodes*)
- "Play from beginning" (*enabled if has_episodes*)
- "Mark all as unwatched" / "Mark all as watched"
- "Clear from Continue Watching"
- "Rename..."
- "Auto-rename"
- Reveal, Copy
- Set poster / Remove poster / Paste image as poster
- "Remove from library..." — DANGER

Videos has NO player backend switcher (FFmpeg/MPV) relevant to us — skip that.
Videos has "Auto-rename" (automatic title cleanup from media metadata) — may be worth adding.
Videos has poster management (set/remove/paste cover image) — already partially handled by drag-drop.

---

## 4F. FolderDetailView File Row Context Menu (P0)

Right-click on a file row in SeriesView/ShowView:

1. `{action_label}` — Open/Read/Watch
2. *(separator)*
3. "Reveal in file explorer" (*if file_path*)
4. "Copy file path" (*if file_path*)
5. "Set as series cover" (*comics/books only, if series_id*)
6. *(separator)*
7. "Rename..." (*books only*)
8. "Mark as read" / "Mark as unread" (*comics/books, toggles finished flag*)
9. "Play from beginning" (*videos only*)
10. "Mark as finished" / "Mark as in progress" (*videos only*)
11. *(separator, only if torrent-linked)*
12. Torrent-related items (skip — not in scope)
13. "Reset progress" (*if item_id*)
14. *(separator)*
15. "Remove from library..." — standard QAction + separate QMessageBox (NOT red QWidgetAction)

Note: DANGER in FolderDetailView uses standard QAction + QMessageBox, NOT the red QWidgetAction style used in grid menus.

We have: Agent 5 added right-click in ShowView with Play, Reveal, Copy. Missing: "Set as series cover", "Mark as read/unread", "Reset progress", "Remove from library".

---

## 4G. FolderDetailView Folder Row Context Menu (P0)

Right-click on a folder row:
1. "Open folder"
2. "Reveal in file explorer"
3. "Copy folder path"

We have: unknown — need to check if we have right-click on folder rows.

---

# SECTION 5: FOLDER DETAIL VIEW (SeriesView / ShowView / BookSeriesView)

## 5A. Column System (P0)

Groundwork uses a `FolderColumn` dataclass with: title, key, alignment, fixed_width, stretch, min_width.

Comics fixed column widths: `(42, 70, 84, 60, 120, 132)` px
Books fixed column widths: `(42, 92, 92, 120, 132)` px
Videos fixed column widths: `(42, 90, 110, 90, 92, 132)` px
Row height: 34px (file rows), 38px minimum (folder rows)

Sort options (14 total, conditionally shown based on columns present):
Title A→Z, Title Z→A, Size ↑/↓, Modified ↑/↓, Number ↑/↓, Pages ↑/↓ (if pages col), Progress ↑/↓ (if progress col), Duration ↑/↓ (if duration col).

We have: 4-6 columns per view. Sort: 6 options. Missing: pages/progress/duration sort toggles, exact column widths may differ.

---

## 5B. Progress Cell Icons (MISSING — P0)

Groundwork renders 12×12 icons in the progress column cell:

Finished: green circle `#4CAF50` (10×10 ellipse), white checkmark `✓` pixelSize 9 Bold, centered.
In-progress: slate circle `#94a3b8` (10×10 ellipse), no text in icon (percentage shown as cell text).
No progress: text `"-"`, no icon.

We have: text only ("Done", "XX%", "-"). No icons.

---

## 5C. Continue Bar in Detail View (see 3D above — MISSING — P1)

---

## 5D. Navigation System (P1)

Groundwork has full forward/back history:
- History stack: `_nav_history: list[str]`, position: `_nav_index: int`
- Back navigates to previous rel path, or emits `backRequested` if at root
- Forward navigates to next rel path (if available)
- Forward button: 28×28px, disabled when no forward history
- Keyboard: `Alt+Left` (back), `Alt+Right` (forward), `Backspace` (back)

We have: back button only. No forward history, no forward button, no Alt+Left/Right.

---

## 5E. Naming Toggle — Comics Only (MISSING — P1)

Comics FolderDetailView has a `QComboBox` ("DetailNamingCombo", width 110px) for switching between "Volumes" / "Chapters" naming modes. Changes section title to "VOLUMES" or "CHAPTERS".

We have: none.

---

## 5F. Search Bar in Detail View (P1)

Groundwork detail view search:
- Width: 180px, objectName "DetailSearch"
- Clear button: `setClearButtonEnabled(True)`
- Placeholder: "Search volumes..." / "Search chapters..." / "Search books..." / "Search episodes..."
- On zero results: table hidden, "no results" label shown + "Clear search" link
- On empty folder: table hidden, "empty" label shown

We have: Agent 5 added search. Width may match. Zero-results label may exist. Need to verify "Clear search" link.

---

## 5G. Folder Row Rendering (P0)

Groundwork folder rows:
- Height: minimum 38px
- Background: slightly lighter than base (palette Base + (+12, +12, +14))
- Font: Bold on all cells
- Title text format: `"📁  {name}"` + `"    ({count} items)"` when mixed mode

We have: folder rows implemented (Agent 5). Exact styling — folder icon is SVG-based, not emoji. Need to match background delta and bold font.

".." up-row:
- Same folder row style
- Title: `"←  .."` (arrow + double dot)

We have: ".." row — need to verify exact text and style.

---

## 5H. Row Click Behavior (P1)

Groundwork:
- Single click: refresh cover panel / hero panel for selected row
- Double-click folder: navigate into subfolder (or go back for ".." row)
- Double-click file: emit `itemActivated`
- Auto-selects first FILE row (after folder rows) when navigating into a folder

We have: double-click behavior exists. Single-click cover panel refresh: unknown. Auto-select first file: unknown.

---

# SECTION 6: SEARCH (Library Level)

## 6A. Search Bar Spec (P0)

All 3 libraries:
- objectName: `"LibrarySearch"`
- `setClearButtonEnabled(True)`
- Debounced (not immediate)
- `Ctrl+F` focuses bar and selects all
- Tooltip: "Separate words to match all\n(e.g. 'one piece' matches series or volumes containing both words)"
- Active state: sets Qt property `"activeSearch" = True` for CSS styling

Placeholders:
- Comics: `"Search series and volumes…"` (Unicode ellipsis U+2026)
- Books: `"Search books and series..."` (literal dots)
- Videos: `"Search shows and episodes…"` (Unicode ellipsis)

We have: search bar exists. Ctrl+F: likely missing. Tooltip: likely missing. activeSearch property: likely missing.

---

## 6B. Comics Search Algorithm (P0)

Simple AND matching: all search tokens must appear in `series_name + all_volume_titles` (case-insensitive).
Applied after debounce.

We have: similar. Need to verify exact implementation.

---

## 6C. Books Search Algorithm (P1)

Scored ranking (more complex):
1. Score each series by token match rank
2. Also show individual BOOK tiles (direct-open) for matching titles — up to 24 extra tiles
3. Series that had book-level hits are included even if series name did not match
4. Results sorted by score

We have: likely same as comics (simple AND). Books should have the richer algorithm.

---

## 6D. Empty State Labels (P1)

When no tiles match:
- objectName: `"LibraryEmptyLabel"`, `AlignCenter`
- No tiles, no search: `"No comics found\nAdd a root folder via the + button or browse Sources for content"`
- No tiles, search active: `'No results for "{query}"'`

We have: unknown — need to verify empty state labels.

---

# SECTION 7: THUMBNAILS AND COVERS

## 7A. Comics Cover Extraction (P0)

Algorithm in `thumbnails.py`:
1. First image file from CBZ/CBR archive (alphabetical)
2. Scale: fill target dimensions — `s = max(sx, sy)` (not fit — cover/expand)
3. Center-crop to exactly `_THUMB_W × _THUMB_H`
4. Save as JPEG at `_THUMB_QUALITY`

`_THUMB_W` and `_THUMB_H` are in `types.py` (not read — need to check actual values).
Hero image: max 1200×1800, JPEG quality 88.

Series cover selection: minimum natural sort key among volumes (Vol 1 typically). Override if user set cover via "Set as series cover" context menu.

Background thumbnail generation: `QThreadPool` (separate `_thumb_pool`), one task at a time via timer.

We have: Agent 5 built cover extraction. Target size was fixed (240×369 in LibraryScanner). Need to verify if `_THUMB_W` and `_THUMB_H` from types.py match our values.

---

## 7B. Books Cover Extraction (P0)

Algorithm in `covers.py`:
1. EPUB only (zip-based)
2. Extract first image alphabetically
3. Override if path contains "cover", basename starts with "cover."/"folder.", or "front" in path
4. No resize for thumbnails — raw bytes base64-encoded as-is
5. Hero: KeepAspectRatio max 1200×1800, JPEG quality 88

No background thread pool — synchronous generation.
Cover candidates sorted by natural sort key (alphabetically-first title preferred).

We have: EpubParser exists (Agent 2). Cover extraction from EPUB: unknown implementation status.

---

## 7C. Videos Poster (P1)

Videos does NOT extract covers from video files. Poster management is separate:
- "Set poster..." — file picker dialog
- "Remove poster" — deletes stored poster
- "Paste image as poster" — from clipboard

We have: ShowView has no cover panel (intentionally hidden). Poster management not implemented.

---

# SECTION 8: KEYBOARD SHORTCUTS (Library Level)

| Shortcut | Comics | Books | Videos |
|----------|--------|-------|--------|
| Ctrl+F   | Focus search | Focus search | Focus search |
| F5       | Request rescan | Request rescan | Request rescan |
| Escape   | Clear search if active, else navigate back | same | same |
| Ctrl+R   | Refresh state | Refresh state | Refresh state |
| Ctrl+A   | Select all tiles | Select all tiles | Select all tiles |
| V        | Toggle grid/list | Toggle grid/list | Toggle grid/list |

We have: F5 rescan (Agent 5 added rescan button). Ctrl+F: missing. Escape clear search: possibly missing. Ctrl+A: missing (no multi-select). V: missing (no list view).

---

# SECTION 9: SHELL DIMENSIONS REFERENCE

From `parity_dimensions.py` ShellSpec:

| Name | Value |
|------|-------|
| topbar_height | 56px |
| sidebar_width | 252px |
| content_padding | 20px |
| content_gap | 24px |

Content area inner layout margins: `(content_padding, 0, content_padding, content_padding)` = (20, 0, 20, 20)
Content area spacing: 24px between sections.

---

# PRIORITY SUMMARY

## P0 — Must Fix for Parity

- Tile dimensions: card width expansion to fill available space
- TileCard: progress bar (3px bottom of cover), page badge (bottom-left pill), cover corner radius 8px
- TileCard: status="finished" hides badge, progress bar turns green
- Continue strip: series dedup, 40-tile limit, finished exclusion, correct progress fields
- Videos continue: 250ms single-click delay
- Continue tile context menus: all 3 libraries
- Comics grid multi-selection context menu
- FolderDetailView file row context menu: "Set as series cover", "Mark as read/unread", "Reset progress", "Remove from library"
- Progress cell icons in detail view (green checkmark / slate circle)
- Videos context menu: poster management, "Auto-rename"
- Search: Ctrl+F, tooltip, debounce verification

## P1 — Should Add for Good UX

- Sidebar (252px) — significant architecture addition
- FoldingDetailView: continue bar (40px), forward navigation, naming toggle
- TileStrip: keyboard navigation (arrow keys), multi-selection (Ctrl+click, Shift+click)
- TileCard: "New" dot, folder icon overlay, hover flash 120ms
- Empty state labels
- Books search algorithm (scored ranking)
- FadingStackedWidget transitions
- Restore hidden button (books only — already noted visible when hidden IDs exist)

## P2 — Nice to Have

- List view mode (V-key, QTreeWidget)
- Drag-and-drop cover replace on tiles
- Videos: poster management (set/remove/paste)
- Videos: "Auto-rename" feature

---

# CONGRESS AGENT ASSIGNMENTS (SUGGESTED)

When the congress opens, assign lanes by domain ownership:

- **Agent 5 (Library UX):** All sections above — this is their territory. Files positions on ALL items. Domain master's position carries full weight.
- **Agent 1 (Comic Reader):** Comment on any items that touch ComicsPage or the ComicReader integration. Also comment on progress field shapes (what they save).
- **Agent 2 (Book Reader):** Comment on book cover extraction, progress fields, books continue. On hold for implementation but can vote.
- **Agent 3 (Video Player):** Comment on videos continue watching progress fields (positionSec, path), 250ms single-click, partial progress updates.
- **Agent 4 (Sources):** Comment only if any library item touches torrent state display (download %, dl_status columns). Otherwise observer.

Agent 0 synthesizes and issues the implementation work order to Agent 5 (and any other agent whose territory is touched).
