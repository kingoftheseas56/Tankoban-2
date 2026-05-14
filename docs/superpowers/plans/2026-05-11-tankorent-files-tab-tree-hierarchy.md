# Tankorent Files Tab — Tree Hierarchy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render Tankorent's "Torrent Properties → Files" tab as a properly nested folder/file tree (matching Deluge's behavior in the reference screenshot) instead of the current single-level-collapse where multi-level paths render as flat-looking rows with a long path-prefix label.

**Architecture:** Rewrite `TorrentFilesTab::populateTree` to walk each file path component-by-component (split on `/`), creating intermediate `QTreeWidgetItem` folder rows on demand and nesting children under them. Folder rows display only the leaf folder name (not the cumulative prefix). Folder rows aggregate Size (sum of leaf sizes) and Progress (size-weighted average) and update on each `refresh()` tick. The right-click priority-cascade in `onTreeContextMenu` extends from one-level-deep to fully recursive over all descendants.

**Tech Stack:** Qt 6.10 (`QTreeWidget`, `QTreeWidgetItem`, `QHash`), C++20.

> **POSTMORTEM CORRECTION 2026-05-11 ~19:30** — the original plan claimed libtorrent's `file_path(i)` returns POSIX-separator paths regardless of host OS. **This is wrong on Windows.** libtorrent-source/src/file_storage.cpp:58-62 defines `TORRENT_SEPARATOR` as `'\\'` on `TORRENT_WINDOWS` (and `'/'` elsewhere). The first ship of this plan split on `'/'` only, found zero separators in real Windows paths, treated every leaf as a top-level row, and Hemanth's smoke reproduced the original flat-rendering bug verbatim despite the fix being "live." Corrected by splitting on BOTH separators via `QRegularExpression(R"([\\/])")` so the implementation is host-portable AND defensive against any libtorrent normalization change. **Memory:** save `feedback_libtorrent_windows_backslash_separator.md` so future agents don't replay the same audit-claim error.

---

## Audience & Caller Context

- **Owner:** Agent 4B (Tankorent / Sources domain).
- **Single file under edit:** `src/ui/pages/tankorent/TorrentFilesTab.cpp`. The header at `src/ui/pages/tankorent/TorrentFilesTab.h` gains two private members (a folder lookup map + a folder metadata struct).
- **Trigger:** Hemanth screenshot 2026-05-11 ~18:45 showing the Community.S01-S06.COMPLETE.1080p.BluRay.DD5.1.With.Commentary.x265-POIASD torrent rendering ~15 visually-identical rows in the Files tab — actually folder rows whose labels are full path prefixes like `"Community.S01-S06.../Season 1"` — alongside the Deluge reference (One Pace torrent) showing the correct expandable tree with `Background.jpg`, `Poster.png`, `Season 1 [1-7] Romance Dawn …`, etc. nested under the torrent root.
- **What's NOT in scope:** changing the libtorrent priority scale, changing the `torrentFiles` JSON contract on `TorrentEngine`, touching the General/Trackers/Peers tabs, touching `TorrentPropertiesWidget` itself, or sort-order changes. Sort + folder/file icon work can be follow-up RTCs.

## Root-cause Summary (so the executor isn't guessing)

`TorrentFilesTab::populateTree` at `src/ui/pages/tankorent/TorrentFilesTab.cpp:146-202` does this:

```cpp
const int sep = fullPath.lastIndexOf('/');
const QString dir  = sep > 0 ? fullPath.left(sep) : QString();
const QString name = sep > 0 ? fullPath.mid(sep + 1) : fullPath;
…
if (!dirItems.contains(dir)) {
    auto* d = new QTreeWidgetItem(m_tree);
    d->setText(0, dir);                 // ← FULL prefix used as label
    …
    dirItems[dir] = d;
}
parent = dirItems[dir];
```

Two compounding bugs:
1. Each unique `dir` string gets ONE top-level folder row — so `Pack/Season 1` and `Pack/Season 2` both become top-level peers, never nesting under a shared `Pack/` parent.
2. The folder row's display text is the entire prefix path, so every folder under the same torrent-root reads `"<TorrentRootName>/Season N"` and visually truncates to look identical.

