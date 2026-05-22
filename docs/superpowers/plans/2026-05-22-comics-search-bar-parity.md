# Comics Search Bar — Stream/Theatre Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the Comics-mode search bar up to Stream/Theatre-mode parity by porting the magnifying-glass search-button, in-flight busy spinner, and persistent search-history dropdown — without inventing new Comics UI patterns and without porting features that have no Comics analogue (Addons, Catalog, ⚙ Clear Library).

**Architecture:** Mirror Stream's pattern verbatim inside `ComicsPage.{cpp,h}` — no new files, no helper classes. Stream keeps all search-bar state inline on its page class and so will Comics. Persistent history lives under a separate `QSettings` key (`comics/searchHistory`) so the two modes' history doesn't cross-pollinate. Busy-spinner state is driven off the `MangaScraper::searchFinished` signal hooked at the `ComicsPage` layer (mirroring Stream's `MetaAggregator::catalogResults` hook), keeping the in-flight UI cue at the page boundary instead of leaking into `ComicsTankoyomiSearchWidget`.

**Tech Stack:** Qt6 / C++20 / `QLineEdit` / `QPushButton` / `QFrame` / `QProgressBar` / `QSettings` / `QTimer`. No new third-party dependencies. No new CMake entries.

**Owner:** Agent 5 (Library UX + Theme) under cross-domain Rule 14 wave-through from Agent 1 (Comics domain owner) and explicit Hemanth ratification.

**Verification standard:** Smoke-first per `feedback_plan_first_zero_errors.md` + project test policy (TDD opt-in only for `tankoban_tests` pure-logic primitives). Each task ends with `build_check.bat` GREEN; the final smoke task runs the live app via `build_and_run.bat` + `tankoctl` UI synthetic-event matrix.

---

## Source-of-truth reference

The Stream implementation this plan ports from:

- `src/ui/pages/StreamPage.h:189-192` — `setSearchBusy` signature + comment
- `src/ui/pages/StreamPage.h:200-208` — `buildSearchHistoryDropdown` + `loadSearchHistory` + `showSearchHistoryDropdown` + `hideSearchHistoryDropdown` + `positionSearchHistoryDropdown` declarations
- `src/ui/pages/StreamPage.h:409` — `m_searchBusy` member (declared `QWidget*` to avoid `<QProgressBar>` include in the header)
- `src/ui/pages/StreamPage.h:417-421` — `m_searchHistoryDropdown` / `m_searchHistoryList` / `m_searchHistoryHideTimer` / `m_searchHistory` / `kMaxSearchHistory`
- `src/ui/pages/StreamPage.cpp:1230-1242` — busy `QProgressBar` construction
- `src/ui/pages/StreamPage.cpp:1244-1255` — magnifying-glass search button construction
- `src/ui/pages/StreamPage.cpp:1346-1350` — `loadSearchHistory()` + `buildSearchHistoryDropdown()` + event-filter install at end of `buildSearchBar`
- `src/ui/pages/StreamPage.cpp:1485` — `setSearchBusy(true)` at submit
- `src/ui/pages/StreamPage.cpp:1496-1524` — `onSearchTextChanged` (empty→busy=false+show-history-if-focused; non-empty→hide-history)
- `src/ui/pages/StreamPage.cpp:1526-1530` — `setSearchBusy` body
- `src/ui/pages/StreamPage.cpp:1682-1725` — `loadSearchHistory` / `saveSearchHistory` / `pushSearchHistory` / `removeSearchHistoryEntry` / `clearSearchHistory` bodies
- `src/ui/pages/StreamPage.cpp:1728-1755` — `buildSearchHistoryDropdown` body
- `src/ui/pages/StreamPage.cpp:1757-1768` — `positionSearchHistoryDropdown` body
- `src/ui/pages/StreamPage.cpp:1770-1865` — `showSearchHistoryDropdown` body (rebuilds row list per show; per-entry × + footer "× Clear search history")
- `src/ui/pages/StreamPage.cpp:1867-1871` — `hideSearchHistoryDropdown` body
- `src/ui/pages/StreamPage.cpp:1873-1889` — `eventFilter` body for `m_searchInput` focus
- `src/ui/pages/StreamPage.cpp:305-312` — `m_metaAggregator` → `setSearchBusy(false)` connect pattern (Comics analogue: `MangaScraper::searchFinished` on the weebcentral scraper)

The Comics integration points:

- `src/ui/pages/ComicsPage.h:363` — `m_searchBar` (existing)
- `src/ui/pages/ComicsPage.h:395` — `m_sourceRegistry` (existing, non-owning)
- `src/ui/pages/ComicsPage.cpp:751-766` — current single-element `searchLayout` HBox holding `m_searchBar` alone
- `src/ui/pages/ComicsPage.cpp:776-788` — current `m_searchTimer` (vestigial 250ms timer no longer drives anything) + `returnPressed` submit lambda
- `src/ui/pages/ComicsPage.cpp:791-814` — Ctrl+F / Esc / F5 shortcuts (PRESERVE all three)
- `src/ui/pages/ComicsPage.cpp:2008-2022` — `showSearchMode(query)` — this is where `pushSearchHistory(q)` goes

The Comics `ComicsTankoyomiSearchWidget` already gets its results via `m_sourceRegistry->find("weebcentral")` → `MangaScraper::searchFinished` (see `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:38-43`), so `ComicsPage` can hook the same scraper signal at construction time without touching the widget.

---

## File structure

**Modified (only these two files):**
- `src/ui/pages/ComicsPage.h` — add ~10 lines of new state + ~9 method declarations
- `src/ui/pages/ComicsPage.cpp` — add ~250 lines (helpers + builder + event filter); restructure ~10 lines of existing `searchLayout` HBox

**Not modified (out of scope per Agent 1's guardrails):**
- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}` — search widget stays as-is; busy state observed at the scraper layer
- `dispatchFandomResolve` and surrounding code in `ComicsPage.cpp` (line 3118+)
- `populateVolumeRowsFromFandom` wiring
- The hero block / `showSeries` paths (~870-960)
- `renderDetail` Phase 8a byline/tags/originalLanguage code
- BookWalker `VolumeCoverResolver` wiring
- `CMakeLists.txt` — no new headers, no edit needed

**Not created (no new files):**
- Stream's pattern keeps everything inline on `StreamPage`; mirror that for Comics. Extracting a `ComicsSearchHistory` helper class would be over-engineering for v1 — revisit if the pattern lands a third time elsewhere.

---

## Task 1: Header — declare new state and method signatures

**Files:**
- Modify: `src/ui/pages/ComicsPage.h:363` (m_searchBar declaration block — add new members nearby)
- Modify: `src/ui/pages/ComicsPage.h` (public/private method declarations — add near other search-related methods)

The header touch must be all of:
- new `m_searchBtn`, `m_searchBusy`, `m_searchHistoryDropdown`, `m_searchHistoryList`, `m_searchHistoryHideTimer`, `m_searchHistory` member fields
- `kMaxSearchHistory` constant
- method declarations: `loadSearchHistory`, `saveSearchHistory`, `pushSearchHistory`, `removeSearchHistoryEntry`, `clearSearchHistory`, `buildSearchHistoryDropdown`, `showSearchHistoryDropdown`, `hideSearchHistoryDropdown`, `positionSearchHistoryDropdown`, `setSearchBusy`
- `bool eventFilter(QObject*, QEvent*) override;` — Comics page does NOT currently override this (verified via grep on `src/ui/pages/ComicsPage.h`), so the declaration is fresh. If during execution the override is found to already exist, merge into the existing one instead of declaring again.

- [ ] **Step 1: Forward-declare QFrame / QPushButton / QTimer if not already present.**

Run: `grep -nE 'class QFrame|class QPushButton|class QTimer|#include <QFrame>|#include <QPushButton>|#include <QTimer>' src/ui/pages/ComicsPage.h`

Expected: confirms which forward-decls / includes already exist. Add the missing ones in the existing forward-decl block (near the top, after `#pragma once` + system includes). Stream's header already includes `<QLineEdit>` + `<QPushButton>` + `<QFrame>` indirectly; the Comics header may already pull them transitively. If a class is already known, do not re-add it.

- [ ] **Step 2: Add the new private members near `m_searchBar` (around line 363).**

Insert below the existing `QLineEdit* m_searchBar = nullptr;`:

```cpp
    QPushButton*     m_searchBtn      = nullptr;
    QWidget*         m_searchBusy     = nullptr;   // QProgressBar held as QWidget* to avoid <QProgressBar> in header
    QFrame*          m_searchHistoryDropdown = nullptr;
    QWidget*         m_searchHistoryList     = nullptr;
    QTimer*          m_searchHistoryHideTimer = nullptr;
    QStringList      m_searchHistory;
    static constexpr int kMaxSearchHistory = 10;
```

- [ ] **Step 3: Add method declarations in the private section near other search-related methods.**

Add (look for `void showSearchMode` or `void onSearchResultActivated` to find the right cluster):

```cpp
    void loadSearchHistory();
    void saveSearchHistory();
    void pushSearchHistory(const QString& query);
    void removeSearchHistoryEntry(const QString& query);
    void clearSearchHistory();
    void buildSearchHistoryDropdown();
    void showSearchHistoryDropdown();
    void hideSearchHistoryDropdown();
    void positionSearchHistoryDropdown();
    void setSearchBusy(bool busy);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
```

