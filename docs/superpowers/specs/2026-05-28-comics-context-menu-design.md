# Comics context menus — Design spec

- **Date:** 2026-05-28
- **Author:** Agent 1 (Comic Reader + Tankoyomi). Brainstorm split across engines: scoped + ground-truthed by Agent 1 (Opus); design sections walked with Hemanth by Agent 1 (DeepSeek V4-Pro); finished + API-verified + spec written by Agent 1 (Opus).
- **Arc tag:** `COMICS_CONTEXT_MENU`
- **Status:** Brainstorm complete — design walked with Hemanth 2026-05-28. Pending spec review → `/superpowers:writing-plans`.
- **Predecessor / template:** `BOOKS_LIBRARY_CONTEXT_MENU` (`docs/superpowers/specs/2026-05-28-books-library-context-menu-design.md`). This is the explicitly-planned Comics sibling named in that spec's §7. Mirrors its D1-D5 + the `ContextMenuHelper` reuse pattern; extends it with the volume layer Books doesn't have.
- **External blueprint:** `C:\Users\Suprabha\Desktop\Tankoban-Max` — the prior-generation app's context menus (Hemanth-pointed 2026-05-28). Informed the volume-menu simplification + the reader-menu additions.
- **Skills invoked:** `/superpowers:brainstorming`, `/hemanth-language`.

---

## 1. Problem

Comics mode lets you download volumes but gives **no right-click affordance to manage them** — most pointedly, **no way to delete a download from the UI** (the trigger for this arc: deleting a download is currently a manual file-delete, a coder workaround). The only existing Comics context menu is on the **Continue Reading strip** (`ComicsPage.cpp`); the **library grid series tiles**, the **series-view volume tiles**, and the **Downloads sidebar page** have none. The **Comic Reader** has a rich menu already (Settings / Page Thumbnails / spread toggle / Gutter Shadow at `ComicReader.cpp:3561`) but lacks Reveal / Copy-path. Books just shipped the library-grid pattern; this applies the equivalent — plus the volume layer Books has no equivalent for — across Comics.

## 2. Decisions locked (brainstorm 2026-05-28)

- **D1 — Approach: flat, no new files.** Each surface owns its own `customContextMenuRequested` (or `contextMenuEvent`) handler, exactly as Books did it all inside `BooksPage`. Rejected a `ComicsContextMenu` class (new abstraction for ~4 switch branches) and rejected extending `ContextMenuHelper` with mode-specific logic (wrong coupling). Each surface builds its own menu with its own actions; the **only** shared pieces are the mode-agnostic `ContextMenuHelper` primitives + one small per-surface 3-way-delete dialog helper.
- **D2 — Delete is a 3-way dialog** everywhere a download can be deleted: **Remove from library only** / **Delete the file too** (danger) / **Cancel**. Consistency with Books, and the file-delete is opt-in so a misclick can't nuke a `.cbz`. The "Delete the file too" path is what unlocks Hemanth's **delete-then-re-download-from-an-alternate-source** flow (delete → tile reverts to undownloaded → click → Sources panel → pick another source).
- **D3 — Volume tile menu is lean** (Tankoban-Max pattern, not the Books full set): **Delete…** (3-way) · **Reveal in File Explorer** · **Copy path**. Per-volume Mark-read/Rename do NOT belong here — comics management lives at the **series** level, not the volume level (Books put them per-tile only because Books is one-file-per-book). Non-downloaded volume tiles get **no menu** (mirrors Books D1 — the left-click already opens the Sources panel, which is the download UI).
- **D4 — Library grid series-tile menu is rich** (Books mirror + one Max addition): **Open series** · **Mark as read/unread** · **Rename…** · **Remove series…** (bulk 3-way) · **Refresh metadata** (Max's "rescan" equivalent — re-pull AniList/catalog metadata, not a folder re-scan) · **Reveal in File Explorer** · **Copy path**.
- **D5 — Continue Reading strip menu = ONE action: "Remove from Continue Reading"** (Hemanth verbatim: "continue reading should just have one: remove from continue watching"). Clears the continue/progress entry so the item leaves the strip; **does not** delete files or touch the library. This simplifies the strip's menu down to that single action.
- **D6 — Comic Reader menu = ADD to the existing menu** (it is NOT empty — it already has Settings / Page Thumbnails / spread toggle / Gutter Shadow): append **Reveal in File Explorer** + **Copy volume path**. Go-to-page is already served by the existing **Page Thumbnails…** grid; Export-page + scaling-quality from Max are explicitly deferred (§7).
- **D7 — Downloads sidebar page menu** (`ComicsDownloadsPage`): **Open series** · **Delete…** (3-way, per-download) · **Reveal** · **Copy path**.
- **D8 — Reuse, don't invent.** Build on `ContextMenuHelper` (`src/ui/ContextMenuHelper.h`: `createMenu` / `addDangerAction` / `revealInExplorer` / `copyToClipboard`) and `MangaDownloadIndex` mutators. The 3-way dialog is a small custom `QMessageBox` (the helper's `confirmRemove` is only 2-way Yes/No) shared as a private helper per surface that needs it.