Fix is to split `fullPath` on `'/'` into ALL components and walk them with a cumulative-prefix lookup, creating each intermediate folder row exactly once and labeling it with only the trailing component name.

## File Structure

- **Modify:** `src/ui/pages/tankorent/TorrentFilesTab.h` — add a `FolderRow` struct + `QHash<QString, FolderRow>` member.
- **Modify:** `src/ui/pages/tankorent/TorrentFilesTab.cpp` — rewrite `populateTree`, add recursive helper `static int cascadePriorityToDescendants(QTreeWidgetItem*, int comboIdx, …)`, extend `refresh()` to update folder progress text in place, extend `setInfoHash` to clear the new folder map.

No new files. No CMakeLists.txt change (existing entries cover this pair).

## Task Decomposition

There are six tasks. Tasks 1–4 are code edits; Task 5 builds; Task 6 commits. Build verify happens once at the end because each individual edit doesn't make the file compile standalone (the header + cpp changes are co-dependent).

---

### Task 1: Add folder-row tracking to the header

**Files:**
- Modify: `src/ui/pages/tankorent/TorrentFilesTab.h:36-58`

- [ ] **Step 1: Add a forward-decl-friendly folder-row struct and a lookup map to the private section.**

Open `src/ui/pages/tankorent/TorrentFilesTab.h`. Below the existing `FileRow` struct + `QMap<int, FileRow> m_rows;` member (currently ending at line 58), insert the folder tracking:

```cpp
    // Folder rows by their cumulative path key (e.g. "Pack/Season 1"). Keys
    // use '/' as separator regardless of host OS — libtorrent file_path() is
    // always POSIX-style. Used during populateTree to deduplicate parent
    // folder creation, and during refresh() to update folder-level progress
    // text in place.
    struct FolderRow {
        QString          pathKey;     // cumulative path from root (no trailing '/')
        qint64           totalSize = 0;
        QTreeWidgetItem* item     = nullptr;
        // Indices of leaf files anywhere below this folder; used to recompute
        // the aggregate progress on each refresh() tick without re-walking
        // the engine's file list.
        QList<int>       descendantFileIndexes;
    };
    QHash<QString, FolderRow> m_folders;
```

Also add `#include <QHash>` and `#include <QList>` at the top of the header if they aren't already pulled in by `<QMap>`. They're transitively available via Qt but be explicit — saves a header chase.

Add `<QList>` to the existing `#include` block at lines 3–5:

```cpp
#include <QWidget>
#include <QString>
#include <QMap>
#include <QHash>
#include <QList>
```

- [ ] **Step 2: Declare the recursive priority-cascade helper.**

Inside the same `private:` section, just below the `static QString priorityLabel(int);` declaration around line 43, add:

```cpp
    // Apply the given combo index to this item and to every descendant leaf.
    // Returns true if at least one leaf priority was changed (used by callers
    // to decide whether to push priorities to the engine).
    void cascadePriorityToDescendants(QTreeWidgetItem* root, int comboIdx);
```

Make it non-static because it reads `m_rows` to find each leaf's combo widget. Keep the existing `static` helpers (`priorityComboIndex`, `libtorrentPriorityForComboIndex`, `priorityLabel`) as they are — they're pure mappings.

---

### Task 2: Rewrite `populateTree` to build a true multi-level hierarchy

**Files:**
- Modify: `src/ui/pages/tankorent/TorrentFilesTab.cpp:146-202`

- [ ] **Step 1: Clear the new folder map alongside the existing maps in `setInfoHash`.**

At `src/ui/pages/tankorent/TorrentFilesTab.cpp:131-134`, the current `setInfoHash` body opens with:

```cpp
    m_infoHash = infoHash;
    m_tree->clear();
    m_rows.clear();
```

Add one line right below `m_rows.clear();`:

```cpp
    m_folders.clear();
```

This keeps the folder lookup in sync when the user switches torrents in the same Properties dialog.

- [ ] **Step 2: Replace the body of `populateTree` end-to-end.**

The entire current body (lines 146-202) is replaced. Open the file and select from the opening `void TorrentFilesTab::populateTree(const QString& /*rootName*/)` through the closing `}` of that function. Replace with:

```cpp
void TorrentFilesTab::populateTree(const QString& /*rootName*/)
{
    const QJsonArray files = m_client->engine()->torrentFiles(m_infoHash);

    // Pass 1: emit folder rows + leaf rows, walking each file path
    // component-by-component to build a true multi-level QTreeWidget tree.
    //
    // libtorrent file_path(i) returns POSIX-separator relative paths
    // regardless of host OS. Split on literal '/' — do NOT use
    // QDir::separator() here.
    for (const auto& v : files) {
        const QJsonObject obj = v.toObject();
        const int     idx      = obj.value("index").toInt();
        const QString fullPath = obj.value("name").toString();
        const qint64  sz       = obj.value("size").toVariant().toLongLong();
        const double  progress = obj.value("progress").toDouble();
        const int     prio     = obj.value("priority").toInt(kPrioNormal);

        const QStringList parts = fullPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;  // defensive — empty paths shouldn't reach us

        // Walk all but the last component, creating/looking-up folder items
        // along the way. The last component is the leaf file.
        QTreeWidgetItem* parent = nullptr;
        QString          cumulative;
        for (int p = 0; p < parts.size() - 1; ++p) {
            const QString& segment = parts[p];
            cumulative = cumulative.isEmpty() ? segment : cumulative + '/' + segment;

            auto fit = m_folders.find(cumulative);
            if (fit == m_folders.end()) {
                auto* folderItem = parent
                    ? new QTreeWidgetItem(parent)
                    : new QTreeWidgetItem(m_tree);
                folderItem->setText(0, segment);          // leaf folder name only
                folderItem->setData(0, ROLE_FILE_INDEX, -1);
                folderItem->setExpanded(true);

                FolderRow fr;
                fr.pathKey = cumulative;
                fr.item    = folderItem;
                fit = m_folders.insert(cumulative, fr);
            }
            // Accumulate this file's size into every ancestor folder.
            fit->totalSize += sz;
            fit->descendantFileIndexes.append(idx);
            parent = fit->item;
        }

        // Emit the leaf row.
        const QString leafName = parts.last();
        auto* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_tree);
        item->setText(0, leafName);
        item->setData(0, ROLE_FILE_INDEX, idx);
        item->setText(1, humanSize(sz));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, QString::number(progress * 100.0, 'f', 1) + "%");
        item->setTextAlignment(2, Qt::AlignCenter);

        auto* combo = new QComboBox;
        combo->addItems({ "Skip", "Low", "Normal", "High", "Maximum" });
        combo->setCurrentIndex(priorityComboIndex(prio));
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, idx](int comboIdx) {
                    onPriorityCombo(idx, libtorrentPriorityForComboIndex(comboIdx));
                });
        m_tree->setItemWidget(item, 3, combo);

        FileRow row;
        row.index    = idx;
        row.fullPath = fullPath;
        row.size     = sz;
        row.item     = item;
        row.combo    = combo;
        m_rows.insert(idx, row);
    }

    // Pass 2: paint the aggregate Size + initial Progress text on each
    // folder row. Progress is recomputed live in refresh(); Size is static
    // for the lifetime of this populate (file sizes don't change).
    for (auto it = m_folders.begin(); it != m_folders.end(); ++it) {
        it->item->setText(1, humanSize(it->totalSize));
        it->item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);

        // Initial aggregate progress: size-weighted average across all
        // descendant leaves whose JSON we already saw above.
        double weightedSum = 0.0;
        qint64 totalBytes  = 0;
        for (int leafIdx : std::as_const(it->descendantFileIndexes)) {
            auto rowIt = m_rows.constFind(leafIdx);
            if (rowIt == m_rows.constEnd()) continue;
            // Use the raw text we set above to back out the percent. Cleaner
            // to recompute from the engine JSON later — see refresh().
            const QString pctText = rowIt->item->text(2);
            const double  pct     = pctText.left(pctText.size() - 1).toDouble() / 100.0;
            weightedSum += pct * static_cast<double>(rowIt->size);
            totalBytes  += rowIt->size;
        }
        const double folderPct = totalBytes > 0 ? (weightedSum / totalBytes) * 100.0 : 0.0;
        it->item->setText(2, QString::number(folderPct, 'f', 1) + "%");
        it->item->setTextAlignment(2, Qt::AlignCenter);
    }
}
```

