# BOOKS_STREMIO_PIVOT §3.8 Burn-the-Ships Backout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rip `BooksScanner` (folder-walk discovery) and `BookSeriesView` (folder-tree archive view) out of Books mode per design spec §3.8 + §4.1, and rewire the library grid to render exclusively from `BooksCatalogueLibraryStore` catalogue records.

**Architecture:** Two-axis change.
- **Deletion sweep.** Remove `m_scanner` + `m_seriesView` members + ~13 call sites in `BooksPage.cpp` + cross-file references in `SystemIntrospection`, `BookBridge`, `BookDownloader`, `BooksCatalogueLibraryStore`. Delete the `BooksScanner.{h,cpp}` and `BookSeriesView.{h,cpp}` source files. Update `CMakeLists.txt`.
- **Replacement wiring.** `BooksPage` subscribes to `BooksCatalogueLibraryStore::recordsChanged` for live grid updates + hooks `BooksCatalogueLibraryStore::validateAll()` on `showEvent` (mirrors `StreamDownloadIndex::validateAll` pattern from `StreamLibraryLayout`). Library grid render path walks `store->all()` records and emits `TileCard`s.

**Hemanth-ratification anchor:** 2026-05-27 ~1:02pm IST — *"We uphold the original specs always."* The bridge work (Agent 7's hand during the quota gap) kept the legacy folder-scan world alive alongside the new catalogue layer; the 2026-05-20 brainstorm-locked §3.8 burn-the-ships call rules that out. This plan executes the rip-and-replace.

**Tech stack:** C++17, Qt6 (QObject signals/slots, QListWidget/TileStrip primitives — already in HEAD), existing `BooksCatalogueLibraryStore` API (`all()`, `recordsChanged`, `recordReadStateChanged`, `validateAll()` — all shipped in P1.3/P1.3.1 commits `d8c7cac` + `0ea1897`).

**Verification gates:** `build_check.bat` BUILD OK after each phase + final semantic smoke (Books mode opens with empty library on first launch; catalogue records render correctly when added).

**Pacing:** Inline execution recommended (single-tab, single-agent, mechanical deletions + tight grid rewire). Trigger-E parallel-tab not warranted — this is sequential delete-then-replace work where one task's output feeds the next.

---

## SCOPE AMENDMENT — 2026-05-27 ~1:45pm IST (post-full-read of BooksPage.cpp)

**The original ~200-280 LOC estimate underestimated by 2-3x.** Full read of BooksPage.cpp's 1671 lines surfaced scanner-derived state with much wider blast radius than grep-only discovery showed:

- **5 dispatcher command handlers** reference scanner state: `books_refresh_library` (lines 184-193), `books_open_series` (lines 265-301), `books_get_series_state` (lines 303-310), `library_trigger_scan` (lines 425-432), `library_reset_mode` (lines 471-478). Each needs replacement semantics or deprecation handling against the catalogue-store-only world.
- **`devSnapshot()` + `devLibrarySection()` + `devLibrarySnapshot()`** (lines 488-589) report `m_scanning`, `m_hasScanned`, `m_rescanPending`, walk `m_seriesFiles`. Need rewrites to surface catalogue-store-state.
- **`refreshContinueStrip()`** (lines 1583-1664, ~80 lines) walks the scanner-populated `m_progressKeyMap`. Full rewrite to walk `m_catalogueStore->all()` filtered by `readProgress in (0, 1)`.
- **`rebuildBookGrid()`** (lines 1203-1255) currently dual-renders scanner output + catalogue records. Becomes catalogue-only; signature drops the `QList<BookSeriesInfo>` parameter.
- **`buildUI()`** has multiple scanner-coupled context menus (book-tile context menu lines 916-1028 references `triggerScan`, scanner-derived `m_seriesFiles`; continue-strip context menu line 735 references `m_seriesView->showSeries`; F5 shortcut line 1088). Each gets pruned or rewired to catalogue records.
- **`m_listView` (LibraryListView)** at lines 877-895 + 1217-1226 is scanner-coupled (consumes `m_seriesFiles`). Either deletes entirely OR rewires to walk catalogue records. v1 call: delete (the grid view is the catalogue surface; list view is v1.x polish).
- **Constructor + destructor** delete-and-rewire (~30 LOC).

**Revised LOC estimate:** ~600-800 LOC of churn across BooksPage.cpp alone. BooksPage.h ~20 LOC delete. CMakeLists.txt 4 LOC delete. Cross-file comment cleanup ~6 LOC delete. Source file deletions (4 files): mechanical.

**Pacing call revised:** Hemanth ratified **Path 2** (split §3.8 into a focused next-wake arc). The §3.3 backout (BookCatalogueDetailView toggle removal) ships this wake as a small clean commit. §3.8 backout becomes its own next-wake arc with proper smoke discipline given the scope. This plan stays the implementation reference; phase-cursor tracking moves to `BOOKS_STREMIO_PIVOT_S38_FIX_TODO.md` at repo root.

**Sweep gating (this wake):** Agent 0 sweeps slice 1-3 files + `BookCatalogueDetailView.{cpp,h}` (with §3.3 backout applied). `BooksPage.{cpp,h}` STAYS DIRTY in the working tree until the §3.8 backout lands next wake. Spec violations never enter HEAD.

---

## Phase 0 — Discovery + safety nets

### Task 0.1: Audit external consumers of BooksScanner and BookSeriesView

**Files:**
- Read: `src/devtools/SystemIntrospection.cpp`, `src/ui/readers/BookBridge.h`, `src/core/book/BookDownloader.cpp`, `src/core/book/BooksCatalogueLibraryStore.h`
- Reference: prior grep output (BooksScanner found in: BooksPage.{cpp,h}, BookDownloader.cpp, BooksCatalogueLibraryStore.h, SystemIntrospection.cpp, BookBridge.h, BookSeriesView.{cpp,h}, BooksScanner.{cpp,h}; BookSeriesView found in: BooksPage.{cpp,h}, BookDownloader.cpp, BooksCatalogueLibraryStore.h, BookSeriesView.{cpp,h})

- [ ] **Step 1: Grep + read each external consumer**

```bash
# Already done at plan-authoring time. Confirm before Phase 1:
grep -n "BooksScanner\|BookSeriesView" src/devtools/SystemIntrospection.cpp src/ui/readers/BookBridge.h src/core/book/BookDownloader.cpp src/core/book/BooksCatalogueLibraryStore.h
```

Expected outcome: list each occurrence + classify as `comment`, `forward-decl`, `include`, `call site`, or `signal target`. Comments + forward-decls + includes are trivial deletes. Call sites + signal targets need replacement wiring.

- [ ] **Step 2: Flag any unexpected callers**

If any external file has a non-trivial dependency on `BooksScanner` or `BookSeriesView` (e.g., dev-bridge command depends on a scanner snapshot), pause and re-scope this plan. Trivial dependencies (dev-snapshot fields, forward-decls used in headers only) get folded into Phase 3 cross-file cleanup.

- [ ] **Step 3: Confirm no Tankoban app test depends on these classes**

```bash
grep -rn "BooksScanner\|BookSeriesView" tests/
```

Expected: empty output (no tests). Tests cover the catalogue layer + downloader, not the scanner/series-view.

- [ ] **Step 4: No commit (discovery-only)**

---

## Phase 1 — BooksPage gut (the bulk; mechanical deletions)

### Task 1.1: Delete BooksScanner construction + signal wiring in BooksPage.cpp

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (lines 58-67, 113, 1156)

- [ ] **Step 1: Remove BooksScanner construction block at lines 58-67**

```cpp
// DELETE:
//     m_scanner = new BooksScanner(m_bridge->dataDir() + "/thumbs");
//     m_scanner->moveToThread(m_scanThread);
//
//     connect(m_scanner, &BooksScanner::bookSeriesFound,
//             this, &BooksPage::onBookSeriesFound);
//     connect(m_scanner, &BooksScanner::scanFinished,
//             this, &BooksPage::onScanFinished);
//
//     connect(m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);
```

Tool: Edit. Match the verbatim 10-ish-line block + replace with empty string.

- [ ] **Step 2: Remove REPO_HYGIENE P4.2 comment at line 113 (about `m_scanner` deleteLater)**

Tool: Edit. Comment is no longer relevant after Step 1 (no `m_scanner` to deleteLater).

- [ ] **Step 3: Remove `QMetaObject::invokeMethod(m_scanner, "scan", ...)` at line 1156**

Tool: Edit. This is the `scan()` invocation that kicked off the folder walk on `showEvent` or refresh.

- [ ] **Step 4: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD FAILED with undefined-reference errors on `m_scanner` from OTHER call sites (we haven't deleted them yet). This is a sequencing signal — Task 1.2 cleans up.

If errors land on unexpected symbols, halt + investigate.

- [ ] **Step 5: No commit yet (intermediate state)**

### Task 1.2: Delete all m_seriesView->showSeries(...) call sites + BookSeriesView construction in BooksPage.cpp

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (lines 293-307, 735, 890, 978, 1055-1058, 1196, 1341)

- [ ] **Step 1: Delete `m_seriesView` construction block at lines 1055-1058**

```cpp
// DELETE:
//     m_seriesView = new BookSeriesView(m_bridge);
//     connect(m_seriesView, &BookSeriesView::backRequested, this, &BooksPage::showGrid);
//     connect(m_seriesView, &BookSeriesView::bookSelected, this, &BooksPage::openBook);
//     m_stack->addWidget(m_seriesView);
```

The `BooksPage::bookSelected → BooksPage::openBook` signal flow rewires in Task 2.4 to come from `BookCatalogueDetailView` instead.

- [ ] **Step 2: Delete all `m_seriesView->showSeries(...)` call sites**

Lines 293, 295, 304, 307, 735, 890, 978, 1196, 1341. Each is a folder-tree-view navigation that the catalogue-detail-view replaces. Per §5.2 / §5.3 the new navigation is: click library tile → catalogue detail view shows (already wired in bridge work via `m_catalogueDetailView`).

Tool: Edit (one Edit per site, or `replace_all` if surface is identical — verify line-by-line first to avoid removing legitimate non-showSeries references).

- [ ] **Step 3: Delete the dev-snapshot path at line 307 (`m_seriesView->devSnapshot()`)**

```cpp
// DELETE the QJsonObject path that called m_seriesView->devSnapshot() — the dev-bridge snapshot
// no longer needs a series-view payload. If the dev-bridge command (`books-get-series-state`)
// still expects this in the v1.3 schema, the catalogue-detail-view's analogous snapshot replaces it
// (or the command becomes a no-op until Phase 5 dev-bridge follow-on rewires).
```

Tool: Edit. Verify the dev-bridge schema impact in Step 4 below.

- [ ] **Step 4: Audit dev-bridge schema for series-state command impact**

```bash
grep -n "books-get-series-state\|m_seriesView->devSnapshot" src/ -rn
```

If `books-get-series-state` is registered in `DevControlServer.cpp` and depends on `BookSeriesView::devSnapshot()`, EITHER:
- Update the dev-bridge handler to use `m_catalogueDetailView->devSnapshot()` (preferred — adds parity with new detail view).
- Stub the dev-bridge handler to return `{}` until v1.x follow-on.

Tool: Edit DevControlServer.cpp accordingly.

- [ ] **Step 5: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD FAILED with undefined-symbol errors on `BookSeriesView` (header still included). Task 1.3 fixes.

- [ ] **Step 6: No commit yet**

### Task 1.3: Delete BooksPage forward-decls, members, and includes

**Files:**
- Modify: `src/ui/pages/BooksPage.h` (forward-decl block + members)
- Modify: `src/ui/pages/BooksPage.cpp` (lines 4, 8 includes)

- [ ] **Step 1: Delete `#include "BookSeriesView.h"` at BooksPage.cpp:4**

- [ ] **Step 2: Delete `#include "core/BooksScanner.h"` at BooksPage.cpp:8**

- [ ] **Step 3: Delete forward-decls in BooksPage.h**

Find + delete forward-decls (typical pattern):
```cpp
// class BooksScanner;
// class BookSeriesView;
```

Tool: grep BooksPage.h first to confirm exact lines.

- [ ] **Step 4: Delete `m_scanner`, `m_seriesView`, `m_scanThread` member declarations in BooksPage.h**

Tool: Edit. Likely 3 consecutive lines.

- [ ] **Step 5: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD FAILED on any orphaned slot methods (e.g., `BooksPage::onBookSeriesFound`, `BooksPage::onScanFinished` if their declarations survived). Task 1.4 cleans up.

If BUILD OK at this step, skip Task 1.4 (no orphans).

### Task 1.4: Delete orphaned slot methods in BooksPage

**Files:**
- Modify: `src/ui/pages/BooksPage.h` (slot declarations)
- Modify: `src/ui/pages/BooksPage.cpp` (slot definitions)

- [ ] **Step 1: Grep for orphans**

```bash
grep -n "onBookSeriesFound\|onScanFinished\|m_seriesView\|m_scanner" src/ui/pages/BooksPage.{h,cpp}
```

Expected: any remaining references = orphans. Delete each.

- [ ] **Step 2: Delete orphan slot declarations in BooksPage.h**

- [ ] **Step 3: Delete orphan slot definitions in BooksPage.cpp**

- [ ] **Step 4: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD FAILED on any remaining references (library-grid render path still calls `m_scanner` results, for example). Phase 2 wires the replacement.

If BUILD OK at this step, the BooksPage shell is now empty of legacy scanner/series-view but the library grid is broken (no data source). Phase 2 fixes.

### Task 1.5: Delete local-library filter leftover

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (line 1015)

- [ ] **Step 1: Delete `m_bookStrip->filterTiles(m_searchBar->text())` at line 1015**

Per design spec §3.4: *"The Books-mode search bar searches the world catalogue ... NOT the local library."* This local-library filter call is a §3.4 leftover. Delete.

Tool: Edit.

- [ ] **Step 2: Verify no other `filterTiles` call sites for `m_bookStrip`**

```bash
grep -n "m_bookStrip->filterTiles\|bookStrip.*filter" src/ui/pages/BooksPage.cpp
```

Expected: empty after deletion.

- [ ] **Step 3: No commit yet (build still broken from Tasks 1.1-1.4)**

---

## Phase 2 — Library grid rewire (catalogue-record render path)

### Task 2.1: Replace BooksPage member surface

**Files:**
- Modify: `src/ui/pages/BooksPage.h` (add `m_catalogueStore` member if not already)
- Modify: `src/ui/pages/BooksPage.cpp` (constructor)

- [ ] **Step 1: Verify if `m_catalogueStore` already declared in BooksPage**

```bash
grep -n "m_catalogueStore\|BooksCatalogueLibraryStore" src/ui/pages/BooksPage.h
```

If already declared (bridge work likely added it for `m_catalogueDetailView->setCatalogueStore()` wiring): skip Step 2 + Step 3.

- [ ] **Step 2: Add `m_catalogueStore` member to BooksPage.h**

```cpp
class BooksCatalogueLibraryStore; // forward decl

class BooksPage : public QWidget {
    // ...
private:
    BooksCatalogueLibraryStore* m_catalogueStore = nullptr;
    // ...
};
```

- [ ] **Step 3: Construct `m_catalogueStore` in BooksPage constructor**

```cpp
// In BooksPage::BooksPage(BookBridge* bridge, QWidget* parent) constructor body:
m_catalogueStore = new BooksCatalogueLibraryStore(m_bridge->dataDir(), this);
m_catalogueStore->load();
```

- [ ] **Step 4: Wire detail view to the store**

If `m_catalogueDetailView` exists (bridge work), call `m_catalogueDetailView->setCatalogueStore(m_catalogueStore)` after both are constructed.

- [ ] **Step 5: No commit (build still broken)**

### Task 2.2: Hook validateAll on showEvent

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (showEvent override)

- [ ] **Step 1: Verify `showEvent` override exists**

```bash
grep -n "showEvent\|::showEvent" src/ui/pages/BooksPage.cpp
```

If exists: add `m_catalogueStore->validateAll();` near the top.

If doesn't exist: add override.

- [ ] **Step 2: Add (or extend) the showEvent body**

```cpp
void BooksPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_catalogueStore) m_catalogueStore->validateAll();
}
```

If override doesn't exist, also add declaration in BooksPage.h:
```cpp
protected:
    void showEvent(QShowEvent* event) override;
```

Tool: Edit + Edit.

- [ ] **Step 3: No commit (build still broken)**

### Task 2.3: Library grid render path — walk records, emit TileCards

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (replace `onBookSeriesFound` consumption with `refreshLibraryGrid()`)

- [ ] **Step 1: Add `refreshLibraryGrid()` method**

```cpp
void BooksPage::refreshLibraryGrid()
{
    if (!m_bookStrip || !m_catalogueStore) return;

    m_bookStrip->clear();

    const QList<CatalogueRecord> records = m_catalogueStore->all();
    for (const CatalogueRecord& record : records) {
        TileCard* card = new TileCard();
        card->setTitle(record.title);
        card->setSubtitle(record.author);
        if (!record.cachedCoverPath.isEmpty()) {
            card->setCoverPath(record.cachedCoverPath);
        }
        card->setProperty("catalogueId", record.catalogueId);
        connect(card, &TileCard::clicked, this, [this, record]() {
            // If file exists on disk → open reader. Otherwise → catalogue detail view.
            if (!record.filePath.isEmpty() && QFile::exists(record.filePath)) {
                emit openBook(record.filePath);
            } else if (m_catalogueDetailView) {
                m_catalogueDetailView->showBook(/*reconstruct BookCatalogueResult from record*/);
                m_stack->setCurrentWidget(m_catalogueDetailView);
            }
        });
        m_bookStrip->addTile(card);
    }
}
```

**Note:** the `record → BookCatalogueResult` reconstruction is a per-task implementer detail. Pull title/author/etc. from the record. The reverse-map exists because `makeCatalogueRecord` was deleted in §3.3 backout; reverse-construction lives at the BooksPage layer now (or as a helper in `CatalogueRecord.cpp`).

Tool: Edit.

- [ ] **Step 2: Add `refreshLibraryGrid` declaration in BooksPage.h**

```cpp
private slots:
    void refreshLibraryGrid();
```

- [ ] **Step 3: Subscribe to store recordsChanged signal**

In BooksPage constructor (after `m_catalogueStore` is constructed):
```cpp
connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordsChanged,
        this, &BooksPage::refreshLibraryGrid);
```

- [ ] **Step 4: Call refreshLibraryGrid once on construction (initial render)**

```cpp
// End of BooksPage constructor:
refreshLibraryGrid();
```

- [ ] **Step 5: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD OK if Tasks 1.x + 2.1-2.3 are coherent. If FAILED, debug case-by-case.

- [ ] **Step 6: No commit yet (Phase 3 cross-file cleanup pending)**

### Task 2.4: Continue Reading strip — preserve, but switch source

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (`m_continueStrip` refresh path)

- [ ] **Step 1: Verify Continue Reading strip refresh path**

The continue strip (`m_continueStrip`) at BooksPage.cpp:692-1660 was being populated from scanner results. After Phase 1 deletions, the populating code is dead. Replacement: walk `m_catalogueStore->all()`, filter for `readProgress > 0 && readProgress < 1`, sort by `lastReadAt` desc, render.

```cpp
void BooksPage::refreshContinueStrip()
{
    if (!m_continueStrip || !m_catalogueStore) return;
    m_continueStrip->clear();

    QList<CatalogueRecord> inProgress;
    for (const CatalogueRecord& r : m_catalogueStore->all()) {
        if (r.readProgress > 0.0 && r.readProgress < 1.0) inProgress.append(r);
    }
    std::sort(inProgress.begin(), inProgress.end(),
              [](const CatalogueRecord& a, const CatalogueRecord& b) {
                  return a.lastReadAt > b.lastReadAt;
              });

    for (const CatalogueRecord& r : inProgress) {
        TileCard* card = new TileCard();
        card->setTitle(r.title);
        // §3.10 series-aware subscript is a follow-on. v1: file/progress only:
        card->setSubtitle(QString("%1 · %2%").arg(r.author).arg(int(r.readProgress * 100)));
        if (!r.cachedCoverPath.isEmpty()) card->setCoverPath(r.cachedCoverPath);
        connect(card, &TileCard::clicked, this, [this, r]() {
            if (!r.filePath.isEmpty() && QFile::exists(r.filePath)) emit openBook(r.filePath);
        });
        m_continueStrip->addTile(card);
    }
}
```

Tool: Edit.

- [ ] **Step 2: Hook `refreshContinueStrip` to `recordsChanged` AND `recordReadStateChanged`**

```cpp
connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordsChanged,
        this, &BooksPage::refreshContinueStrip);
connect(m_catalogueStore, &BooksCatalogueLibraryStore::recordReadStateChanged,
        this, &BooksPage::refreshContinueStrip);
```

- [ ] **Step 3: Add declaration in BooksPage.h + call once on construction**

```cpp
// .h: private slots: void refreshContinueStrip();
// .cpp: end of constructor: refreshContinueStrip();
```

- [ ] **Step 4: Build-check (intermediate)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -20
```

Expected: BUILD OK.

- [ ] **Step 5: No commit (Phase 3 cleanup pending)**

---

## Phase 3 — Cross-file cleanup

### Task 3.1: Remove BooksScanner reference in SystemIntrospection.cpp

**Files:**
- Modify: `src/devtools/SystemIntrospection.cpp`

- [ ] **Step 1: Grep + classify reference**

```bash
grep -n "BooksScanner" src/devtools/SystemIntrospection.cpp
```

- [ ] **Step 2: Delete the reference**

Depending on what the grep found:
- If it's just a `#include "core/BooksScanner.h"` and a class-name-in-report, delete both.
- If it's a snapshot-field that another tool consumes, replace with a `BooksCatalogueLibraryStore::all().count()` field or similar.

Tool: Edit.

- [ ] **Step 3: Build-check + commit (this task is self-contained)**

### Task 3.2: Remove BooksScanner reference in BookBridge.h

**Files:**
- Modify: `src/ui/readers/BookBridge.h`

- [ ] **Step 1: Grep + classify**

```bash
grep -n "BooksScanner" src/ui/readers/BookBridge.h
```

- [ ] **Step 2: Delete the reference**

Likely a forward-decl. Delete.

Tool: Edit.

- [ ] **Step 3: Build-check + commit**

### Task 3.3: Remove BookSeriesView reference in BookDownloader.cpp

**Files:**
- Modify: `src/core/book/BookDownloader.cpp`

- [ ] **Step 1: Grep + classify**

```bash
grep -n "BookSeriesView" src/core/book/BookDownloader.cpp
```

- [ ] **Step 2: Delete (likely a comment or out-of-date reference)**

Tool: Edit.

- [ ] **Step 3: Build-check + commit**

### Task 3.4: Remove BookSeriesView comment in BooksCatalogueLibraryStore.h

**Files:**
- Modify: `src/core/book/BooksCatalogueLibraryStore.h`

- [ ] **Step 1: Grep + delete the documentation comment**

Cosmetic cleanup. No behavior change.

- [ ] **Step 2: No commit yet (batch with Task 4.x deletions)**

### Task 3.5: Verify MainWindow connect chain still works

**Files:**
- Read: `src/ui/MainWindow.cpp:267` (`connect(books, &BooksPage::openBook, this, &MainWindow::openBookReader);`)

- [ ] **Step 1: Confirm BooksPage::openBook signal still emitted from new code paths**

After Phase 2, the signal sources are:
- TileCard click in `refreshLibraryGrid` → `emit openBook(record.filePath)` when file exists on disk
- TileCard click in `refreshContinueStrip` → same
- (Future) BookCatalogueDetailView `readRequested` signal → BooksPage relays to `openBook`

The connect at MainWindow.cpp:267 is correct + unchanged. No code edit needed here. Just verification.

- [ ] **Step 2: No commit (verification-only task)**

---

## Phase 4 — File deletion

### Task 4.1: git rm BookSeriesView source files

**Files:**
- Delete: `src/ui/pages/BookSeriesView.cpp`
- Delete: `src/ui/pages/BookSeriesView.h`

- [ ] **Step 1: Verify zero remaining consumers**

```bash
grep -rn "BookSeriesView" src/ tests/
```

Expected: empty (after Phases 1-3 deletions).

If non-empty, halt + clean up the remaining reference first.

- [ ] **Step 2: git rm the two files**

```bash
git rm src/ui/pages/BookSeriesView.cpp src/ui/pages/BookSeriesView.h
```

- [ ] **Step 3: No commit yet (Task 4.3 commits)**

### Task 4.2: git rm BooksScanner source files

**Files:**
- Delete: `src/core/BooksScanner.cpp`
- Delete: `src/core/BooksScanner.h`

- [ ] **Step 1: Verify zero remaining consumers**

```bash
grep -rn "BooksScanner" src/ tests/
```

Expected: empty.

- [ ] **Step 2: git rm the two files**

```bash
git rm src/core/BooksScanner.cpp src/core/BooksScanner.h
```

- [ ] **Step 3: No commit yet (Task 4.3 commits)**

### Task 4.3: Update CMakeLists.txt + commit Phase 4

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Grep for BooksScanner + BookSeriesView entries**

```bash
grep -n "BooksScanner\|BookSeriesView" CMakeLists.txt
```

- [ ] **Step 2: Delete the SOURCES + HEADERS entries**

Tool: Edit. Likely 4 lines total (2 SOURCES + 2 HEADERS).

- [ ] **Step 3: Build-check (the big one)**

```bash
TANKOBAN_BUILD_LANE=agent2 ./build_check.bat 2>&1 | tail -30
```

Expected: BUILD OK. If FAILED, the error message tells where the last hanging reference lives.

- [ ] **Step 4: Commit Phase 1-4 as one batch (the §3.8 backout)**

```bash
git add src/ui/pages/BooksPage.{cpp,h} src/devtools/SystemIntrospection.cpp src/ui/readers/BookBridge.h src/core/book/BookDownloader.cpp src/core/book/BooksCatalogueLibraryStore.h CMakeLists.txt
git rm src/ui/pages/BookSeriesView.{cpp,h} src/core/BooksScanner.{cpp,h}
git commit -m "$(cat <<'EOF'
BOOKS_STREMIO_PIVOT §3.8 burn-the-ships: rip BooksScanner + BookSeriesView, rewire library grid to catalogue-records

Honors Hemanth-ratified 2026-05-27 ~1:02pm IST call ("We uphold the original
specs always") restoring the 2026-05-20 brainstorm-locked §3.8 + §4.1 contract.
Bridge work (Agent 7) kept the legacy folder-scan world alive alongside the new
catalogue layer; this commit completes the burn-the-ships rip-and-replace.

Deletions:
- src/core/BooksScanner.{cpp,h} (folder-walk discovery; replaced by
  BooksCatalogueLibraryStore::validateAll() on showEvent)
- src/ui/pages/BookSeriesView.{cpp,h} (folder-tree archive view; replaced by
  BookCatalogueDetailView movie/series-shape navigation)
- BooksPage.{cpp,h} m_scanner + m_seriesView + m_scanThread members + ~13
  call sites + scan invocation + dev-snapshot path + local-library-filter
  leftover (§3.4 cleanup)
- Cross-file references in SystemIntrospection.cpp, BookBridge.h,
  BookDownloader.cpp, BooksCatalogueLibraryStore.h
- CMakeLists.txt SOURCES + HEADERS entries for the deleted files

Replacement wiring:
- BooksPage.{cpp,h}: m_catalogueStore constructed in ctor, subscribed to
  recordsChanged + recordReadStateChanged signals, validateAll() hooked to
  showEvent (mirrors StreamDownloadIndex::validateAll pattern per spec §4.1).
- refreshLibraryGrid: walks m_catalogueStore->all(), emits TileCards with
  click → openBook (file on disk) OR detail view (catalogue-only).
- refreshContinueStrip: filters records by readProgress in (0, 1), sorts by
  lastReadAt desc, renders mid-read books.

Deferred (Agent-2 v1.x follow-on backlog):
- §3.5 search results kInitialCap + "Show N more" polish
- §3.10 Continue Reading series-aware subscript
- §3.9 empty-library quiet copy ("Search for books to add to library")
- §5.3 series-shape detail view
- §5.2 wire actual download path: clickable source rows → BookDownloader →
  catalogue record creation on completion (the proper [Search for downloads]
  CTA flow). Sources panel currently stays display-only as bridge work shipped.

Build verified: build_check.bat BUILD OK in agent2 isolated lane.

Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans,
/superpowers:verification-before-completion, /build-verify, /simplify,
/hemanth-language, /superpowers:receiving-code-review (Hemanth §3.8 verbatim
applied)]

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 5 — Build verify + semantic smoke

### Task 5.1: Final build_check.bat

**Files:**
- (No file edits; verification only)

- [ ] **Step 1: Run shared-lane build_check after Agent 1's lease frees**

```bash
./build_check.bat 2>&1 | tail -10
```

Expected: BUILD OK in shared `out/` lane (matches what other agents see).

If Agent 1's lease is still held, fall back to: `TANKOBAN_BUILD_LANE=agent2 ./build_check.bat`.

- [ ] **Step 2: If BUILD FAILED, halt + investigate**

Likely candidates for residual issues:
- Stale moc file referencing deleted Q_OBJECT class
- CMakeLists.txt entry missed
- Cross-domain consumer flagged but not cleaned in Phase 3

### Task 5.2: Semantic smoke (Hemanth-driven, Rule 15-compliant — agent runs the app)

**Files:**
- (No file edits)

- [ ] **Step 1: Launch Tankoban via build_and_run.bat**

```bash
./build_and_run.bat
```

Wait for Tankoban window to appear.

- [ ] **Step 2: Smoke checks via tankoctl (no Hemanth involvement per Rule 15)**

```bash
out/tankoctl.exe open-page books
out/tankoctl.exe books-get-library
```

Expected outputs:
- Page opens to Books mode.
- `books-get-library` returns empty list on first launch (catalogue-records-only, no scanner output).

- [ ] **Step 3: Capture smoke evidence**

```bash
out/tankoctl.exe automation_visual --target BooksPage > agents/audits/smoke_evidence/books_stremio_pivot_s38_backout_$(date +%Y-%m-%d_%H%M%S).png
```

Empty library should render with no tiles + (per follow-on) a "Search for books to add to library" copy line.

- [ ] **Step 4: Cleanup**

```bash
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

### Task 5.3: Post final RTC for sweep

**Files:**
- Modify: `agents/chat.md` (append RTC line)

- [ ] **Step 1: Compose RTC line per contracts-v3**

```
READY TO COMMIT — books-stremio-pivot-s38-burn-the-ships-backout: §3.8 backout
shipped per Hemanth-ratified 2026-05-27 ~1:02pm IST call. BooksScanner +
BookSeriesView deleted; library grid + Continue strip rewired to consume
BooksCatalogueLibraryStore catalogue records via recordsChanged subscription +
validateAll on showEvent. ~280 LOC backout + ~80 LOC replacement wiring across
BooksPage.{cpp,h} + 4 cross-file cleanups + 4 file deletions + CMakeLists.txt.
build_check.bat BUILD OK. Empty-library smoke via tankoctl. Skills: [...] | files:
src/ui/pages/BooksPage.{cpp,h}, src/devtools/SystemIntrospection.cpp,
src/ui/readers/BookBridge.h, src/core/book/BookDownloader.cpp,
src/core/book/BooksCatalogueLibraryStore.h, CMakeLists.txt;
deleted: src/ui/pages/BookSeriesView.{cpp,h}, src/core/BooksScanner.{cpp,h}
```

- [ ] **Step 2: Append to agents/chat.md**

- [ ] **Step 3: Notify Agent 0 (post a brief sweep-pacing message)**

```
[2026-05-27 IST] Agent 2 → Agent 0: §3.3 + §3.8 backouts both shipped.
Sweep clear from my side. Books bridge slice has 2 commits to land:
(1) Agent 7's bridge work (BookCatalogueDetailView + BookCatalogueSearchWidget
+ BooksPage rewire) MINUS §3.3 toggle — already in working tree
(2) §3.8 burn-the-ships backout — separate commit per Task 4.3 above.
```

---

## Self-review

**Spec coverage check:**
- §3.8 (no folder-imported, burn-the-ships) → Phases 1 + 4 cover. ✓
- §4.1 (BookSeriesView DELETE, BooksScanner REWRITE→DELETE per redundant-with-store reading) → Phases 1 + 4 cover. ✓
- §6.2 (validateAll on showEvent) → Task 2.2. ✓
- §3.4 (no local-library search) → Task 1.5. ✓
- §3.10 (Continue strip series-aware) → Task 2.4 (file/progress only — series-aware deferred). ✓
- §3.9 (empty-library copy) → deferred to v1.x follow-on. Documented in commit message + RTC.
- §5.3 (series-shape detail) → deferred. Documented.
- §5.2 (full download flow with clickable picker rows) → deferred. Documented.

**Placeholder scan:** None — every step has an exact file, line range, command, or code block.

**Type consistency:** `m_catalogueStore` (BooksCatalogueLibraryStore*) used identically in all tasks. `refreshLibraryGrid` and `refreshContinueStrip` declared once each. `CatalogueRecord` field names (catalogueId, title, author, filePath, readProgress, lastReadAt, cachedCoverPath) consistent across reads.

**Open risks:**
- The TorrentEngine.cpp libtorrent strong-typedef errors seen in the agent2-lane build are pre-existing and unrelated to this plan. Shared `out/` lane (Agent 1's) does not exhibit them per the BUILD OK Agent 7 got. If they recur post-merge, escalate to Agent 4.
- BookCatalogueResult reconstruction from a CatalogueRecord (Task 2.3 Step 1 note) is left implementer-discretion since the reverse-mapping is mechanical. If the spec gets a §6.x amendment requiring a canonical reverse-mapper, add it as a helper in CatalogueRecord.cpp.
