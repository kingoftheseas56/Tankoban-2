# Global Nav History Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship browser-grade global Back / Forward navigation in the Tankoban 2 topbar — chevrons, keyboard shortcuts, mouse buttons 4/5, history persistence across sessions, full state restore on Back-navigation — across every navigable page in the app.

**Architecture:** A `NavHistory` controller owned by `MainWindow` holds the global back/forward stack with cursor + truncate-on-new-nav. Each navigable page implements `INavStateProvider` (`captureNavState`/`restoreNavState`) — opaque JSON blobs that the page interprets. Stream's existing internal nav-stack is retired as a navigation mechanism; its `NavEntry` struct + `showEntryRaw` dispatcher are retained as render machinery driven by the global controller. Persistence via single `nav_history.json` write at `closeEvent`. Reader/player overlay open: Back closes the overlay (does not pop history). Modal dialog open: Back/Forward grayed out.

**Tech Stack:** C++20, Qt6 (`QPushButton`, `QShortcut`, `QApplication::activeModalWidget`, `mousePressEvent` override, `QJsonObject` blobs, `QSaveFile` atomic write). MSVC build via `build_check.bat`. Smoke via `tankoctl` dev-bridge for state queries (~5–10× faster than UIA tree walks), `pywinauto-mcp` for UIA-driven UI clicks, Hemanth visual smoke for the final pass.

**Reference spec:** [docs/superpowers/specs/2026-05-14-global-nav-history-design.md](../specs/2026-05-14-global-nav-history-design.md)

**Owner:** Agent 5 (Library UX + Theme). Per spec §3.8, Agent 5 owns ALL UI work for this feature app-wide. Other agents retain primary ownership of engine/scraper/player/reader internals — if a per-page hook would need to touch those, defer + ask. Post READY TO COMMIT lines to `agents/chat.md` per Rule 11 (Agent 0 batches commits via `/commit-sweep`).

**Smoke discipline:** No test infrastructure exists (src/tests/ is empty post-audiobook-removal). Plan uses smoke-first per CLAUDE.md. Each task ends with: build_check.bat GREEN → smoke (tankoctl + pywinauto-mcp where feasible; Hemanth visual where modal/visual-only) → READY TO COMMIT line in chat.md.

**Rule 19 MCP LANE LOCK:** Before any pywinauto-mcp / windows-mcp smoke, claim the lane in chat.md (`MCP LOCK CLAIMED — Agent 5 — <task tag>`). Release post-smoke (`MCP LOCK RELEASED — Agent 5 — <task tag>`). Rule 17: run `scripts/stop-tankoban.ps1` post-smoke to clean PIDs.

---

## File Structure

**New files:**

- `src/ui/NavHistory.h` — controller class declaration (Q_OBJECT, public surface, NavEntry struct).
- `src/ui/NavHistory.cpp` — controller impl (cursor logic, push/back/forward, persistence load/save, eviction).
- `src/ui/INavStateProvider.h` — pure-virtual interface header (no .cpp; interface-only).

**Modified files (substrate + overlay + input):**

- `src/ui/MainWindow.h` — add forward decl for NavHistory; add member `m_navHistory`, `m_backBtn`, `m_forwardBtn`; declare new private methods (`onBackRequested`, `onForwardRequested`, `onNavEntryRequested`, `onNavAvailabilityChanged`, `mousePressEvent` override).
- `src/ui/MainWindow.cpp` — wire NavHistory in ctor; add chevrons to `buildTopBar`; register Alt+Left/Right QShortcuts in `bindShortcuts`; override `mousePressEvent`; intercept Back when reader/player/modal is active; in `activatePage`, route nav-event push to NavHistory; emit `closeEvent` -> NavHistory persistence flush.
- `CMakeLists.txt` — add `src/ui/NavHistory.cpp` to SOURCES, `src/ui/NavHistory.h` + `src/ui/INavStateProvider.h` to HEADERS.

**Modified files (per-page hooks):**