Notes for the executor on what changed and why:

1. The previous code keyed `dirItems` by the entire path-minus-basename string AND used that string as the displayed folder label. The new code walks components and builds a cumulative key for lookup but displays only the trailing `segment`. That's the single load-bearing fix for Hemanth's screenshot.
2. `Qt::SkipEmptyParts` defends against malformed paths with leading/trailing/repeated slashes. libtorrent shouldn't emit those, but it's free defense.
3. Pass 2 paints folder Size + initial Progress so the columns line up with Deluge's visual on the first frame. `refresh()` (Task 3) takes over from there.

---

### Task 3: Extend `refresh()` to recompute folder-level progress live

**Files:**
- Modify: `src/ui/pages/tankorent/TorrentFilesTab.cpp:206-219`

- [ ] **Step 1: Replace the body of `refresh()`.**

Current body:

```cpp
void TorrentFilesTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;

    const QJsonArray files = m_client->engine()->torrentFiles(m_infoHash);
    for (const auto& v : files) {
        const QJsonObject obj = v.toObject();
        const int idx = obj.value("index").toInt();
        auto it = m_rows.find(idx);
        if (it == m_rows.end()) continue;
        const double progress = obj.value("progress").toDouble();
        it->item->setText(2, QString::number(progress * 100.0, 'f', 1) + "%");
    }
}
```

Replace with:

```cpp
void TorrentFilesTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;

    const QJsonArray files = m_client->engine()->torrentFiles(m_infoHash);

    // Stash live per-leaf progress so the folder pass below has fresh values
    // without a second engine query.
    QHash<int, double> progressByIdx;
    progressByIdx.reserve(files.size());

    for (const auto& v : files) {
        const QJsonObject obj = v.toObject();
        const int    idx      = obj.value("index").toInt();
        const double progress = obj.value("progress").toDouble();
        progressByIdx.insert(idx, progress);

        auto it = m_rows.find(idx);
        if (it == m_rows.end()) continue;
        it->item->setText(2, QString::number(progress * 100.0, 'f', 1) + "%");
    }

    // Folder rows: size-weighted average of descendant-leaf progress.
    for (auto fit = m_folders.begin(); fit != m_folders.end(); ++fit) {
        double weightedSum = 0.0;
        qint64 totalBytes  = 0;
        for (int leafIdx : std::as_const(fit->descendantFileIndexes)) {
            auto rowIt = m_rows.constFind(leafIdx);
            if (rowIt == m_rows.constEnd()) continue;
            const double pct = progressByIdx.value(leafIdx, 0.0);
            weightedSum += pct * static_cast<double>(rowIt->size);
            totalBytes  += rowIt->size;
        }
        const double folderPct = totalBytes > 0 ? (weightedSum / totalBytes) * 100.0 : 0.0;
        fit->item->setText(2, QString::number(folderPct, 'f', 1) + "%");
    }
}
```

Notes:
- Two-pass within one tick to keep all reads from a single `torrentFiles()` snapshot, avoiding drift if the engine state changes mid-refresh.
- `std::as_const` matches Qt's `QList` iteration idiom in this codebase (see TankorentPage.cpp and elsewhere).

---

### Task 4: Make the right-click priority cascade recurse to all descendants

**Files:**
- Modify: `src/ui/pages/tankorent/TorrentFilesTab.cpp:262-338`

- [ ] **Step 1: Add the recursive helper at the end of the file.**

Below the closing brace of `onTreeContextMenu`, insert:

```cpp
void TorrentFilesTab::cascadePriorityToDescendants(QTreeWidgetItem* root, int comboIdx)
{
    if (!root) return;

    // Depth-first walk. For each leaf hit, drive its combo to the new index
    // — that fires the existing currentIndexChanged signal which calls
    // writePrioritiesToEngine() once at the end via setCurrentIndex below.
    const int childCount = root->childCount();
    if (childCount == 0) {
        const int idx = root->data(0, ROLE_FILE_INDEX).toInt();
        if (idx < 0) return;            // empty folder, shouldn't happen
        auto rowIt = m_rows.find(idx);
        if (rowIt != m_rows.end() && rowIt->combo)
            rowIt->combo->setCurrentIndex(comboIdx);
        return;
    }
    for (int i = 0; i < childCount; ++i)
        cascadePriorityToDescendants(root->child(i), comboIdx);
}
```

