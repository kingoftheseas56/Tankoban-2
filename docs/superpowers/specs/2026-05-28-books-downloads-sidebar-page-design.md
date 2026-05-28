# Books Downloads sidebar page + TankoLibrary sidebar removal — Design spec

- **Date:** 2026-05-28
- **Author:** Agent 2 (Book Reader + TankoLibrary)
- **Arc tag:** `BOOKS_DOWNLOADS_SIDEBAR_PAGE`
- **Status:** Brainstorm complete — design approved by Hemanth 2026-05-28. Pending spec review → `/superpowers:writing-plans`.
- **Predecessors:** `STREAM_DOWNLOADS_SIDEBAR_PAGE` (Agent 4, 2026-05-25 — `StreamDownloadsPage`) + `COMICS_DOWNLOADS_SIDEBAR_PAGE` (Agent 9 Trigger-D, 2026-05-26 — `ComicsDownloadsPage`). This applies the same sidebar-Downloads pattern to Books mode.
- **Skills invoked:** `/superpowers:brainstorming`, `/hemanth-language`.

---

## 1. Problem / goal

The sidebar drawer (`SidebarDrawer`) has a per-mode **Downloads** entry for Theatre (`setStreamDownloadsVisible`) and Comics (`setComicsDownloadsVisible`) but **not Books**. It also still carries a **TankoLibrary** source entry, which is now vestigial: book discovery + per-book download moved into Books mode natively (FictionDB catalogue search + the §5.2 per-book Get flow), the same way Tankoyomi was absorbed into Comics and Tankorent into Theatre.

**Goal:** remove the TankoLibrary sidebar entry and add a Books **Downloads** sidebar entry (Books-mode-only) that opens a Books Downloads page.

## 2. Decisions locked (brainstorm 2026-05-28)

- **D1 — Books Downloads page has two sections:** **Downloading** (in-progress transfers with live %) + **Downloaded** (completed books grouped by series, read-only). This is distinct from the library grid (which shows only finished books) — the Downloads page is where active transfers are watched.
- **D2 — TankoLibrary removal = sidebar entry only.** Remove the `m_btnTankoLibrary` button from `SidebarDrawer`. The `TankoLibraryPage` + its `sources_*_tankolibrary` dev-bridge commands + MainWindow page wiring **stay** (unreachable from the UI, reversible). Nothing user-facing is lost — book search/download lives in the Books catalogue + per-book Get flow.
- **D3 — Layout mirrors the existing Downloads pages** (`ComicsDownloadsPage` structure: back button + scroll + section headers). Not redesigning the Downloads-page shape.
- **D4 — Books-mode-scoped visibility.** The Downloads entry shows only when the active mode is Books (`PAGE_BOOKS` / `PAGE_BOOKS_DOWNLOADS`), exactly like Comics/Theatre.

## 3. What the user experiences