If a `protected:` section already exists, append the override there instead of opening a fresh one. If a `bool eventFilter(...)` is already declared (re-grep with `grep -n 'eventFilter' src/ui/pages/ComicsPage.h` before editing), DO NOT re-declare — append a branch to the existing override body in Task 6.

- [ ] **Step 4: Build-verify header-only change.**

Run: `build_check.bat`
Expected: `BUILD OK`. Header-only edits can still surface includes that aren't transitively pulled — if it fails on `QFrame`/`QPushButton`/`QTimer`/`QStringList`, add the missing forward declaration or include and re-run.

- [ ] **Step 5: Commit.**

```bash
git add src/ui/pages/ComicsPage.h
git commit -m "comics(search): declare search-bar parity members + method signatures

Header-only declarations for the Stream-bar port: history persistence
(m_searchHistory + kMaxSearchHistory cap of 10), dropdown widget state
(m_searchHistoryDropdown + m_searchHistoryList + m_searchHistoryHideTimer),
busy spinner placeholder (m_searchBusy as QWidget*), search button
(m_searchBtn), and the ten new method signatures. Adds bool eventFilter
override. No body changes — Tasks 2-7 land the implementations.

Cross-domain commission from Agent 1 to Agent 5; v1 scope per
Hemanth ratification (skip Addons/Catalog/Clear-Library)."
```

---

## Task 2: Port history persistence helpers

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` — add 5 helpers near other search helpers (search for `void ComicsPage::showSearchMode(` and add above it)

- [ ] **Step 1: Add `loadSearchHistory` + `saveSearchHistory` + `pushSearchHistory` + `removeSearchHistoryEntry` + `clearSearchHistory`.**

Insert this block in `src/ui/pages/ComicsPage.cpp` immediately above `void ComicsPage::showSearchMode(`:

```cpp
// ─── Search history (Stream-bar parity 2026-05-22) ───────────────────────
//
// QSettings key is `comics/searchHistory` — deliberately disjoint from
// Stream's `stream/searchHistory` so the two modes don't cross-pollinate
// past queries (Agent 1 guardrail at handoff).

void ComicsPage::loadSearchHistory()
{
    QSettings s;
    m_searchHistory = s.value(QStringLiteral("comics/searchHistory")).toStringList();
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
}

void ComicsPage::saveSearchHistory()
{
    QSettings s;
    s.setValue(QStringLiteral("comics/searchHistory"), m_searchHistory);
}

void ComicsPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;
    m_searchHistory.removeAll(q);
    m_searchHistory.prepend(q);
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void ComicsPage::removeSearchHistoryEntry(const QString& query)
{
    m_searchHistory.removeAll(query);
    saveSearchHistory();
    // Rebuild the open dropdown so the row disappears immediately.
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible()) {
        showSearchHistoryDropdown();
    }
}

void ComicsPage::clearSearchHistory()
{
    if (m_searchHistory.isEmpty()) return;
    m_searchHistory.clear();
    saveSearchHistory();
    hideSearchHistoryDropdown();
}
```

- [ ] **Step 2: Verify `QSettings` is includable from `ComicsPage.cpp`.**

Run: `grep -nE '#include <QSettings>|#include "QSettings"' src/ui/pages/ComicsPage.cpp`

Expected: `<QSettings>` present. If absent, add `#include <QSettings>` to the include block at the top.

- [ ] **Step 3: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. Failure modes to watch: missing `<QSettings>`, missing `<QStringList>` (likely transitive), `kMaxSearchHistory` undeclared (Task 1 must have landed first).

- [ ] **Step 4: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): port history persistence helpers from Stream

loadSearchHistory / saveSearchHistory / pushSearchHistory /
removeSearchHistoryEntry / clearSearchHistory mirror StreamPage's
implementations at StreamPage.cpp:1682-1725. QSettings key is
'comics/searchHistory' — disjoint from Stream's 'stream/searchHistory'
per Agent 1 guardrail (no cross-mode pollution of past queries).
Cap at 10 entries to match Stream's kMaxSearchHistory."
```

---

## Task 3: Port dropdown construction + positioning

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` — add 2 helpers immediately below the Task 2 block

- [ ] **Step 1: Add `buildSearchHistoryDropdown` + `positionSearchHistoryDropdown`.**

Insert below the `clearSearchHistory` body:

```cpp
void ComicsPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName("ComicsSearchHistory");
    m_searchHistoryDropdown->setStyleSheet(
        "QFrame#ComicsSearchHistory { background: #1a1a1a; border: 1px solid #3a3a3a;"
        "  border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(0, 4, 0, 4);
    outer->setSpacing(0);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    outer->addWidget(m_searchHistoryList);

    // Delayed-hide timer so a click on a dropdown row gets its release
    // event before the dropdown is dismissed (FocusOut → 150ms → hide).
    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void ComicsPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchBar) return;
    const QPoint topLeft =
        m_searchBar->mapTo(this, QPoint(0, m_searchBar->height() + 2));
    m_searchHistoryDropdown->setGeometry(
        topLeft.x(), topLeft.y(), m_searchBar->width(),
        m_searchHistoryDropdown->sizeHint().height());
}
```