## 3. What the user experiences

**Right-click a downloaded volume tile** (series view) → dark menu:
- **Delete…** → the 3-way dialog. On "Delete the file too", the `.cbz` is removed, the index entry is evicted, and the tile flips back to undownloaded — left-click then opens the Sources panel for re-download from any source.
- **Reveal in File Explorer** / **Copy path** → disabled when the `.cbz` is missing on disk.
- A **non-downloaded** volume tile (incl. an un-downloaded Volume X) → no menu; left-click opens Sources as today.
- **Volume X** once downloaded behaves like any volume — its entry sits at volume `kVolumeXNumber` (99999); `entryForSeriesAndVolume` finds it by number, no special-casing.

**Right-click a library grid series tile** → **Open series · Mark as read/unread · Rename… · Remove series… · Refresh metadata · Reveal · Copy path.**
- **Remove series…** → the 3-way dialog worded for all N downloaded volumes of the series; applies the chosen semantics across every volume via `MangaDownloadIndex::evictBySeries` (+ file removal on "delete files too").
- **Rename…** → renames the series folder on disk AND updates the `canonicalPath` of every `MangaDownloadIndex` entry for the series so records stay valid.
- **Mark as read/unread** → toggles the series `finished` flag in the progress store; flips the tile badge; marking read drops it from Continue Reading.

**Right-click a Continue Reading card** → **Remove from Continue Reading** (single action; clears the continue entry, refreshes the strip; no file/library change).

**Right-click inside the Comic Reader** → the existing menu, now with **Reveal in File Explorer** + **Copy volume path** appended (path from `m_cbzPath`).

**Right-click a Downloads sidebar row** → **Open series · Delete… · Reveal · Copy path** (Delete = the 3-way on that download).

**The 3-way delete dialog** (custom `QMessageBox`, dark-styled, danger button on the destructive option):
- **Remove from library only** → evict index entry/entries; files untouched.
- **Delete the file too** → evict + `QFile::remove(canonicalPath)`; irreversible.
- **Cancel** → no-op.
- After any removal, `MangaDownloadIndex::entriesChanged()` fires → grids/series-view/downloads auto-refresh; `refreshContinueStrip()` is called so a removed in-progress volume also leaves Continue Reading.

## 4. Components & data flow

All changes are flat, in the surfaces that own each menu — **no new files**.

- **Series-view volume tiles — `src/ui/pages/comics/ComicsSeriesView.cpp`.** The volume flow container gets `Qt::CustomContextMenu` + a handler that maps `pos` → the `VolumeTile` under it. If the tile is non-downloaded → bail. If downloaded → build the lean menu; look up the `MangaDownloadIndex::Entry` via `entryForSeriesAndVolume("weebcentral", seriesId, volumeNumber)` for `canonicalPath`. Delete routes through `evictByChapter` for each chapter in the volume (`evictBySeries` is the series-wide variant used by D4).
- **Library grid series tiles — `src/ui/pages/ComicsPage.cpp`.** The comics grid (`m_tileStrip`/equivalent) gets a `customContextMenuRequested` handler mirroring the existing Continue Reading handler. Tile carries `seriesId` / `seriesName` / `sourceId` (set during grid rebuild). Remove-series iterates the series' volumes; Refresh-metadata re-pulls via the existing catalog/AniList resolve path.
- **Continue Reading strip — `src/ui/pages/ComicsPage.cpp`** (existing handler, simplified per D5 to the single Remove-from-CR action).
- **Comic Reader — `src/ui/readers/ComicReader.cpp:3561`** (`contextMenuEvent`, extend): append Reveal + Copy using `m_cbzPath`.
- **Downloads sidebar page — `src/ui/pages/comics/ComicsDownloadsPage.cpp`** (confirm exact file in the plan): per-row handler.
- **Shared 3-way dialog:** a small private helper `removeDownloadDialog(QStringList canonicalPaths, evictFn, QString noun)` per surface (or one on ComicsSeriesView + one on ComicsPage) — shows the dialog once, applies the chosen semantics to all paths. Avoids duplicating the dialog across the single-volume and bulk-series paths.
- **Reused as-is:** `ContextMenuHelper::{createMenu,addDangerAction,revealInExplorer,copyToClipboard}`; `MangaDownloadIndex::{evictBySeries,evictByChapter,entryForSeriesAndVolume}` (synchronous on the calling thread, mutex-guarded, emit `entriesChanged()`); the progress store for read/unread; the existing catalog/AniList resolve for Refresh-metadata.

