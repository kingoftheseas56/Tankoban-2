# Books Library Context Menu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a right-click context menu to the Books home library grid — owned-book tiles get Read / Mark read·unread / Rename / Remove / Reveal / Copy path; series tiles get Open series / Remove series — with a 3-way "ask each time" remove dialog.

**Architecture:** All in `BooksPage`. Set `Qt::CustomContextMenu` on `m_bookStrip` and add a `customContextMenuRequested` handler that branches on the tile's existing `catalogueSeries` / `catalogueRecord` properties, mirroring the proven Continue Reading handler (`BooksPage.cpp:709`). Reuse `ContextMenuHelper`, `BooksCatalogueLibraryStore::{recordFor,evictByCatalogueId,catalogueIdsForSeries,upsertRecord}`, and the reader-progress JsonStore (`m_bridge->progress/saveProgress`). A shared `removeFromLibrary()` helper owns the remove dialog so single-book and series paths don't duplicate it.

**Tech Stack:** C++20, Qt6 (`QMenu`, `QMessageBox`, `QInputDialog`, `QWidget::customContextMenuRequested`). No new files. Spec: `docs/superpowers/specs/2026-05-28-books-library-context-menu-design.md`.

**Verification:** This is a UI surface — verified by `build_check.bat` BUILD OK + live smoke (right-click each tile type, exercise each action), consistent with the Continue Reading menu which has no unit tests. No pure-logic GoogleTest seam is introduced (the only logic — series-id collection — is `catalogueIdsForSeries`, already tested in the store).

**Flat-on-master:** no worktree (shared checkout, Path A — flag READY TO COMMIT for Agent 0 sweep at the end).

---

## Ground truth (verified signatures, 2026-05-28)

- `BooksPage` (`src/ui/pages/BooksPage.{h,cpp}`): `m_bookStrip` is `TileStrip*` (created `BooksPage.cpp:873`); `m_catalogueStore` is `BooksCatalogueLibraryStore*`; `m_bridge` is `CoreBridge*`; `m_seriesDetailView` is `BookSeriesDetailView*`; `m_stack` is the page stack. Signal `void openBook(const QString& filePath)`. Slots `refreshContinueStrip()`, `rebuildBookGrid()`. Anon-namespace helper `progressKeyFor(const QString&)` (backslash-normalized as of this wake).
- `TileStrip` exposes `TileCard* tileAt(const QPoint&)` (used at `BooksPage.cpp:711` for the Continue strip).
- Library tile properties (set in `rebuildBookGrid` helpers):
  - Owned book (`addCatalogueRecordTile`, `BooksPage.cpp:1068`): `property("catalogueRecord")==true`, `property("catalogueId")`, `property("tileTitle")`.
  - Series group (`addLibrarySeriesTile`, `BooksPage.cpp:1101`): `property("catalogueSeries")==true`, `property("seriesId")`, `property("tileTitle")`, `property("fileCount")`.
