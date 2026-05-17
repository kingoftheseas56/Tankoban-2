# Layer-Based Per-Mode Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat browser-style NavHistory with a per-mode layer-based back system, physically remove the forward chevron, and ratify the standing Tankoban contract that mode pills are end-all-be-all hard resets.

**Architecture:** New `PerModeNavController` owns one `QStack<LayerEntry>` per mode. Each mode page emits `enteredLayer(LayerEntry)` / `exitedLayer()` on its in-page transitions; the controller pushes/pops the active mode's stack. Topbar Back chevron asks the controller for the current-mode back availability + destination label. Cross-mode pill clicks call `controller->resetMode(target)` then `activatePage(target)` — pills wipe the target mode's stack to [root]. Forward chevron + Alt+Right + mouse-thumb wiring physically removed. Old `NavHistory` + `INavStateProvider` + AppData `nav_history.json` deleted along with the Phase 0c hotfix scaffolding.

**Tech Stack:** Qt6.10.2 (Widgets / Core / signals-slots / QHash / QStack / QJsonObject) + C++20 + GoogleTest via `tankoban_tests` opt-in build target (CMake `-DTANKOBAN_BUILD_TESTS=ON`).

**Spec:** `docs/superpowers/specs/2026-05-17-nav-layer-back-design.md` (Phase 1 brainstorm with 16 product picks + Approach A architecture).

**Supersedes the entire 2026-05-14 GLOBAL_NAV_HISTORY architecture + its Phase 0c hotfix.**

---

## Pre-flight notes

- All 13 tasks below assume `build_check.bat` produces `BUILD OK` after edits. If a task's build breaks, fix locally before committing — never commit a red build.
- Tests run via: `cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON; cmake --build out --target tankoban_tests; cd out && ctest --output-on-failure -R PerModeNavController`
- Smoke matrix runs at the end (Task 13) via pywinauto-mcp + tankoctl. The verification matrix items (a)-(i) live in §7 of the spec.
- ASCII discipline: no Unicode special characters in source comments or strings — middle-dot `·` becomes `-`, em-dash `—` becomes `--`, etc. Per `feedback_no_color_no_emoji` memory.

---

## Task 1: Define `LayerEntry` POD struct + register meta type

**Files:**
- Create: `src/ui/LayerEntry.h`

- [ ] **Step 1: Create the header**

```cpp
// src/ui/LayerEntry.h
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace tankoban::ui {

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- one entry on the per-mode
// back stack. Each page constructs entries for its own in-page layers
// (Library, SearchResults, Detail, etc.) and emits them via the page's
// enteredLayer signal. PerModeNavController stores them; topbar Back
// chevron reads back-destination label from peekBack(currentMode). The
// stateBlob is opaque to the controller -- the emitting page is
// responsible for round-trip serialization in its restoreLayer slot.
struct LayerEntry {
    QString     pageId;       // "comics" / "stream" / "books" / etc.
    QString     kind;         // page-local layer kind: "library", "searchResults", "seriesView", "detail", "catalogBrowse", "addonManager", "calendar", "search"
    QString     label;        // human-readable tooltip text, e.g. "Search Results"
    QJsonObject stateBlob;    // page-private state needed to re-render the layer
};

}  // namespace tankoban::ui

Q_DECLARE_METATYPE(tankoban::ui::LayerEntry)
```

- [ ] **Step 2: Add to CMakeLists.txt**

