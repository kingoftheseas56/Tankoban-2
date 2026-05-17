# Theatre Source Picker + Nyaa Registration Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the 2 Nyaa-not-registered bugs across the indexer fan-out, then add a Source dropdown to the Theatre Layers-3 pack panel that lets the user limit a search to a single indexer (e.g. "Nyaa only for anime") with QSettings persistence.

**Architecture:** Piece A (bug fixes) ships as a single small commit — adds `NyaaIndexer` to the Theatre `StreamAggregator::searchPacks` dispatch loop + adds `"nyaa"` to the Tankorent tab Videos allowlist. Piece B (source picker) threads an optional `sourceFilter` parameter from `TheatreDownloadPanel::m_sourceCombo` through `UnifiedPackSearchEngine::search` to `StreamAggregator::searchPacks`, where it gates which indexer(s) are dispatched. UI lives above the existing 4-chip type filter row.

**Tech Stack:** Qt6 / C++20 / CMake-Ninja-MSVC. No new dependencies. No new tests (Tankoban TDD policy: opt-in only for `tankoban_tests` pure-logic primitives — this work is UI plumbing + integration, smoke-first per CLAUDE.md Tier-2 skill discipline).

**Reference spec:** [docs/superpowers/specs/2026-05-17-theatre-source-picker-and-nyaa-fixes-design.md](../specs/2026-05-17-theatre-source-picker-and-nyaa-fixes-design.md)

