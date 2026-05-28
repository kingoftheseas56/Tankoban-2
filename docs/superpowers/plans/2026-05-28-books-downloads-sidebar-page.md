# Books Downloads Sidebar Page + TankoLibrary Sidebar Removal — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the TankoLibrary sidebar entry and add a Books-mode-only Downloads sidebar entry that opens a `BooksDownloadsPage` with two sections — Downloading (in-progress, live %) and Downloaded (completed, grouped by series).

**Architecture:** Mirror the existing `ComicsDownloadsPage` / `StreamDownloadsPage` sidebar-page pattern. `BooksPage` exposes its in-flight downloads (a small POD getter + a `downloadsChanged()` signal) and its catalogue store (accessor). `BooksDownloadsPage` (new `QFrame`) renders the two sections from those two sources. `SidebarDrawer` drops the TankoLibrary button and gains a Books Downloads button (default-hidden, shown only in Books mode). `MainWindow` constructs + wires the page exactly like the comics block at `MainWindow.cpp:921`.

**Tech Stack:** C++20, Qt6 Widgets (`QFrame`, `QScrollArea`, `QVBoxLayout`, `QLabel`, `QPushButton`, `QPixmap`). No new third-party deps. Spec: `docs/superpowers/specs/2026-05-28-books-downloads-sidebar-page-design.md`.

**Verification:** `build_check.bat` BUILD OK after wiring tasks; live smoke via `build_and_run.bat`. UI surface — no pure-logic GoogleTest seam (consistent with `ComicsDownloadsPage`, which has none).

**Flat-on-master:** no worktree (shared checkout, Path A — flag READY TO COMMIT for Agent 0 at the end).

---

## Ground truth (verified signatures, 2026-05-28)

- `BooksPage` (`src/ui/pages/BooksPage.{h,cpp}`): owns `m_catalogueStore` (`BooksCatalogueLibraryStore*`), `m_activeDownloads` (`QHash<QString /*handle*/, ActiveCatalogueDownload>` — struct `{QString sourceId; BookCatalogueResult book; QString coverPath; QString format; QString filePath;}` at `BooksPage.h:165`). Download lifecycle slots: `onCatalogueDownloadRequested` (inserts into `m_activeDownloads` at `BooksPage.cpp:1818`), `onBookDownloadProgress(handle,recv,total)` (`:1824`), `onBookDownloadComplete(handle,filePath)` (erases at `:1848`), `onBookDownloadFailed(handle,reason)` (removes at `:1870`). Signal `void openBook(const QString&)`.
- `BooksCatalogueLibraryStore`: `QList<CatalogueRecord> all() const`, `QList<QString> catalogueIdsForSeries(QString) const`, signal `recordsChanged()`. `CatalogueRecord` has `catalogueId, title, author, cachedCoverPath, seriesId, seriesName` + `int seriesTotal`.
- `SidebarDrawer` (`src/ui/widgets/SidebarDrawer.{h,cpp}`): members `m_btnTankorent/m_btnTankoLibrary/m_btnStreamDownloads/m_btnComicsDownloads` (`SidebarDrawer.h:58-61`). `buildUi()` makes items via a `makeItem(label,iconPath,pageId)` lambda + `layout->addWidget` (`.cpp:212-222`). `setActiveSource(pageId)` calls `styleItem(btn,pageId)` per button (`.cpp:323-330`). `setComicsDownloadsVisible(bool)` toggles `m_btnComicsDownloads` (`.cpp:338-342`). `setStreamDownloadsVisible` mirror. Signal `sourceClicked(QString pageId)`. pageId doc comment at `.h:27`.
- `MainWindow` (`src/ui/MainWindow.cpp`): `PAGE_BOOKS="books"` (`:61`), `PAGE_COMICS_DOWNLOADS="comicsDownloads"` (`:70`). `booksPage` local built at `:732` (`auto *booksPage = new BooksPage(m_bridge);`). `comicsPage` at `:702`. Comics-downloads construction template at `:921-927`. sourceClicked→`activatePage(pageId)` at `:158-162`. Visibility toggle block at `:1034-1040`. Members `m_streamDownloadsPage`/`m_comicsDownloadsPage` at `MainWindow.h:253-254`.
- `activatePage(pageId)` selects the stacked widget whose `objectName()==pageId` (every page sets its objectName; `ComicsDownloadsPage` reaches the stack purely via objectName + `sourceClicked`).

---