- `BooksCatalogueLibraryStore`: `std::optional<CatalogueRecord> recordFor(id)`, `void evictByCatalogueId(id)` (drops record, leaves file; emits `recordsChanged`), `QList<QString> catalogueIdsForSeries(seriesId)`, `void upsertRecord(const CatalogueRecord&)` (replace by catalogueId, emits `recordsChanged`). `recordsChanged` is wired to `rebuildBookGrid` at `BooksPage.cpp:82`.
- `CatalogueRecord` has `QString catalogueId, title, filePath, seriesId, seriesName, ...` (mutable POD; copy + edit `filePath` then `upsertRecord`).
- `CoreBridge`: `QJsonObject progress(domain,itemId) const`, `void saveProgress(domain,itemId,QJsonObject)`. Reader writes `finished` bool into the `"books"` domain keyed by the path-hash.
- `ContextMenuHelper` (`src/ui/ContextMenuHelper.h`): `QMenu* createMenu(QWidget*)`, `void revealInExplorer(QString)`, `void copyToClipboard(QString)`. (`addDangerAction` exists but the destructive emphasis lives on the remove dialog's button instead.)
- Includes already present in `BooksPage.cpp`: `ContextMenuHelper.h` (18), `<QMessageBox>` (48), `<QFileInfo>` (41), `<QFile>` (47), plus `QInputDialog`/`QLineEdit` (used by the Continue rename at `BooksPage.cpp:760`). No new includes needed.

---

## File Structure

- **Modify** `src/ui/pages/BooksPage.h` — declare `void showBookContextMenu(const QPoint& pos);` (private slot) + `void removeFromLibrary(const QStringList& catalogueIds, const QString& subjectLabel);` (private).
- **Modify** `src/ui/pages/BooksPage.cpp` — wire `m_bookStrip` context-menu policy + connect; implement the two methods.

No other files.

---

### Task 1: Declare the new methods in the header

**Files:**
- Modify: `src/ui/pages/BooksPage.h:69-74` (private slots) and `:97-101` (private)

- [ ] **Step 1: Add the slot declaration**

In the `private slots:` block (after `void rebuildBookGrid();` at `BooksPage.h:74`):

```cpp
    // BOOKS_LIBRARY_CONTEXT_MENU (2026-05-28) — right-click menu on m_bookStrip
    // tiles. Branches on the tile's catalogueSeries / catalogueRecord property.
    void showBookContextMenu(const QPoint& pos);
```

- [ ] **Step 2: Add the helper declaration**

In the `private:` block (after `void addLibrarySeriesTile(...)` at `BooksPage.h:100`):

```cpp
    // Shared 3-way remove dialog (library-only / delete-file / cancel) applied
    // to one book or every owned book of a series.
    void removeFromLibrary(const QStringList& catalogueIds,
                           const QString& subjectLabel);
```

- [ ] **Step 3: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK` (declarations only; definitions land in later tasks — header alone still compiles since nothing references them yet).

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/BooksPage.h
git commit -m "[Agent 2, BOOKS_LIBRARY_CONTEXT_MENU]: declare grid context-menu slot + remove helper"
```

---

### Task 2: Wire the context-menu policy + connect on the library grid

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp:873-876` (where `m_bookStrip` is created)

- [ ] **Step 1: Enable custom context menu on m_bookStrip**

Immediately after `layout->addWidget(m_bookStrip);` (`BooksPage.cpp:876`), add:

```cpp
    // BOOKS_LIBRARY_CONTEXT_MENU — right-click any library tile (mirrors the
    // Continue Reading strip handler).
    m_bookStrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookStrip, &QWidget::customContextMenuRequested,
            this, &BooksPage::showBookContextMenu);
```

- [ ] **Step 2: Add a temporary empty definition so it links**

At the end of `BooksPage.cpp` (before any trailing namespace close, after `refreshContinueStrip`'s definition), add a stub to be filled in Task 3:

```cpp
void BooksPage::showBookContextMenu(const QPoint& pos)
{
    Q_UNUSED(pos);
}

void BooksPage::removeFromLibrary(const QStringList& catalogueIds,
                                  const QString& subjectLabel)
{
    Q_UNUSED(catalogueIds);
    Q_UNUSED(subjectLabel);
}
```

- [ ] **Step 3: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/BooksPage.cpp
git commit -m "[Agent 2, BOOKS_LIBRARY_CONTEXT_MENU]: wire m_bookStrip custom context menu + method stubs"
```

---

### Task 3: Implement `showBookContextMenu` (owned-book + series branches)

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (replace the `showBookContextMenu` stub)

- [ ] **Step 1: Replace the stub with the full handler**