- `src/ui/pages/ComicsPage.{h,cpp}` — inherit `INavStateProvider`; implement `captureNavState` + `restoreNavState`.
- `src/ui/pages/BooksPage.{h,cpp}` — same.
- `src/ui/pages/VideosPage.{h,cpp}` + `src/ui/pages/ShowView.{h,cpp}` — same (VideosPage owns; ShowView contributes its state to VideosPage's blob).
- `src/ui/pages/TankorentPage.{h,cpp}` — same.
- `src/ui/pages/tankolibrary/TankoLibraryPage.{h,cpp}` — same.
- `src/ui/pages/TankoyomiPage.{h,cpp}` + MangaDetailView — same.
- `src/ui/pages/StreamPage.{h,cpp}` + `src/ui/pages/stream/StreamDetailView.{h,cpp}` — heaviest slice; retire `m_navStack` as nav mechanism; retain `NavEntry` struct + `showEntryRaw` as render dispatcher driven by `restoreNavState`. Heads-up to Agent 4 in chat.md before starting.

**Untouched (engine/scraper/player/reader primary-ownership code):**

- `src/core/torrent/*`
- `src/core/stream/*`
- `src/core/manga/*`
- `native_sidecar/*`
- `src/ui/readers/ComicReader.*`
- `src/ui/readers/BookReader.*` + `src/ui/dialogs/MangaTransferDialog.*` (it's a modal, gated by Task 7)
- `src/ui/player/VideoPlayer.*` (interception lives in MainWindow, not VideoPlayer)

---

## Tasks

### Task 1: INavStateProvider interface + NavHistory data types

**Files:**
- Create: `src/ui/INavStateProvider.h`
- Create: `src/ui/NavHistory.h` (data types + class skeleton; impl in Task 2)
- Modify: `CMakeLists.txt` (add headers)

- [ ] **Step 1: Create the interface header.**

Write `src/ui/INavStateProvider.h`:

```cpp
#pragma once

#include <QJsonObject>
#include <QString>

// INavStateProvider — implemented by every navigable page in Tankoban so
// the global NavHistory (src/ui/NavHistory.h) can snapshot and restore
// per-page state on Back / Forward.
//
// Contract:
//   captureNavState() — called BEFORE navigating away from this page.
//   Must be cheap (no I/O, no thread waits). The returned blob is
//   opaque to NavHistory; each page defines its own schema.
//
//   restoreNavState(blob) — called when Back / Forward lands on a
//   previously-captured entry for this page. Return true on success;
//   false if the blob references data that no longer exists (e.g.,
//   deleted show id). NavHistory drops failing entries and tries the
//   next one.
//
//   navStateLabel() — short human-readable tag for logging / debug.
//
// Multiple inheritance is intentional: implementing classes already
// inherit QWidget (or a subclass). INavStateProvider is a non-Q_OBJECT
// pure interface so this is C++-clean. Implementing classes MUST NOT
// add Q_OBJECT to this interface.
class INavStateProvider {
public:
    virtual ~INavStateProvider() = default;

    virtual QJsonObject captureNavState() const = 0;
    virtual bool restoreNavState(const QJsonObject& blob) = 0;
    virtual QString navStateLabel() const = 0;
};
```

- [ ] **Step 2: Create NavHistory header with data types + class skeleton.**

Write `src/ui/NavHistory.h`:

```cpp
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

// NavHistory — global back/forward stack controller owned by MainWindow.
// Single source of truth for the topbar chevrons, Alt+Left/Right keyboard
// shortcuts, and mouse buttons 4/5.
//
// Each navigable page implements INavStateProvider (see INavStateProvider.h).
// MainWindow registers an active provider via setActiveProvider() so
// NavHistory can call captureNavState() at the moment a nav event fires.
//
// Persistence: load on ctor (best-effort; corrupted file -> empty stack);
// save on flushToDisk() (called from MainWindow::closeEvent).
//
// Stack cap: kMaxEntries (100). Oldest evicted from front on push.

class INavStateProvider;

struct NavEntry {
    QString pageId;          // e.g. "videos", "stream", "tankoyomi"
    QJsonObject stateBlob;   // opaque to NavHistory; page-defined schema
    qint64 timestampMs = 0;  // QDateTime::currentMSecsSinceEpoch() at push
};

class NavHistory : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 100;
    static constexpr int kSchemaVersion = 1;

    explicit NavHistory(QObject* parent = nullptr);
    ~NavHistory() override;

    // MainWindow notifies which page is currently active.
    // Provider may be nullptr (e.g., during teardown).
    void setActiveProvider(const QString& pageId, INavStateProvider* provider);

    // Called by MainWindow when a nav event fires (page switch or
    // detail open). Captures current page state into the current entry
    // first, then pushes a fresh entry tagged with targetPageId.
    void recordNavEvent(const QString& targetPageId);

    // User-driven Back / Forward.
    void back();
    void forward();

    // Chevron / shortcut enable state.
    bool canGoBack() const;
    bool canGoForward() const;

    // Explicit "snapshot the active page right now" — called from
    // MainWindow::closeEvent before persistence flush, so the latest
    // page state is captured even though no navigation happened.
    void captureCurrent();

    // Persistence flush. Called from MainWindow::closeEvent.
    void flushToDisk();

signals:
    // Emitted when back() / forward() shifts the cursor.
    // MainWindow listens and activates the target page + hands the
    // blob to the page's restoreNavState.
    void entryRequested(const NavEntry& entry);

    // Emitted whenever back/forward availability changes.
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);

private:
    // Disk paths.
    QString persistencePath() const;
    void loadFromDisk();

    QVector<NavEntry> m_stack;
    int m_cursor = -1;  // -1 = empty stack; otherwise index of current entry

    QString m_activePageId;
    INavStateProvider* m_activeProvider = nullptr;

    bool m_lastBackAvailable = false;
    bool m_lastForwardAvailable = false;
    void notifyAvailability();
};
```

- [ ] **Step 3: Stub the .cpp file so the class compiles.**

Write `src/ui/NavHistory.cpp`:

```cpp
#include "NavHistory.h"

#include "INavStateProvider.h"

NavHistory::NavHistory(QObject* parent) : QObject(parent) {}
NavHistory::~NavHistory() = default;

void NavHistory::setActiveProvider(const QString& pageId, INavStateProvider* provider) {
    m_activePageId = pageId;
    m_activeProvider = provider;
}

void NavHistory::recordNavEvent(const QString&) { /* Task 2 */ }
void NavHistory::back() { /* Task 2 */ }
void NavHistory::forward() { /* Task 2 */ }
bool NavHistory::canGoBack() const { return m_cursor > 0; }
bool NavHistory::canGoForward() const { return m_cursor >= 0 && m_cursor < m_stack.size() - 1; }
void NavHistory::captureCurrent() { /* Task 2 */ }
void NavHistory::flushToDisk() { /* Task 3 */ }
QString NavHistory::persistencePath() const { return {}; /* Task 3 */ }
void NavHistory::loadFromDisk() { /* Task 3 */ }
void NavHistory::notifyAvailability() { /* Task 2 */ }
```

- [ ] **Step 4: Wire CMakeLists.**

In `CMakeLists.txt`, find the `set(SOURCES ...)` block and add `src/ui/NavHistory.cpp` alphabetically near `src/ui/MainWindow.cpp`. Find the `set(HEADERS ...)` block and add `src/ui/NavHistory.h` + `src/ui/INavStateProvider.h` near `src/ui/MainWindow.h`. Match the existing indentation style.

- [ ] **Step 5: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK` (compiles clean; no callers yet so no link errors).

If FAIL: check `out/build_check.log` tail; common issues are typos in CMakeLists or a missing `#include <QObject>` / `<QString>` / `<QJsonObject>`.

- [ ] **Step 6: Post READY TO COMMIT line in chat.md.**

Append a single RTC line to `agents/chat.md`:

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T1 2026-05-14 ~HH:MMam — NavHistory + INavStateProvider scaffold. NEW src/ui/INavStateProvider.h (~30 LOC, pure-virtual interface w/ captureNavState/restoreNavState/navStateLabel). NEW src/ui/NavHistory.h (~95 LOC, NavEntry struct + class skeleton w/ kMaxEntries=100, kSchemaVersion=1, signals entryRequested + backAvailableChanged + forwardAvailableChanged). NEW src/ui/NavHistory.cpp (~30 LOC stubs; impl lands in T2-T3). CMakeLists.txt SOURCES + HEADERS entries added. BUILD OK first try. No behavior change yet (no callers wired). Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/INavStateProvider.h, src/ui/NavHistory.h, src/ui/NavHistory.cpp, CMakeLists.txt, agents/chat.md
```

---

### Task 2: NavHistory cursor + push/back/forward + capture

**Files:**
- Modify: `src/ui/NavHistory.cpp` (fill in stubs from Task 1)

- [ ] **Step 1: Implement notifyAvailability.**

Replace the stub with:

```cpp
void NavHistory::notifyAvailability() {
    const bool back = canGoBack();
    const bool fwd = canGoForward();
    if (back != m_lastBackAvailable) {
        m_lastBackAvailable = back;
        emit backAvailableChanged(back);
    }
    if (fwd != m_lastForwardAvailable) {
        m_lastForwardAvailable = fwd;
        emit forwardAvailableChanged(fwd);
    }
}
```

- [ ] **Step 2: Implement captureCurrent.**

```cpp
void NavHistory::captureCurrent() {
    if (m_cursor < 0 || m_cursor >= m_stack.size()) return;
    if (!m_activeProvider) return;
    // Capture into the current entry so a future Back-restores us
    // to where the user actually was at the moment they navigated.
    m_stack[m_cursor].stateBlob = m_activeProvider->captureNavState();
}
```

- [ ] **Step 3: Implement recordNavEvent.**

```cpp
#include <QDateTime>
// ... (top of file)

void NavHistory::recordNavEvent(const QString& targetPageId) {
    // Step 1: snapshot the current page's state into the current entry,
    // so that if the user later Backs to here, we restore where they were.
    captureCurrent();

    // Step 2: truncate any forward entries above the cursor (browser-style).
    if (m_cursor >= 0 && m_cursor < m_stack.size() - 1) {
        m_stack.resize(m_cursor + 1);
    }

    // Step 3: evict oldest if at capacity.
    while (m_stack.size() >= kMaxEntries) {
        m_stack.removeFirst();
        if (m_cursor >= 0) m_cursor--;
    }

    // Step 4: push a fresh entry. State blob is empty for now; it will
    // be filled when this page next captures (either at next navigation
    // or via setActiveProvider's callback chain — see Task 4 chevron click).
    NavEntry entry;
    entry.pageId = targetPageId;
    entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_stack.append(entry);
    m_cursor = m_stack.size() - 1;

    notifyAvailability();
}
```

- [ ] **Step 4: Implement back() and forward().**

```cpp
void NavHistory::back() {
    if (!canGoBack()) return;
    // Capture current page state before shifting so the entry we're
    // leaving has the latest snapshot (round-trip integrity).
    captureCurrent();

    // Walk backward, skipping entries whose page fails to restore
    // (e.g., target showId was deleted between sessions). Stop at
    // m_cursor == 0 (stack-bottom).
    while (m_cursor > 0) {
        m_cursor--;
        const NavEntry& target = m_stack[m_cursor];
        emit entryRequested(target);
        // entryRequested handler in MainWindow activates the page and
        // calls restoreNavState. If the page's restore returns false,
        // MainWindow will call back() again to skip the stale entry.
        // We break here; the recursive skip happens via MainWindow's
        // slot, NOT inside back().
        break;
    }
    notifyAvailability();
}

void NavHistory::forward() {
    if (!canGoForward()) return;
    captureCurrent();
    while (m_cursor < m_stack.size() - 1) {
        m_cursor++;
        const NavEntry& target = m_stack[m_cursor];
        emit entryRequested(target);
        break;
    }
    notifyAvailability();
}
```

- [ ] **Step 5: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

If FAIL: likely missing `#include <QDateTime>`. Add it and rebuild.

- [ ] **Step 6: Smoke (no runtime smoke this task — NavHistory still has no callers).**

This task is logic-only. End-to-end smoke deferred to Task 4 when chevrons are wired.

- [ ] **Step 7: Post RTC line in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T2 2026-05-14 ~HH:MMam — NavHistory core logic: notifyAvailability + captureCurrent + recordNavEvent (truncate-forward + evict-oldest) + back/forward (skip-stale shape, walks one entry at a time). ~80 LOC added to NavHistory.cpp. BUILD OK. No runtime smoke yet — wired in T4. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/NavHistory.cpp, agents/chat.md
```

---

### Task 3: NavHistory persistence (load on ctor + save at quit)

**Files:**
- Modify: `src/ui/NavHistory.cpp` (replace persistencePath / loadFromDisk / flushToDisk stubs)

- [ ] **Step 1: Implement persistencePath.**

At the top of NavHistory.cpp, add includes:

```cpp
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
```

Replace the stub:

```cpp
QString NavHistory::persistencePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) return {};
    QDir().mkpath(dir);
    return dir + "/nav_history.json";
}
```

- [ ] **Step 2: Implement loadFromDisk.**

```cpp
void NavHistory::loadFromDisk() {
    const QString path = persistencePath();
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject root = doc.object();
    if (root.value("schemaVersion").toInt(0) != kSchemaVersion) return;

    const QJsonArray entries = root.value("entries").toArray();
    m_stack.clear();
    m_stack.reserve(entries.size());
    for (const QJsonValue& v : entries) {
        if (!v.isObject()) continue;
        const QJsonObject e = v.toObject();
        NavEntry entry;
        entry.pageId = e.value("pageId").toString();
        entry.stateBlob = e.value("stateBlob").toObject();
        entry.timestampMs = static_cast<qint64>(e.value("timestampMs").toDouble(0));
        if (entry.pageId.isEmpty()) continue;  // skip malformed
        m_stack.append(entry);
    }

    // Trim to cap if a future version had a higher one.
    while (m_stack.size() > kMaxEntries) m_stack.removeFirst();

    const int savedCursor = root.value("cursor").toInt(-1);
    if (savedCursor >= 0 && savedCursor < m_stack.size()) {
        m_cursor = savedCursor;
    } else if (!m_stack.isEmpty()) {
        m_cursor = m_stack.size() - 1;  // land at the newest entry
    } else {
        m_cursor = -1;
    }
}
```

Add `#include <QFile>` to the include block.

- [ ] **Step 3: Implement flushToDisk.**

```cpp
void NavHistory::flushToDisk() {
    // Snapshot the current page's latest state so the persisted entry
    // reflects what the user was looking at right before quit.
    captureCurrent();

    const QString path = persistencePath();
    if (path.isEmpty()) return;

    QJsonArray entries;
    for (const NavEntry& e : m_stack) {
        QJsonObject obj;
        obj["pageId"] = e.pageId;
        obj["stateBlob"] = e.stateBlob;
        obj["timestampMs"] = static_cast<double>(e.timestampMs);
        entries.append(obj);
    }

    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["cursor"] = m_cursor;
    root["entries"] = entries;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}
```

- [ ] **Step 4: Call loadFromDisk from ctor.**

Replace the ctor body:

```cpp
NavHistory::NavHistory(QObject* parent) : QObject(parent) {
    loadFromDisk();
    // Note: do NOT emit availability signals from the ctor — listeners
    // aren't connected yet. notifyAvailability is called after the first
    // recordNavEvent or after explicit notifyAvailability() from
    // MainWindow's setup code.
}
```

- [ ] **Step 5: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 6: Smoke (manual file-level).**

This task is testable without UI. Strategy:

1. Launch Tankoban once via `build_and_run.bat` (will create a fresh `%APPDATA%/Tankoban/nav_history.json` once Task 4 wires NavHistory to recordNavEvent — for THIS task, the file won't be created yet because no caller invokes flushToDisk). Skip live smoke; defer to Task 4 end-to-end.

For now, verify the path resolves correctly by adding a temporary qDebug at end of persistencePath, building, launching, and checking the path in stderr. Remove the qDebug before RTC.

Run via `pwsh -NoProfile -Command "echo $env:APPDATA"` to verify the path Qt will resolve to is the expected `C:\Users\Suprabha\AppData\Roaming\Tankoban\`.

- [ ] **Step 7: Post RTC line in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T3 2026-05-14 ~HH:MMam — NavHistory persistence: loadFromDisk (schema-version gate, malformed-entry skip, cap-trim, cursor-clamp) + flushToDisk (QSaveFile atomic write, captureCurrent before serialize) + persistencePath via QStandardPaths::AppDataLocation/nav_history.json. ctor now loads from disk on construction. ~90 LOC. BUILD OK. End-to-end smoke deferred to T4 when chevrons wire the controller into MainWindow. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/NavHistory.cpp, agents/chat.md
```

---

### Task 4: Topbar chevrons — visual + click wiring

**Files:**
- Modify: `src/ui/MainWindow.h` (forward decl NavHistory; add members; declare slots)
- Modify: `src/ui/MainWindow.cpp` (instantiate; insert chevrons into `buildTopBar`; wire clicks)

- [ ] **Step 1: Add NavHistory member + chevron buttons to MainWindow.h.**

Forward-declare at the top of MainWindow.h (with other forward decls):

```cpp
class NavHistory;
struct NavEntry;
```

In the `private:` member section near `m_hamburgerBtn`, add:

```cpp
    // Global Back / Forward navigation (spec docs/superpowers/specs/2026-05-14-global-nav-history-design.md)
    NavHistory   *m_navHistory  = nullptr;
    QPushButton  *m_backBtn     = nullptr;
    QPushButton  *m_forwardBtn  = nullptr;
```

In the `private:` method section, add:

```cpp
    // Global Nav slots
    void onBackChevronClicked();
    void onForwardChevronClicked();
    void onNavEntryRequested(const NavEntry& entry);
    void onBackAvailabilityChanged(bool available);
    void onForwardAvailabilityChanged(bool available);
```

- [ ] **Step 2: Instantiate NavHistory in MainWindow ctor.**

Find the ctor in `MainWindow.cpp`. After `m_bridge = ...` instantiation (or near where other top-level controllers are created — `SidebarDrawer`, etc.), insert:

```cpp
    m_navHistory = new NavHistory(this);
    connect(m_navHistory, &NavHistory::entryRequested,
            this, &MainWindow::onNavEntryRequested);
    connect(m_navHistory, &NavHistory::backAvailableChanged,
            this, &MainWindow::onBackAvailabilityChanged);
    connect(m_navHistory, &NavHistory::forwardAvailableChanged,
            this, &MainWindow::onForwardAvailabilityChanged);
```

Add `#include "NavHistory.h"` at the top of MainWindow.cpp.

- [ ] **Step 3: Insert chevrons into buildTopBar.**

Locate `buildTopBar()` in MainWindow.cpp (~line 354). Inside, after `m_hamburgerBtn` is added to `leftLayout` and before `m_brandLabel` is added, insert:

```cpp
    // Global Back chevron (spec: docs/superpowers/specs/2026-05-14-global-nav-history-design.md §6)
    m_backBtn = new QPushButton(leftSlot);
    m_backBtn->setObjectName("TopBarBackBtn");
    m_backBtn->setFixedSize(28, 24);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setIcon(QIcon(":/icons/chevron_left.svg"));
    m_backBtn->setIconSize(QSize(16, 16));
    m_backBtn->setToolTip("Back (Alt+Left)");
    m_backBtn->setAccessibleName("Back");
    m_backBtn->setAccessibleDescription("Navigate to the previous page. Keyboard: Alt+LeftArrow.");
    m_backBtn->setFocusPolicy(Qt::NoFocus);
    m_backBtn->setEnabled(false);  // initial: stack empty
    leftLayout->addWidget(m_backBtn, 0, Qt::AlignVCenter);
    connect(m_backBtn, &QPushButton::clicked,
            this, &MainWindow::onBackChevronClicked);

    // Global Forward chevron
    m_forwardBtn = new QPushButton(leftSlot);
    m_forwardBtn->setObjectName("TopBarForwardBtn");
    m_forwardBtn->setFixedSize(28, 24);
    m_forwardBtn->setCursor(Qt::PointingHandCursor);
    m_forwardBtn->setIcon(QIcon(":/icons/chevron_right.svg"));
    m_forwardBtn->setIconSize(QSize(16, 16));
    m_forwardBtn->setToolTip("Forward (Alt+Right)");
    m_forwardBtn->setAccessibleName("Forward");
    m_forwardBtn->setAccessibleDescription("Navigate to the next page. Keyboard: Alt+RightArrow.");
    m_forwardBtn->setFocusPolicy(Qt::NoFocus);
    m_forwardBtn->setEnabled(false);
    leftLayout->addWidget(m_forwardBtn, 0, Qt::AlignVCenter);
    connect(m_forwardBtn, &QPushButton::clicked,
            this, &MainWindow::onForwardChevronClicked);

    // Visual separator: bump the gap between Forward and Brand from
    // the default 6px iconBtn spacing to 12px to read as "nav cluster" vs
    // "brand identity".
    leftLayout->addSpacing(6);
```

- [ ] **Step 4: Implement the click slots.**

Add the slot impls at the end of `MainWindow.cpp` (or near other slot definitions):

```cpp
void MainWindow::onBackChevronClicked() {
    if (m_navHistory) m_navHistory->back();
}

void MainWindow::onForwardChevronClicked() {
    if (m_navHistory) m_navHistory->forward();
}

void MainWindow::onBackAvailabilityChanged(bool available) {
    if (m_backBtn) m_backBtn->setEnabled(available);
}

void MainWindow::onForwardAvailabilityChanged(bool available) {
    if (m_forwardBtn) m_forwardBtn->setEnabled(available);
}

void MainWindow::onNavEntryRequested(const NavEntry& entry) {
    // Activate the target page first (no-op if already there).
    activatePage(entry.pageId);
    // The page's restoreNavState gets called via the active-provider
    // chain: when activatePage flips to the target page, we re-register
    // its provider with NavHistory (see Step 5). The provider's
    // restoreNavState is invoked here with the target blob.
    // If restoreNavState returns false (stale entry), recurse one
    // step further in the same direction.
    // Note: m_navHistory->m_activeProvider was just set by activatePage
    // -> setActiveProvider; this is intentional ordering.
    // TODO Task 8-14 ports each page's INavStateProvider; until then
    // the cast will return nullptr for non-implementing pages.
    INavStateProvider* provider = nullptr;
    if (m_pageStack) {
        for (int i = 0; i < m_pageStack->count(); ++i) {
            QWidget* w = m_pageStack->widget(i);
            if (w && w->objectName() == entry.pageId) {
                provider = dynamic_cast<INavStateProvider*>(w);
                break;
            }
        }
    }
    if (provider) {
        const bool ok = provider->restoreNavState(entry.stateBlob);
        if (!ok) {
            // Stale — drop and walk one more in the same direction.
            // We can't tell direction from here cleanly; defer the
            // skip-stale logic to T8+ when first real provider lands.
        }
    }
}
```

Add `#include "INavStateProvider.h"` at the top of MainWindow.cpp.

- [ ] **Step 5: Hook activatePage to register the active provider with NavHistory + record the nav event.**

Locate `activatePage(const QString& pageId)` in MainWindow.cpp (~line 709). After the existing body (the `m_activePageId = pageId; ... setCurrentIndex` block) and before any signal emit, insert:

```cpp
    // Global Nav: register the new active provider so NavHistory can
    // capture its state on the NEXT nav event.
    if (m_navHistory && m_pageStack) {
        INavStateProvider* provider = nullptr;
        for (int i = 0; i < m_pageStack->count(); ++i) {
            QWidget* w = m_pageStack->widget(i);
            if (w && w->objectName() == pageId) {
                provider = dynamic_cast<INavStateProvider*>(w);
                break;
            }
        }
        m_navHistory->setActiveProvider(pageId, provider);

        // Only record a nav event if this activatePage call came from
        // user action (sidebar click, drawer click, detail open), NOT
        // from a Back/Forward restoration (which sets m_activePageId
        // via the same path but should NOT push a new history entry).
        // Distinguish via a per-call flag — see Step 6.
    }
```

- [ ] **Step 6: Add a guard flag to prevent Back/Forward restoration from looping.**

Add to MainWindow.h `private:` section near `m_activePageId`:

```cpp
    bool m_inNavRestore = false;  // true during onNavEntryRequested processing
```

In MainWindow.cpp `onNavEntryRequested`, wrap the activatePage call:

```cpp
void MainWindow::onNavEntryRequested(const NavEntry& entry) {
    m_inNavRestore = true;
    activatePage(entry.pageId);
    // ... provider lookup + restoreNavState as in Step 4 ...
    m_inNavRestore = false;
}
```

In `activatePage`, gate the recordNavEvent on the flag:

```cpp
    if (m_navHistory && m_pageStack && !m_inNavRestore) {
        // ... provider lookup ...
        m_navHistory->setActiveProvider(pageId, provider);
        m_navHistory->recordNavEvent(pageId);
    } else if (m_navHistory && m_pageStack) {
        // Restore path: just update the active provider, don't push.
        // ... provider lookup ...
        m_navHistory->setActiveProvider(pageId, provider);
    }
```

- [ ] **Step 7: Flush persistence in closeEvent.**

Locate `MainWindow::closeEvent` in MainWindow.cpp. At the top of the method body (BEFORE any save-state or quit logic that may exit the function), add:

```cpp
    if (m_navHistory) m_navHistory->flushToDisk();
```

- [ ] **Step 8: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

If FAIL: likely a missing forward-declaration include or a typo in slot wiring. Common: `NavEntry` is forward-declared but slot takes `const NavEntry&` — needs the full include in the .cpp. The `#include "NavHistory.h"` covers this.

- [ ] **Step 9: Smoke via tankoctl dev-bridge.**

Lane lock + launch:

```
chat.md append:
MCP LOCK CLAIMED — Agent 5 — GLOBAL_NAV_HISTORY T4 smoke
```

Build + launch with dev-control: run `build_and_run.bat` (which auto-sets `--dev-control` per the dev-bridge contract).

Tankoctl smokes:

```
out\tankoctl.exe ping
out\tankoctl.exe get-state
out\tankoctl.exe open-page videos
out\tankoctl.exe open-page books
out\tankoctl.exe open-page videos
out\tankoctl.exe get-state
```

Expected:
- `ping` returns schema `tankoban.dev.v1` in <200ms.
- After `open-page videos` + `open-page books` + `open-page videos`, calling get-state should reflect activePageId=videos.
- The Back chevron should be enabled visually (no tankoctl assertion possible; verify via pywinauto-mcp UIA dump next step).

Stop Tankoban: `pwsh scripts/stop-tankoban.ps1`.

```
chat.md append:
MCP LOCK RELEASED — Agent 5 — GLOBAL_NAV_HISTORY T4 smoke
```

- [ ] **Step 10: Smoke via pywinauto-mcp (UIA-driven click on Back chevron).**

Re-claim lane + relaunch. Use pywinauto-mcp's `automation_elements` (UIA tree query) to find `TopBarBackBtn` by AutomationId. Use `automation_mouse` to click it. Confirm `get-state` returns activePageId reverts to a previous page.

If pywinauto-mcp UI surfaces a HITL gate, use `approve_automation` with a 15-min window (memory `pywinauto_HITL_gate_bypassed_via_approve_automation` documents this).

End with `MCP LOCK RELEASED` + `scripts/stop-tankoban.ps1`.

- [ ] **Step 11: Verify persistence file is written.**

After step 10's smoke, before relaunching, check `%APPDATA%\Tankoban\nav_history.json` exists with the expected JSON shape. Use Bash to cat:

```
ls "$env:APPDATA/Tankoban/nav_history.json"
```

(Use PowerShell `Get-Content` via the PowerShell tool if Bash glob expansion misbehaves on Windows paths.)

Expected: file exists, schemaVersion=1, entries array contains the navigation history from the smoke.

- [ ] **Step 12: Post RTC line in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T4 2026-05-14 ~HH:MMam — Topbar chevron Back/Forward shipped. MainWindow.h: forward-decl NavHistory + NavEntry, 3 new members (m_navHistory, m_backBtn, m_forwardBtn) + m_inNavRestore guard flag, 5 new slot decls. MainWindow.cpp: NavHistory instantiated in ctor + signals connected; buildTopBar inserts both chevrons between hamburger and brand w/ AutomationId TopBarBackBtn / TopBarForwardBtn, tooltips, accessibleName, NoFocus, disabled-initial; click slots + availability slots + entryRequested slot impl; activatePage gates recordNavEvent on !m_inNavRestore, registers active provider via setActiveProvider; closeEvent calls flushToDisk. Smoke GREEN: tankoctl ping/get-state/open-page sequences flip activePageId correctly; UIA chevron click reverts page; nav_history.json written to %APPDATA%/Tankoban with schemaVersion=1. ~150 LOC. BUILD OK first try. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/MainWindow.h, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 5: Keyboard + mouse input bindings

**Files:**
- Modify: `src/ui/MainWindow.h` (declare mousePressEvent override)
- Modify: `src/ui/MainWindow.cpp` (register QShortcuts in bindShortcuts; override mousePressEvent)

- [ ] **Step 1: Declare mousePressEvent override in MainWindow.h.**

In the `protected:` section of MainWindow.h (next to `changeEvent` / `nativeEvent`):

```cpp
    void mousePressEvent(QMouseEvent* event) override;
```

Forward-declare or include `<QMouseEvent>` as appropriate.

- [ ] **Step 2: Register Alt+Left / Alt+Right shortcuts.**

Locate `bindShortcuts()` in MainWindow.cpp. Add at the end of the method:

```cpp
    // Global Back / Forward keyboard shortcuts (spec §3.5).
    // Qt::ApplicationShortcut context so they fire from any focused
    // widget. Plain LeftArrow / RightArrow are consumed by text inputs
    // for cursor movement; Alt+arrow is free.
    auto* backShortcut = new QShortcut(QKeySequence("Alt+Left"), this);
    backShortcut->setContext(Qt::ApplicationShortcut);
    connect(backShortcut, &QShortcut::activated,
            this, &MainWindow::onBackChevronClicked);

    auto* forwardShortcut = new QShortcut(QKeySequence("Alt+Right"), this);
    forwardShortcut->setContext(Qt::ApplicationShortcut);
    connect(forwardShortcut, &QShortcut::activated,
            this, &MainWindow::onForwardChevronClicked);
```

Add `#include <QShortcut>` at the top of MainWindow.cpp if not already present.

- [ ] **Step 3: Implement mousePressEvent override.**

At the end of MainWindow.cpp (or near other event overrides):

```cpp
void MainWindow::mousePressEvent(QMouseEvent* event) {
    // Browser-style mouse thumb buttons (spec §3.6).
    if (event->button() == Qt::BackButton) {
        onBackChevronClicked();
        event->accept();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        onForwardChevronClicked();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}
```

Add `#include <QMouseEvent>` at the top of MainWindow.cpp.

- [ ] **Step 4: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 5: Smoke via pywinauto-mcp (keyboard).**

Lane lock; build_and_run; using pywinauto-mcp `automation_keyboard`, send `{ALT down}{LEFT}{ALT up}` while focus is anywhere except a text input. Verify via tankoctl `get-state` that activePageId reverted.

If the Alt+Left send is unreliable (windows-mcp punctuation-key issues per memory `feedback_windows_mcp_punctuation_keys`), use pywinauto-mcp's lower-level keyboard send instead of windows-mcp's Shortcut.

Mouse button 4/5 smoke is OPTIONAL — Hemanth's hardware availability unknown. If pywinauto-mcp can synthesize Qt::BackButton mouse events, run that. Otherwise defer to Hemanth visual.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

- [ ] **Step 6: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T5 2026-05-14 ~HH:MMam — Keyboard + mouse input bindings. MainWindow.h: protected mousePressEvent override declared. MainWindow.cpp: bindShortcuts adds 2 QShortcut instances (Alt+Left + Alt+Right, ApplicationShortcut context) wired to onBack/Forward chevron slots; mousePressEvent intercepts Qt::BackButton / Qt::ForwardButton and routes to slot. ~25 LOC. Alt+Left smoke GREEN via pywinauto-mcp keyboard send + tankoctl get-state delta. Mouse 4/5 smoke deferred to Hemanth visual. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/MainWindow.h, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 6: Reader / player overlay Back interception

**Files:**
- Modify: `src/ui/MainWindow.cpp` (add overlay-active gate to onBackChevronClicked; also update m_inReader-style state)
- Modify: `src/ui/MainWindow.h` (add isReaderOrPlayerActive() helper if needed)

Background: ComicReader / BookReader / VideoPlayer are opened via `openComicReader` / `openBookReader` / `openVideoPlayer` (MainWindow.h:110-119). Each has a corresponding `closeComicReader` / `closeBookReader` / `closeVideoPlayer`. The overlays are NOT in `m_pageStack`; they're separate widgets layered on top.

- [ ] **Step 1: Add a helper to detect overlay-active state.**

In MainWindow.h `private:`:

```cpp
    // True when ComicReader / BookReader / VideoPlayer is open on top
    // of the page stack. Used to route Back -> close-the-overlay instead
    // of pop-the-history-stack.
    bool isReaderOrPlayerActive() const;
```

In MainWindow.cpp, implement using whatever existing widget-tracking members the readers/player set. Skim `openComicReader` to find the active-tracking member (likely `m_comicReader` / `m_bookReader` / `m_videoPlayer` pointers — verify with grep):

```cpp
bool MainWindow::isReaderOrPlayerActive() const {
    // Reader/player widgets are kept around between opens; "active"
    // means visible on top of m_pageStack.
    if (m_comicReader && m_comicReader->isVisible()) return true;
    if (m_bookReader && m_bookReader->isVisible()) return true;
    if (m_videoPlayer && m_videoPlayer->isVisible()) return true;
    return false;
}
```

If the member names differ (the readers may have inline qobject_cast lookups instead), use those. Verify by grep before writing.

- [ ] **Step 2: Update onBackChevronClicked to route through overlay-close.**

```cpp
void MainWindow::onBackChevronClicked() {
    // Spec §3.10: Back closes the reader / player if one is active.
    // Reader/player are not history entries — they're modal overlays.
    if (isReaderOrPlayerActive()) {
        if (m_comicReader && m_comicReader->isVisible()) closeComicReader();
        else if (m_bookReader && m_bookReader->isVisible()) closeBookReader();
        else if (m_videoPlayer && m_videoPlayer->isVisible()) closeVideoPlayer();
        return;
    }
    if (m_navHistory) m_navHistory->back();
}
```

- [ ] **Step 3: Update onForwardChevronClicked to be a no-op when overlay is active.**

```cpp
void MainWindow::onForwardChevronClicked() {
    if (isReaderOrPlayerActive()) return;  // spec §3.10: Forward disabled in reader/player
    if (m_navHistory) m_navHistory->forward();
}
```

- [ ] **Step 4: Update availability slots to gray chevrons while overlay is active.**

We need to disable both chevrons visually while a reader/player is open, then re-enable based on NavHistory state when the overlay closes. The simplest approach: track overlay-active state and re-evaluate from the existing `openXReader` / `closeXReader` paths.

In each of `openComicReader`, `openBookReader`, `openVideoPlayer`, after the overlay becomes visible, add:

```cpp
    if (m_backBtn) m_backBtn->setEnabled(true);   // Back is enabled to close the reader
    if (m_forwardBtn) m_forwardBtn->setEnabled(false);
```

(Back stays enabled because it now means "close reader" — the gray state visually conveys "no history move available" which would be wrong; instead we leave Back enabled to communicate "Back works here too".)

In each of `closeComicReader`, `closeBookReader`, `closeVideoPlayer`, after the overlay is hidden, add:

```cpp
    if (m_navHistory && m_backBtn) m_backBtn->setEnabled(m_navHistory->canGoBack());
    if (m_navHistory && m_forwardBtn) m_forwardBtn->setEnabled(m_navHistory->canGoForward());
```

- [ ] **Step 5: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 6: Smoke — reader/player overlay Back.**

Lane lock; build_and_run.

Open a comic via UIA-driven click on a tile in Comics page (use existing pywinauto-mcp patterns from prior MCP smokes — the `automation_elements` UIA dump gives AutomationId for each tile). When ComicReader is visible, send Alt+Left and verify ComicReader closes + user lands back on Comics page.

Repeat for BookReader and VideoPlayer.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

If a reader/player smoke is intractable via MCP (e.g., VideoPlayer needs sidecar startup time), document the gap and defer that specific reader's smoke to Hemanth visual in T15.

- [ ] **Step 7: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T6 2026-05-14 ~HH:MMam — Reader/player overlay Back interception. MainWindow.h: isReaderOrPlayerActive const helper. MainWindow.cpp: helper impl checks m_comicReader/m_bookReader/m_videoPlayer visibility; onBackChevronClicked routes to closeComicReader/closeBookReader/closeVideoPlayer when overlay active; onForwardChevronClicked no-ops when overlay active; openX/closeX helpers update m_backBtn/m_forwardBtn enabled state. ~35 LOC. Smoke GREEN: ComicReader + BookReader Alt+Left close-via-Back. VideoPlayer smoke deferred to Hemanth visual (sidecar startup non-deterministic). BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/MainWindow.h, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 7: Modal dialog gating

**Files:**
- Modify: `src/ui/MainWindow.cpp` (extend the overlay-active gate to include modal dialogs)

- [ ] **Step 1: Extend onBackChevronClicked + onForwardChevronClicked to no-op while a modal dialog is open.**

Update:

```cpp
void MainWindow::onBackChevronClicked() {
    // Spec §3.12: modal dialog open -> Back is a no-op.
    if (QApplication::activeModalWidget()) return;
    // Spec §3.10: reader/player active -> Back closes overlay.
    if (isReaderOrPlayerActive()) { /* existing close-routing */ return; }
    if (m_navHistory) m_navHistory->back();
}

void MainWindow::onForwardChevronClicked() {
    if (QApplication::activeModalWidget()) return;
    if (isReaderOrPlayerActive()) return;
    if (m_navHistory) m_navHistory->forward();
}
```

Add `#include <QApplication>` if not already present (MainWindow.cpp almost certainly already has it).

- [ ] **Step 2: Disable chevrons visually while a modal is open.**

The cleanest hook is an application-level event filter that listens for `QEvent::Show` / `QEvent::Hide` on QDialog widgets. Add to MainWindow.cpp:

```cpp
// In MainWindow ctor, after NavHistory instantiation:
qApp->installEventFilter(this);
```

Add to MainWindow.h `protected:`:

```cpp
    bool eventFilter(QObject* watched, QEvent* event) override;
```

Add to MainWindow.cpp:

```cpp
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Spec §3.12: gray out chevrons while a modal dialog is on screen.
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        if (qobject_cast<QDialog*>(watched)) {
            QWidget* modal = QApplication::activeModalWidget();
            const bool inModal = (modal != nullptr);
            if (m_backBtn) {
                const bool nav = m_navHistory && m_navHistory->canGoBack();
                m_backBtn->setEnabled(nav && !inModal && !isReaderOrPlayerActive());
            }
            if (m_forwardBtn) {
                const bool nav = m_navHistory && m_navHistory->canGoForward();
                m_forwardBtn->setEnabled(nav && !inModal && !isReaderOrPlayerActive());
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
```

Add `#include <QDialog>` at the top of MainWindow.cpp.

- [ ] **Step 3: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 4: Smoke — modal dialog open + Back no-op.**

Lane lock; build_and_run.

Trigger a modal dialog (the easiest is TankorentPage's Add Torrent dialog — tankoctl `open-page tankorent`, then UIA-click the AddTorrent button if reachable). When modal is visible:

- Send Alt+Left via pywinauto-mcp keyboard. Verify modal stays open + activePageId unchanged via tankoctl get-state.
- Press Esc to close modal.
- Verify chevrons re-enable.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

- [ ] **Step 5: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T7 2026-05-14 ~HH:MMam — Modal dialog gating. MainWindow.h: protected eventFilter override. MainWindow.cpp: onBack/onForward early-return when QApplication::activeModalWidget != nullptr; qApp->installEventFilter(this) in ctor; eventFilter watches QDialog Show/Hide events and re-evaluates chevron enabled state combining NavHistory canGoX, overlay-active, modal-active. ~40 LOC. Smoke GREEN: AddTorrent modal open -> Alt+Left no-op + chevrons grayed -> Esc close -> chevrons re-enable to match nav stack state. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/MainWindow.h, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 8: ComicsPage hook (per-page INavStateProvider)

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (inherit INavStateProvider; declare 3 virtuals)
- Modify: `src/ui/pages/ComicsPage.cpp` (implement)

ComicsPage state to capture: scroll position of the tile grid, current sort, search input text, optional selected-tile id (for "back from reader scrolls to where you were").

- [ ] **Step 1: Recon — verify ComicsPage's current sort/search/scroll machinery.**

Read `src/ui/pages/ComicsPage.h` end-to-end. Note the member names for: scroll area (QScrollArea or QListView), search QLineEdit, sort enum or QComboBox, tile selection state.

If the names differ from what's assumed below, adapt the code blocks in Step 2-3. Don't make up member names.

- [ ] **Step 2: Add INavStateProvider inheritance + virtuals to ComicsPage.h.**

At the top:

```cpp
#include "../INavStateProvider.h"  // adjust relative path
```

Change class declaration:

```cpp
class ComicsPage : public QWidget, public INavStateProvider {
    Q_OBJECT
public:
    // ... existing public surface ...

    // INavStateProvider
    QJsonObject captureNavState() const override;
    bool restoreNavState(const QJsonObject& blob) override;
    QString navStateLabel() const override { return QStringLiteral("comics"); }
```

- [ ] **Step 3: Implement captureNavState in ComicsPage.cpp.**

Schema for ComicsPage blob: `{ "scrollY": int, "sort": string, "search": string }`. Adapt to actual member names from Step 1.

```cpp
QJsonObject ComicsPage::captureNavState() const {
    QJsonObject blob;
    if (auto* scroll = findChild<QScrollArea*>()) {
        if (scroll->verticalScrollBar()) {
            blob["scrollY"] = scroll->verticalScrollBar()->value();
        }
    }
    // Adapt to actual sort + search members:
    // blob["sort"] = m_sortCombo ? m_sortCombo->currentData().toString() : "";
    // blob["search"] = m_searchEdit ? m_searchEdit->text() : "";
    return blob;
}
```

- [ ] **Step 4: Implement restoreNavState in ComicsPage.cpp.**

```cpp
bool ComicsPage::restoreNavState(const QJsonObject& blob) {
    // Apply state in this order: search first (filters the visible list),
    // then sort (re-orders), then scroll (lands where the user was).
    // const QString search = blob.value("search").toString();
    // if (m_searchEdit) m_searchEdit->setText(search);
    //
    // const QString sort = blob.value("sort").toString();
    // if (m_sortCombo && !sort.isEmpty()) {
    //     int idx = m_sortCombo->findData(sort);
    //     if (idx >= 0) m_sortCombo->setCurrentIndex(idx);
    // }
    //
    if (auto* scroll = findChild<QScrollArea*>()) {
        if (scroll->verticalScrollBar()) {
            scroll->verticalScrollBar()->setValue(blob.value("scrollY").toInt(0));
        }
    }
    return true;  // ComicsPage doesn't deal in stale targets — always restores
}
```

Adapt the commented blocks to ComicsPage's real members.

- [ ] **Step 5: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 6: Smoke via tankoctl + pywinauto-mcp.**

Lane lock; build_and_run.

Sequence:
1. `tankoctl open-page comics`.
2. Scroll Comics page (use windows-mcp Scroll or pywinauto-mcp). Type a query in search.
3. Click a comic tile to open ComicReader.
4. Close ComicReader (Esc or close-X).
5. `tankoctl get-state` — verify activePageId=comics.
6. Visual: confirm scroll position and search text restored.
7. Click Back chevron — should leave Comics for a previous page.
8. Click Forward chevron — should return to Comics with state restored.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

If pywinauto-mcp can't reliably reach the scrollbar position assertion, accept the visual smoke (a Hemanth pass) as sufficient for this slice.

- [ ] **Step 7: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T8 2026-05-14 ~HH:MMam — ComicsPage INavStateProvider hook. ComicsPage.h: include INavStateProvider.h + multi-inherit + 3 override decls. ComicsPage.cpp: captureNavState writes {scrollY, sort, search}; restoreNavState applies in order search->sort->scroll; returns true (page has no stale-target case). ~40 LOC. Smoke GREEN: scroll + search persisted across detail-open + back-navigation. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

### Task 9: BooksPage hook (per-page INavStateProvider)

**Files:**
- Modify: `src/ui/pages/BooksPage.h`
- Modify: `src/ui/pages/BooksPage.cpp`

- [ ] **Step 1: Recon BooksPage members.** Same as Task 8 Step 1, for BooksPage.

- [ ] **Step 2: Add INavStateProvider inheritance to BooksPage.h.** Mirror Task 8 Step 2, replacing ComicsPage with BooksPage and `"comics"` with `"books"`.

- [ ] **Step 3: Implement captureNavState in BooksPage.cpp.** Mirror Task 8 Step 3. Blob schema: `{ "scrollY", "sort", "search" }`.

- [ ] **Step 4: Implement restoreNavState in BooksPage.cpp.** Mirror Task 8 Step 4.

- [ ] **Step 5: Verify build.** `build_check.bat` → BUILD OK.

- [ ] **Step 6: Smoke.** Mirror Task 8 Step 6, replacing comics paths with books paths.

- [ ] **Step 7: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T9 2026-05-14 ~HH:MMam — BooksPage INavStateProvider hook (mirrors T8 shape). ~40 LOC. Smoke GREEN. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/BooksPage.h, src/ui/pages/BooksPage.cpp, agents/chat.md
```

---

### Task 10: VideosPage + ShowView hook

**Files:**
- Modify: `src/ui/pages/VideosPage.h`
- Modify: `src/ui/pages/VideosPage.cpp`
- Modify: `src/ui/pages/ShowView.h` (to expose its current state to VideosPage's capture)
- Modify: `src/ui/pages/ShowView.cpp`

VideosPage has two views: library (tile grid) + ShowView (show detail). Both contribute to the blob; the blob's `view` field distinguishes them.

- [ ] **Step 1: Recon VideosPage + ShowView state machinery.**

Read both files. Identify:
- VideosPage scrollarea, sort combo, filter (Movies/TV/Anime), search input.
- ShowView's selected show id, selected season tab index, selected episode (if any persisting).
- The transition mechanism: how does VideosPage switch between library and ShowView (likely a QStackedWidget internal to VideosPage)? Look for `m_innerStack` or similar.

- [ ] **Step 2: Add INavStateProvider inheritance to VideosPage.h.**

Same shape as Task 8 Step 2. `navStateLabel` returns `"videos"`.

- [ ] **Step 3: Expose ShowView state-snapshot helpers.**

In `ShowView.h` `public:`:

```cpp
    // Used by VideosPage::captureNavState to bundle ShowView state into
    // the page-level blob.
    QJsonObject snapshotState() const;
    bool restoreFromSnapshot(const QJsonObject& blob);
```

In `ShowView.cpp`:

```cpp
QJsonObject ShowView::snapshotState() const {
    QJsonObject blob;
    blob["showId"] = m_currentShowId;  // adjust member name
    blob["seasonIndex"] = m_currentSeasonIndex;  // adjust member name
    if (auto* scroll = findChild<QScrollArea*>()) {
        if (scroll->verticalScrollBar()) {
            blob["scrollY"] = scroll->verticalScrollBar()->value();
        }
    }
    return blob;
}

bool ShowView::restoreFromSnapshot(const QJsonObject& blob) {
    const QString showId = blob.value("showId").toString();
    if (showId.isEmpty()) return false;
    // Re-open the show. If the show no longer exists, return false
    // so NavHistory drops this entry.
    if (!openShowById(showId)) return false;  // adapt to real ShowView API
    const int seasonIdx = blob.value("seasonIndex").toInt(0);
    setSeasonIndex(seasonIdx);  // adapt
    if (auto* scroll = findChild<QScrollArea*>()) {
        if (scroll->verticalScrollBar()) {
            scroll->verticalScrollBar()->setValue(blob.value("scrollY").toInt(0));
        }
    }
    return true;
}
```

Adapt to actual ShowView API.

- [ ] **Step 4: Implement VideosPage captureNavState.**

```cpp
QJsonObject VideosPage::captureNavState() const {
    QJsonObject blob;
    // Determine which subview is active.
    const bool inDetail = m_innerStack && m_innerStack->currentWidget() == m_showView;
    blob["view"] = inDetail ? "detail" : "library";
    if (inDetail && m_showView) {
        blob["showState"] = m_showView->snapshotState();
    } else {
        if (auto* scroll = findChild<QScrollArea*>()) {
            if (scroll->verticalScrollBar()) {
                blob["scrollY"] = scroll->verticalScrollBar()->value();
            }
        }
        // sort + filter + search — adapt to real members
        // blob["sort"] = ...
        // blob["filter"] = ...
        // blob["search"] = ...
    }
    return blob;
}
```

- [ ] **Step 5: Implement VideosPage restoreNavState.**

```cpp
bool VideosPage::restoreNavState(const QJsonObject& blob) {
    const QString view = blob.value("view").toString();
    if (view == "detail") {
        if (!m_showView) return false;
        const bool ok = m_showView->restoreFromSnapshot(blob.value("showState").toObject());
        if (!ok) return false;
        if (m_innerStack) m_innerStack->setCurrentWidget(m_showView);
        return true;
    }
    // Library view.
    if (m_innerStack) m_innerStack->setCurrentWidget(/* m_libraryWidget */);
    // Apply search + sort + filter — adapt to real members.
    if (auto* scroll = findChild<QScrollArea*>()) {
        if (scroll->verticalScrollBar()) {
            scroll->verticalScrollBar()->setValue(blob.value("scrollY").toInt(0));
        }
    }
    return true;
}
```

- [ ] **Step 6: Add a navigationRequested signal from VideosPage on library->detail transition.**

VideosPage opens ShowView when a tile is clicked. Find the click handler (likely in `installFolderTileContextMenu` or similar). The "open show detail" path needs to notify MainWindow of a nav event.

In VideosPage.h `signals:`:

```cpp
    // Emitted when the user opens a show detail (library -> detail
    // transition counts as a global nav event per spec §3.1).
    void navigationRequested();
```

In the open-show-detail code path:

```cpp
    // ... after the page switches to ShowView ...
    emit navigationRequested();
```

In MainWindow.cpp, connect VideosPage's navigationRequested to a slot that calls `m_navHistory->recordNavEvent("videos")`. Locate where VideosPage is constructed (`m_videosPage = new VideosPage(...)` in `buildPageStack`). After construction:

```cpp
    connect(m_videosPage, &VideosPage::navigationRequested,
            this, [this]() {
                if (m_navHistory) m_navHistory->recordNavEvent("videos");
            });
```

- [ ] **Step 7: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`.

- [ ] **Step 8: Smoke.**

Lane lock; build_and_run.

1. tankoctl `open-page videos` + `scan-videos`.
2. Wait for tiles. tankoctl `get-videos 10` to confirm tiles loaded.
3. UIA-click a show tile (find AutomationId via `automation_elements`). ShowView opens.
4. Click a season tab (sub-view, not a nav event).
5. tankoctl `get-state` — confirm activePageId=videos. NavHistory now has [...prior][videos library][videos detail with showState].
6. Click Back chevron. ShowView should reverse to library view, scroll position restored.
7. Click Forward chevron. ShowView re-opens with the same season tab + scroll.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

- [ ] **Step 9: Post RTC line.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T10 2026-05-14 ~HH:MMam — VideosPage + ShowView INavStateProvider hook. VideosPage.h inherits INavStateProvider; signals navigationRequested. ShowView.h: snapshotState/restoreFromSnapshot pair. VideosPage.cpp: captureNavState distinguishes view=library|detail; restoreNavState routes to either library or ShowView::restoreFromSnapshot (which can return false on deleted-show). ShowView.cpp: snapshotState writes {showId, seasonIndex, scrollY}; restoreFromSnapshot re-opens show by id + sets season + scroll. MainWindow.cpp: connect VideosPage::navigationRequested -> NavHistory::recordNavEvent("videos"). ~110 LOC. Smoke GREEN: library scroll/sort/filter + show detail season-tab + scroll all restored on Back/Forward. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/VideosPage.h, src/ui/pages/VideosPage.cpp, src/ui/pages/ShowView.h, src/ui/pages/ShowView.cpp, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 11: TankorentPage hook

**Files:**
- Modify: `src/ui/pages/TankorentPage.h`
- Modify: `src/ui/pages/TankorentPage.cpp`
- Modify: `src/ui/MainWindow.cpp` (connect navigationRequested if Tankorent has detail-open transitions)

Chat.md heads-up to Agent 4B before starting: TankorentPage is Agent 4B's domain. Per spec §3.8, the UI hook is Agent 5's; engine/scraper code is NOT touched.

- [ ] **Step 1: Heads-up to Agent 4B in chat.md.**

Append to `agents/chat.md`:

```
NOTE — Agent 5 -> Agent 4B: starting TankorentPage INavStateProvider hook now (GLOBAL_NAV_HISTORY T11). UI-only — touching TankorentPage.{h,cpp} to add captureNavState/restoreNavState/navStateLabel. Engine, scraper, TorrentEngine, TorrentClient untouched. Will RTC when done. Per spec §3.8 Agent 5 owns UI; deferring to you if engine code becomes needed.
```

- [ ] **Step 2: Recon TankorentPage state.** Search input, filter chips, scroll, optional detail view (if Tankorent has a torrent-detail subview that counts as a separate nav event — verify).

- [ ] **Step 3: Add INavStateProvider inheritance.** Mirror Task 8 Step 2. `navStateLabel` returns `"tankorent"`.

- [ ] **Step 4: Implement captureNavState.** Blob: `{ "scrollY", "search", "filter" }` plus a "view" tag if detail subview exists.

- [ ] **Step 5: Implement restoreNavState.**

- [ ] **Step 6: Wire navigationRequested signal if applicable.**

- [ ] **Step 7: Build verify.** `build_check.bat` → BUILD OK.

- [ ] **Step 8: Smoke.** Lane lock + build_and_run + tankoctl + UIA. Verify search + scroll + filter restore.

- [ ] **Step 9: Post RTC + heads-up reply in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T11 2026-05-14 ~HH:MMam — TankorentPage INavStateProvider hook. UI-only per spec §3.8 — engine/scraper untouched. ~50 LOC. Smoke GREEN. Agent 4B heads-up posted before start. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/TankorentPage.h, src/ui/pages/TankorentPage.cpp, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 12: TankoLibraryPage hook

**Files:**
- Modify: `src/ui/pages/tankolibrary/TankoLibraryPage.h`
- Modify: `src/ui/pages/tankolibrary/TankoLibraryPage.cpp`

Chat.md heads-up to Agent 4B before starting (mirrors Task 11 Step 1).

- [ ] **Step 1: Heads-up to Agent 4B in chat.md.**
- [ ] **Step 2: Recon TankoLibraryPage state.** Search input, source filter, scroll, optional book detail subview.
- [ ] **Step 3: Add INavStateProvider inheritance.** `navStateLabel` returns `"tankolibrary"`.
- [ ] **Step 4: Implement captureNavState.**
- [ ] **Step 5: Implement restoreNavState.**
- [ ] **Step 6: Wire navigationRequested signal if applicable.**
- [ ] **Step 7: Build verify.**
- [ ] **Step 8: Smoke.**
- [ ] **Step 9: Post RTC + heads-up reply in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T12 2026-05-14 ~HH:MMam — TankoLibraryPage INavStateProvider hook. UI-only per spec §3.8 — source plugin code untouched. ~50 LOC. Smoke GREEN. Agent 4B heads-up posted. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankolibrary/TankoLibraryPage.h, src/ui/pages/tankolibrary/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 13: TankoyomiPage + MangaDetailView hook

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.h`
- Modify: `src/ui/pages/TankoyomiPage.cpp`
- Modify: MangaDetailView header + cpp (location: search `src/ui/pages/tankoyomi/` or `src/ui/pages/`)

Chat.md heads-up to Agent 4B before starting. **Recent activity flag:** Agent 4B is currently very active on TANKOYOMI_MIHON_OVERHAUL (4 Hemanth-smoke fixes shipped 2026-05-13). Verify chat.md tail for any active lane before starting.

- [ ] **Step 1: Verify Agent 4B is not mid-flight on Tankoyomi. Read chat.md tail.**

If Agent 4B has an active MCP LOCK or in-progress RTC on TankoyomiPage / MangaDetailView, pause and ask in chat.md for a clear lane.

- [ ] **Step 2: Heads-up to Agent 4B in chat.md.**

- [ ] **Step 3: Recon TankoyomiPage + MangaDetailView state.**

TankoyomiPage has library/search view + MangaDetailView (manga detail). Verify the inner-stack pattern (likely mirrors VideosPage). State: search input, source filter, scroll, selected manga id (for MangaDetailView).

- [ ] **Step 4: Add INavStateProvider inheritance to TankoyomiPage.** `navStateLabel` returns `"tankoyomi"`. Blob structured `{ view: "search"|"detail", ... }` mirroring VideosPage.

- [ ] **Step 5: Expose MangaDetailView snapshotState/restoreFromSnapshot pair.** Mirror ShowView from Task 10 Step 3.

- [ ] **Step 6: Implement TankoyomiPage captureNavState + restoreNavState.** Mirror Task 10 Steps 4-5 for the dual-view structure.

- [ ] **Step 7: Wire navigationRequested signal on tile-click.**

- [ ] **Step 8: Build verify.**

- [ ] **Step 9: Smoke.**

- [ ] **Step 10: Post RTC + heads-up reply.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T13 2026-05-14 ~HH:MMam — TankoyomiPage + MangaDetailView INavStateProvider hook (dual-view shape mirrors VideosPage+ShowView from T10). UI-only per spec §3.8 — scraper, MangaDownloader, source plugins untouched. ~110 LOC. Smoke GREEN. Agent 4B heads-up posted; lane verified clear before start. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/TankoyomiPage.h, src/ui/pages/TankoyomiPage.cpp, src/ui/pages/tankoyomi/MangaDetailView.h, src/ui/pages/tankoyomi/MangaDetailView.cpp, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 14: StreamPage refactor (heaviest slice)

**Files:**
- Modify: `src/ui/pages/StreamPage.h`
- Modify: `src/ui/pages/StreamPage.cpp`
- Modify: `src/ui/pages/stream/StreamDetailView.h`
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`
- Modify: `src/ui/MainWindow.cpp` (signal connect)

**Critical:** chat.md heads-up to Agent 4 BEFORE any edit. StreamPage shipped 10-phase STREAM_DOWNLOADS_NETFLIX_OVERHAUL on 2026-05-13 + CW_NAMESPACE_BOUNDARY fix; the m_navStack / NavEntry / showEntryRaw machinery is recently active.

- [ ] **Step 1: Heads-up to Agent 4 in chat.md.**

```
NOTE — Agent 5 -> Agent 4: starting StreamPage INavStateProvider refactor now (GLOBAL_NAV_HISTORY T14). Spec docs/superpowers/specs/2026-05-14-global-nav-history-design.md §4.4 — retire m_navStack as a navigation mechanism but RETAIN the NavEntry struct + showEntryRaw dispatcher as render machinery driven by the global controller. m_beforePlayerEntry preserved. P6 NavEntry::Detail openShowDetail path preserved. goBack removed. Engine, sidecar, SubtitlesAggregator, BulkPlan untouched — ONLY navigation glue. Will RTC when done. Per spec §3.8 Agent 5 owns UI; if engine code becomes needed, will pause and HELP-request.
```

- [ ] **Step 2: Read StreamPage.h + StreamPage.cpp in full.**

Locate every usage of `m_navStack`, `NavEntry`, `showEntryRaw`, `goBack`. Map every call site. Take notes so the refactor doesn't drop any active flow.

- [ ] **Step 3: Add INavStateProvider inheritance to StreamPage.h.**

```cpp
#include "../INavStateProvider.h"

class StreamPage : public QWidget, public INavStateProvider {
    Q_OBJECT
public:
    // ... existing public surface ...

    // INavStateProvider
    QJsonObject captureNavState() const override;
    bool restoreNavState(const QJsonObject& blob) override;
    QString navStateLabel() const override { return QStringLiteral("stream"); }

signals:
    // Emitted on user-initiated within-page navigation (library -> detail,
    // or detail -> different detail). MainWindow's slot calls
    // NavHistory::recordNavEvent("stream") which captures the current
    // blob into the previous entry and pushes a fresh one.
    void navigationRequested();
```

- [ ] **Step 4: Implement captureNavState in StreamPage.cpp.**

Translate the existing `NavEntry` struct into a QJsonObject. The blob's `view` field distinguishes Library / Detail / Search. Other fields mirror NavEntry's payload (showId, stremioId, etc.).

```cpp
QJsonObject StreamPage::captureNavState() const {
    QJsonObject blob;
    // Use the current internal state machine to determine the view.
    // Map from StreamPage's internal NavEntry::Kind enum to the blob's
    // "view" string.
    blob["view"] = "library";  // default
    // if (m_currentView == NavEntry::Kind::Detail) {
    //     blob["view"] = "detail";
    //     blob["showId"] = m_currentShowId;
    //     blob["stremioId"] = m_currentStremioId;
    //     blob["season"] = m_currentSeason;
    // }
    // (Adapt to real internal state members.)
    return blob;
}
```

- [ ] **Step 5: Implement restoreNavState in StreamPage.cpp.**

Translate the blob back into a `NavEntry`, then call `showEntryRaw(entry)` (the existing render dispatcher) to reproduce the view.

```cpp
bool StreamPage::restoreNavState(const QJsonObject& blob) {
    const QString view = blob.value("view").toString();
    if (view == "detail") {
        NavEntry entry;
        entry.kind = NavEntry::Kind::Detail;
        entry.showId = blob.value("showId").toString();
        entry.stremioId = blob.value("stremioId").toString();
        entry.season = blob.value("season").toInt(1);
        if (entry.showId.isEmpty()) return false;  // stale
        showEntryRaw(entry);
        return true;
    }
    if (view == "library") {
        NavEntry entry;
        entry.kind = NavEntry::Kind::Library;
        showEntryRaw(entry);
        return true;
    }
    // Unknown view tag — stale.
    return false;
}
```

- [ ] **Step 6: Emit navigationRequested at all user-initiated nav transitions inside StreamPage.**

Find every push to `m_navStack` in StreamPage.cpp (e.g., `openShowDetail` pushes a Detail entry). Replace each push site with:

```cpp
    emit navigationRequested();
    // ... existing render dispatch via showEntryRaw or equivalent ...
```

(The push to m_navStack itself is removed — the global controller is the single source of truth now.)

- [ ] **Step 7: Remove goBack and any in-page Back affordance.**

In StreamPage.h, remove `void goBack()` (or whatever the existing back method is). In StreamPage.cpp, remove its implementation. Find any QPushButton labeled "Back" inside StreamPage's UI (if it exists from the m_navStack era) and remove it.

- [ ] **Step 8: Preserve m_beforePlayerEntry path.**

The VideoPlayer close-handler in StreamPage uses `m_beforePlayerEntry` to restore Stream state after the player closes. This is INDEPENDENT of the global nav stack — it's a "player closed, restore what was on Stream before the player opened" hook. Keep it. If the player-close path was calling `goBack()`, replace with a direct `showEntryRaw(m_beforePlayerEntry.value())` call.

- [ ] **Step 9: Connect navigationRequested in MainWindow.cpp.**

Find where StreamPage is instantiated in `buildPageStack` (`m_streamPage = new StreamPage(...)`). After construction:

```cpp
    connect(m_streamPage, &StreamPage::navigationRequested,
            this, [this]() {
                if (m_navHistory) m_navHistory->recordNavEvent("stream");
            });
```

- [ ] **Step 10: Verify build.**

Run: `build_check.bat`

Expected: `BUILD OK`. This task touches recently-shipped code, so a clean build is the bare minimum.

If FAIL: log the failure mode in chat.md immediately so Agent 4 can review.

- [ ] **Step 11: Smoke — preserve all P6 behavior + add new Forward path.**

Lane lock; build_and_run.

Smoke matrix:
1. `tankoctl open-page stream`. Stream library opens.
2. UIA-click a show tile. Stream detail opens. Verify show data renders.
3. Alt+Left (global Back). Stream library should restore.
4. Alt+Right (global Forward). Stream detail should re-open with same show context.
5. From detail, click a different show via sidebar/related — verify navigationRequested fires for second nav event.
6. Click an episode. VideoPlayer opens.
7. Close VideoPlayer (Esc). m_beforePlayerEntry path should restore Stream detail (P6 behavior preserved).
8. Alt+Left. Should leave Stream for previous page (if global stack has one).
9. Cross-page test: open Comics, then Stream, then a show detail. Back x2 should land at Comics.

If any step regresses P6 behavior, halt and post the failure in chat.md to Agent 4.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

- [ ] **Step 12: Post RTC + Agent 4 ack in chat.md.**

```
READY TO COMMIT - [Agent 5, GLOBAL_NAV_HISTORY T14 2026-05-14 ~HH:MMam — StreamPage + StreamDetailView refactored: m_navStack retired as navigation mechanism; NavEntry struct + showEntryRaw dispatcher retained as render machinery driven by restoreNavState. goBack removed. m_beforePlayerEntry path preserved (VideoPlayer close still restores Stream detail). navigationRequested signal added at all push sites; connected in MainWindow to NavHistory::recordNavEvent("stream"). captureNavState + restoreNavState round-trip Library/Detail/Search blobs via showEntryRaw. ~200 LOC churn. Smoke GREEN end-to-end: P6 show-detail open + back/forward + episode -> player -> close -> detail-restore all working; cross-page global back from Stream detail works. Agent 4 heads-up posted before edit; ack pending. BUILD OK. Skills invoked: [/superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/StreamPage.h, src/ui/pages/StreamPage.cpp, src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp, src/ui/MainWindow.cpp, agents/chat.md
```

---

### Task 15: End-to-end smoke + Hemanth visual + close-out

**Files:** None (verification + chat.md note).

- [ ] **Step 1: Run repo-health to confirm no drift.**

Run: `pwsh -NoProfile -File scripts/repo-health.ps1`

Expected: clean output. Any new tracked-generated-file warning or large-source-file warning should be investigated.

- [ ] **Step 2: Verify zero-residue grep on removed StreamPage internals.**

Confirm `goBack` no longer exists in StreamPage.cpp / StreamPage.h. Confirm no caller in src/ tries to invoke a `StreamPage::goBack`.

- [ ] **Step 3: End-to-end MCP smoke matrix.**

Lane lock + build_and_run + go through every nav-eligible surface in one session:

1. Comics -> tile -> reader -> Alt+Left -> reader closes -> Alt+Left -> previous page.
2. Books -> tile -> reader -> close -> back/forward.
3. Videos -> show detail -> season tab -> back -> forward (verify season tab restored).
4. Stream -> show detail -> back -> forward (verify detail restored).
5. Tankoyomi -> manga detail -> back -> forward.
6. Tankorent -> AddTorrent modal -> Alt+Left no-op -> Esc -> back chevron works again.
7. Persistence: navigate Comics -> Stream -> Tankoyomi. Quit app via close button. Relaunch. Verify nav_history.json contains the trail; Back chevron enabled; Alt+Left walks back through the stack from before quit.
8. Mouse 4/5 (if Hemanth's hardware supports it — Hemanth visual). Defer to Hemanth.
9. Stack cap: rapid-fire 105+ nav events; verify oldest evicted via inspecting nav_history.json.

End: MCP LOCK RELEASED + stop-tankoban.ps1.

- [ ] **Step 4: Final chat.md status update + Hemanth ask.**

```
GLOBAL_NAV_HISTORY arc complete pending Hemanth visual — Agent 5 — 2026-05-14 ~HH:MMam.

All 14 substrate + per-page slices shipped, build GREEN throughout, MCP smoke matrix GREEN across all 9 scenarios (mouse 4/5 deferred to Hemanth hardware). Persistence file confirmed at %APPDATA%/Tankoban/nav_history.json with schemaVersion=1.

Hemanth smoke spec:
1. Launch via build_and_run.bat. Topbar should show [hamburger][<][>][Tankoban]. Both chevrons grayed at first launch.
2. Click around — Comics, Stream, a show detail. Back chevron enables. Click it; expect previous page.
3. Try Alt+Left + Alt+Right.
4. If your mouse has thumb buttons, try them.
5. Open a comic / book / video. Press Back. Expect the reader/player to close.
6. Open Add Torrent dialog from Tankorent. Try Back. Expect nothing to happen.
7. Quit the app mid-history. Relaunch. Expect Back to walk back through your previous session's nav.

Phase 3 close-out pending Hemanth verdict. /commit-sweep can land all 14 RTCs (T1-T14) once smoke clean.
```

- [ ] **Step 5: Hand off to Hemanth.**

Stop here. Hemanth visual smoke is the only remaining gate. If he flags any regression, iterate in a follow-up wake. If he says it works, Agent 0 sweeps the 14 RTCs.

---

## Self-Review

**1. Spec coverage:**
- §3.1 granularity (page + detail) -> Task 4 records on activatePage + Task 10/13/14 record library->detail transitions via navigationRequested signal. ✓
- §3.2 restore depth (everything) -> Task 8-13 each page captures + restores scroll/sort/filter/search/sub-view as appropriate. ✓
- §3.3 Stream interplay (unified) -> Task 14. ✓
- §3.4 persistence -> Task 3 + Task 4 Step 7. ✓
- §3.5 keyboard -> Task 5 Step 2. ✓
- §3.6 mouse 4/5 -> Task 5 Step 3. ✓
- §3.7 stack cap (100) -> Task 1 (kMaxEntries) + Task 2 (eviction in recordNavEvent). ✓
- §3.8 coordination -> Task 11 / 12 / 13 / 14 heads-ups + RTC handoffs to Agent 4 / 4B. ✓
- §3.9 placement -> Task 4 Step 3. ✓
- §3.10 reader/player Back -> Task 6. ✓
- §3.11 truncate forward -> Task 2 Step 3 (recordNavEvent truncates m_stack to cursor+1 before push). ✓
- §3.12 modal dialog Back -> Task 7. ✓
- §3.13 Rule-14 bake-ins (startup, accessibility, disabled-state, lazy capture, no history dropdown, no Refresh, brand unchanged) -> Task 4 Step 3 (accessibility, initial disabled), Task 2 (lazy capture in recordNavEvent), implicit (no Refresh / no dropdown). ✓
- §4.4 StreamPage stack retired + showEntryRaw retained -> Task 14 Steps 5-7. ✓
- §4.5 persistence schema (`{schemaVersion, cursor, entries}`) -> Task 3 Step 3. ✓
- §6 visual spec (QSS, sizes, tooltips) -> Task 4 Step 3 (everything except the QSS scope file). QSS is currently inline-set via setObjectName; a separate Theme.cpp pass for #TopBarBackBtn:hover/disabled rules is implicit. **GAP: explicit QSS hover/disabled rules not in any task — but Tankoban's existing QPushButton:disabled rules already handle the visual dim (opacity inheritance + Theme.cpp's button base styling). Adding explicit rules is optional polish; leave to a follow-up wake if Hemanth's visual smoke flags it.**

**2. Placeholder scan:**
- "adapt to real member names" comments in Task 8/9/10/11/12/13 are intentional — the agent executing each task MUST do the recon step before writing the capture/restore code. These are NOT TODOs that ship in the final code; they're scaffolds the executor fills in based on the actual page members. The plan instructs to "adapt" — that's the agent's job, not a placeholder. Acceptable.
- Task 6 Step 1 / Task 14 Step 6: "adapt to real" instructions. Same justification.

**3. Type consistency:**
- `NavEntry` defined in Task 1 with fields `{pageId, stateBlob, timestampMs}` — used consistently across Task 2 (recordNavEvent), Task 3 (persistence), Task 4 (slot signatures), Task 14 (Stream NavEntry struct is internal to StreamPage, distinct, called out in Step 5). ✓
- `INavStateProvider` virtuals: `captureNavState`, `restoreNavState`, `navStateLabel` — consistent across Tasks 1, 8, 9, 10, 11, 12, 13, 14. ✓
- `kMaxEntries = 100`, `kSchemaVersion = 1` — defined in Task 1, used in Task 2 + Task 3 consistently. ✓
- `m_navHistory`, `m_backBtn`, `m_forwardBtn`, `m_inNavRestore` — defined Task 4 Step 1, used consistently. ✓

**4. Ambiguity check:**
- Task 4 Step 6 introduces `m_inNavRestore` guard. Task 14 + per-page tasks don't explicitly mention it, but they shouldn't need to — only MainWindow's onNavEntryRequested manipulates it. ✓
- Task 2 Step 4 (back/forward skip-stale): explicitly notes the recursive skip is deferred to MainWindow's slot, not in NavHistory itself. Task 4 Step 4 acknowledges this with a TODO referencing T8+. This is the cleanest place to add stale-skip later; if a future Hemanth flag complains the chevrons get stuck on stale entries, add the skip logic to onNavEntryRequested in a follow-up task. **Flagged as a follow-up if observed during Hemanth's visual smoke.**

No further inline fixes needed. Plan ready for execution.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-14-global-nav-history.md`. Two execution options:

**1. Subagent-Driven (recommended)** — Agent 5 dispatches a fresh subagent per task, reviews between tasks, fast iteration. Each task is one commit (Rule 11 RTC line; Agent 0 sweeps).

**2. Inline Execution** — Agent 5 executes tasks in this session using executing-plans, batch execution with checkpoints for Hemanth review.

Phase 3 is a separate Hemanth fire per his Phase 1 prompt. Awaiting his choice + summon.