## File Structure

- **Create** `src/ui/pages/books/BooksDownloadsPage.{h,cpp}` — the two-section Downloads page (one responsibility: render Books downloads). Registered in `CMakeLists.txt`.
- **Modify** `src/ui/pages/BooksPage.{h,cpp}` — expose `activeDownloads()` + `downloadsChanged()` + `catalogueStore()`; track per-handle percent.
- **Modify** `src/ui/widgets/SidebarDrawer.{h,cpp}` — drop TankoLibrary button; add Books Downloads button + `setBooksDownloadsVisible`.
- **Modify** `src/ui/MainWindow.{cpp,h}` — construct/wire the page; `PAGE_BOOKS_DOWNLOADS`; visibility toggle.

---

### Task 1: BooksPage exposes active downloads + catalogue store

**Files:**
- Modify: `src/ui/pages/BooksPage.h`
- Modify: `src/ui/pages/BooksPage.cpp`

- [ ] **Step 1: Add the POD, getters, and signal to the header**

In `BooksPage.h`, add to the `public:` section (near the other public API, before `signals:`):

```cpp
    // BOOKS_DOWNLOADS_SIDEBAR_PAGE (2026-05-28) — projection of an in-flight
    // download for the Books Downloads page.
    struct ActiveDownloadInfo {
        QString title;
        QString author;
        QString coverPath;
        int     percent = 0;
    };
    QList<ActiveDownloadInfo> activeDownloads() const;
    BooksCatalogueLibraryStore* catalogueStore() const { return m_catalogueStore; }
```

In the existing `signals:` block (where `openBook` is declared), add:

```cpp
    // Fired on download start / progress / complete / fail so the Books
    // Downloads page can refresh its in-progress section.
    void downloadsChanged();
```

- [ ] **Step 2: Add a `percent` field to the ActiveCatalogueDownload struct**

In `BooksPage.h`, modify the struct at `:165`:

```cpp
    struct ActiveCatalogueDownload {
        QString sourceId;
        BookCatalogueResult book;
        QString coverPath;
        QString format;
        QString filePath;  // set on completion
        int     percent = 0;  // latest progress %, updated in onBookDownloadProgress
    };
```

- [ ] **Step 3: Implement `activeDownloads()` in the cpp**

Add near `buildRecordFromContext` (e.g. after it, around `BooksPage.cpp:1748`):

```cpp
QList<BooksPage::ActiveDownloadInfo> BooksPage::activeDownloads() const
{
    QList<ActiveDownloadInfo> out;
    out.reserve(m_activeDownloads.size());
    for (auto it = m_activeDownloads.constBegin(); it != m_activeDownloads.constEnd(); ++it) {
        const ActiveCatalogueDownload& ctx = it.value();
        ActiveDownloadInfo info;
        info.title     = ctx.book.title;
        info.author    = ctx.book.author;
        info.coverPath = ctx.coverPath;
        info.percent   = ctx.percent;
        out.append(info);
    }
    return out;
}
```

- [ ] **Step 4: Emit `downloadsChanged()` at the four lifecycle points + track percent**

In `onCatalogueDownloadRequested`, immediately after `m_activeDownloads.insert(handle, ctx);` (`BooksPage.cpp:1818`):

```cpp
    m_activeDownloads.insert(handle, ctx);
    emit downloadsChanged();
```

Replace `onBookDownloadProgress` (`:1824-1833`) with a version that stores the percent and notifies:

```cpp
void BooksPage::onBookDownloadProgress(const QString& handle,
                                       qint64 bytesReceived,
                                       qint64 bytesTotal)
{
    int pct = 0;
    if (bytesTotal > 0)
        pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
    if (auto it = m_activeDownloads.find(handle); it != m_activeDownloads.end()) {
        it.value().percent = pct;
        emit downloadsChanged();
    }
    if (m_catalogueDetailView)
        m_catalogueDetailView->notifyDownloadProgress(handle, pct);
}
```

In `onBookDownloadComplete`, after `m_activeDownloads.erase(it);` (`:1848`):

```cpp
    m_activeDownloads.erase(it);
    emit downloadsChanged();
```

In `onBookDownloadFailed`, after `m_activeDownloads.remove(handle);` (`:1870`):

```cpp
    m_activeDownloads.remove(handle);
    emit downloadsChanged();
```