## 5. Error handling / edge cases

- **Missing file** (index entry exists, `.cbz` deleted out-of-band): Reveal / Copy disabled when `!QFile::exists(canonicalPath)`. "Remove from library only" still drops the stale record; "Delete the file too" evicts the record regardless of whether the file was already gone.
- **Non-downloaded volume tile:** handler bails (no menu) — consistent with D3/Books D1.
- **Volume X tile:** identical menu once downloaded; `entryForSeriesAndVolume(..., 99999)` resolves it like any volume.
- **Rename collision / folder in use:** `QDir::rename` returns false → `QMessageBox::warning`, records left unchanged (mirrors Books).
- **Series with one downloaded volume:** "Remove series" dialog still applies; wording uses the actual downloaded count.
- **Concurrent delete:** `MangaDownloadIndex` is mutex-guarded; the modal 3-way dialog blocks the UI thread so two deletes can't race the same entry.

## 6. Testing

Primarily a UI surface — verified by live smoke (consistent with the Books context menu + the existing Continue Reading menu, which have no unit tests):
- Volume tile: Delete (library-only) drops the index entry, `.cbz` persists; Delete (file too) removes both and the tile reverts to undownloaded; re-click → Sources panel → re-download from an alternate source works; Reveal/Copy behave; non-downloaded tile shows no menu.
- Library grid: Open / Mark / Rename (folder + records updated, reopen works) / Remove series (both semantics across all volumes) / Refresh metadata / Reveal / Copy.
- Continue Reading: the single Remove-from-CR action clears the card.
- Reader: Reveal + Copy path appended to the existing menu, both work.
- Downloads page: Open / Delete / Reveal / Copy.
- `build_check.bat` BUILD OK; full live smoke via `build_and_run.bat`. The delete plumbing (`MangaDownloadIndex` evictions) already carries test coverage through the download-index tests; no new pure-logic primitives (menu building is Qt widget construction; the 3-way is a `QMessageBox`).

## 7. Out of scope

- Reader **Export current page** (save/copy-to-clipboard) and **image scaling quality** menu (Off/Smoother/Sharper) from Tankoban-Max — queued, not v1.
- "Open in new window" (Max has it everywhere; Tankoban is single-window — N/A).
- Context menus on **search results** and **series-detail Sources rows** (Books D1 — they act through their existing buttons).
- Multi-select / batch actions across arbitrary tiles (only series-group bulk remove is in scope).
- Drag-and-drop; keyboard context-menu-key handling beyond Qt defaults.
- The library **duplicate-series** display bug (folder-origin + bookmark-origin One Piece showing twice) — real, but a separate dedup arc, not a context-menu concern.

## 8. Ground-truth verified (Opus pass, against DeepSeek's brainstorm claims)

- `MangaDownloadIndex::evictBySeries` / `evictByChapter` / `entryForSeriesAndVolume` — **confirmed real** (`MangaDownloadIndex.h:82,83,93`), mutex-guarded.
- `ContextMenuHelper` at `src/ui/ContextMenuHelper.h` — `createMenu`, `addDangerAction`, `revealInExplorer`, `copyToClipboard` confirmed; **`confirmRemove` is 2-way only** → the 3-way dialog is a custom `QMessageBox` (not the helper).
- **Comic Reader already has a context menu** (`ComicReader.cpp:3561`: Settings / Page Thumbnails / spread toggle / Gutter Shadow) — corrects DeepSeek's "reader has no menu wired." Reader work = *append* Reveal + Copy, not build new. Has `m_cbzPath` + `m_currentPage`.