- Open the sidebar in **Books mode** → it shows a **Downloads** entry; the **TankoLibrary** entry is gone. (In Comics/Theatre the Books Downloads entry is hidden, like the other modes' entries are.)
- Click **Downloads** → the Books Downloads page:
  - **Downloading** section — one row per in-progress book (cover thumb + title + author + a live **NN%**). A row vanishes when its download completes (it then appears under Downloaded). Section hides entirely when nothing is downloading.
  - **Downloaded** section — completed books grouped by series (series tile shows the series + owned-count; standalone books as their own tiles), same grouping as the library grid. Read-only in v1.
  - Back button → returns to the Books library (mode root).
- Sidebar elsewhere is unchanged.

## 4. Components & data flow

- **`BooksDownloadsPage`** *(new — `src/ui/pages/books/BooksDownloadsPage.{h,cpp}`)*. A `QFrame` mirroring `ComicsDownloadsPage`: topbar back button (`backRequested` signal), `QScrollArea`, a **Downloading** section + a **Downloaded** section, empty-state handling.
  - `setCatalogueStore(BooksCatalogueLibraryStore*)` — Downloaded section source. Subscribes to `recordsChanged` to refresh. Groups by `seriesId` (mirrors `BooksPage::rebuildBookGrid`'s grouping; a local ~15-line loop, store reads only).
  - `setBooksPage(BooksPage*)` — Downloading section source. Subscribes to a new `BooksPage::downloadsChanged()` signal; pulls the active list via a new getter.
- **`BooksPage`** *(additions)*:
  - A small POD `struct ActiveDownloadInfo { QString title, author, coverPath; int percent; }` (or reuse fields off `ActiveCatalogueDownload`).
  - Store the latest percent per handle: extend the existing `m_activeDownloads` entries (add `int percent = 0`) updated in `onBookDownloadProgress` (`bytesTotal>0 ? recv*100/total : 0`).
  - Getter `QList<ActiveDownloadInfo> activeDownloads() const` — projects `m_activeDownloads`.
  - Signal `void downloadsChanged()` — emitted on download **start** (`onCatalogueDownloadRequested` after inserting), **progress** (`onBookDownloadProgress`), **complete** (`onBookDownloadComplete`), **fail** (`onBookDownloadFailed`).
- **`SidebarDrawer`** *(modify — `src/ui/widgets/SidebarDrawer.{h,cpp}`)*:
  - Remove `m_btnTankoLibrary` + its `buildUi`/`styleItem`/click wiring; update the pageId doc comment (drop `"tankolibrary"`, add `"booksDownloads"`).
  - Add `m_btnBooksDownloads` + `void setBooksDownloadsVisible(bool)` (default hidden, mirrors `setComicsDownloadsVisible`), pageId `"booksDownloads"`, included in `setActiveSource` active-state styling.
- **`MainWindow`** *(modify — `src/ui/MainWindow.cpp`)*:
  - `static constexpr const char* PAGE_BOOKS_DOWNLOADS = "booksDownloads";`
  - Construct `m_booksDownloadsPage`, set objectName, `setCatalogueStore(booksPage->catalogueStore())` + `setBooksPage(booksPage)`, add to `m_pageStack`, wire `backRequested` → return to `PAGE_BOOKS`.
  - In the `activatePage` sidebar block: `m_sidebar->setBooksDownloadsVisible(pageId == PAGE_BOOKS || pageId == PAGE_BOOKS_DOWNLOADS);`
  - `sourceClicked` already routes via `activatePage(pageId)`; `"booksDownloads"` resolves to the page by objectName. No router change beyond the page existing in the stack.
  - **TankoLibrary:** leave `TankoLibraryPage` construction + `tankolibrary_*` dev-bridge forwarding intact (D2). Only the sidebar button is gone, so it's no longer reachable from the UI.

**New accessor needed:** `BooksPage::catalogueStore()` (returns `m_catalogueStore`) so MainWindow can wire the Downloads page to the same store. (BooksPage owns the store privately today.)

## 5. Error handling / edge cases

- **Nothing downloading:** Downloading section hidden; if also no completed books, the page shows an empty-state label (mirror `ComicsDownloadsPage::updateEmptyState`).
- **Download completes while page open:** `downloadsChanged()` → row leaves Downloading; `recordsChanged()` (the new record upsert) → it appears under Downloaded. Both refresh paths already fire.
- **Cover missing for an active download:** show the placeholder tile (no cover), same as the series view's missing-cover handling.
- **Page open in non-Books mode:** can't happen via UI — the entry is Books-only and back returns to Books.

## 6. Testing

UI surface — verified by `build_check.bat` BUILD OK + live smoke (start a book download → see it in Downloading with %, watch it move to Downloaded on completion; confirm TankoLibrary entry gone, Downloads entry present in Books mode only). No new pure-logic GoogleTest seam (the percent projection is trivial arithmetic; grouping reuses the store). Consistent with `ComicsDownloadsPage`, which has no unit tests.

## 7. Out of scope

- Redesigning the Downloads-page visual shape (mirrors Comics).
- Pause/resume/cancel controls on active downloads (future; v1 is display + live %).
- Full retire of `TankoLibraryPage` / its dev-bridge commands (D2 — sidebar entry only).
- Books download **history** persistence (no separate book download index; completed = catalogue store, active = transient `m_activeDownloads`).
- Comics/Theatre changes.