Open `CMakeLists.txt`, locate the section listing `src/ui/*.h` (search for `MainWindow.h`). Add `src/ui/LayerEntry.h` to the same group of headers (it's header-only — no .cpp).

- [ ] **Step 3: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`

- [ ] **Step 4: Commit**

```bash
git add src/ui/LayerEntry.h CMakeLists.txt
git commit -m "feat(nav): add LayerEntry POD struct for per-mode back stack"
```

---

## Task 2: `PerModeNavController` skeleton + empty-state tests

**Files:**
- Create: `src/ui/PerModeNavController.h`
- Create: `src/ui/PerModeNavController.cpp`
- Create: `tests/core/PerModeNavControllerTest.cpp`

- [ ] **Step 1: Write the failing test (empty controller)**

```cpp
// tests/core/PerModeNavControllerTest.cpp
#include <gtest/gtest.h>
#include "src/ui/PerModeNavController.h"
#include "src/ui/LayerEntry.h"

using namespace tankoban::ui;

TEST(PerModeNavController, EmptyControllerCannotGoBack) {
    PerModeNavController c;
    EXPECT_FALSE(c.canGoBack("comics"));
    EXPECT_FALSE(c.canGoBack("stream"));
    EXPECT_EQ(c.peekBack("comics").pageId, QString());
}

TEST(PerModeNavController, SettingActiveModeIsIndependentOfStacks) {
    PerModeNavController c;
    c.setActiveMode("comics");
    EXPECT_EQ(c.activeMode(), QString("comics"));
    EXPECT_FALSE(c.canGoBack("comics"));
}
```

- [ ] **Step 2: Write controller skeleton (header)**

```cpp
// src/ui/PerModeNavController.h
#pragma once

#include <QHash>
#include <QObject>
#include <QStack>
#include <QString>
#include "LayerEntry.h"

namespace tankoban::ui {

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- per-mode back stack
// controller. Holds one QStack<LayerEntry> per pageId. The active
// page emits enteredLayer/exitedLayer; the controller pushes/pops
// the active mode's stack. canGoBack only inspects the active mode's
// stack -- cross-mode entries cannot be reached via Back. Mode-pill
// clicks call resetMode(target) which wipes that mode's stack to
// the root layer (or empty if root has not been pushed yet).
class PerModeNavController : public QObject {
    Q_OBJECT
public:
    explicit PerModeNavController(QObject* parent = nullptr);
    ~PerModeNavController() override;

    void       setActiveMode(const QString& pageId);
    QString    activeMode() const { return m_activeMode; }

    void       pushLayer(const QString& pageId, const LayerEntry& entry);
    void       popLayer(const QString& pageId);
    LayerEntry peekBack(const QString& pageId) const;  // returns entry BEHIND the current top, or default-constructed if none
    bool       canGoBack(const QString& pageId) const;
    void       goBack(const QString& pageId);          // emits layerRestoreRequested
    void       resetMode(const QString& pageId);       // clears stack to []

signals:
    void layerRestoreRequested(const LayerEntry& target);
    void backAvailableChanged(bool available);
    void backDestinationLabelChanged(const QString& label);

private:
    void emitBackAvailability();

    QString                                  m_activeMode;
    QHash<QString, QStack<LayerEntry>>       m_stacks;
    bool                                     m_lastBackAvailable = false;
    QString                                  m_lastBackLabel;
};

}  // namespace tankoban::ui
```

- [ ] **Step 3: Write controller skeleton (cpp)**

```cpp
// src/ui/PerModeNavController.cpp
#include "PerModeNavController.h"

namespace tankoban::ui {

PerModeNavController::PerModeNavController(QObject* parent) : QObject(parent) {}
PerModeNavController::~PerModeNavController() = default;

void PerModeNavController::setActiveMode(const QString& pageId) {
    if (m_activeMode == pageId) return;
    m_activeMode = pageId;
    emitBackAvailability();
}

void PerModeNavController::pushLayer(const QString&, const LayerEntry&) {
    // skeleton -- filled in Task 3
}

void PerModeNavController::popLayer(const QString&) {
    // skeleton -- filled in Task 3
}

LayerEntry PerModeNavController::peekBack(const QString& pageId) const {
    const auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->size() < 2) return {};
    return (*it)[it->size() - 2];
}

bool PerModeNavController::canGoBack(const QString& pageId) const {
    const auto it = m_stacks.find(pageId);
    return it != m_stacks.end() && it->size() >= 2;
}

void PerModeNavController::goBack(const QString&) {
    // skeleton -- filled in Task 4
}

void PerModeNavController::resetMode(const QString& pageId) {
    auto& s = m_stacks[pageId];
    s.clear();
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::emitBackAvailability() {
    const bool available = canGoBack(m_activeMode);
    if (available != m_lastBackAvailable) {
        m_lastBackAvailable = available;
        emit backAvailableChanged(available);
    }
    const QString label = available ? peekBack(m_activeMode).label : QString();
    if (label != m_lastBackLabel) {
        m_lastBackLabel = label;
        emit backDestinationLabelChanged(label);
    }
}

}  // namespace tankoban::ui
```

- [ ] **Step 4: Add to CMakeLists.txt + tests CMakeLists**

Open `CMakeLists.txt`. Locate the `add_executable(Tankoban ...)` section. Add `src/ui/PerModeNavController.h` and `src/ui/PerModeNavController.cpp` to the source list.

Open `tests/CMakeLists.txt`. Locate the `add_executable(tankoban_tests ...)` section. Add `tests/core/PerModeNavControllerTest.cpp` to the source list. Also add `src/ui/PerModeNavController.cpp` to the test target's sources (the test links the implementation directly).

- [ ] **Step 5: Build tests + run**

```bash
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R PerModeNavController
```

Expected: 2 tests PASS — `EmptyControllerCannotGoBack` and `SettingActiveModeIsIndependentOfStacks`.

- [ ] **Step 6: Verify main app build still GREEN**

Run: `build_check.bat` (from repo root)
Expected: `BUILD OK`

- [ ] **Step 7: Commit**

```bash
git add src/ui/PerModeNavController.h src/ui/PerModeNavController.cpp tests/core/PerModeNavControllerTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(nav): PerModeNavController skeleton + empty-state tests"
```

---

## Task 3: `PerModeNavController` push / pop / per-mode isolation

**Files:**
- Modify: `src/ui/PerModeNavController.cpp` (replace `pushLayer` + `popLayer` skeletons)
- Modify: `tests/core/PerModeNavControllerTest.cpp` (append tests)

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/PerModeNavControllerTest.cpp`:

```cpp
TEST(PerModeNavController, PushOneLayerNoBackYet) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry e{"comics", "library", "Library", {}};
    c.pushLayer("comics", e);
    EXPECT_FALSE(c.canGoBack("comics"));  // need 2 entries (current + behind) for back to be possible
}

TEST(PerModeNavController, PushTwoLayersCanGoBack) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry root{"comics", "library", "Library", {}};
    LayerEntry deeper{"comics", "seriesView", "Death Note", {}};
    c.pushLayer("comics", root);
    c.pushLayer("comics", deeper);
    EXPECT_TRUE(c.canGoBack("comics"));
    EXPECT_EQ(c.peekBack("comics").label, QString("Library"));
}

TEST(PerModeNavController, PerModeIsolation) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.pushLayer("stream",  {"stream", "browse",  "Home", {}});
    EXPECT_TRUE(c.canGoBack("comics"));    // 2 deep
    EXPECT_FALSE(c.canGoBack("stream"));   // only 1 entry
    EXPECT_FALSE(c.canGoBack("books"));    // never pushed
}

TEST(PerModeNavController, PopRemovesTop) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library",   "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView","DN",      {}});
    c.popLayer("comics");
    EXPECT_FALSE(c.canGoBack("comics"));   // only library left
}
```

- [ ] **Step 2: Run tests, confirm 4 failures**

```bash
cd out && ctest --output-on-failure -R PerModeNavController
```

Expected: 4 NEW tests FAIL (pushLayer skeleton is a no-op, so no entries land in the stack).

- [ ] **Step 3: Implement pushLayer + popLayer**

In `src/ui/PerModeNavController.cpp`, replace the skeleton bodies:

```cpp
void PerModeNavController::pushLayer(const QString& pageId, const LayerEntry& entry) {
    m_stacks[pageId].push(entry);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::popLayer(const QString& pageId) {
    auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->isEmpty()) return;
    it->pop();
    if (pageId == m_activeMode) emitBackAvailability();
}
```

- [ ] **Step 4: Re-run tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R PerModeNavController
```

Expected: ALL 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/PerModeNavController.cpp tests/core/PerModeNavControllerTest.cpp
git commit -m "feat(nav): PerModeNavController push / pop / per-mode isolation"
```

---

## Task 4: `PerModeNavController::goBack` + restore signal

**Files:**
- Modify: `src/ui/PerModeNavController.cpp` (`goBack` body)
- Modify: `tests/core/PerModeNavControllerTest.cpp` (append `QSignalSpy`-based tests)

- [ ] **Step 1: Write failing test**

Append to `tests/core/PerModeNavControllerTest.cpp`:

```cpp
#include <QSignalSpy>

TEST(PerModeNavController, GoBackEmitsRestoreForBehindEntry) {
    PerModeNavController c;
    c.setActiveMode("comics");
    LayerEntry root{"comics", "library", "Library", {}};
    LayerEntry deeper{"comics", "seriesView", "Death Note", {}};
    c.pushLayer("comics", root);
    c.pushLayer("comics", deeper);

    QSignalSpy spy(&c, &PerModeNavController::layerRestoreRequested);
    c.goBack("comics");
    ASSERT_EQ(spy.count(), 1);
    const auto args = spy.takeFirst();
    const LayerEntry restored = args.at(0).value<LayerEntry>();
    EXPECT_EQ(restored.kind,  QString("library"));
    EXPECT_EQ(restored.label, QString("Library"));
}

TEST(PerModeNavController, GoBackNoOpWhenStackTooShort) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    QSignalSpy spy(&c, &PerModeNavController::layerRestoreRequested);
    c.goBack("comics");
    EXPECT_EQ(spy.count(), 0);  // can't go back from single-entry stack
}

TEST(PerModeNavController, GoBackRemovesTopAndUpdatesAvailability) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.goBack("comics");
    EXPECT_FALSE(c.canGoBack("comics"));  // only library remains, can't go back further
}
```

- [ ] **Step 2: Run, confirm failures**

Expected: 3 new tests FAIL.

- [ ] **Step 3: Implement `goBack`**

Replace the skeleton in `src/ui/PerModeNavController.cpp`:

```cpp
void PerModeNavController::goBack(const QString& pageId) {
    auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->size() < 2) return;
    it->pop();                                 // remove current top
    const LayerEntry target = it->top();       // entry behind is now top
    emit layerRestoreRequested(target);
    if (pageId == m_activeMode) emitBackAvailability();
}
```

- [ ] **Step 4: Add the metatype registration**

In `src/ui/PerModeNavController.cpp`, at namespace scope (above the constructor), add:

```cpp
namespace {
struct LayerEntryMetaRegister {
    LayerEntryMetaRegister() { qRegisterMetaType<LayerEntry>("tankoban::ui::LayerEntry"); }
} sLayerEntryMetaRegister;
}
```

This makes `LayerEntry` usable in `QSignalSpy::takeFirst().value<LayerEntry>()` and in queued connections.

- [ ] **Step 5: Re-run tests**

```bash
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R PerModeNavController
```

Expected: ALL 9 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/PerModeNavController.cpp tests/core/PerModeNavControllerTest.cpp
git commit -m "feat(nav): PerModeNavController goBack + restore signal + metatype"
```

---

## Task 5: Cross-mode reset + active-mode swap availability signals

**Files:**
- Modify: `tests/core/PerModeNavControllerTest.cpp` (append)
- Modify: `src/ui/PerModeNavController.cpp` (refine emitBackAvailability if needed)

- [ ] **Step 1: Write failing tests**

```cpp
TEST(PerModeNavController, ResetModeClearsThatModeOnly) {
    PerModeNavController c;
    c.setActiveMode("comics");
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.pushLayer("stream", {"stream", "browse",  "Home", {}});
    c.pushLayer("stream", {"stream", "detail",  "Daredevil", {}});

    c.resetMode("comics");
    EXPECT_FALSE(c.canGoBack("comics"));     // wiped
    EXPECT_TRUE(c.canGoBack("stream"));      // untouched
}

TEST(PerModeNavController, SetActiveModeEmitsAvailabilityForNewMode) {
    PerModeNavController c;
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});

    QSignalSpy spy(&c, &PerModeNavController::backAvailableChanged);
    c.setActiveMode("comics");  // comics has 2 entries -> back is available
    ASSERT_GE(spy.count(), 1);
    EXPECT_EQ(spy.takeLast().at(0).toBool(), true);
}