- [ ] **Step 2: Verify `<QFrame>` + `<QVBoxLayout>` + `<QTimer>` are includable.**

Run: `grep -nE '#include <QFrame>|#include <QVBoxLayout>|#include <QTimer>|#include <QHBoxLayout>' src/ui/pages/ComicsPage.cpp`

Expected: all four likely present (this is a Qt widget host file). Add any missing.

- [ ] **Step 3: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. The two new methods are referenced from nowhere yet — that's fine, Qt MOC will not complain about unused non-slot methods.

- [ ] **Step 4: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): port history-dropdown construction + positioning

buildSearchHistoryDropdown mirrors StreamPage.cpp:1728-1755 — opaque
QFrame parented to ComicsPage (so it floats above the QStackedWidget),
hidden until showSearchHistoryDropdown rebuilds rows. ObjectName
'ComicsSearchHistory' avoids QSS rule collision with Stream's
'StreamSearchHistory'. 150ms delayed-hide timer covers the
FocusOut → row-click race.

positionSearchHistoryDropdown mirrors StreamPage.cpp:1757-1768 — maps
the search bar's local origin into ComicsPage coordinates and lays out
the dropdown flush-left, same width as the bar."
```

---

## Task 4: Port show / hide dropdown (with per-row × and footer "Clear search history")

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` — add 2 helpers below Task 3 block

- [ ] **Step 1: Add `showSearchHistoryDropdown` + `hideSearchHistoryDropdown`.**

Insert below `positionSearchHistoryDropdown`:

```cpp
void ComicsPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    // Clear old rows.
    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (m_searchHistory.isEmpty()) {
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(m_searchHistory.size(), kMaxSearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";
    const char* kClearAllBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.55);"
        "  border: none; text-align: left; padding: 6px 10px;"
        "  font-size: 11px; font-weight: 500; letter-spacing: 0.4px; }"
        "QPushButton:hover { color: #fff; }";

    for (int i = 0; i < rows; ++i) {
        const QString q = m_searchHistory.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* queryBtn = new QPushButton(q, row);
        queryBtn->setCursor(Qt::PointingHandCursor);
        queryBtn->setStyleSheet(kRowBtnStyle);
        queryBtn->setFocusPolicy(Qt::NoFocus);
        connect(queryBtn, &QPushButton::clicked, this, [this, q]() {
            if (m_searchBar) m_searchBar->setText(q);
            // Re-enter the same submit path used by Enter / search button.
            showSearchMode(q);
            hideSearchHistoryDropdown();
        });
        rowLayout->addWidget(queryBtn, 1);

        auto* removeBtn = new QPushButton(QStringLiteral("×"), row);   // ×
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet(kRemoveBtnStyle);
        removeBtn->setFocusPolicy(Qt::NoFocus);
        removeBtn->setToolTip(tr("Remove from history"));
        connect(removeBtn, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);
    }

    // Footer: "× Clear search history" wipes the entire list. Single visible
    // affordance per Stream's 2026-04-25 pattern.
    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        "QFrame { border: none; background: rgba(255,255,255,0.08);"
        "  max-height: 1px; min-height: 1px; }");
    layout->addWidget(divider);

    auto* clearAllBtn = new QPushButton(
        QStringLiteral("×  Clear search history"), m_searchHistoryList);
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setStyleSheet(kClearAllBtnStyle);
    clearAllBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearAllBtn, &QPushButton::clicked,
            this, &ComicsPage::clearSearchHistory);
    layout->addWidget(clearAllBtn);

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void ComicsPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}
```

- [ ] **Step 2: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. New methods are referenced from nowhere yet (Task 6 will wire them via the event filter).

- [ ] **Step 3: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): port show/hide dropdown with per-row × + footer

Mirrors StreamPage.cpp:1770-1865 / :1867-1871 verbatim, modulo class
naming. Rows rebuilt per show (cheap; capped at 10). Each row is a
QPushButton-with-row-layout (the query button fills + submits via
showSearchMode; the × button removes via removeSearchHistoryEntry).
Footer adds a single 'Clear search history' affordance, mirrored from
Hemanth's Stream-bar 2026-04-25 request."
```

---

## Task 5: Add icon search button + busy spinner; restructure search row HBox

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp:751-766` (existing `searchLayout` construction block)

- [ ] **Step 1: Replace the single-element search HBox with a three-element row.**

Find the existing block:

```cpp
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 12, 0, 0);
    searchLayout->addWidget(m_searchBar);
    gridLayout->addLayout(searchLayout);

    m_searchBar->setToolTip("Press Enter to search Tankoyomi sources");
```

Replace with:

```cpp
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 12, 0, 0);
    searchLayout->setSpacing(8);
    searchLayout->addWidget(m_searchBar, 1);

    // ── Busy spinner — indeterminate QProgressBar (range 0..0) animates
    // natively; 16x16 sits between input + search button. Mirrors
    // StreamPage.cpp:1230-1242.
    auto* busy = new QProgressBar();
    busy->setRange(0, 0);
    busy->setTextVisible(false);
    busy->setFixedSize(16, 16);
    busy->setObjectName("ComicsSearchBusy");
    busy->setStyleSheet(
        "#ComicsSearchBusy { background: transparent; border: none; }"
        "#ComicsSearchBusy::chunk { background: rgba(255,255,255,0.5); }");
    busy->hide();
    searchLayout->addWidget(busy);
    m_searchBusy = busy;

    // ── Magnifying-glass search button (icon-only, 36×36). Reuses the
    // SVG Agent 4 shipped this wake at :/icons/search.svg.
    m_searchBtn = new QPushButton();
    m_searchBtn->setFixedHeight(36);
    m_searchBtn->setFixedWidth(36);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setObjectName("ComicsSearchBtn");
    m_searchBtn->setIcon(QIcon(QStringLiteral(":/icons/search.svg")));
    m_searchBtn->setIconSize(QSize(18, 18));
    m_searchBtn->setToolTip(tr("Search"));
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        if (!m_searchBar) return;
        const QString q = m_searchBar->text().trimmed();
        if (q.isEmpty()) return;
        showSearchMode(q);
    });
    searchLayout->addWidget(m_searchBtn);

    gridLayout->addLayout(searchLayout);

    m_searchBar->setToolTip(tr("Press Enter or click the search icon to search Tankoyomi sources"));
```

- [ ] **Step 2: Add the `setSearchBusy` body — place near the other search helpers (above `loadSearchHistory`).**

```cpp
void ComicsPage::setSearchBusy(bool busy)
{
    if (!m_searchBusy) return;
    m_searchBusy->setVisible(busy);
}
```

- [ ] **Step 3: Add `#include <QProgressBar>` + `#include <QIcon>` if not already present.**

Run: `grep -nE '#include <QProgressBar>|#include <QIcon>' src/ui/pages/ComicsPage.cpp`

Expected: add whichever is missing.

- [ ] **Step 4: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. Watch for icon-load warnings (`:/icons/search.svg` is in `resources/resources.qrc` per obs 6660 — confirm by `grep -n 'search.svg' resources/resources.qrc`).

- [ ] **Step 5: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): add icon search button + busy spinner to bar row

The single-element searchLayout becomes a three-element row: m_searchBar
(stretches), 16x16 indeterminate busy QProgressBar (hidden until
setSearchBusy(true)), and a 36x36 magnifying-glass m_searchBtn (icon
only, reuses the :/icons/search.svg Agent 4 just shipped). Button-click
re-enters showSearchMode via the same Enter path. ObjectNames are
'ComicsSearchBusy' / 'ComicsSearchBtn' to avoid QSS collision with
Stream's 'StreamSearchBusy' / 'StreamSearchBtn'."
```

---

## Task 6: Wire event filter for focus show/hide + textChanged hide-on-typing

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp` — add `eventFilter` body near other Qt overrides (e.g., near `paintEvent` / `resizeEvent` / `showEvent`); extend the existing `textChanged` lambda
- Modify: `src/ui/pages/ComicsPage.cpp:776-788` (existing `textChanged` + `returnPressed` lambdas) — add hide-on-non-empty in `textChanged`; call `pushSearchHistory` in `returnPressed`; ALSO call `loadSearchHistory()` + `buildSearchHistoryDropdown()` + `m_searchBar->installEventFilter(this)` at the end of the search-bar setup

- [ ] **Step 1: Add `eventFilter` body.**

If `ComicsPage::eventFilter` does NOT already exist (verified during Task 1), add it near the other override bodies. Search for `void ComicsPage::resizeEvent` or similar to find the right cluster. Add:

```cpp
bool ComicsPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_searchBar) {
        if (event->type() == QEvent::FocusIn) {
            // Show history only if input is empty.
            if (m_searchBar->text().trimmed().isEmpty()) {
                showSearchHistoryDropdown();
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}
```

If `eventFilter` already exists, prepend the `if (obj == m_searchBar) { … }` branch above the existing logic and keep the existing fallthrough.

- [ ] **Step 2: Extend the existing `textChanged` lambda** (around line 779-783) **to hide the dropdown when typing starts and show it when input becomes empty + focused.**