```cpp
void BooksPage::showBookContextMenu(const QPoint& pos)
{
    if (!m_bookStrip) return;
    auto* card = m_bookStrip->tileAt(pos);
    if (!card) return;

    // ── Series-group tile ────────────────────────────────────────────────
    if (card->property("catalogueSeries").toBool()) {
        const QString seriesId = card->property("seriesId").toString();
        if (seriesId.isEmpty()) return;
        const QString title = card->property("tileTitle").toString();

        auto* menu = ContextMenuHelper::createMenu(this);
        auto* openAct   = menu->addAction(QStringLiteral("Open series"));
        menu->addSeparator();
        auto* removeAct = menu->addAction(QStringLiteral("Remove series from library"));

        auto* chosen = menu->exec(m_bookStrip->mapToGlobal(pos));
        if (chosen == openAct) {
            if (m_seriesDetailView) {
                m_seriesDetailView->loadSeries(seriesId);
                if (m_stack) m_stack->setCurrentWidget(m_seriesDetailView);
            }
        } else if (chosen == removeAct) {
            QStringList ids;
            if (m_catalogueStore) ids = m_catalogueStore->catalogueIdsForSeries(seriesId);
            removeFromLibrary(ids, title.isEmpty() ? QStringLiteral("this series") : title);
        }
        menu->deleteLater();
        return;
    }

    // ── Owned-book tile ──────────────────────────────────────────────────
    const QString catalogueId = card->property("catalogueId").toString();
    if (catalogueId.isEmpty() || !m_catalogueStore) return;
    const auto rec = m_catalogueStore->recordFor(catalogueId);
    if (!rec) return;
    const QString filePath = rec->filePath;
    const QString progKey  = progressKeyFor(filePath);
    const bool fileOk = !filePath.isEmpty() && QFile::exists(filePath);

    bool finished = false;
    if (m_bridge && !filePath.isEmpty()) {
        const QJsonObject prog = m_bridge->progress(QStringLiteral("books"), progKey);
        finished = prog.value(QStringLiteral("finished")).toBool();
    }

    auto* menu = ContextMenuHelper::createMenu(this);
    auto* readAct = menu->addAction(QStringLiteral("Read"));
    readAct->setEnabled(fileOk);
    auto* markAct = menu->addAction(finished ? QStringLiteral("Mark as unread")
                                             : QStringLiteral("Mark as read"));
    menu->addSeparator();
    auto* renameAct = menu->addAction(QStringLiteral("Rename..."));
    renameAct->setEnabled(fileOk);
    auto* removeAct = menu->addAction(QStringLiteral("Remove from library"));
    menu->addSeparator();
    auto* revealAct = menu->addAction(QStringLiteral("Reveal in File Explorer"));
    revealAct->setEnabled(!filePath.isEmpty());
    auto* copyAct = menu->addAction(QStringLiteral("Copy path"));
    copyAct->setEnabled(!filePath.isEmpty());

    auto* chosen = menu->exec(m_bookStrip->mapToGlobal(pos));
    if (chosen == readAct) {
        if (fileOk) emit openBook(filePath);
    } else if (chosen == markAct) {
        if (m_bridge && !filePath.isEmpty()) {
            QJsonObject prog = m_bridge->progress(QStringLiteral("books"), progKey);
            prog[QStringLiteral("finished")] = !finished;
            m_bridge->saveProgress(QStringLiteral("books"), progKey, prog);
            refreshContinueStrip();
        }
    } else if (chosen == renameAct) {
        QFileInfo fi(filePath);
        const QString newName = QInputDialog::getText(
            this, QStringLiteral("Rename"), QStringLiteral("New name:"),
            QLineEdit::Normal, fi.completeBaseName());
        if (!newName.isEmpty() && newName != fi.completeBaseName()) {
            const QString newPath = fi.absolutePath() + QLatin1Char('/')
                                  + newName + QLatin1Char('.') + fi.suffix();
            if (QFile::rename(filePath, newPath)) {
                CatalogueRecord updated = *rec;       // keep catalogueId, update path
                updated.filePath = newPath;
                m_catalogueStore->upsertRecord(updated);  // emits recordsChanged → rebuild
            } else {
                QMessageBox::warning(this, QStringLiteral("Rename failed"),
                    QStringLiteral("Could not rename \"%1\". The file may be in use "
                                   "by another program.").arg(fi.fileName()));
            }
        }
    } else if (chosen == removeAct) {
        removeFromLibrary({catalogueId}, rec->title);
    } else if (chosen == revealAct) {
        ContextMenuHelper::revealInExplorer(filePath);
    } else if (chosen == copyAct) {
        ContextMenuHelper::copyToClipboard(filePath);
    }
    menu->deleteLater();
}
```

- [ ] **Step 2: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`. (`removeFromLibrary` is still the stub — fine, it's defined.)

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/BooksPage.cpp
git commit -m "[Agent 2, BOOKS_LIBRARY_CONTEXT_MENU]: implement library-grid context menu (book + series branches)"
```

---

### Task 4: Implement `removeFromLibrary` (3-way ask-each-time dialog)

**Files:**
- Modify: `src/ui/pages/BooksPage.cpp` (replace the `removeFromLibrary` stub)

- [ ] **Step 1: Replace the stub with the dialog + eviction**