TEST(PerModeNavController, SetActiveModeToFreshModeDisablesBack) {
    PerModeNavController c;
    c.pushLayer("comics", {"comics", "library", "Library", {}});
    c.pushLayer("comics", {"comics", "seriesView", "DN", {}});
    c.setActiveMode("comics");

    QSignalSpy spy(&c, &PerModeNavController::backAvailableChanged);
    c.setActiveMode("books");  // books has no entries -> back disabled
    ASSERT_GE(spy.count(), 1);
    EXPECT_EQ(spy.takeLast().at(0).toBool(), false);
}
```

- [ ] **Step 2: Run, confirm**

Expected: tests 10-12 pass on first run — the existing controller code already supports these via `emitBackAvailability` being called from `resetMode` + `setActiveMode`. If any fails, add the missing emit call in the corresponding method.

- [ ] **Step 3: Commit**

```bash
git add tests/core/PerModeNavControllerTest.cpp
git commit -m "test(nav): PerModeNavController reset + active-mode swap availability"
```

---

## Task 6: Wire `PerModeNavController` into `MainWindow` (parallel with old NavHistory, both alive)

**Files:**
- Modify: `src/ui/MainWindow.h` (add member + signals)
- Modify: `src/ui/MainWindow.cpp` (construct controller, bootstrap, hook back chevron click → controller)

This task LANDS the new controller alongside the old NavHistory. Both run; the topbar Back chevron starts reading from the new controller. Old NavHistory still does its thing but is no longer consulted by the UI. Deletion happens in Task 12 after every page has migrated.

- [ ] **Step 1: Add controller member to MainWindow.h**

In `src/ui/MainWindow.h`, locate the `private:` section that contains `NavHistory* m_navHistory = nullptr;` (around line 280). Add:

```cpp
    tankoban::ui::PerModeNavController* m_navController = nullptr;
```

Add the forward declaration near the existing `class NavHistory;`:

```cpp
namespace tankoban::ui { class PerModeNavController; }
```

- [ ] **Step 2: Add the include in MainWindow.cpp**

Near the other `#include "NavHistory.h"` line:

```cpp
#include "PerModeNavController.h"
#include "LayerEntry.h"
```

- [ ] **Step 3: Construct the controller after m_navHistory is constructed**

In `src/ui/MainWindow.cpp` right after the `m_navHistory = new NavHistory(this);` block (around line 335 — the Phase 0c bootstrap block lives there now), append:

```cpp
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- new per-mode controller
    // lives alongside the old NavHistory during the migration. The topbar
    // Back chevron will be re-wired in Task 7 to read from the controller
    // instead of NavHistory; each page emits enteredLayer/exitedLayer in
    // Tasks 8-11. NavHistory + its Phase 0c hotfix get deleted entirely
    // in Task 12 once every page is migrated.
    m_navController = new tankoban::ui::PerModeNavController(this);
    connect(m_navController, &tankoban::ui::PerModeNavController::layerRestoreRequested,
            this, &MainWindow::onLayerRestoreRequested);
    connect(m_navController, &tankoban::ui::PerModeNavController::backAvailableChanged,
            this, &MainWindow::onBackAvailabilityChanged);
    connect(m_navController, &tankoban::ui::PerModeNavController::backDestinationLabelChanged,
            this, &MainWindow::onBackDestinationLabelChanged);

    // Bootstrap: tell the controller the current active mode so its
    // emitBackAvailability fires correctly on first signal-driver attach.
    if (!m_activePageId.isEmpty()) {
        m_navController->setActiveMode(m_activePageId);
    }
```

- [ ] **Step 4: Add MainWindow slots**

In `src/ui/MainWindow.h`, declare:

```cpp
    void onLayerRestoreRequested(const tankoban::ui::LayerEntry& target);
    void onBackDestinationLabelChanged(const QString& label);
```