**Brotherhood contract notes:**
- Agents do NOT `git commit` directly. Each task ends with appending a `READY TO COMMIT - [Agent 4, ...]` line to `agents/chat.md` per Rule 11 — Agent 0 batches via `/commit-sweep` later.
- Build verify is `build_check.bat` from repo root (compile-only, agent-safe, no GUI spawn). Expected output: `BUILD OK` on the last line, exit 0. On failure: 30-line cl.exe tail printed.
- Smokes are described as user-actions for Hemanth to execute via `build_and_run.bat` — the plan does NOT drive MCP smokes (Hemanth is mid-smoke on the prior arc per `feedback_mcp_lane_lock.md` and we don't claim MCP LOCK here).
- ASCII-only sweep is the last step before each RTC — check for em-dashes, smart quotes, etc.

---

## File Structure

**Files modified (in execution order):**

- `src/core/stream/StreamAggregator.cpp` — Task 1: add Nyaa dispatch; Task 2: gate by sourceFilter
- `src/ui/pages/TankorentPage.cpp` — Task 1: add `"nyaa"` to videos allowlist (one-word edit)
- `src/core/stream/StreamAggregator.h` — Task 2: add optional sourceFilter param to searchPacks signature
- `src/core/stream/UnifiedPackSearchEngine.h` — Task 2: add optional sourceFilter param to search signature + member
- `src/core/stream/UnifiedPackSearchEngine.cpp` — Task 2: thread sourceFilter to aggregator call
- `src/ui/pages/stream/TheatreDownloadPanel.h` — Task 3: declare combo + filter member + slot + persistence helpers
- `src/ui/pages/stream/TheatreDownloadPanel.cpp` — Task 3: build combo widget + layout placement; Task 4: wire signal + persistence + pass to engine

Each file has one clear responsibility. No files are created or deleted.

---

## Task 1: Piece A — Both Nyaa Registration Fixes (bundled)

**Files:**
- Modify: `src/core/stream/StreamAggregator.cpp` (add Nyaa to pack-search dispatch loop)
- Modify: `src/ui/pages/TankorentPage.cpp:1188` (add `"nyaa"` to videos allowlist)

**Why bundled:** Both are 1-2 line bug fixes correcting the same missing-registration class. Shipping them as one RTC is cleaner than two trivial RTCs. They have independent value (Nyaa starts working on both surfaces today, before the source-picker UI lands).

- [ ] **Step 1: Add `#include "core/indexers/NyaaIndexer.h"` to StreamAggregator.cpp**

Locate the existing indexer includes near the top of `src/core/stream/StreamAggregator.cpp`. The file already includes `PirateBayIndexer.h`, `X1337xIndexer.h`, `YtsIndexer.h`, `EztvIndexer.h`, `ExtTorrentsIndexer.h`. Add the Nyaa include alphabetically among them (or directly above PirateBayIndexer.h — match the existing ordering style).

```cpp
#include "core/indexers/NyaaIndexer.h"
```

If the existing block has no clear alphabetical order, place it as the first indexer include.

- [ ] **Step 2: Add `dispatch(...)` call for Nyaa in `StreamAggregator::searchPacks` loop**

Open `src/core/stream/StreamAggregator.cpp`. Find the `for (const QString& query : queries)` loop near line 761 — it currently dispatches 5 indexers. Add Nyaa as the FIRST dispatch in each iteration (so it appears first in result ordering, matching how `TankorentPage` orders it first):

Current code at lines 761-772:
```cpp
    for (const QString& query : queries) {
        dispatch(QStringLiteral("piratebay"),
                 new PirateBayIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("1337x"),
                 new X1337xIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("yts"),
                 new YtsIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("eztv"),
                 new EztvIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("exttorrents"),
                 new ExtTorrentsIndexer(m_packNam, this), query);
    }
```

Replace with:
```cpp
    for (const QString& query : queries) {
        dispatch(QStringLiteral("nyaa"),
                 new NyaaIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("piratebay"),
                 new PirateBayIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("1337x"),
                 new X1337xIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("yts"),
                 new YtsIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("eztv"),
                 new EztvIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("exttorrents"),
                 new ExtTorrentsIndexer(m_packNam, this), query);
    }
```

Also locate the header comment block above `searchPacks` (around line 44-46) that lists the indexers and update it to include Nyaa.

Find this comment fragment:
```cpp
    // (PirateBay + 1337x + YTS + EZTV + ExtTorrents) and emits a single
```

Replace with:
```cpp
    // (Nyaa + PirateBay + 1337x + YTS + EZTV + ExtTorrents) and emits a single
```

- [ ] **Step 3: Add `"nyaa"` to `kMediaTypeIndexers["videos"]` allowlist in TankorentPage.cpp**

Open `src/ui/pages/TankorentPage.cpp` at line 1188. Find:
```cpp
    { "videos",     { "yts", "eztv", "piratebay", "1337x", "exttorrents" } },
```

Replace with:
```cpp
    { "videos",     { "nyaa", "yts", "eztv", "piratebay", "1337x", "exttorrents" } },
```

That is the ONLY change to this file in this task. Do not modify other allowlist entries.

- [ ] **Step 4: Build verify**

Run from repo root:
```
build_check.bat
```

Expected: `BUILD OK` printed near the end + exit code 0. If it fails, the most likely cause is a missing include — check Step 1's header path.

- [ ] **Step 5: ASCII sweep**

Run from repo root (PowerShell):
```powershell
Get-Content src/core/stream/StreamAggregator.cpp, src/ui/pages/TankorentPage.cpp -Encoding Byte | Where-Object { $_ -gt 127 } | Select-Object -First 5
```

Expected: zero output. If any byte > 127 appears, find and fix the offending non-ASCII character.

- [ ] **Step 6: Post RTC to chat.md**

Append to the end of `agents/chat.md`:

```
## Agent 4 - Piece A: Nyaa registration fixes (StreamAggregator + Tankorent allowlist) - 2026-05-17

READY TO COMMIT - [Agent 4, Piece A: 2 Nyaa-not-registered bug fixes shipped per docs/superpowers/specs/2026-05-17-theatre-source-picker-and-nyaa-fixes-design.md. (1) src/core/stream/StreamAggregator.cpp: NyaaIndexer added to the Theatre Layers-3 pack-search dispatch loop (was hardcoded to 5 indexers: piratebay/1337x/yts/eztv/exttorrents; Nyaa now leads). Header comment block updated to reflect 6-indexer fan-out. New include for core/indexers/NyaaIndexer.h. (2) src/ui/pages/TankorentPage.cpp:1188: "nyaa" added to kMediaTypeIndexers["videos"] allowlist - previously Nyaa was filtered out on Videos searches with "All Sources" picked (explicit Nyaa pick already bypassed the allowlist per Hemanth's 2026-04-20 commit). Bug 3 (TorrentPackPicker.cpp:160-164) deliberately skipped - file is scheduled for deletion in THEATRE_DOWNLOAD_OVERHAUL Phase G. ~6 LOC added across 2 files, zero LOC removed. build_check.bat BUILD OK. ASCII sweep clean. NyaaIndexer itself unchanged - it was already CMake-registered and IndexerStatusPanel-wired; this just adds the missing dispatch site. Hemanth smoke matrix: (1) Tankorent tab + Videos type + All Sources + search "Demon Slayer" -> Nyaa results visible alongside YTS/PirateBay/etc. (2) Theatre any show + click Layers-3 -> pack list now includes Nyaa packs alongside the other 5 indexers' packs. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion].] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/stream/StreamAggregator.cpp, src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

## Task 2: Piece B.1 — Thread sourceFilter through API signatures

**Files:**
- Modify: `src/core/stream/StreamAggregator.h` (add optional `sourceFilter` param to `searchPacks`)
- Modify: `src/core/stream/StreamAggregator.cpp` (gate dispatch by `sourceFilter`)
- Modify: `src/core/stream/UnifiedPackSearchEngine.h` (add optional `sourceFilter` param to `search`, add `m_pendingSource` member)
- Modify: `src/core/stream/UnifiedPackSearchEngine.cpp` (store + forward sourceFilter)

**Why this task:** Backward-compatible API change. Adds an optional `sourceFilter` parameter (defaulting to `"all"`) at every layer of the pack-search call chain. No UI yet — Task 3 builds that. After this task, behavior is identical to today (the gate `if (sourceFilter == "all")` lets everything through), but the plumbing is ready.

- [ ] **Step 1: Update `StreamAggregator::searchPacks` signature in the header**

Open `src/core/stream/StreamAggregator.h`. Find the `searchPacks` declaration near line 56:

```cpp
    void searchPacks(const QString& imdbId,
                     const QString& showName,
                     int season);
```

Replace with:
```cpp
    // sourceFilter "all" -> dispatch all 6 indexers (default).
    // sourceFilter "<id>" -> dispatch only that indexer; valid ids match the
    // dispatch loop's QStringLiteral keys (nyaa, piratebay, 1337x, yts, eztv,
    // exttorrents). Unknown ids dispatch nothing (caller responsibility).
    void searchPacks(const QString& imdbId,
                     const QString& showName,
                     int season,
                     const QString& sourceFilter = QStringLiteral("all"));
```

- [ ] **Step 2: Update `StreamAggregator::searchPacks` implementation signature in the .cpp**

Open `src/core/stream/StreamAggregator.cpp` at line 685. Update the function signature to match:

Current:
```cpp
void StreamAggregator::searchPacks(const QString& imdbId,
                                   const QString& showName,
                                   int season)
{
```

Replace with:
```cpp
void StreamAggregator::searchPacks(const QString& imdbId,
                                   const QString& showName,
                                   int season,
                                   const QString& sourceFilter)
{
```

(Default value lives only in the header per Qt/C++ convention.)

- [ ] **Step 3: Gate the dispatch loop by sourceFilter**

In the same file, find the dispatch loop (now 6 calls after Task 1). Wrap each `dispatch(...)` call in a check against `sourceFilter`. Use a small lambda to keep this readable:

Replace this block:
```cpp
    for (const QString& query : queries) {
        dispatch(QStringLiteral("nyaa"),
                 new NyaaIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("piratebay"),
                 new PirateBayIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("1337x"),
                 new X1337xIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("yts"),
                 new YtsIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("eztv"),
                 new EztvIndexer(m_packNam, this), query);
        dispatch(QStringLiteral("exttorrents"),
                 new ExtTorrentsIndexer(m_packNam, this), query);
    }
```

With:
```cpp
    // THEATRE_SOURCE_PICKER 2026-05-17: gate each indexer by sourceFilter.
    // "all" (default) preserves the existing fan-out shape. A specific id
    // skips siblings, letting the Theatre source-combo UI route to a single
    // indexer (e.g. "nyaa" for anime).
    auto wants = [&](const QString& id) {
        return sourceFilter == QStringLiteral("all") || sourceFilter == id;
    };
    for (const QString& query : queries) {
        if (wants(QStringLiteral("nyaa")))
            dispatch(QStringLiteral("nyaa"),
                     new NyaaIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("piratebay")))
            dispatch(QStringLiteral("piratebay"),
                     new PirateBayIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("1337x")))
            dispatch(QStringLiteral("1337x"),
                     new X1337xIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("yts")))
            dispatch(QStringLiteral("yts"),
                     new YtsIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("eztv")))
            dispatch(QStringLiteral("eztv"),
                     new EztvIndexer(m_packNam, this), query);
        if (wants(QStringLiteral("exttorrents")))
            dispatch(QStringLiteral("exttorrents"),
                     new ExtTorrentsIndexer(m_packNam, this), query);
    }