- [ ] **Step 2: Replace the inline folder-cascade in `onTreeContextMenu`.**

In `onTreeContextMenu` (currently lines 271-289), the `setPriority` lambda contains:

```cpp
    auto setPriority = [this, item, idx, isFolder](int libtorrentPriority) {
        const int comboIdx = priorityComboIndex(libtorrentPriority);
        if (!isFolder) {
            auto rowIt = m_rows.find(idx);
            if (rowIt != m_rows.end() && rowIt->combo)
                rowIt->combo->setCurrentIndex(comboIdx);
        } else {
            // Apply to every descendant leaf
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                const int childIdx = child->data(0, ROLE_FILE_INDEX).toInt();
                if (childIdx < 0) continue;
                auto rowIt = m_rows.find(childIdx);
                if (rowIt != m_rows.end() && rowIt->combo)
                    rowIt->combo->setCurrentIndex(comboIdx);
            }
        }
        writePrioritiesToEngine();
    };
```

The `else` branch only walks one level — it skips children of subfolders. Replace the whole lambda with:

```cpp
    auto setPriority = [this, item, idx, isFolder](int libtorrentPriority) {
        const int comboIdx = priorityComboIndex(libtorrentPriority);
        if (!isFolder) {
            auto rowIt = m_rows.find(idx);
            if (rowIt != m_rows.end() && rowIt->combo)
                rowIt->combo->setCurrentIndex(comboIdx);
        } else {
            cascadePriorityToDescendants(item, comboIdx);
        }
        writePrioritiesToEngine();
    };
```

This routes both single-leaf folders and multi-level folders through the same recursive helper. No behavior change for single-level folders; multi-level folders now cascade to all descendant leaves.

---

### Task 5: Verify the build

**Files:** none modified.

- [ ] **Step 1: Kill any running Tankoban so the build doesn't trip on a locked binary.**

Run from the repo root:

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

Expected: prints a list of killed PIDs (or "no Tankoban processes found"). Exit code 0.

- [ ] **Step 2: Compile-only verification.**

Run:

```
build_check.bat
```

Expected last line: `BUILD OK`. If it fails, the most likely culprits are (a) a missed `#include <QHash>` in the header, (b) a typo in the cumulative-path key string concat, (c) `std::as_const` requiring `<utility>` (it does, but Qt headers pull it in transitively — if MSVC complains, add `#include <utility>` at the top of `TorrentFilesTab.cpp`).

- [ ] **Step 3: HEMANTH-FACING smoke checklist (do NOT run via MCP — this is the user-driven gate per the player-domain Hemanth-driven convention, but for Tankorent the MCP smoke is allowed if you choose).**