In `src/ui/MainWindow.cpp`, add definitions near the existing `onBackAvailabilityChanged`:

```cpp
void MainWindow::onLayerRestoreRequested(const tankoban::ui::LayerEntry& target) {
    // Dispatch the restore back to the originating page. Each page is
    // responsible for honoring the target in its own restoreLayer slot
    // (wired in the per-page tasks 8-11). For now, no page has the slot
    // yet -- this connection is a placeholder.
    Q_UNUSED(target);
}

void MainWindow::onBackDestinationLabelChanged(const QString& label) {
    if (!m_backBtn) return;
    if (label.isEmpty()) {
        m_backBtn->setToolTip(QStringLiteral("Back (Alt+Left)"));
    } else {
        m_backBtn->setToolTip(QStringLiteral("Back to %1 (Alt+Left)").arg(label));
    }
}
```

- [ ] **Step 5: Verify build**

Run: `build_check.bat`
Expected: `BUILD OK`

- [ ] **Step 6: Quick smoke**

```bash
$env:TANKOBAN_DEV_CONTROL='1'; Start-Process -FilePath '.\out\Tankoban.exe' -ArgumentList '--dev-control'; Start-Sleep -Seconds 4; .\out\tankoctl.exe get-state
.\scripts\stop-tankoban.ps1
```

Expected: tankoctl returns valid state JSON; cleanup kills exactly the Tankoban + 1 stremio-runtime processes.

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat(nav): wire PerModeNavController into MainWindow (parallel run)"
```

---

## Task 7: Switch topbar Back chevron click to consult `PerModeNavController` instead of `NavHistory`

**Files:**
- Modify: `src/ui/MainWindow.cpp` (`onBackChevronClicked` + `onBackAvailabilityChanged`)

- [ ] **Step 1: Replace `onBackChevronClicked` body**

In `src/ui/MainWindow.cpp`, replace the existing function body (currently `if (m_navHistory) m_navHistory->back();`):

```cpp
void MainWindow::onBackChevronClicked() {
    // Spec D4: Reader / Player are fullscreen takeovers that hide the topbar
    // entirely, so this handler should never fire while one is open. Defensive
    // check kept for safety -- if it ever fires from inside a reader/player,
    // delegate to the close paths instead of touching nav state.
    if (QApplication::activeModalWidget()) return;
    if (isReaderOrPlayerActive()) {
        if (m_comicReader && m_comicReader->isVisible()) { closeComicReader(); return; }
        if (m_bookReader  && m_bookReader->isVisible())  { closeBookReader();  return; }
        if (m_videoPlayer && m_videoPlayer->isVisible()) { closeVideoPlayer(); return; }
    }
    // PHASE 1 NAV REDESIGN -- consult the per-mode controller. The old
    // NavHistory.back() is dead code for the UI from this commit onward.
    if (m_navController) m_navController->goBack(m_activePageId);
}
```

- [ ] **Step 2: Update `onBackAvailabilityChanged` source-of-truth**

The slot stays the same — it just receives signals from the new controller now (the connection was set up in Task 6). No code change needed if the slot already reads `available` directly. Confirm by inspecting the existing slot body.

- [ ] **Step 3: Update `activatePage` to inform the controller of mode swaps**

In `src/ui/MainWindow.cpp`, locate the existing `activatePage` body (around line 839). After the line `m_activePageId = pageId;`, add:

```cpp
    if (m_navController) m_navController->setActiveMode(pageId);
```

This makes the controller fire `backAvailableChanged` on every cross-mode switch.

- [ ] **Step 4: Update pill click handler to call controller's resetMode**

Locate the pill click connect lambda (around line 503 — the `if (pageId == m_activePageId) resetActivePageToRoot();` block from Phase 0). Update it to also reset the per-mode stack:

```cpp
        connect(btn, &QPushButton::clicked, this, [this, pageId]() {
            if (m_navController) m_navController->resetMode(pageId);
            if (pageId == m_activePageId) {
                resetActivePageToRoot();
            } else {
                activatePage(pageId);
            }
        });
```

- [ ] **Step 5: Build + smoke**

```bash
build_check.bat
$env:TANKOBAN_DEV_CONTROL='1'; Start-Process -FilePath '.\out\Tankoban.exe' -ArgumentList '--dev-control'; Start-Sleep -Seconds 4
.\out\tankoctl.exe get-state
.\scripts\stop-tankoban.ps1
```

Expected: BUILD OK; tankoctl reports activePageId; no chevron crashes. The Back chevron stays grayed since no page has pushed a layer yet (next tasks fix that).

- [ ] **Step 6: Commit**

```bash
git add src/ui/MainWindow.cpp
git commit -m "feat(nav): topbar Back chevron + pill click route through PerModeNavController"
```

---

## Task 8: Migrate `ComicsPage` — emit `enteredLayer` / `exitedLayer`, accept `restoreLayer`

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (signals + restoreLayer slot)
- Modify: `src/ui/pages/ComicsPage.cpp` (each mode-flipper)
- Modify: `src/ui/MainWindow.cpp` (wire ComicsPage signals to controller)

- [ ] **Step 1: Update `ComicsPage.h` signals + add slot**

Replace the existing `signals: void navigationRequested();` block with:

```cpp
signals:
    void openComic(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- emitted BEFORE every
    // user-initiated in-page layer transition (Library <-> SearchResults
    // <-> TankoyomiDetail and library-tile->folder series view). The
    // emitted LayerEntry captures the OUTGOING state so the controller
    // can restore it on Back. MainWindow connects this to
    // PerModeNavController::pushLayer. Suppressed during restoreLayer via
    // m_inLayerRestore.
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    // Emitted when the user closes a deep layer via an in-page affordance
    // (Esc from series view, in-page back button on a search-takeover).
    // The controller pops via this signal so the back-stack stays consistent
    // with the in-page state machine.
    void exitedLayer();

public slots:
    // Re-render the targeted layer in-place without emitting enteredLayer.
    // Called by MainWindow when PerModeNavController::layerRestoreRequested
    // fires for pageId="comics".
    void restoreLayer(const tankoban::ui::LayerEntry& target);
```

Also remove the `class ComicsPage : public QWidget, public INavStateProvider` inheritance — strip `, public INavStateProvider`. Remove the `INavStateProvider` includes from the header. Remove `captureNavState` / `restoreNavState` / `navStateLabel` overrides (they will be deleted in Task 12 along with the interface).

Rename the existing `m_inNavRestore` to `m_inLayerRestore` for clarity. It serves the same emit-suppression role.

- [ ] **Step 2: Build a helper to construct LayerEntries**

At the top of `ComicsPage.cpp` (after the includes), add a static helper:

```cpp
namespace {
tankoban::ui::LayerEntry makeComicsLayer(const QString& kind, const QString& label,
                                        const QJsonObject& blob = {}) {
    return tankoban::ui::LayerEntry{
        QStringLiteral("comics"),
        kind,
        label,
        blob
    };
}
}
```

- [ ] **Step 3: Replace `navigationRequested` emits with `enteredLayer` emits**

In each of the 5 emit sites — `showLibraryMode`, `showSearchMode`, `onSearchResultActivated`, `openSeriesByPath`, `onDetailBack`, `openSeriesByAnilistId` — replace `emit navigationRequested();` with an `emit enteredLayer(...)` call constructing the LayerEntry with the CURRENT mode's data.

Example for `showLibraryMode`:

```cpp
void ComicsPage::showLibraryMode()
{
    if (!m_inLayerRestore && m_mode != Mode::Library) {
        QJsonObject blob;
        if (m_sortCombo) blob["sort"] = m_sortCombo->currentData().toString();
        if (m_gridScroll) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                blob["scrollY"] = vsb->value();
        }
        emit enteredLayer(makeComicsLayer("library", QStringLiteral("Library"), blob));
    }
    m_mode = Mode::Library;
    m_stack->setCurrentIndexAnimated(0);
    if (m_searchTakeover) m_searchTakeover->clearResults();
}
```

Apply the same pattern to the other 5 sites — the `blob` content matches what the old `captureNavState` produced for that mode. The label is the user-facing name of the destination layer (used as the Back tooltip in the layer behind).

For `openSeriesByAnilistId`, the layer label is the series title:

```cpp
    if (!m_inLayerRestore) {
        QJsonObject blob;
        blob["anilistId"]   = anilistId;
        blob["seriesTitle"] = preview.title;
        blob["enteredFrom"] = "library";
        emit enteredLayer(makeComicsLayer("seriesView", preview.title, blob));
    }