```

- [ ] **Step 4: Update `UnifiedPackSearchEngine::search` signature in the header**

Open `src/core/stream/UnifiedPackSearchEngine.h`. Find the `search` declaration near line 64:

```cpp
    void search(const QString& imdbId, const QString& showName, int season);
```

Replace with:
```cpp
    // sourceFilter forwarded to StreamAggregator::searchPacks - "all" =
    // fan out to every Tankorent indexer (default); "<id>" = single
    // indexer. See StreamAggregator::searchPacks for valid id keys.
    void search(const QString& imdbId, const QString& showName, int season,
                const QString& sourceFilter = QStringLiteral("all"));
```

Also add a private member to track the in-flight source filter. Find the private member block (after `m_pendingSeason`):

```cpp
    QString           m_pendingImdb;
    QString           m_pendingShow;
    int               m_pendingSeason      = 0;
    int               m_pendingSourceCount = 0;
    int               m_totalEmitted       = 0;
```

Add a new member after `m_pendingSeason`:
```cpp
    QString           m_pendingImdb;
    QString           m_pendingShow;
    int               m_pendingSeason      = 0;
    QString           m_pendingSource      = QStringLiteral("all");
    int               m_pendingSourceCount = 0;
    int               m_totalEmitted       = 0;
```

- [ ] **Step 5: Update `UnifiedPackSearchEngine::search` impl + forward sourceFilter**

Open `src/core/stream/UnifiedPackSearchEngine.cpp`. Update the function signature to match the header, store sourceFilter into m_pendingSource, then forward it to the aggregator call.

Find the `search` function definition (search for `void UnifiedPackSearchEngine::search`). Update its signature to take the new param, store the param into `m_pendingSource` near where `m_pendingImdb` etc. are assigned (around line 50-60), and pass `m_pendingSource` to `m_aggregator->searchPacks(...)` at line 71.

For example, the assignment block currently looks like:
```cpp
    m_pendingImdb        = imdbId;
    m_pendingShow        = showName;
    m_pendingSeason      = season;
    m_totalEmitted       = 0;
    m_pendingSourceCount = 1;