Find:

```cpp
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        m_searchBar->setProperty("activeSearch", !m_searchBar->text().trimmed().isEmpty());
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);
    });
```

Replace with:

```cpp
    connect(m_searchBar, &QLineEdit::textChanged, this, [this]() {
        const bool hasText = !m_searchBar->text().trimmed().isEmpty();
        m_searchBar->setProperty("activeSearch", hasText);
        m_searchBar->style()->unpolish(m_searchBar);
        m_searchBar->style()->polish(m_searchBar);

        if (hasText) {
            // User is typing a new query — irrelevant history would obscure.
            hideSearchHistoryDropdown();
        } else if (m_searchBar->hasFocus()) {
            // Cleared while focused — re-offer history.
            showSearchHistoryDropdown();
        }
    });
```

- [ ] **Step 3: Append `loadSearchHistory()` + `buildSearchHistoryDropdown()` + event-filter install at the end of the search-bar setup block (immediately before the `// Ctrl+F focuses search bar` comment around line 790).**

Insert:

```cpp
    // ── Search history (Stream-bar parity 2026-05-22) ──
    loadSearchHistory();
    buildSearchHistoryDropdown();
    m_searchBar->installEventFilter(this);
```

- [ ] **Step 4: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. If `eventFilter` collides with an existing declaration, follow the contingency in Step 1.

- [ ] **Step 5: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): wire history dropdown into focus + textChanged

Event filter on m_searchBar fires the dropdown on FocusIn-when-empty
and a delayed hide (150ms) on FocusOut. textChanged hides while
typing, re-shows on clear-while-focused. End of buildLibraryMode's
search section now loads persisted history, builds the dropdown
widget once, and installs the event filter. Mirrors StreamPage.cpp's
buildSearchBar tail at :1346-1350 + eventFilter at :1873-1889."
```

---

## Task 7: Push to history at submit + drive busy state off the scraper signal

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp:784-788` (existing `returnPressed` lambda) — push to history before submit + hide dropdown
- Modify: `src/ui/pages/ComicsPage.cpp:2008-2022` (`showSearchMode`) — call `pushSearchHistory` + `hideSearchHistoryDropdown` + `setSearchBusy(true)`
- Modify: `src/ui/pages/ComicsPage.cpp` — at the same site where `ComicsPage` connects its other `m_sourceRegistry` / scraper signals (search for `m_sourceRegistry` callsites), add the `searchFinished` connection that flips `setSearchBusy(false)`. If no such connect site exists yet in `ComicsPage`, attach it right after `m_searchTakeover` is constructed (currently around line 191).
- Modify: `m_searchTakeover` back-from-search path — ensure `setSearchBusy(false)` fires on `backRequested` too (defensive reset; mirrors StreamPage.cpp:1955)

- [ ] **Step 1: Update the `returnPressed` lambda to push history (the existing lambda already calls `showSearchMode`; `pushSearchHistory` will be added inside `showSearchMode` itself in Step 2, so this step is just hiding the dropdown).**

Find:

```cpp
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchBar->text().trimmed();
        if (q.isEmpty()) return;
        showSearchMode(q);
    });
```

Replace with:

```cpp
    connect(m_searchBar, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_searchBar->text().trimmed();
        if (q.isEmpty()) return;
        hideSearchHistoryDropdown();
        showSearchMode(q);
    });
```

- [ ] **Step 2: Update `showSearchMode(query)` to push history + flip the busy spinner on.**

Find the existing `void ComicsPage::showSearchMode(const QString& query)` body. At its top, before any other logic, insert:

```cpp
    pushSearchHistory(query);
    hideSearchHistoryDropdown();
    setSearchBusy(true);
```

This is the canonical submit point — both Enter and the search-icon click and history-row clicks route through here, so all three paths push to history exactly once.

- [ ] **Step 3: Hook the busy-spinner-off signal on the weebcentral scraper.**

Locate the existing `m_searchTakeover = new ComicsTankoyomiSearchWidget(...)` site (around line 191). Immediately AFTER that constructor call and the existing two `connect(...)` lines that wire `backRequested` and `resultPicked`, add:

```cpp
    // Spinner-off hook: when the weebcentral scraper finishes (success or
    // empty), drop the busy state. Mirrors StreamPage's MetaAggregator
    // connect pattern at StreamPage.cpp:305-312. Lambda captures `this`
    // safely — ComicsPage owns m_sourceRegistry's parent context.
    if (m_sourceRegistry) {
        if (auto* scraper = m_sourceRegistry->find(QStringLiteral("weebcentral"))) {
            connect(scraper, &MangaScraper::searchFinished, this,
                    [this](const QList<MangaResult>&) {
                        setSearchBusy(false);
                    });
        }
    }
```