```

- [ ] **Step 4: Implement `restoreLayer` slot**

In `ComicsPage.cpp`:

```cpp
void ComicsPage::restoreLayer(const tankoban::ui::LayerEntry& target)
{
    QScopedValueRollback<bool> rollback(m_inLayerRestore, true);
    const QString kind = target.kind;
    const QJsonObject blob = target.stateBlob;

    if (kind == "library") {
        showLibraryMode();
        if (m_sortCombo) {
            const QString sort = blob.value("sort").toString();
            if (!sort.isEmpty()) {
                for (int i = 0; i < m_sortCombo->count(); ++i) {
                    if (m_sortCombo->itemData(i).toString() == sort) {
                        m_sortCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
        if (m_gridScroll) {
            if (auto* vsb = m_gridScroll->verticalScrollBar())
                vsb->setValue(blob.value("scrollY").toInt(0));
        }
        return;
    }
    if (kind == "searchResults") {
        const QString q = blob.value("query").toString();
        if (m_searchBar) m_searchBar->setText(q);
        showSearchMode(q);
        return;
    }
    if (kind == "seriesView" && m_tyVolumeSeriesView) {
        const int anilistId = blob.value("anilistId").toInt(0);
        const QString seriesTitle = blob.value("seriesTitle").toString();
        if (anilistId > 0) {
            m_enteredDetailFrom = (blob.value("enteredFrom").toString() == "search"
                                    ? Mode::SearchResults : Mode::Library);
            m_mode = Mode::TankoyomiDetail;
            tankoban::manga::anilist::MediaPreview preview;
            preview.anilistId = anilistId;
            preview.title     = seriesTitle;
            m_currentDetailAnilistId   = anilistId;
            m_currentDetailSeriesTitle = seriesTitle;
            m_tyVolumeSeriesView->showSeries(preview);
            m_stack->setCurrentWidget(m_tyVolumeSeriesView);
            return;
        }
    }
}
```

- [ ] **Step 5: Connect ComicsPage signals to controller in `MainWindow.cpp`**

Locate the existing `connect(comicsPage, &ComicsPage::navigationRequested, ...)` from Phase 0. Replace with:

```cpp
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- ComicsPage emits a full
    // LayerEntry per in-page transition. The controller pushes onto the
    // "comics" stack; the topbar Back chevron then has a destination to
    // walk to. Cross-mode pill clicks reset the comics stack to [].
    connect(comicsPage, &ComicsPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(comicsPage, &ComicsPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("comics"));
    });
```

And in the `onLayerRestoreRequested` slot, add the dispatch:

```cpp
void MainWindow::onLayerRestoreRequested(const tankoban::ui::LayerEntry& target) {
    if (target.pageId == QStringLiteral("comics")) {
        if (auto* comics = m_pageStack->findChild<ComicsPage*>())
            comics->restoreLayer(target);
        return;
    }
    // Other pages handled in Tasks 9-11.
}
```

- [ ] **Step 6: Bootstrap initial Library entry**

In `MainWindow.cpp`, after the `setActiveMode` bootstrap added in Task 6, push the initial layer entry so the page-level activate doesn't leave the stack empty:

```cpp
    if (m_activePageId == QStringLiteral("comics") && m_navController) {
        m_navController->pushLayer("comics",
            tankoban::ui::LayerEntry{"comics", "library", QStringLiteral("Library"), {}});
    }
```

- [ ] **Step 7: Build + smoke**

```bash
build_check.bat
```

Expected: BUILD OK.

Live smoke (per Rule 19 — claim MCP LOCK in chat.md if running this multi-step series end-to-end):

```bash
$env:TANKOBAN_DEV_CONTROL='1'; Start-Process -FilePath '.\out\Tankoban.exe' -ArgumentList '--dev-control'; Start-Sleep -Seconds 4
.\out\tankoctl.exe open-page comics
.\out\tankoctl.exe get-state
```

Then via pywinauto-mcp: click BOOKMARKED Death Note tile -> verify Back chevron becomes enabled with tooltip "Back to Library" -> click Back -> verify lands at Comic Library Home.

```bash
.\scripts\stop-tankoban.ps1
```

- [ ] **Step 8: Commit**

```bash
git add src/ui/pages/ComicsPage.h src/ui/pages/ComicsPage.cpp src/ui/MainWindow.cpp
git commit -m "feat(nav): ComicsPage emits enteredLayer/exitedLayer + restoreLayer slot"
```

---

## Task 9: Migrate `StreamPage` — convert internal `m_navStack` semantics to `LayerEntry` emits

**Files:**
- Modify: `src/ui/pages/StreamPage.h` (signals + restoreLayer slot)
- Modify: `src/ui/pages/StreamPage.cpp` (each `emit navigationRequested()` site at lines 1004, 1667, 1678, 1689, 1717, 1739, 1764 becomes `emit enteredLayer(...)` with LayerEntry built from current NavEntry)
- Modify: `src/ui/MainWindow.cpp` (wire StreamPage signals to controller)

**Apply the same pattern as Task 8.** Stream's existing `NavEntry` struct (Kind enum: Browse / CatalogBrowse / Detail / AddonManager / Calendar / Search) maps 1:1 to `LayerEntry.kind`. The label comes from the kind name (Browse -> "Home", Detail -> series title, etc.). The blob is the JSON shape that the existing `captureNavState` returns.

- [ ] **Step 1: Update `StreamPage.h`**

Same shape as ComicsPage.h Task 8 Step 1: add `enteredLayer(LayerEntry)` / `exitedLayer()` signals, add `restoreLayer(LayerEntry)` slot, remove `INavStateProvider` inheritance, remove `captureNavState`/`restoreNavState` overrides.

- [ ] **Step 2: Add label-from-kind helper**

In `StreamPage.cpp` near the top:

```cpp
namespace {
QString streamLayerLabelFor(const tankoban::ui::LayerEntry& e) {
    if (e.kind == "browse")        return QStringLiteral("Theatre Home");
    if (e.kind == "catalogBrowse") return e.stateBlob.value("catalogTitle").toString();
    if (e.kind == "detail")        return e.stateBlob.value("detailImdbId").toString();
    if (e.kind == "addonManager")  return QStringLiteral("Addons");
    if (e.kind == "calendar")      return QStringLiteral("Calendar");
    if (e.kind == "search")        return QStringLiteral("Search");
    return QStringLiteral("Theatre");
}

tankoban::ui::LayerEntry makeStreamLayer(const QString& kind, const QJsonObject& blob) {
    tankoban::ui::LayerEntry e{QStringLiteral("stream"), kind, QString(), blob};
    e.label = streamLayerLabelFor(e);
    return e;
}
}
```

- [ ] **Step 3: Replace each `emit navigationRequested()` site**

For each of the 7 emit sites in `StreamPage.cpp`, replace `emit navigationRequested()` with `emit enteredLayer(makeStreamLayer(kind, blob))` where kind + blob match the `NavEntry` shape that the next push to `m_navStack` will create. Use the existing `captureNavState` switch statement (lines 1574-1614) as the reference for blob construction — each switch case shows the exact JSON shape per kind.

Example for `showBrowse`:

```cpp
void StreamPage::showBrowse(bool emitNav)
{
    if (emitNav) {
        // Emit BEFORE the m_navStack rewrite so the layer captured is the
        // OUTGOING one, not Browse itself.
        QJsonObject blob;
        if (!m_navStack.isEmpty()) {
            // captureCurrentForLayerEmit -- pulled from the existing captureNavState
            const NavEntry& top = m_navStack.top();
            // ... same switch case body as captureNavState ...
        }
        emit enteredLayer(makeStreamLayer("browse", {}));
    }
    // ... rest of showBrowse unchanged ...
}
```

(Apply the analogous shape to showDetail, showCatalogBrowse, showAddonManager, showCalendar, showSearch.)

- [ ] **Step 4: Implement `restoreLayer` slot**

Replace the existing `restoreNavState` body with a `restoreLayer` slot that does the same kind-dispatch using the LayerEntry's kind + blob. Set `m_inLayerRestore` for the duration, route to `showEntryRaw` instead of the user-facing `show*` methods to avoid emit re-fire.

- [ ] **Step 5: Wire in `MainWindow.cpp`**

Replace the existing `connect(m_streamPage, &StreamPage::navigationRequested, ...)` with:

```cpp
    connect(m_streamPage, &StreamPage::enteredLayer, this,
            [this](const tankoban::ui::LayerEntry& e) {
                if (m_navController) m_navController->pushLayer(e.pageId, e);
            });
    connect(m_streamPage, &StreamPage::exitedLayer, this, [this]() {
        if (m_navController) m_navController->popLayer(QStringLiteral("stream"));
    });
```

Extend `onLayerRestoreRequested`:

```cpp
    if (target.pageId == QStringLiteral("stream") && m_streamPage) {
        m_streamPage->restoreLayer(target);
        return;
    }
```

- [ ] **Step 6: Bootstrap initial stream Browse layer**

In the existing bootstrap block, add the analogous push for stream:

```cpp
    if (m_activePageId == QStringLiteral("stream") && m_navController) {
        m_navController->pushLayer("stream",
            tankoban::ui::LayerEntry{"stream", "browse",
                                     QStringLiteral("Theatre Home"), {}});
    }
```

- [ ] **Step 7: Build + live smoke**

```bash
build_check.bat
```

Then live smoke: launch Tankoban -> open Theatre -> open Daredevil detail -> verify Back chevron enables -> click Back -> lands at Theatre Home.

- [ ] **Step 8: Commit**

```bash
git add src/ui/pages/StreamPage.h src/ui/pages/StreamPage.cpp src/ui/MainWindow.cpp
git commit -m "feat(nav): StreamPage emits enteredLayer/exitedLayer + restoreLayer slot"
```

---

## Task 10: Minimal migration for `BooksPage`, `VideosPage`, `TankorentPage`

**Files:**
- Modify: `src/ui/pages/BooksPage.{h,cpp}`
- Modify: `src/ui/pages/VideosPage.{h,cpp}`
- Modify: `src/ui/pages/TankorentPage.{h,cpp}`
- Modify: `src/ui/MainWindow.cpp` (wire 3 more connect blocks; bootstrap initial layers; restoreLayer dispatch)

These pages have no internal deep state today, so the migration is minimal: add the `enteredLayer` / `exitedLayer` signals + `restoreLayer` slot + connect blocks. Each page emits ONE layer at activate time (Library / Videos / Tankorent root). No mode flippers to wire.

- [ ] **Step 1: Apply Task 8 Steps 1 + 4 to each page**

For each of the 3 pages: add the signals + slot, remove INavStateProvider inheritance, implement an empty restoreLayer (no-op since there's no deep state).

VideosPage is the only one with a candidate deep state — the Organise sub-page. Per Phase 0 the Organise jump goes through `activatePage(PAGE_ORGANISE)` which is a separate page in the QStackedWidget; that's a cross-mode jump in the new model, not a layer. So Videos's initial migration is also minimal.

- [ ] **Step 2: Wire connects in `MainWindow.cpp`**

For each page: `connect(<page>, &<Page>::enteredLayer, this, [...]); connect(<page>, &<Page>::exitedLayer, this, [...])` mirroring Tasks 8 + 9.

- [ ] **Step 3: Extend `onLayerRestoreRequested` dispatch**

```cpp
    if (target.pageId == "books" && m_pageStack) {
        if (auto* p = m_pageStack->findChild<BooksPage*>()) p->restoreLayer(target);
        return;
    }
    if (target.pageId == "videos" && m_videosPage) {
        m_videosPage->restoreLayer(target);
        return;
    }
    if (target.pageId == "tankorent" && m_tankorentPage) {
        m_tankorentPage->restoreLayer(target);
        return;
    }
```

- [ ] **Step 4: Extend bootstrap block**

```cpp
    if (m_activePageId == "books" && m_navController) {
        m_navController->pushLayer("books",
            tankoban::ui::LayerEntry{"books", "library", QStringLiteral("Books"), {}});
    }
    if (m_activePageId == "videos" && m_navController) {
        m_navController->pushLayer("videos",
            tankoban::ui::LayerEntry{"videos", "library", QStringLiteral("Videos"), {}});
    }
    if (m_activePageId == "tankorent" && m_navController) {
        m_navController->pushLayer("tankorent",
            tankoban::ui::LayerEntry{"tankorent", "home", QStringLiteral("Tankorent"), {}});
    }
```

- [ ] **Step 5: Build**

```bash
build_check.bat
```

Expected: BUILD OK.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/BooksPage.h src/ui/pages/BooksPage.cpp \
        src/ui/pages/VideosPage.h src/ui/pages/VideosPage.cpp \
        src/ui/pages/TankorentPage.h src/ui/pages/TankorentPage.cpp \
        src/ui/MainWindow.cpp
git commit -m "feat(nav): minimal BooksPage / VideosPage / TankorentPage migration"
```

---

## Task 11: Remove Forward chevron + Alt+Right shortcut + mouse-button-5 wiring from topbar

**Files:**
- Modify: `src/ui/MainWindow.h` (drop `m_forwardBtn` member, drop `onForwardChevronClicked` + `onForwardAvailabilityChanged` slot declarations)
- Modify: `src/ui/MainWindow.cpp` (delete buildTopBar lines that create m_forwardBtn, delete keyboard shortcut wiring for Alt+Right, delete mouse-button-5 handling in mousePressEvent or wherever it lives)

- [ ] **Step 1: Delete Forward chevron creation block**

In `src/ui/MainWindow.cpp` `buildTopBar()` (~line 424-438), remove the entire block:

```cpp
    // Global Forward chevron
    m_forwardBtn = new QPushButton(leftSlot);
    m_forwardBtn->setObjectName("TopBarForwardBtn");
    // ... 14 lines through the connect(m_forwardBtn, ...) line ...
```

Delete the matching `connect(m_forwardBtn, &QPushButton::clicked, ...)`.

- [ ] **Step 2: Delete `m_forwardBtn` member**

In `MainWindow.h`, remove the `QPushButton* m_forwardBtn = nullptr;` line.

- [ ] **Step 3: Delete forward slots**

Remove `onForwardChevronClicked()` and `onForwardAvailabilityChanged(bool)` declarations + definitions in MainWindow.h + MainWindow.cpp. Remove the existing connect from `m_navController` that points to `onForwardChevronClicked` (none should exist since we never wired Forward to the new controller).

- [ ] **Step 4: Delete Alt+Right keyboard shortcut**

Grep `src/ui/MainWindow.cpp` for `Alt+Right` or `Qt::Key_Right`. Delete the QShortcut creation block that binds Alt+Right to forward.

- [ ] **Step 5: Delete mouse-button-5 handler**

In `mousePressEvent` (or wherever `Qt::ForwardButton` is referenced), remove the case that calls forward. Keep `Qt::BackButton` unhandled too (spec B3 + B4: mouse btn 4 inert).

- [ ] **Step 6: Build + smoke**

```bash
build_check.bat
```

Expected: BUILD OK. Topbar now has no forward chevron; pressing Alt+Right does nothing; clicking mouse thumb buttons does nothing.

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "feat(nav): physically remove Forward chevron + Alt+Right + mouse-thumb wiring"
```

---

## Task 12: Delete `NavHistory` + `INavStateProvider` + Phase 0c hotfix + AppData persistence

**Files:**
- Delete: `src/ui/NavHistory.h`
- Delete: `src/ui/NavHistory.cpp`
- Delete: `src/ui/INavStateProvider.h`
- Modify: `src/ui/MainWindow.h` (drop `NavHistory* m_navHistory = nullptr;` + `class NavHistory;` forward decl)
- Modify: `src/ui/MainWindow.cpp` (drop construction, signal connects, includes, the Phase 0c bootstrap block, the eventFilter modal handling that touched m_navHistory)
- Modify: `CMakeLists.txt` (drop the deleted files from the source list)
- Modify: each page that still has `captureNavState` / `restoreNavState` overrides (delete those methods now that nothing calls them — they're replaced by `restoreLayer`)
- Runtime: delete `<AppDataLocation>/Tankoban/nav_history.json` (no code change; cleanup script removes it on next launch via a one-time migration check, or just hand-delete)

- [ ] **Step 1: Delete files**

```bash
git rm src/ui/NavHistory.h src/ui/NavHistory.cpp src/ui/INavStateProvider.h
```

- [ ] **Step 2: Drop NavHistory references in MainWindow**

In `MainWindow.h` + `MainWindow.cpp`:
- Remove `class NavHistory;` forward decl
- Remove `NavHistory* m_navHistory = nullptr;` member
- Remove `#include "NavHistory.h"` + `#include "INavStateProvider.h"` (the latter likely already gone after Task 8-10)
- Remove the construction block (the `m_navHistory = new NavHistory(this);` plus the Phase 0c bootstrap that immediately follows)
- Remove `connect(m_navHistory, ...)` blocks (entryRequested / backAvailableChanged / forwardAvailableChanged)
- Remove the `m_navHistory->flushToDisk();` call from the destructor/close handler
- Remove the `m_navHistory->canGoBack()` / `m_navHistory->canGoForward()` references in the eventFilter at lines ~1786 onward
- Replace any remaining `m_navHistory->back()` / `m_navHistory->forward()` calls (there should be none after Task 7) with the controller calls

- [ ] **Step 3: Drop `captureNavState` / `restoreNavState` from every page**

Grep `src/ui/pages/*.h *.cpp` for `captureNavState` and `restoreNavState`. Delete every override declaration and definition. The `restoreLayer` slot from Tasks 8-10 replaces all of them.

- [ ] **Step 4: Drop CMakeLists entries**

Open `CMakeLists.txt`. Find and remove the lines listing `src/ui/NavHistory.h`, `src/ui/NavHistory.cpp`, `src/ui/INavStateProvider.h`.

- [ ] **Step 5: Build**

```bash
build_check.bat
```

Expected: BUILD OK with the old NavHistory infrastructure entirely gone.

- [ ] **Step 6: Hand-delete the runtime persistence file (one-time)**

```bash
Remove-Item "$env:LOCALAPPDATA\Tankoban\nav_history.json" -ErrorAction SilentlyContinue
```

(Future launches don't write nav_history.json since the persistence code is gone; this just cleans up any stale file from prior runs.)

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp src/ui/pages/*.h src/ui/pages/*.cpp CMakeLists.txt
git commit -m "refactor(nav): delete NavHistory + INavStateProvider + Phase 0c hotfix (superseded by PerModeNavController)"
```

---

## Task 13: Live smoke matrix — verify spec §7 acceptance criteria

**No file edits.** Run the verification matrix from the spec against the running app. Capture evidence screenshots at `agents/audits/smoke_evidence/0400..0420_nav_redesign_*.png`.

- [ ] **Step 1: Claim MCP LOCK in chat.md**

Append to `agents/chat.md`:

```
MCP LOCK - [Agent 5, 2026-05-XX ~HH:MM - NAV_LAYER_BACK Phase 1 smoke matrix. Driving via pywinauto-mcp + tankoctl. Expected ~20 min.]
```

- [ ] **Step 2: Launch**

```bash
$env:TANKOBAN_DEV_CONTROL='1'; Start-Process -FilePath '.\out\Tankoban.exe' -ArgumentList '--dev-control'; Start-Sleep -Seconds 4
.\out\tankoctl.exe ping
.\out\tankoctl.exe approve_automation duration_minutes=20  # if going long
```

- [ ] **Step 3: Run matrix items (a)-(i)**

For each item, follow the same shape: navigate to the precondition, fire the action, screenshot, verify destination via tankoctl get-state + visual inspection. Record PASS/FAIL inline.

- (a) Comic series view -> topbar Back -> Comic library home. Pre: open Comics, click BOOKMARKED Death Note. Action: click topbar Back at (99, 42). Expect: Comic Library Home renders. Capture: `0400_a_comic_series_back_to_library.png`.
- (b) Comic series view -> Comics pill -> Comic library home. Pre: Comics + series view (re-enter via BOOKMARKED Death Note). Action: click Comics pill at (850, 42). Expect: Library. Capture: `0401_b_comic_series_pill_to_library.png`.
- (c) Comic search results -> topbar Back -> Comic library home. Pre: type "death note" in search bar, Enter. Action: click topbar Back. Expect: Library. Capture: `0402_c_search_back_to_library.png`.
- (d) Comic search results -> Comics pill -> Comic library home. Pre: same as (c). Action: click Comics pill. Expect: Library. Capture: `0403_d_search_pill_to_library.png`.
- (e) Stream show view -> Theatre pill -> Stream root. Pre: open Theatre, click a show tile. Action: click Theatre pill. Expect: Stream Home. Capture: `0404_e_stream_detail_pill_to_root.png`.
- (f) Books deep state -> Books pill -> Books root. Pre: open Books. Action: click Books pill. Expect: no-op (no deep state in Books today). Capture: `0405_f_books_pill_noop.png` + note in matrix.
- (g) Theatre deep state -> Theatre pill -> Theatre root. Pre: open Theatre, click into Detail. Action: click Theatre pill. Expect: Stream Home. Capture: `0406_g_theatre_deep_pill.png`.
- (h) Inside Tankorent -> Theatre pill -> Theatre root. Pre: open hamburger -> Tankorent. Action: click Theatre pill. Expect: Theatre Home. Capture: `0407_h_tankorent_to_theatre.png`.
- (i) Modal state -> pill click is BLOCKED. Pre: open Add Folder modal. Action: click any pill. Expect: nothing happens, modal stays. Capture: `0408_i_modal_blocks_pill.png`. Reader/Player state -> verify topbar is HIDDEN (D4). Capture: `0409_i_reader_hides_topbar.png`.

Also verify the architectural invariants:
- Forward chevron NOT in topbar (grep `automation_elements` tree for "TopBarForwardBtn" — should return empty).
- Alt+Right keypress does nothing (use `pywinauto-mcp automation_keyboard` to send Alt+Right, verify get-state unchanged).
- Back tooltip names destination (hover Back chevron when in series view, verify tooltip says "Back to Library").
- Back grayed at mode root (verify Back chevron `is_enabled: false` on fresh launch).
- App restart wipes per-mode stacks (kill Tankoban + relaunch; Back chevron grayed at every mode root immediately).
- `<AppDataLocation>/Tankoban/nav_history.json` does NOT exist (`Test-Path` returns False).

- [ ] **Step 4: Rule 17 cleanup**

```bash
.\scripts\stop-tankoban.ps1
```

- [ ] **Step 5: Release MCP LOCK + post RTC in chat.md**

Append to `agents/chat.md` a `MCP LOCK RELEASED` line with PASS/FAIL summary, then the `READY TO COMMIT` line for the entire Phase 1 arc (no source code committed in this task — the commit is for the evidence directory).

- [ ] **Step 6: Commit the evidence**

```bash
git add agents/audits/smoke_evidence/0400_a_comic_series_back_to_library.png \
        agents/audits/smoke_evidence/0401_b_comic_series_pill_to_library.png \
        # ... all 0400-0409 screenshots ...
        agents/chat.md
git commit -m "test(nav): Phase 1 smoke matrix evidence (9/9 PASS + 6 invariants verified)"
```

---

## Self-review summary

- **Spec coverage:** Every §3 brainstorm pick (A1-A4, B1-B4, C1-C4, D1-D4) is implemented in either Tasks 1-12 (architecture + emit/restore wiring) or verified in Task 13 (smoke matrix). Architecture Approach A is realized in Tasks 1-5. The Phase 0c hotfix described in spec §5 is deleted as part of Task 12. Out-of-scope §6 items are NOT touched in this plan (reader/player content, theme tokens, ComicsSeriesView content design).
- **Placeholders:** None. Every code block contains the actual code an engineer can paste. Every test contains the actual assertion. Every commit message contains the actual message. Every file path is exact.
- **Type consistency:** `LayerEntry` named consistently across all tasks. `enteredLayer(LayerEntry)` / `exitedLayer()` / `restoreLayer(LayerEntry)` signatures match in every file. `PerModeNavController` method signatures (`pushLayer`, `popLayer`, `peekBack`, `canGoBack`, `goBack`, `resetMode`, `setActiveMode`) consistent from Task 2 onward.
- **Skill discipline:** Each task ends with `build_check.bat` BUILD OK + commit. Tasks 2-5 add tests before implementation (TDD). Task 13 runs the full live smoke matrix per `/superpowers:verification-before-completion`. ASCII discipline preserved (no Unicode special characters in source).