```

Change to:
```cpp
    m_pendingImdb        = imdbId;
    m_pendingShow        = showName;
    m_pendingSeason      = season;
    m_pendingSource      = sourceFilter;
    m_totalEmitted       = 0;
    m_pendingSourceCount = 1;
```

And the aggregator call at line 71:
```cpp
    m_aggregator->searchPacks(imdbId, showName, season);
```

Change to:
```cpp
    m_aggregator->searchPacks(imdbId, showName, season, sourceFilter);
```

- [ ] **Step 6: Build verify**

Run from repo root:
```
build_check.bat
```

Expected: `BUILD OK`. If failure: likely a signature mismatch — verify the .h matches the .cpp exactly for both `searchPacks` and `search`. The default arg lives in the header only.

- [ ] **Step 7: ASCII sweep**

Run from repo root (PowerShell):
```powershell
Get-Content src/core/stream/StreamAggregator.h, src/core/stream/StreamAggregator.cpp, src/core/stream/UnifiedPackSearchEngine.h, src/core/stream/UnifiedPackSearchEngine.cpp -Encoding Byte | Where-Object { $_ -gt 127 } | Select-Object -First 5
```

Expected: zero output.

- [ ] **Step 8: Wait** — do not post RTC for this task. The Piece B (B.1 + B.2 + B.3) RTC is bundled at the end of Task 5.

---

## Task 3: Piece B.2 — Build the source combo widget in TheatreDownloadPanel

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h` (declare `m_sourceCombo` + `m_sourceFilter` member)
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp` (build + style + place the combo, above the filter chip row)

- [ ] **Step 1: Declare the combo + filter member in the header**

Open `src/ui/pages/stream/TheatreDownloadPanel.h`. Find the existing widget pointer members section (likely near other `QWidget* m_...` declarations, around the `m_filterChipRow` declaration). Add:

```cpp
    QComboBox* m_sourceCombo = nullptr;
    QString    m_sourceFilter = QStringLiteral("all");