If the `MangaScraper` type isn't already included in `ComicsPage.cpp`, add `#include "core/manga/MangaScraper.h"` to the include block.

- [ ] **Step 4: Defensive reset — flip busy off on the back-from-search path.**

Locate the existing `connect(m_searchTakeover, &ComicsTankoyomiSearchWidget::backRequested, ...)` (around line 194). The handler currently routes back to the grid; extend its body (or its connected slot) to also call:

```cpp
        setSearchBusy(false);
        hideSearchHistoryDropdown();
```

If the existing connect uses a member-function slot rather than a lambda, add those two lines at the top of that slot's body instead. Mirrors StreamPage.cpp:1955-1957.

- [ ] **Step 5: Build-verify.**

Run: `build_check.bat`
Expected: `BUILD OK`. Watch for unresolved-include errors on `MangaScraper`.

- [ ] **Step 6: Commit.**

```bash
git add src/ui/pages/ComicsPage.cpp
git commit -m "comics(search): wire history-push at submit + busy spinner on scraper signals

showSearchMode is the canonical submit funnel; all three entry paths
(Enter, icon click, history-row click) route through it so pushSearchHistory
runs exactly once per submitted query. setSearchBusy(true) fires there;
setSearchBusy(false) fires on either MangaScraper::searchFinished (the
scraper is already a known dependency — ComicsTankoyomiSearchWidget
consumes the same signal at construction time) or on backRequested
(defensive reset for the user-bailed-mid-search case). Mirrors
StreamPage's setSearchBusy call sites at :1485 / :305-312 / :1955."
```

---

## Task 8: Build + smoke matrix (sentinel smoke proves the parity end-to-end)

**Files:**
- No edits — verification-only

This is the rule-21-style sentinel smoke that proves the feature works. Per `feedback_one_fix_per_rebuild.md` + the project's smoke-first verification policy, the executing agent must run this matrix themselves (Hemanth's role is visual-judgment only — see `feedback_agent_launches_app.md`).

- [ ] **Step 1: Pre-flight kill any running Tankoban.**

Run: `taskkill //F //IM Tankoban.exe 2>$null; taskkill //F //IM ffmpeg_sidecar.exe 2>$null`

Expected: SUCCESS or `not found`. Either is fine.

- [ ] **Step 2: Full build + launch.**

Run: `build_and_run.bat`
Expected: build completes; Tankoban window appears.

- [ ] **Step 3: Open Comics mode + confirm bridge is alive.**

Run: `out\tankoctl.exe open-page comics`
Then: `out\tankoctl.exe ping`
Expected: ping returns reply within ~150ms.

- [ ] **Step 4: Confirm the new search-button widget is present in the UIA tree.**

Run: `powershell -NoProfile -File scripts/uia-dump.ps1 -TargetClass ComicsPage -MaxDepth 6 | Select-String 'ComicsSearchBtn|ComicsSearchBusy|m_searchBar|LibrarySearch'`
Expected: both `ComicsSearchBtn` and the existing `LibrarySearch` (m_searchBar) AutomationIds appear. `ComicsSearchBusy` appears as a child of the search row but `Visible=false`.

- [ ] **Step 5: Smoke the history-dropdown on focus.**

Sequence:
1. Click the Comics search bar via `mcp__pywinauto-mcp__automation_mouse` (target by AutomationId `LibrarySearch`).
2. With empty input + focus, confirm `ComicsSearchHistory` frame appears in the UIA tree: `powershell -NoProfile -File scripts/uia-dump.ps1 -TargetClass ComicsPage | Select-String ComicsSearchHistory`.
3. On a fresh install / empty history, the dropdown should NOT appear (early-return in `showSearchHistoryDropdown` when `m_searchHistory.isEmpty()`). Verify it's absent or `Visible=false`.

Expected: dropdown hidden on empty history.

- [ ] **Step 6: Smoke the history-push at submit.**

Sequence:
1. Type a query — e.g., `Death Note` — via `mcp__pywinauto-mcp__automation_keyboard`.
2. Press Enter (or click the magnifying-glass `ComicsSearchBtn`).
3. Confirm Comics flips to the search-takeover view (UIA tree shows `ComicsTankoyomiSearchWidget`) and `ComicsSearchBusy` flips to `Visible=true` briefly during the in-flight window, then `Visible=false` after `searchFinished`.
4. Go back to library: trigger `backRequested` (click the back button in `ComicsTankoyomiSearchWidget`).
5. Click into the search bar with empty input again.
6. Confirm `ComicsSearchHistory` is now visible AND contains a row with text `Death Note` AND a footer button `× Clear search history`.

Expected: full round-trip GREEN.

- [ ] **Step 7: Smoke the history-resubmit + per-entry remove.**