If Agent 4B does drive an MCP smoke instead of handing off:
1. `build_and_run.bat` and wait for the window to come up.
2. Open Tankorent tab → double-click any torrent with multi-level structure (Hemanth's Community.S01-S06.../Season N/episode.mkv pack, or any season pack with a root folder + Season subfolders).
3. Click the "Files" tab.
4. Expect: ONE top-level row labeled with just the torrent root folder name (e.g. `Community.S01-S06.COMPLETE.1080p.BluRay.DD5.1.With.Commentary.x265-POIASD`), expanded by default; underneath, one row per Season folder labeled `Season 1`, `Season 2`, … (no path prefix); underneath each Season, the individual episode files with their basenames only.
5. Folder rows show aggregate Size in the Size column (e.g. Season 1 total = sum of its episodes).
6. Folder rows show aggregate Progress %; on a downloading torrent the percent updates each second.
7. Right-click any Season folder → "Skip" → verify ALL episodes under that Season flip to Skip in their per-row dropdowns. Right-click the root folder → "Normal" → verify everything resets.
8. Right-click a single leaf file → "Maximum" → that one row's dropdown flips, no neighbors affected.

---

### Task 6: Commit

**Files:** staged.

- [ ] **Step 1: Stage the modified files.**

```
git add src/ui/pages/tankorent/TorrentFilesTab.h src/ui/pages/tankorent/TorrentFilesTab.cpp
```

- [ ] **Step 2: Append a READY TO COMMIT line to `agents/chat.md`.**

The body should be a single newline-free line per Rule 11. Suggested skeleton (Agent 4B's voice):

```
READY TO COMMIT - [Agent 4B, TANKORENT_FILES_TAB_TREE_HIERARCHY 2026-05-11 — Files tab in Torrent Properties now renders a true multi-level folder tree matching Deluge's reference behavior. populateTree() rewritten to walk each libtorrent file_path() component-by-component (split on '/') and build cumulative-key folder lookups, so paths like Pack/Season 1/episode.mkv now nest under a single Pack/ root with Season N children, instead of collapsing to flat-looking rows whose labels were the full prefix. Folder rows show leaf folder names only (Season 1 not Pack/Season 1), aggregate Size (sum of descendant leaf sizes) and aggregate Progress (size-weighted average, refreshed each 1Hz tick). Right-click priority-cascade now recurses through all descendants instead of one level. Setting context: Hemanth screenshot 2026-05-11 ~18:45 showed the Community.S01-S06.../Season N pack rendering ~15 visually-identical rows due to the truncated path-prefix label. New header members: FolderRow struct + QHash<QString, FolderRow> m_folders. New private method: cascadePriorityToDescendants. setInfoHash also clears m_folders alongside m_rows. build_check.bat BUILD OK. Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/pages/tankorent/TorrentFilesTab.h, src/ui/pages/tankorent/TorrentFilesTab.cpp, agents/chat.md, docs/superpowers/plans/2026-05-11-tankorent-files-tab-tree-hierarchy.md
```

Then `git add agents/chat.md docs/superpowers/plans/2026-05-11-tankorent-files-tab-tree-hierarchy.md`.

- [ ] **Step 3: Done — leave the actual commit to Agent 0's next `/commit-sweep`.**

Per Rule 11, agents flag READY TO COMMIT in chat.md; Agent 0 batches commits. Do NOT run `git commit` here.

---

## Self-Review

Run this against the spec (Hemanth screenshot + Deluge reference) before handing off.

1. **Spec coverage:**
   - Multi-level nesting via component walk → Task 2 ✓
   - Folder labels show leaf component only, not cumulative prefix → Task 2 Step 2 (`folderItem->setText(0, segment)`) ✓
   - Folder Size aggregate → Task 2 Pass 2 ✓
   - Folder Progress aggregate + live refresh → Task 2 Pass 2 + Task 3 ✓
   - Folder priority cascade reaches multi-level descendants → Task 4 ✓
   - Map cleared on torrent switch → Task 2 Step 1 ✓

2. **Placeholder scan:** No TBDs, no "add error handling", no "similar to Task N", no untyped method references. Every code step has full code.

3. **Type consistency:** `cascadePriorityToDescendants` declared in header Task 1 Step 2 and defined in body Task 4 Step 1 — same signature `(QTreeWidgetItem*, int)`. `FolderRow` struct fields used in Tasks 2 + 3 match Task 1 declaration. `m_folders` keyed by cumulative path string everywhere.

4. **Out-of-scope creep watch:** No changes to `torrentFiles` JSON contract. No changes to `TorrentPropertiesWidget`. No changes to General/Trackers/Peers tabs. No CMakeLists changes. No QSS theming changes. No icon work. No sort changes.

## Optional Follow-ups (separate RTC, ask Hemanth first)

- Folder/file icon column-0 painting (Deluge uses tiny page + folder icons; Tankoban's gray-only convention permits SVGs from `resources/`).
- Sort: folders-first then files, both alphabetical within each level. Deluge does this; Tankorent currently follows libtorrent's file-index order.
- Tooltip showing the full path on hover over leaf rows (the basename label hides long episode names).

## Execution Handoff

Two options for Hemanth to pick:

**1. Subagent-Driven (recommended)** — Agent 4 dispatches a fresh subagent per task, reviews each before the next, fast iteration. Use `superpowers:subagent-driven-development`.

**2. Inline Execution** — Agent 4B (or whoever picks this up) executes tasks sequentially in their own session using `superpowers:executing-plans`, with the self-review checklist as the gate before the commit step.