- [ ] **Step 5: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/BooksPage.h src/ui/pages/BooksPage.cpp
git commit -m "[Agent 2, BOOKS_DOWNLOADS_SIDEBAR_PAGE]: BooksPage exposes activeDownloads + downloadsChanged + catalogueStore"
```

---

### Task 2: Create the BooksDownloadsPage widget

**Files:**
- Create: `src/ui/pages/books/BooksDownloadsPage.h`
- Create: `src/ui/pages/books/BooksDownloadsPage.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/ui/pages/books/BooksDownloadsPage.h`:

```cpp
#pragma once

// BOOKS_DOWNLOADS_SIDEBAR_PAGE 2026-05-28 (Agent 2). Books-mode Downloads page
// reached from SidebarDrawer's "Downloads" entry (Books-only). Two sections:
//   - Downloading: in-progress transfers (live %), from BooksPage::activeDownloads().
//   - Downloaded:  completed books grouped by series, from BooksCatalogueLibraryStore.
// Read-only display in v1 (mirrors ComicsDownloadsPage).

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class BooksCatalogueLibraryStore;
class BooksPage;

class BooksDownloadsPage : public QFrame
{
    Q_OBJECT
public:
    explicit BooksDownloadsPage(QWidget* parent = nullptr);
    ~BooksDownloadsPage() override = default;

    void setCatalogueStore(BooksCatalogueLibraryStore* store);
    void setBooksPage(BooksPage* page);

signals:
    void backRequested();

private slots:
    void refresh();

private:
    void buildUi();
    void populateDownloading();
    void populateDownloaded();
    QWidget* makeRow(const QString& coverPath, const QString& title,
                     const QString& subtitle);

    BooksCatalogueLibraryStore* m_store = nullptr;
    BooksPage* m_booksPage = nullptr;

    QPushButton* m_backBtn = nullptr;
    QScrollArea* m_scroll = nullptr;

    QLabel*      m_downloadingHeader = nullptr;
    QWidget*     m_downloadingBody = nullptr;
    QVBoxLayout* m_downloadingLayout = nullptr;

    QLabel*      m_downloadedHeader = nullptr;
    QWidget*     m_downloadedBody = nullptr;
    QVBoxLayout* m_downloadedLayout = nullptr;

    QLabel*      m_emptyState = nullptr;
};
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/pages/books/BooksDownloadsPage.cpp`:

```cpp
#include "BooksDownloadsPage.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"
#include "ui/pages/BooksPage.h"

BooksDownloadsPage::BooksDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    buildUi();
}

void BooksDownloadsPage::setCatalogueStore(BooksCatalogueLibraryStore* store)
{
    if (m_store == store) return;
    m_store = store;
    if (m_store)
        connect(m_store, &BooksCatalogueLibraryStore::recordsChanged,
                this, &BooksDownloadsPage::refresh);
    refresh();
}

void BooksDownloadsPage::setBooksPage(BooksPage* page)
{
    if (m_booksPage == page) return;
    m_booksPage = page;
    if (m_booksPage)
        connect(m_booksPage, &BooksPage::downloadsChanged,
                this, &BooksDownloadsPage::refresh);
    refresh();
}