```cpp
void BooksPage::removeFromLibrary(const QStringList& catalogueIds,
                                  const QString& subjectLabel)
{
    if (!m_catalogueStore || catalogueIds.isEmpty()) return;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Remove from library"));
    box.setIcon(QMessageBox::Question);
    box.setText(catalogueIds.size() > 1
        ? QStringLiteral("Remove \"%1\" (%2 books) from your library?")
              .arg(subjectLabel).arg(catalogueIds.size())
        : QStringLiteral("Remove \"%1\" from your library?").arg(subjectLabel));
    box.setInformativeText(QStringLiteral(
        "\"Remove from library only\" keeps the downloaded file(s) on disk."));
    auto* recordOnlyBtn = box.addButton(QStringLiteral("Remove from library only"),
                                        QMessageBox::AcceptRole);
    auto* deleteFileBtn = box.addButton(
        catalogueIds.size() > 1 ? QStringLiteral("Delete the files too")
                                : QStringLiteral("Delete the file too"),
        QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(recordOnlyBtn);
    box.exec();

    auto* clicked = box.clickedButton();
    if (clicked != recordOnlyBtn && clicked != deleteFileBtn) return;  // cancel / closed
    const bool deleteFiles = (clicked == deleteFileBtn);

    for (const QString& id : catalogueIds) {
        if (deleteFiles) {
            const auto rec = m_catalogueStore->recordFor(id);
            if (rec && !rec->filePath.isEmpty())
                QFile::remove(rec->filePath);
        }
        m_catalogueStore->evictByCatalogueId(id);  // emits recordsChanged → rebuildBookGrid
    }
    refreshContinueStrip();  // a removed in-progress book also leaves Continue Reading
}
```

- [ ] **Step 2: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/BooksPage.cpp
git commit -m "[Agent 2, BOOKS_LIBRARY_CONTEXT_MENU]: implement 3-way remove dialog (library-only / delete-file / cancel)"
```

---

### Task 5: Build + live smoke

**Files:** none (verification only)

- [ ] **Step 1: Kill any running app (Rule 1), then full build + launch**

Run: `taskkill //F //IM Tankoban.exe` then `build_and_run.bat`
Expected: `BUILD OK`; Tankoban launches.

- [ ] **Step 2: Switch to Books mode for smoke**

Run: `out\tankoctl.exe open-page books`
Expected: `{"activePageId":"books",...}`.

- [ ] **Step 3: Smoke each action (eyes-on — dev bridge can't see the menu paint)**

Owned book tile (right-click):
- Read → opens reader.
- Mark as read → tile's book leaves Continue Reading; reopening the menu shows "Mark as unread".
- Rename… → file renamed on disk AND tile still opens correctly afterward (record updated).
- Remove from library → dialog appears; "Remove from library only" drops the tile, file still on disk; "Delete the file too" drops tile + deletes file; "Cancel" no-ops.
- Reveal in File Explorer → Explorer opens with the file selected.
- Copy path → clipboard has the file path.

Series tile (right-click):
- Open series → loads the series page.
- Remove series from library → dialog worded for N books; applies the chosen semantics to all owned books of the series.

- [ ] **Step 4: Post READY TO COMMIT for Agent 0 (shared checkout, Path A)**

Add a contracts-v3 RTC line to `agents/chat.md` listing the touched files (`BooksPage.h`, `BooksPage.cpp`) + the spec/plan, `Skills invoked: superpowers:brainstorming, superpowers:writing-plans, superpowers:executing-plans, /build-verify`.

---

## Self-review

- **Spec coverage:** D1 (grid-only scope) → Task 2 (only `m_bookStrip` wired). D2 (full owned-book set) → Task 3 owned-book branch. D3 (3-way remove) → Task 4. D4 (series Open + Remove) → Task 3 series branch + Task 4. D5 (reuse helpers) → all tasks use `ContextMenuHelper` + store + bridge, no new files. §3 rename-updates-record → Task 3 `upsertRecord`. §5 edge cases (missing file, rename failure, single-book series) → Task 3 `fileOk` gating + warning, Task 4 count-aware wording. All covered.
- **Placeholder scan:** none — every step has concrete code/commands.
- **Type consistency:** `showBookContextMenu(const QPoint&)` and `removeFromLibrary(const QStringList&, const QString&)` match between header (Task 1) and definitions (Tasks 3-4). `catalogueIdsForSeries` returns `QList<QString>` assignable to `QStringList` (QStringList is QList<QString>). `recordFor` returns `std::optional<CatalogueRecord>` — used via `*rec` / `rec->`. `progress`/`saveProgress` signatures match CoreBridge. Consistent.