Sequence:
1. Click the `Death Note` history row.
2. Confirm Comics flips back into the search-takeover view with that query.
3. Back to library, focus the bar again.
4. Click the `×` next to a history row.
5. Confirm that row disappears from the dropdown without closing it.

Expected: clicks behave per Stream's pattern.

- [ ] **Step 8: Smoke the "Clear search history" footer.**

Sequence:
1. With ≥1 history entry, click the `× Clear search history` footer.
2. Confirm dropdown closes (per `clearSearchHistory` → `hideSearchHistoryDropdown` at end).
3. Re-focus the bar. Confirm dropdown stays hidden (no history left → early-return).

Expected: clean wipe.

- [ ] **Step 9: QSettings cross-mode disjoint verification.**

Run: `out\tankoctl.exe settings-get comics/searchHistory` (if the v1.9 settings-* subcommand exists; otherwise inspect via `out\tankoctl.exe jsonstore-get comics/searchHistory` or by opening `%APPDATA%\Tankoban\Tankoban.ini` and grepping for `searchHistory`).
Then: same query against `stream/searchHistory`.
Expected: Comics history contains `Death Note` (or whatever was typed); Stream history does NOT (unrelated entries only, if any).

- [ ] **Step 10: Preserved-keyboard-layer verification.**

Sequence:
1. While on the Comics grid, press `Ctrl+F`. Confirm focus jumps to `LibrarySearch` and text is selected (already covered by the existing shortcut at ComicsPage.cpp:791-795).
2. With non-empty input, press `Esc`. Confirm input clears (no regression in the existing Esc shortcut at line 800-814).
3. While on the grid, press `F5`. Confirm a rescan is triggered (existing line 817-818 shortcut).

Expected: all three Comics-only shortcuts unchanged.

- [ ] **Step 11: Post-smoke cleanup.**

Run: `powershell -NoProfile -File scripts/stop-tankoban.ps1`
Expected: Tankoban + sidecar processes cleaned up per Rule 17.

- [ ] **Step 12: Final commit (RTC line authoring + chat.md sweep).**

```bash
git add agents/chat.md
git commit -m "[Agent 5 (Library UX), COMICS_SEARCH_BAR_PARITY ship]"
```

Then post a `READY TO COMMIT — [Agent 5 (Library UX), COMICS_SEARCH_BAR_PARITY ship]:` line to `agents/chat.md` summarizing what shipped, the skills invoked, files touched, and the sentinel-smoke verdict. Agent 0 sweeps later.

---

## Self-review (run after the plan is saved, before execution)

**Spec coverage:**

| Hemanth/Agent 1 v1 requirement | Task |
|---|---|
| Magnifying-glass search button | Task 5 |
| Busy spinner | Tasks 5 + 7 |
| Search history (persist + dropdown + per-entry remove + clear all) | Tasks 2 + 3 + 4 + 6 + 7 |
| QSettings key `comics/searchHistory` (NOT shared with stream) | Task 2 Step 1 |
| Preserve Ctrl+F + F5 + Esc routing | Task 8 Step 10 (no edits to those shortcut blocks across any task) |
| Preserve inline focus-border QSS on the input | No edits to ComicsPage.cpp:759-762 across any task |
| ❌ Skip ⚙ Clear Library menu | Not in any task — explicit non-goal |
| ❌ Skip Addons + Catalog buttons | Not in any task — explicit non-goal |

**Placeholder scan:** zero. Every step contains either the exact code block to insert or the exact shell command + expected output.

**Type / signature consistency:** `setSearchBusy(bool)` and `m_searchBusy` (declared as `QWidget*` per Stream's pattern, assigned a `QProgressBar*` at runtime); `pushSearchHistory(const QString&)` matches the Task 2 declaration and the Task 7 callsite; `kMaxSearchHistory = 10` matches Stream's value; `showSearchMode` is the canonical submit funnel called from all three entry paths.

**Agent 1's guardrails honored:**
- No edits in `dispatchFandomResolve` (line 3118+) ✓
- No edits in `populateVolumeRowsFromFandom` wiring ✓
- No edits in `showSeries` hero block (~870-960) ✓
- No edits in `renderDetail` Phase 8a ✓
- No edits in BookWalker `VolumeCoverResolver` wiring ✓
- QSettings key disjoint (`comics/searchHistory`, NOT `stream/searchHistory`) ✓
- No new headers → no CMakeLists touch needed ✓

**Rule-compliance:**
- One file touched per coherent change set (Tasks 1-7 each commit a logically distinct piece) — honors `feedback_one_fix_per_rebuild.md`
- Build-verify gate at the end of every code task — honors `feedback_quality_standard.md`
- Sentinel smoke at the end before RTC — honors `feedback_smoke_on_failing_streams.md` + Rule 17
- Plan-first authored before any code touch — honors `feedback_plan_first_zero_errors.md`