void BooksDownloadsPage::buildUi()
{
    setObjectName(QStringLiteral("BooksDownloadsPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top bar — back.
    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    m_backBtn = new QPushButton(QStringLiteral("< Books"), topRow);
    m_backBtn->setObjectName(QStringLiteral("BooksDownloadsBackButton"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(QStringLiteral(
        "QPushButton#BooksDownloadsBackButton { background: transparent; border: none;"
        " color: rgba(255,255,255,0.7); font-size: 13px; padding: 0 8px; }"
        "QPushButton#BooksDownloadsBackButton:hover { color: #ffffff; }"));
    connect(m_backBtn, &QPushButton::clicked, this, &BooksDownloadsPage::backRequested);
    topLayout->addWidget(m_backBtn);
    topLayout->addStretch(1);
    root->addWidget(topRow);

    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(24, 8, 24, 24);
    col->setSpacing(10);

    const QString headerCss = QStringLiteral(
        "color: rgba(255,255,255,0.55); font-size: 12px; font-weight: 700;"
        " letter-spacing: 1px; padding-top: 8px;");

    m_downloadingHeader = new QLabel(QStringLiteral("DOWNLOADING"), content);
    m_downloadingHeader->setStyleSheet(headerCss);
    col->addWidget(m_downloadingHeader);
    m_downloadingBody = new QWidget(content);
    m_downloadingLayout = new QVBoxLayout(m_downloadingBody);
    m_downloadingLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadingLayout->setSpacing(6);
    col->addWidget(m_downloadingBody);

    m_downloadedHeader = new QLabel(QStringLiteral("DOWNLOADED"), content);
    m_downloadedHeader->setStyleSheet(headerCss);
    col->addWidget(m_downloadedHeader);
    m_downloadedBody = new QWidget(content);
    m_downloadedLayout = new QVBoxLayout(m_downloadedBody);
    m_downloadedLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadedLayout->setSpacing(6);
    col->addWidget(m_downloadedBody);

    m_emptyState = new QLabel(QStringLiteral("No downloads yet."), content);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(QStringLiteral(
        "color: rgba(255,255,255,0.4); font-size: 14px; padding: 40px 0;"));
    col->addWidget(m_emptyState);

    col->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

QWidget* BooksDownloadsPage::makeRow(const QString& coverPath, const QString& title,
                                     const QString& subtitle)
{
    auto* row = new QWidget(m_downloadingBody);
    row->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.04); border-radius: 6px;"));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(12);

    auto* thumb = new QLabel(row);
    thumb->setFixedSize(40, 60);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setStyleSheet(QStringLiteral(
        "background: rgba(255,255,255,0.06); border-radius: 4px;"));
    if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
        QPixmap pm(coverPath);
        if (!pm.isNull())
            thumb->setPixmap(pm.scaled(thumb->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    }
    h->addWidget(thumb, 0, Qt::AlignTop);

    auto* textCol = new QWidget(row);
    auto* tv = new QVBoxLayout(textCol);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(2);
    auto* nameLabel = new QLabel(title, textCol);
    nameLabel->setWordWrap(true);
    nameLabel->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 14px; font-weight: 600; background: transparent;"));
    tv->addWidget(nameLabel);
    if (!subtitle.isEmpty()) {
        auto* subLabel = new QLabel(subtitle, textCol);
        subLabel->setStyleSheet(QStringLiteral(
            "color: rgba(255,255,255,0.55); font-size: 12px; background: transparent;"));
        tv->addWidget(subLabel);
    }
    tv->addStretch(1);
    h->addWidget(textCol, 1);
    return row;
}

void BooksDownloadsPage::populateDownloading()
{
    while (QLayoutItem* item = m_downloadingLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    int count = 0;
    if (m_booksPage) {
        const auto active = m_booksPage->activeDownloads();
        for (const auto& d : active) {
            QString sub = d.author;
            const QString pctText = QStringLiteral("%1%").arg(d.percent);
            sub = sub.isEmpty() ? pctText : (sub + QStringLiteral("  ·  ") + pctText);
            m_downloadingLayout->addWidget(makeRow(d.coverPath, d.title, sub));
            ++count;
        }
    }
    const bool any = count > 0;
    m_downloadingHeader->setVisible(any);
    m_downloadingBody->setVisible(any);
}

void BooksDownloadsPage::populateDownloaded()
{
    while (QLayoutItem* item = m_downloadedLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    int count = 0;
    if (m_store) {
        QSet<QString> seriesSeen;
        for (const CatalogueRecord& r : m_store->all()) {
            if (!r.seriesId.isEmpty()) {
                if (seriesSeen.contains(r.seriesId)) continue;
                seriesSeen.insert(r.seriesId);
                const int owned = m_store->catalogueIdsForSeries(r.seriesId).size();
                const QString title = r.seriesName.isEmpty() ? r.title : r.seriesName;
                const QString sub = QStringLiteral("%1 book%2").arg(owned)
                                        .arg(owned == 1 ? QString() : QStringLiteral("s"));
                const QString cover = QFile::exists(r.cachedCoverPath)
                    ? r.cachedCoverPath : QString();
                m_downloadedLayout->addWidget(makeRow(cover, title, sub));
            } else {
                const QString cover = QFile::exists(r.cachedCoverPath)
                    ? r.cachedCoverPath : QString();
                m_downloadedLayout->addWidget(makeRow(cover, r.title, r.author));
            }
            ++count;
        }
    }
    const bool any = count > 0;
    m_downloadedHeader->setVisible(any);
    m_downloadedBody->setVisible(any);
}

void BooksDownloadsPage::refresh()
{
    populateDownloading();
    populateDownloaded();
    const bool downloadingShown = m_downloadingHeader && m_downloadingHeader->isVisible();
    const bool downloadedShown = m_downloadedHeader && m_downloadedHeader->isVisible();
    if (m_emptyState) m_emptyState->setVisible(!downloadingShown && !downloadedShown);
}
```

- [ ] **Step 3: Register in CMakeLists.txt**

Find where `src/ui/pages/books/BookSeriesDetailView.cpp` is listed in the executable `SOURCES` and the explicit `HEADERS` list (added during the FictionDB arc). Add the sibling lines:

```cmake
    src/ui/pages/books/BooksDownloadsPage.cpp
```
and in the HEADERS list:
```cmake
    src/ui/pages/books/BooksDownloadsPage.h
```

- [ ] **Step 4: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pages/books/BooksDownloadsPage.h src/ui/pages/books/BooksDownloadsPage.cpp CMakeLists.txt
git commit -m "[Agent 2, BOOKS_DOWNLOADS_SIDEBAR_PAGE]: add BooksDownloadsPage (Downloading + Downloaded sections)"
```

---

### Task 3: SidebarDrawer — drop TankoLibrary, add Books Downloads

**Files:**
- Modify: `src/ui/widgets/SidebarDrawer.h`
- Modify: `src/ui/widgets/SidebarDrawer.cpp`

- [ ] **Step 1: Header — swap the member + update the API + doc comment**

In `SidebarDrawer.h`: update the pageId doc comment at `:27`:

```cpp
    // pageId in {"tankorent","streamDownloads","comicsDownloads","booksDownloads"}; empty string clears.
```

Add the Books Downloads API after `setComicsDownloadsVisible` (`:36`):

```cpp
    // Show/hide the Books Downloads sidebar entry. Only visible in Books mode.
    void setBooksDownloadsVisible(bool visible);
```

Replace the `m_btnTankoLibrary` member (`:59`) with the Books Downloads button:

```cpp
    QPushButton* m_btnBooksDownloads = nullptr;
```
(Delete the `m_btnTankoLibrary` line entirely.)

- [ ] **Step 2: buildUi — remove TankoLibrary item, add Books Downloads item**

In `SidebarDrawer.cpp` `buildUi`, delete the TankoLibrary line (`:213`) and its `layout->addWidget(m_btnTankoLibrary);` (`:220`). After the comics-downloads item (`:217`), add:

```cpp
    m_btnBooksDownloads   = makeItem(tr("Downloads"),    QStringLiteral(":/icons/download.svg"), QStringLiteral("booksDownloads"));
    m_btnBooksDownloads->setVisible(false);  // Hidden by default; shown only in Books mode
```

Update the add-widget block (`:219-222`) to:

```cpp
    layout->addWidget(m_btnTankorent);
    layout->addWidget(m_btnStreamDownloads);
    layout->addWidget(m_btnComicsDownloads);
    layout->addWidget(m_btnBooksDownloads);
```

- [ ] **Step 3: setActiveSource + the new visibility setter**

In `setActiveSource` (`:323-330`), remove the `styleItem(m_btnTankoLibrary, ...)` line and add:

```cpp
    styleItem(m_btnBooksDownloads,   QStringLiteral("booksDownloads"));
```

After `setComicsDownloadsVisible` (`:338-342`), add:

```cpp
void SidebarDrawer::setBooksDownloadsVisible(bool visible)
{
    if (m_btnBooksDownloads)
        m_btnBooksDownloads->setVisible(visible);
}
```

- [ ] **Step 4: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```bash
git add src/ui/widgets/SidebarDrawer.h src/ui/widgets/SidebarDrawer.cpp
git commit -m "[Agent 2, BOOKS_DOWNLOADS_SIDEBAR_PAGE]: sidebar drops TankoLibrary entry, gains Books Downloads entry"
```

---

### Task 4: MainWindow — construct, wire, mode-scope the page

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Header — member + include**

In `MainWindow.h`, after `ComicsDownloadsPage *m_comicsDownloadsPage = nullptr;` (`:254`):

```cpp
    class BooksDownloadsPage *m_booksDownloadsPage = nullptr;
```
(Inline forward-class form avoids a header include in MainWindow.h.)

In `MainWindow.cpp`, add the include near the other page includes (`:10-13`):

```cpp
#include "pages/books/BooksDownloadsPage.h"
```

- [ ] **Step 2: Add the PAGE constant**

In `MainWindow.cpp` near `PAGE_COMICS_DOWNLOADS` (`:70`):

```cpp
static constexpr const char *PAGE_BOOKS_DOWNLOADS = "booksDownloads";
```

- [ ] **Step 3: Construct + wire the page (mirror the comics block at :921)**

In `MainWindow.cpp`, after the comics-downloads `connect(... backRequested ...)` block ends (`:929`), add:

```cpp
    // BOOKS_DOWNLOADS_SIDEBAR_PAGE 2026-05-28 (Agent 2) — Books-mode Downloads
    // page from SidebarDrawer's "Downloads" entry (Books-only). Downloading
    // section from BooksPage::activeDownloads(); Downloaded from the catalogue store.
    m_booksDownloadsPage = new BooksDownloadsPage(this);
    m_booksDownloadsPage->setObjectName(PAGE_BOOKS_DOWNLOADS);
    m_booksDownloadsPage->setBooksPage(booksPage);
    m_booksDownloadsPage->setCatalogueStore(booksPage->catalogueStore());
    m_pageStack->addWidget(m_booksDownloadsPage);
    connect(m_booksDownloadsPage, &BooksDownloadsPage::backRequested, this, [this]() {
        activatePage(PAGE_BOOKS);
    });
```

- [ ] **Step 4: Mode-scope the sidebar entry visibility**

In the `activatePage` sidebar block (`:1034-1040`), after the `setComicsDownloadsVisible(...)` line, add:

```cpp
                m_sidebar->setBooksDownloadsVisible(
                    pageId == PAGE_BOOKS || pageId == PAGE_BOOKS_DOWNLOADS);
```

- [ ] **Step 5: Verify it compiles**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "[Agent 2, BOOKS_DOWNLOADS_SIDEBAR_PAGE]: wire BooksDownloadsPage into MainWindow + Books-mode visibility"
```

---

### Task 5: Build + live smoke + RTC

**Files:** none (verification only)

- [ ] **Step 1: Kill the running app, full build + launch**

Run: `taskkill //F //IM Tankoban.exe` then `build_and_run.bat`
Expected: `BUILD OK`; app launches.

- [ ] **Step 2: Smoke (eyes-on)**

Run: `out\tankoctl.exe open-page books`, then in the app:
- Open the sidebar in Books mode → **TankoLibrary entry is gone**, **Downloads entry present**.
- Switch to Comics/Theatre → the Books Downloads entry is hidden (each mode shows only its own).
- Click **Downloads** in Books → page opens; **Downloaded** section lists owned books grouped by series.
- Start a book download (search → Get → a LibGen row) → it appears under **Downloading** with a live %, then moves to **Downloaded** on completion.
- Back button → returns to the Books library.

- [ ] **Step 3: Post READY TO COMMIT for Agent 0 (shared checkout, Path A)**

Add a contracts-v3 RTC line to `agents/chat.md` listing the files + `Skills invoked: superpowers:brainstorming, superpowers:writing-plans, superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion, /hemanth-language`.

---

## Self-review

- **Spec coverage:** D1 (two sections) → Task 2 `populateDownloading`/`populateDownloaded`. D2 (TankoLibrary sidebar-only) → Task 3 drops the button only; MainWindow leaves `TankoLibraryPage` + dev-bridge untouched (Task 4 adds nothing that removes them). D3 (mirror layout) → Task 2 mirrors ComicsDownloadsPage shape. D4 (Books-only visibility) → Task 4 Step 4. §4 BooksPage getter/signal/accessor → Task 1. §4 `catalogueStore()` accessor → Task 1 Step 1. §5 edge cases: empty-state → Task 2 `refresh`; download completes while open → `downloadsChanged` + `recordsChanged` both wired (Task 1 + Task 2 setters); missing cover → `makeRow` placeholder. All covered.
- **Placeholder scan:** none — every step has concrete code/commands.
- **Type consistency:** `ActiveDownloadInfo {title,author,coverPath,percent}` defined in Task 1, consumed in Task 2 `populateDownloading`. `activeDownloads()` / `downloadsChanged()` / `catalogueStore()` signatures match between Task 1 (BooksPage) and Task 2/Task 4 (consumers). `setBooksDownloadsVisible(bool)` matches between Task 3 (SidebarDrawer) and Task 4 (MainWindow call). `setBooksPage`/`setCatalogueStore` match between Task 2 (page) and Task 4 (wiring). `catalogueIdsForSeries` returns `QList<QString>` (`.size()` used). Consistent.