```

Also add the forward declaration at the top if not already present:
```cpp
class QComboBox;
```

(Search the header for an existing forward-decl block — there's likely already one for QWidget, QLabel, etc. Add `QComboBox` to that block.)

- [ ] **Step 2: Include `<QComboBox>` in TheatreDownloadPanel.cpp**

Open `src/ui/pages/stream/TheatreDownloadPanel.cpp`. Locate the Qt includes block near the top. Add:
```cpp
#include <QComboBox>
```

Place it alphabetically with the other Qt includes (next to `<QCheckBox>` or `<QFrame>` — wherever it fits the existing pattern).

- [ ] **Step 3: Build + style the combo in `buildPackListState`, above the filter chip row**

In `src/ui/pages/stream/TheatreDownloadPanel.cpp::buildPackListState` (around line 181-198), the current code creates `m_filterChipRow` and adds it to `col`. Insert a new "Source" row immediately BEFORE the filter chip row creation.

Find this block (around lines 193-198):
```cpp
    m_filterChipRow = new QWidget(m_packListPage);
    m_filterChipRow->setObjectName(QStringLiteral("TheatreDownloadFilterChipRow"));
    auto* chipLayout = new QHBoxLayout(m_filterChipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(6);
    col->addWidget(m_filterChipRow);
```

Insert ABOVE this block:
```cpp
    // THEATRE_SOURCE_PICKER 2026-05-17 - source-selection dropdown.
    // Mirrors the standalone Tankorent tab's source combo
    // (TankorentPage.cpp:1136-1143). "All Sources" default fans out to
    // all 6 Tankorent indexers; explicit source pick gates dispatch in
    // StreamAggregator::searchPacks. Distinct from the source-FILTER
    // chip row removed by the chip-simplification arc earlier today:
    // that was a post-search result filter; this is a pre-search
    // dispatch gate.
    {
        auto* sourceRow = new QWidget(m_packListPage);
        sourceRow->setObjectName(QStringLiteral("TheatreDownloadSourceRow"));
        auto* sourceLayout = new QHBoxLayout(sourceRow);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        sourceLayout->setSpacing(8);

        auto* label = new QLabel(QStringLiteral("Source"), sourceRow);
        label->setObjectName(QStringLiteral("TheatreDownloadSourceLabel"));
        label->setStyleSheet(
            "color: rgba(255,255,255,0.65); font-size: 12px;");
        sourceLayout->addWidget(label);

        m_sourceCombo = new QComboBox(sourceRow);
        m_sourceCombo->setObjectName(QStringLiteral("TheatreDownloadSourceCombo"));
        m_sourceCombo->setFixedHeight(28);
        m_sourceCombo->setMinimumWidth(160);
        // Option list matches TankorentPage.cpp:1136-1143 minus
        // torrents-csv (books-only; preserved off the Theatre surface
        // to mirror existing parity).
        m_sourceCombo->addItem(QStringLiteral("All Sources"),  QStringLiteral("all"));
        m_sourceCombo->addItem(QStringLiteral("Nyaa"),         QStringLiteral("nyaa"));
        m_sourceCombo->addItem(QStringLiteral("PirateBay"),    QStringLiteral("piratebay"));
        m_sourceCombo->addItem(QStringLiteral("1337x"),        QStringLiteral("1337x"));
        m_sourceCombo->addItem(QStringLiteral("YTS"),          QStringLiteral("yts"));
        m_sourceCombo->addItem(QStringLiteral("EZTV"),         QStringLiteral("eztv"));
        m_sourceCombo->addItem(QStringLiteral("ExtraTorrents"), QStringLiteral("exttorrents"));
        m_sourceCombo->setStyleSheet(
            "QComboBox#TheatreDownloadSourceCombo {"
            " background: rgba(255,255,255,0.04);"
            " color: #f3f4f6;"
            " border: 1px solid rgba(255,255,255,0.10);"
            " border-radius: 4px;"
            " padding: 2px 8px;"
            "}"
            "QComboBox#TheatreDownloadSourceCombo:hover {"
            " background: rgba(255,255,255,0.07);"
            "}"
            "QComboBox#TheatreDownloadSourceCombo::drop-down {"
            " border: none;"
            "}");
        sourceLayout->addWidget(m_sourceCombo);
        sourceLayout->addStretch(1);

        col->addWidget(sourceRow);
    }
```

- [ ] **Step 4: Build verify**

Run from repo root:
```
build_check.bat
```

Expected: `BUILD OK`. If failure: most likely a missing forward decl or include in the header — verify `QComboBox` forward-decl exists in `TheatreDownloadPanel.h` and `<QComboBox>` is included in `.cpp`.

- [ ] **Step 5: ASCII sweep**

```powershell
Get-Content src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp -Encoding Byte | Where-Object { $_ -gt 127 } | Select-Object -First 5
```

Expected: zero output.

- [ ] **Step 6: Wait** — do not post RTC. Bundled at Task 5.

---

## Task 4: Piece B.3 — Wire combo + QSettings persistence + thread filter to engine

**Files:**
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.h` (declare slot + persistence helpers)
- Modify: `src/ui/pages/stream/TheatreDownloadPanel.cpp` (connect signal, save/load QSettings, pass filter to engine)

- [ ] **Step 1: Declare slot + helpers in header**

Open `src/ui/pages/stream/TheatreDownloadPanel.h`. In the existing private slots block (search for `private slots:` or similar — there should be other slots near `onFilterChipClicked`), add:

```cpp
    void onSourceComboChanged(int index);
```

In the private helpers block (near `buildPackListState()` etc.), add:

```cpp
    void loadPersistedSource();
    void savePersistedSource();
```

If there's no existing helpers block, place the two helpers next to other private member functions.

Also add `#include <QSettings>` to the header forward-decl block — actually `QSettings` is typically included in the .cpp only. Use `class QSettings;` forward-decl in the header IF the header references it; otherwise include in the .cpp only. For this task, both helpers' implementations are .cpp-only, so include `<QSettings>` in the .cpp (next step).

- [ ] **Step 2: Add `#include <QSettings>` to TheatreDownloadPanel.cpp**

Open `src/ui/pages/stream/TheatreDownloadPanel.cpp`. Add to the Qt includes block:

```cpp
#include <QSettings>
```

Place alphabetically.

- [ ] **Step 3: Implement helpers**

In `src/ui/pages/stream/TheatreDownloadPanel.cpp`, add the two helper definitions near the end of the file (after the last existing function, before the closing namespace). Use the exact bodies below:

```cpp
void TheatreDownloadPanel::loadPersistedSource() {
    if (!m_sourceCombo) return;
    QSettings settings;
    const QString saved = settings.value(
        QStringLiteral("theatre/pack_panel/source"),
        QStringLiteral("all")).toString();
    m_sourceFilter = saved;
    const int idx = m_sourceCombo->findData(saved);
    if (idx >= 0) {
        // setCurrentIndex emits currentIndexChanged - block during
        // initial load so we don't re-save the value we just read.
        const QSignalBlocker blocker(m_sourceCombo);
        m_sourceCombo->setCurrentIndex(idx);
    }
}

void TheatreDownloadPanel::savePersistedSource() {
    QSettings settings;
    settings.setValue(QStringLiteral("theatre/pack_panel/source"),
                      m_sourceFilter);
}
```

(`QSignalBlocker` is RAII-scoped — restores signals on destruction. Include `<QSignalBlocker>` if not already pulled in; usually it comes for free via `<QObject>`.)

- [ ] **Step 4: Implement the slot**

In `src/ui/pages/stream/TheatreDownloadPanel.cpp`, add the slot definition near the other slot impls (search for `onFilterChipClicked` and place this nearby):

```cpp
void TheatreDownloadPanel::onSourceComboChanged(int /*index*/) {
    if (!m_sourceCombo) return;
    m_sourceFilter = m_sourceCombo->currentData().toString();
    if (m_sourceFilter.isEmpty())
        m_sourceFilter = QStringLiteral("all");
    savePersistedSource();
    // Note: we do NOT re-fire the in-flight search here. Source change
    // takes effect on the NEXT search() call (i.e. when the panel is
    // dismissed + re-opened, or when the engine is invoked again).
    // Re-firing mid-render would orphan an in-flight Tankorent fan-out
    // and complicate the stale-callback guards in
    // UnifiedPackSearchEngine::onTankorentPacksAvailable.
}
```

- [ ] **Step 5: Connect the combo + call loadPersistedSource in `buildPackListState`**

In `src/ui/pages/stream/TheatreDownloadPanel.cpp::buildPackListState`, AFTER the `col->addWidget(sourceRow);` line from Task 3, add the signal connect:

```cpp
        connect(m_sourceCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &TheatreDownloadPanel::onSourceComboChanged);
        loadPersistedSource();
    }   // end of source-row scope block from Task 3
```

(The `loadPersistedSource()` call inside the same scope block sets the combo's initial index from QSettings; the QSignalBlocker inside ensures the slot doesn't fire during the load.)

- [ ] **Step 6: Pass m_sourceFilter to engine->search at the 2 call sites**

In `src/ui/pages/stream/TheatreDownloadPanel.cpp`, find the two `m_searchEngine->search(...)` call sites (line 143 and line 982 in the current file). Update each.

Call site at line 143 (currently):
```cpp
    if (m_searchEngine)
        m_searchEngine->search(imdbId, showName, season);
```

Replace with:
```cpp
    if (m_searchEngine)
        m_searchEngine->search(imdbId, showName, season, m_sourceFilter);
```

Call site at line 982 (currently):
```cpp
    if (m_searchEngine)
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0);
```

Replace with:
```cpp
    if (m_searchEngine)
        m_searchEngine->search(m_imdbId, m_showName, /*season=*/0, m_sourceFilter);
```

- [ ] **Step 7: Build verify**

Run from repo root:
```
build_check.bat
```

Expected: `BUILD OK`. Common failure: `QSignalBlocker` not found — add `#include <QSignalBlocker>` if needed. Or `QOverload` issue — verify Qt6 syntax (older Qt5 patterns differ).

- [ ] **Step 8: ASCII sweep**

```powershell
Get-Content src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp -Encoding Byte | Where-Object { $_ -gt 127 } | Select-Object -First 5
```

Expected: zero output.

- [ ] **Step 9: Wait** — RTC for Piece B bundled at Task 5.

---

## Task 5: Piece B — Self-review + bundled RTC

**Files:** none modified — review-only task.

- [ ] **Step 1: Re-read the changed files end-to-end**

Read these files in full one last time:
- `src/core/stream/StreamAggregator.h` — verify default arg present, signature matches .cpp
- `src/core/stream/StreamAggregator.cpp` — verify dispatch gate uses `wants()` lambda, 6 indexers, default param NOT repeated in .cpp signature
- `src/core/stream/UnifiedPackSearchEngine.h` — verify default arg + new member `m_pendingSource`
- `src/core/stream/UnifiedPackSearchEngine.cpp` — verify sourceFilter forwarded to aggregator at line 71
- `src/ui/pages/stream/TheatreDownloadPanel.h` — verify combo + filter member + slot + helpers all declared
- `src/ui/pages/stream/TheatreDownloadPanel.cpp` — verify combo built, connect wired, loadPersistedSource called, slot+helpers implemented, both `m_searchEngine->search` calls pass m_sourceFilter

Things to watch for:
- Did the `wants()` lambda in StreamAggregator.cpp accidentally drop the `m_packNam`-vs-`m_nam` parameter? They are different members — pack search uses `m_packNam`. Verify each `new <Indexer>(m_packNam, this)` is preserved.
- Does loadPersistedSource handle the case where saved value is unknown (e.g. "nyaa" saved but user deletes Nyaa from the option list later)? Current code: `findData` returns -1 if not found, and we just leave the combo at its default index 0 ("All Sources"). m_sourceFilter is still set to the saved value, which means the engine call would send an unknown id. The StreamAggregator gate (`sourceFilter == "<id>"`) would match nothing, dispatching zero indexers, and the panel would show an empty result. Acceptable fallback for v1; flag for follow-up if Hemanth wants stricter validation.
- Are there other callers of `searchPacks` or `UnifiedPackSearchEngine::search`? Run a grep to confirm the two we plumbed are exhaustive:

```
Grep pattern: searchPacks\(  in src/
Grep pattern: ->search\(.*imdbId  in src/ui/pages/stream/
```

If any caller surfaces beyond the two we updated, the default arg covers it (backward-compatible). But verify.

- [ ] **Step 2: Compile-only final verify**

```
build_check.bat
```

Expected: `BUILD OK` (already verified in Task 4, but re-running cheap insurance).

- [ ] **Step 3: Append bundled RTC for Piece B to chat.md**

Append to the end of `agents/chat.md`:

```
## Agent 4 - Piece B: Theatre source picker dropdown - 2026-05-17

READY TO COMMIT - [Agent 4, Piece B: Theatre source picker shipped per docs/superpowers/specs/2026-05-17-theatre-source-picker-and-nyaa-fixes-design.md. Three sub-pieces bundled: (B.1 API plumbing) Added optional sourceFilter param (default "all") to StreamAggregator::searchPacks + UnifiedPackSearchEngine::search; gated the 6-indexer dispatch loop in StreamAggregator with a wants() lambda; threaded sourceFilter through UnifiedPackSearchEngine as new m_pendingSource member + forwarded to aggregator->searchPacks. Backward-compatible at every API boundary. (B.2 UI) Added QComboBox m_sourceCombo to TheatreDownloadPanel above the existing 4-chip type filter row in buildPackListState. Option list: All Sources / Nyaa / PirateBay / 1337x / YTS / EZTV / ExtraTorrents (matches TankorentPage.cpp:1136-1143 minus torrents-csv which is books-only on the standalone Tankorent tab). 28px height, 160px min width, grayscale QSS matching panel control language (#TheatreDownloadSourceCombo selector + hover + drop-down). (B.3 wire + persist) QSettings key theatre/pack_panel/source persists across launches (default "all"); QSignalBlocker prevents re-save during load; onSourceComboChanged slot updates m_sourceFilter + saves; both m_searchEngine->search call sites in the panel (TheatreDownloadPanel.cpp:143 + :982) pass m_sourceFilter. Source change does NOT re-fire in-flight search - takes effect on next search() invocation (panel dismiss + reopen). Edge case: unknown saved id falls through to empty-result render; acceptable v1 fallback (flagged for follow-up). The chip-simplification arc earlier today removed source-FILTER chips (post-search result filtering); this source-PICKER dropdown gates dispatch pre-search - explicit code comment in TheatreDownloadPanel.cpp distinguishes the two surfaces. ~110 LOC added across 6 files, zero LOC removed. build_check.bat BUILD OK at every task boundary. ASCII sweep clean. Hemanth smoke matrix: (1) open Demon Slayer in Theatre + click Layers-3 -> "Source" dropdown visible above the chip row, defaults to "All Sources", pack list populated as today. (2) pick "Nyaa" from dropdown -> dismiss panel + click Layers-3 again -> only Nyaa packs in tile list. (3) kill Tankoban via taskkill + relaunch via build_and_run.bat -> Layers-3 dropdown still shows "Nyaa" pick. (4) pick "All Sources" -> next search fans out to all 6 indexers. Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review].] | Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /superpowers:subagent-driven-development, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/core/stream/StreamAggregator.h, src/core/stream/StreamAggregator.cpp, src/core/stream/UnifiedPackSearchEngine.h, src/core/stream/UnifiedPackSearchEngine.cpp, src/ui/pages/stream/TheatreDownloadPanel.h, src/ui/pages/stream/TheatreDownloadPanel.cpp, agents/chat.md
```

- [ ] **Step 4: Hand back to Hemanth for smoke**

Once the RTC is appended, work is complete. Hemanth must rebuild via `build_and_run.bat` and run the 4-test smoke matrix described in the RTC. If any smoke fails, return to Phase 1 of `/superpowers:systematic-debugging` — do not patch-on-top blindly.

---

## Self-review pass (done by plan-writer)

**Spec coverage:** Spec sections A.1 + A.2 → Task 1. B.1 → Task 2. B.2 → Task 3. B.3 → Task 4 (combined with B.4 + B.5 from the spec — "above the chip row" placement is realized in Task 3 step 3). B.5 placement → Task 3 step 3. Out-of-scope items (anime auto-detect, override toggle, Sources sidebar changes, TorrentPackPicker, IndexerStatusPanel changes) → not touched. Spec's "Risks + edge cases" risk 3 (chip-simplification overlap) → explicitly commented in Task 3 step 3 source-row block. ✓

**Placeholder scan:** Zero TBD / TODO / "fill in later" / "similar to Task N" / "add error handling" patterns. Every code block is complete. ✓

**Type consistency:** `sourceFilter` parameter name consistent across StreamAggregator.h + .cpp + UnifiedPackSearchEngine.h + .cpp. `m_sourceFilter` member naming consistent in TheatreDownloadPanel. `m_pendingSource` chosen to mirror the `m_pendingImdb` / `m_pendingShow` / `m_pendingSeason` naming family in UnifiedPackSearchEngine. ✓

**Ambiguity check:** "Above the filter chip row" → resolved by Task 3 step 3's exact insertion point. "Default 'all'" → consistent. The two `m_searchEngine->search` call sites (lines 143 + 982) → both updated, not just one. ✓
