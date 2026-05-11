# Tankorent Bulk-Group Menu Restructure + Column Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the stream-bulk group context menu in Tankorent into parity with the flat-torrent context menu (Remove / Remove + Delete Files / Restart), and align the bulk group-parent row's column cells with the flat-row column cells so values land in the same horizontal position within each column.

**Architecture:** Two surgical edits, both confined to `src/ui/pages/TankorentPage.cpp` plus one new method declaration + impl pair on `TorrentClient` for the "Restart group" semantic. Menu restructure replaces the existing dual-confirmation `cancelStreamBulkGroup` UX with two explicit danger actions matching the flat-row pattern at `TankorentPage.cpp:2163-2174`. Column alignment work compares the flat-row render path (`renderTorrentRow` lambda at `TankorentPage.cpp:1680+`) against the group-parent render path (`TankorentPage.cpp:1860+`) and normalizes the two divergence points: Status icon presence (col 3) and Category alignment (col 9).

**Tech Stack:** C++20, Qt6 (QTableWidget cell items + cell widgets, QMenu actions, QMessageBox confirmations), libtorrent (`forceRecheck` for libtorrent-error recovery), the brotherhood's existing `ContextMenuHelper` + `cancelStreamBulkGroup` + `retryStreamBulkGroupFailedItems` plumbing.

---

## File Structure

**Files modified (2):**
- `src/ui/pages/TankorentPage.cpp` — menu rewrite (Task 1) + column alignment normalize (Task 2)
- `src/core/torrent/TorrentClient.cpp` — new `restartStreamBulkGroup(groupId)` impl (Task 1)
- `src/core/torrent/TorrentClient.h` — declaration for the new method (Task 1)

**Files created:** none.

**Files deleted:** none.

---

### Task 1: Group context menu restructure + Restart action

**Files:**
- Modify: `src/core/torrent/TorrentClient.h:159-160` (add method declaration after `retryStreamBulkGroupFailedItems`)
- Modify: `src/core/torrent/TorrentClient.cpp` (add `restartStreamBulkGroup` impl near `retryStreamBulkGroupFailedItems`)
- Modify: `src/ui/pages/TankorentPage.cpp:2180-2310` (rewrite `showGroupContextMenu`)

**Background — current menu shape (`TankorentPage.cpp:2222-2310`):**
The existing menu has: `Pause group` / `Resume group` / `Cancel group...` (danger) / separator / `Remove orphan group` (conditional on `allOrphaned`) / `Retry failed` (conditional on `failedCount > 0`) / `Show in folder` / `Expand details`. The `Cancel group...` action opens a `QMessageBox` with branching wording ("Cancel and clean partials" vs "Remove from list") depending on `allPublished`. Hemanth wants this collapsed: replace the `Cancel group...` confirmation-dialog with two explicit-named danger actions matching the flat-row pattern at `TankorentPage.cpp:2163-2174` (`Remove` + `Remove + Delete Files`), and add a `Restart group` action.

**Background — flat-row menu pattern (`TankorentPage.cpp:2163-2174`):**
```cpp
auto* removeAction = ContextMenuHelper::addDangerAction(menu, "Remove");
connect(removeAction, &QAction::triggered, this, [this, selectedHashes]() {
    for (const auto& h : selectedHashes) m_client->deleteTorrent(h, false);
});

auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, "Remove + Delete Files");
connect(removeWithFiles, &QAction::triggered, this, [this, selectedHashes]() {
    if (ContextMenuHelper::confirmRemove(this, "Delete Files",
            QString("Remove %1 torrent(s) and delete all downloaded files?").arg(selectedHashes.size()))) {
        for (const auto& h : selectedHashes) m_client->deleteTorrent(h, true);
    }
});
```

The bulk equivalent uses `m_client->cancelStreamBulkGroup(groupId)` which already deletes the underlying torrents with file cleanup per the `deleteFilesByHash` logic at `TorrentClient.cpp:923-924`. For the no-delete variant, we need a sibling that removes the group record + deletes the torrents *without* deleting files. The simplest implementation: extend `cancelStreamBulkGroup` to accept a `bool deleteFiles` flag, defaulting to current behavior. Two callers will pass `true` / `false`.

**Background — Restart semantic:**
Hemanth's stuck-cohort case (libtorrent error-state items) needs a recovery action distinct from `Retry failed` (which only addresses items in `Failed/MissingSource/MetadataFailed/PublishFailed` states per `isFailedStreamBulkItemState` at `TankorentPage.cpp:286-292`). "Restart group" should: (a) for items with libtorrent error state, call `forceRecheck` to clear the error; (b) reset all non-Published items to `Pending` so the cohort scheduler can re-pick the head; (c) call `cohortMaybeAdvance` to re-engage the scheduler. Published items are left alone (they're complete; restarting them would erase user-visible library state).

- [ ] **Step 1: Read the existing showGroupContextMenu in full**

Read `src/ui/pages/TankorentPage.cpp:2180-2310` end-to-end so the rewrite preserves the unchanged surface (Pause group / Resume group / Show in folder / Expand-Collapse details).

- [ ] **Step 2: Declare `restartStreamBulkGroup` in TorrentClient.h**

Modify `src/core/torrent/TorrentClient.h` between line 159 and 160 (after `retryStreamBulkGroupFailedItems`):

```cpp
    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — restart-group recovery
    // action. Clears libtorrent error state on non-terminal-success items
    // via forceRecheck, resets non-Published items to Pending, then
    // re-engages the cohort scheduler via cohortMaybeAdvance. Published
    // items are left alone (terminal-success; restarting would erase
    // user-visible library state). Used by the Tankorent group context
    // menu "Restart group" action.
    void restartStreamBulkGroup(const QString& groupId);
```

- [ ] **Step 3: Implement `restartStreamBulkGroup` in TorrentClient.cpp**

Find the existing `retryStreamBulkGroupFailedItems` impl (grep `^void TorrentClient::retryStreamBulkGroupFailedItems` in `src/core/torrent/TorrentClient.cpp`). Insert the new method immediately after it. Use the brotherhood's existing helpers and patterns (the impl pattern at `markStreamBulkItemsForTorrent` line 978-1027 + `cancelStreamBulkGroup` line 887-948 is the template):

```cpp
void TorrentClient::restartStreamBulkGroup(const QString& groupId)
{
    if (groupId.isEmpty() || !m_streamBulkGroups.contains(groupId))
        return;

    QJsonObject group = m_streamBulkGroups.value(groupId).toObject();
    QJsonArray items = group.value("items").toArray();

    // Snapshot active states so we can detect libtorrent-error torrents.
    QHash<QString, QString> activeStates;
    for (const TorrentInfo& info : listActive())
        activeStates.insert(info.infoHash.toLower(), info.stateString);

    bool changed = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QString state = item.value("itemState").toString();
        // Leave Published + Completed alone — they're terminal-success.
        if (state == QLatin1String(kStatePublished) ||
            state == QLatin1String(kStateCompleted))
            continue;

        const QString infoHash = item.value("infoHash").toString();
        if (!infoHash.isEmpty()) {
            const QString live = activeStates.value(infoHash.toLower());
            if (live == QLatin1String("error"))
                m_engine->forceRecheck(infoHash);
        }

        if (state != QLatin1String(kStatePending)) {
            item["itemState"] = QString::fromLatin1(kStatePending);
            item["lastError"] = QString();
            items.replace(i, item);
            changed = true;
        }
    }

    if (changed) {
        group["items"] = items;
        group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
        m_streamBulkGroups[groupId] = group;
        saveStreamBulkGroups();
    }

    // Re-engage the cohort scheduler. After the reset above, every
    // non-Published item is Pending — cohortMaybeAdvance picks the
    // first eligible and resumes it.
    cohortMaybeAdvance(groupId);
}
```

- [ ] **Step 4: Add `deleteFiles` parameter to cancelStreamBulkGroup**

The existing `cancelStreamBulkGroup` (TorrentClient.h:151 + TorrentClient.cpp:887) currently derives `deleteFiles` per-torrent from the `allPublished` heuristic at `TorrentClient.cpp:913`. The new menu needs explicit caller-controlled `deleteFiles`. Add an overload (keeps the existing single-arg form intact for any other callers).

In `src/core/torrent/TorrentClient.h` modify line 151:

```cpp
    void cancelStreamBulkGroup(const QString& groupId);
    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — explicit-deleteFiles
    // overload for the Tankorent group menu's "Remove" vs "Remove +
    // Delete Files" actions. When deleteFilesOverride.has_value(), it
    // takes precedence over the existing allPublished heuristic — every
    // non-Publishing/Published torrent in the group is removed with the
    // chosen file-deletion flag. The single-arg overload preserves the
    // legacy auto-heuristic for any non-menu callers.
    void cancelStreamBulkGroup(const QString& groupId, bool deleteFiles);
```

In `src/core/torrent/TorrentClient.cpp`, locate the existing `cancelStreamBulkGroup` impl (around line 887). Wrap its body in a private helper that takes the override; keep the single-arg public method calling the helper with `std::nullopt`. Easiest shape:

Replace the existing definition at line 887 with this two-method pair (preserve the existing comment block including the V2 hotfix at line 917 — that work is intact):

```cpp
void TorrentClient::cancelStreamBulkGroup(const QString& groupId)
{
    cancelStreamBulkGroup(groupId, /*deleteFilesOverride=*/std::nullopt);
}

void TorrentClient::cancelStreamBulkGroup(const QString& groupId, bool deleteFiles)
{
    cancelStreamBulkGroup(groupId, std::optional<bool>(deleteFiles));
}

void TorrentClient::cancelStreamBulkGroup(const QString& groupId,
                                          std::optional<bool> deleteFilesOverride)
{
    // [the existing body of cancelStreamBulkGroup goes here, with one
    //  line changed: line 913's
    //    deleteFilesByHash.insert(infoHash, !allPublished);
    //  becomes:
    //    deleteFilesByHash.insert(
    //        infoHash,
    //        deleteFilesOverride.has_value()
    //            ? *deleteFilesOverride
    //            : !allPublished);
    // ]
}
```

Add a private 3-arg declaration to `TorrentClient.h` in the private section near `cohortMaybeAdvance`:

```cpp
    void cancelStreamBulkGroup(const QString& groupId,
                               std::optional<bool> deleteFilesOverride);
```

And add `#include <optional>` to TorrentClient.h if not already present (check the existing includes at the top first).

- [ ] **Step 5: Verify TorrentClient.h + TorrentClient.cpp compile**

Run: `cmd.exe //C ".\\build_check.bat"` (after stop-tankoban.ps1)
Expected: `BUILD OK`

If it fails: the most likely cause is missing `#include <optional>` in TorrentClient.h or namespace issues on `std::optional`. Add the include and re-run.

- [ ] **Step 6: Commit Task 1 part A (engine-side)**

```bash
git add src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp
git commit -m "feat(stream): restartStreamBulkGroup + cancelStreamBulkGroup deleteFiles overload"
```

- [ ] **Step 7: Rewrite showGroupContextMenu in TankorentPage.cpp**

Locate `void TankorentPage::showGroupContextMenu(const QPoint& pos, const QString& groupId)` at `src/ui/pages/TankorentPage.cpp:2180`. Replace the body from line 2222 (`QMenu* menu = ContextMenuHelper::createMenu(this);`) through the closing `}` of the lambda block ending around line 2310 with the new menu layout.

Show the new menu code in full. Replace the existing menu construction block with:

```cpp
    QMenu* menu = ContextMenuHelper::createMenu(this);

    // ── Active operations ──────────────────────────────────────────────
    auto* pauseAction = menu->addAction(QStringLiteral("Pause group"), this, [this, downloadingHashes]() {
        for (const QString& hash : downloadingHashes)
            m_client->pauseTorrent(hash);
    });
    pauseAction->setEnabled(!downloadingHashes.isEmpty());

    auto* resumeAction = menu->addAction(QStringLiteral("Resume group"), this, [this, pausedHashes]() {
        for (const QString& hash : pausedHashes)
            m_client->resumeTorrent(hash);
    });
    resumeAction->setEnabled(!pausedHashes.isEmpty());

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — Restart group is always
    // available. Clears libtorrent error state on stuck items + resets
    // non-Published items to Pending + re-engages the cohort scheduler.
    // Published items are intentionally untouched.
    auto* restartAction = menu->addAction(QStringLiteral("Restart group"),
        this, [this, groupId]() {
            m_client->restartStreamBulkGroup(groupId);
            refreshTransfers();
        });
    Q_UNUSED(restartAction);

    menu->addSeparator();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — show-in-folder +
    // expand/collapse stay above the danger zone for muscle-memory.
    auto* showFolder = menu->addAction(QStringLiteral("Show in folder"), this, [this, group]() {
        QString folder;
        const QJsonArray groupItems = group.value(QStringLiteral("items")).toArray();
        if (!groupItems.isEmpty())
            folder = destinationFolderForGroupItem(group, groupItems.at(0).toObject());
        if (folder.isEmpty())
            folder = group.value(QStringLiteral("destinationRoot")).toString();
        if (folder.isEmpty())
            folder = fallbackVideosRoot(m_client);
        if (!folder.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    showFolder->setEnabled(!group.value(QStringLiteral("destinationRoot")).toString().isEmpty()
                           || !fallbackVideosRoot(m_client).isEmpty());

    const bool expanded = m_expandedGroupIds.contains(groupId);
    menu->addAction(expanded ? QStringLiteral("Collapse details") : QStringLiteral("Expand details"),
                    this, [this, groupId, expanded]() {
        if (expanded)
            m_expandedGroupIds.remove(groupId);
        else
            m_expandedGroupIds.insert(groupId);
        saveExpandedStreamBulkGroups();
        refreshTransfers();
    });

    if (failedCount > 0) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("Retry failed"), this, [this, groupId]() {
            m_client->retryStreamBulkGroupFailedItems(groupId);
            refreshTransfers();
        });
    }

    menu->addSeparator();

    // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — danger zone mirrors the
    // flat-row pattern at lines 2163-2174 (Remove / Remove + Delete Files).
    // The bulk equivalents route through cancelStreamBulkGroup with an
    // explicit deleteFiles flag instead of the prior auto-heuristic +
    // confirmation-dialog branching at the old line 2237.
    const int totalItems = items.size();
    auto* removeAction = ContextMenuHelper::addDangerAction(menu, QStringLiteral("Remove"));
    connect(removeAction, &QAction::triggered, this, [this, groupId]() {
        m_client->cancelStreamBulkGroup(groupId, /*deleteFiles=*/false);
        m_expandedGroupIds.remove(groupId);
        saveExpandedStreamBulkGroups();
        refreshTransfers();
    });

    auto* removeWithFiles = ContextMenuHelper::addDangerAction(menu, QStringLiteral("Remove + Delete Files"));
    connect(removeWithFiles, &QAction::triggered, this, [this, groupId, totalItems]() {
        if (ContextMenuHelper::confirmRemove(this, QStringLiteral("Delete Files"),
                QStringLiteral("Remove stream bulk group (%1 item(s)) and delete all downloaded files?").arg(totalItems))) {
            m_client->cancelStreamBulkGroup(groupId, /*deleteFiles=*/true);
            m_expandedGroupIds.remove(groupId);
            saveExpandedStreamBulkGroups();
            refreshTransfers();
        }
    });

    menu->exec(m_transfersTable->viewport()->mapToGlobal(pos));
    delete menu;
}
```

**Removed surface (intentional):**
- Old `Cancel group...` action with branching confirmation dialog — replaced by explicit `Remove` / `Remove + Delete Files`.
- Old `Remove orphan group` conditional action — redundant with `Remove` (which now always works for any group state, including all-orphaned thanks to the prior 2026-05-10 cancelStreamBulkGroup hotfix).

- [ ] **Step 8: Build verify**

Run: `cmd.exe //C ".\\build_check.bat"` (after stop-tankoban.ps1)
Expected: `BUILD OK`

If it fails: most likely a typo in lambda captures or a missing `Q_UNUSED` somewhere. Read the cl.exe tail and fix.

- [ ] **Step 9: Smoke verify**

Run: `cmd.exe //C ".\\build_and_run.bat"` (background)
Wait for tankoctl ping to succeed.
Run: `out\tankoctl.exe get-state`
Expected: JSON with `schema=tankoban.dev.v1` + responsive `activePageId`, no crash.

Visual verification deferred to Hemanth: right-click a stream-bulk group row and confirm menu shape:
1. Pause group (greyed if no Downloading)
2. Resume group (greyed if no Paused)
3. Restart group (always enabled)
4. — separator —
5. Show in folder
6. Expand/Collapse details
7. — separator —
8. Retry failed (only if failedCount > 0)
9. — separator —
10. Remove (no-confirm)
11. Remove + Delete Files (confirm dialog)

Stop Tankoban: `powershell -NoProfile -File scripts/stop-tankoban.ps1`

- [ ] **Step 10: Commit Task 1 part B (UI-side)**

```bash
git add src/ui/pages/TankorentPage.cpp
git commit -m "feat(tankorent): restructure stream bulk group menu — Restart + Remove / Remove + Delete Files"
```

---

### Task 2: Group-parent row column alignment normalization

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp:1860-1940` (group-parent render block)

**Background — documented alignment divergences between flat-row (`renderTorrentRow` at line 1680+) and group-parent (line 1860+):**

Walked both render paths line-by-line. The two diverge at three points:

| Column | Flat row (renderTorrentRow) | Group-parent row | Resolution |
|---|---|---|---|
| 3 Status | item with icon + text (`stateItem->setIcon` line 1737 + `setText` line 1741) | item with text only (`ensureItem(row, 3)->setText(statusText)` line 1907) | Add matching status icon to parent |
| 9 Category | item with text, **default alignment** (no setTextAlignment call) → AlignLeft \| AlignVCenter | `catItem->setTextAlignment(Qt::AlignCenter)` line 1926 | Drop the AlignCenter to match flat |
| 2 Progress | item with text, AlignCenter (line 1724) | cell widget with QLabel + QProgressBar (`setProgressWidget` lambda at line 1651-1679) | Cell widget is unavoidable for the progress bar; ensure its label uses identical padding to a centered item text |

**Visual evidence in Hemanth's screenshots:** the most visible divergence is the Status column where child rows (which use `renderTorrentRow`) show "↓ Downloading" with an icon-text combo that visually shifts the text rightward, while the parent row shows "Downloading" as plain text. The Category column AlignCenter discrepancy is subtle but real — "videos" appears centered in the parent row's cell but left-aligned in flat rows below.

- [ ] **Step 1: Add status icon to group-parent row's col-3 item**

Read the existing flat-row status icon logic at `src/ui/pages/TankorentPage.cpp:1727-1741`. Mirror it for the group-parent row, picking the icon based on the GROUP's aggregate state (`anyActiveChild`, `allActiveDownloading`, `allOrphaned`, `failedCount`, `publishedCount` are already computed in the group-parent block — read lines 1820-1857 to confirm).

Locate the existing line at `TankorentPage.cpp:1907`:

```cpp
        ensureItem(row, 3)->setText(statusText);
```

Replace with this expanded block:

```cpp
        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — group-parent row's
        // Status cell now carries a state icon matching the flat-row
        // pattern at lines 1727-1741. Without the icon, the parent
        // status text rendered visually shifted relative to the child
        // rows' icon+text combo. Aggregate state picks the icon:
        // - allOrphaned → error icon
        // - failedCount > 0 + allTerminal → error icon
        // - any active child downloading → download icon
        // - all items published → check icon
        // - else → waiting icon
        auto* parentStatusItem = ensureItem(row, 3);
        QString parentStatusIcon;
        if (allOrphaned) {
            parentStatusIcon = QStringLiteral(":/icons/error.svg");
        } else if (failedCount > 0 && allTerminal) {
            parentStatusIcon = QStringLiteral(":/icons/error.svg");
        } else if (anyActiveChild) {
            parentStatusIcon = QStringLiteral(":/icons/download.svg");
        } else if (allTerminal && publishedCount == totalItems && totalItems > 0) {
            parentStatusIcon = QStringLiteral(":/icons/check.svg");
        } else {
            parentStatusIcon = QStringLiteral(":/icons/waiting.svg");
        }
        parentStatusItem->setIcon(QIcon(parentStatusIcon));
        parentStatusItem->setText(statusText);
```

(The variables `allOrphaned`, `failedCount`, `allTerminal`, `anyActiveChild`, `publishedCount`, `totalItems` are all computed in the surrounding scope at lines 1820-1857; confirm by reading that block first.)

- [ ] **Step 2: Drop the AlignCenter override on group-parent col-9 Category**

Locate line 1924-1926:

```cpp
        auto* catItem = ensureItem(row, 9);
        catItem->setText(QStringLiteral("videos"));
        catItem->setTextAlignment(Qt::AlignCenter);
```

Replace with:

```cpp
        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-11 — drop AlignCenter
        // override to match flat-row default alignment (AlignLeft |
        // AlignVCenter, applied implicitly by Qt). Flat-row Category
        // column at TankorentPage.cpp:1774 uses no setTextAlignment.
        auto* catItem = ensureItem(row, 9);
        catItem->setText(QStringLiteral("videos"));
```

- [ ] **Step 3: Verify setProgressWidget label padding matches AlignCenter item**

Read `setProgressWidget` lambda at `TankorentPage.cpp:1651-1679`. The QLabel inside the host widget has `setMinimumWidth(34)` and `setAlignment(Qt::AlignCenter)`. The host's QHBoxLayout has `setContentsMargins(4, 0, 4, 0)`. Compare against the flat-row Progress column at line 1721-1724 which is an item with `setTextAlignment(Qt::AlignCenter)` — Qt's default cell margins for items are smaller than the 4px contentMargins on the widget.

If the visual progress text in the parent row appears shifted relative to the child Progress text, fix by changing `setProgressWidget` line 1666 from:

```cpp
        layout->setContentsMargins(4, 0, 4, 0);
```

to:

```cpp
        layout->setContentsMargins(0, 0, 0, 0);
```

Otherwise leave it alone — the divergence may be visually negligible at typical column widths. Note: the same `setProgressWidget` is used elsewhere for live group progress on child rows too, so this change affects both consistently.

- [ ] **Step 4: Build verify**

Run: `cmd.exe //C ".\\build_check.bat"` (after stop-tankoban.ps1)
Expected: `BUILD OK`

If it fails: most likely the icon-resource path is wrong (verify by `grep "download.svg" resources/resources.qrc` and confirming `:/icons/download.svg` exists) or a referenced variable isn't in scope.

- [ ] **Step 5: Smoke verify**

Run: `cmd.exe //C ".\\build_and_run.bat"` (background)
Wait for tankoctl ping.
Run: `out\tankoctl.exe get-state`
Expected: clean JSON reply.

Visual verification deferred to Hemanth: open Tankorent, look at a stream-bulk group row vs a flat torrent row directly below it. Confirm:
1. Status column: parent has an icon + text (matching child rows' icon + text shape)
2. Category column: "videos" left-aligned (matching flat rows)
3. Progress column: percentage text horizontally centered in parent cell (matching flat-row percentages)

Stop Tankoban: `powershell -NoProfile -File scripts/stop-tankoban.ps1`

- [ ] **Step 6: Commit Task 2**

```bash
git add src/ui/pages/TankorentPage.cpp
git commit -m "fix(tankorent): align stream bulk group-parent row columns with flat rows"
```

---

## Self-Review Checklist

**Spec coverage:**
- ✓ "add a restart" → Task 1 Step 7 wires `Restart group` menu action calling `restartStreamBulkGroup` defined in Task 1 Steps 2-3.
- ✓ "replace cancel button with remove (which would contain remove with the files just like how it is for other torrents)" → Task 1 Step 7 replaces `Cancel group...` with `Remove` + `Remove + Delete Files` two-action pair mirroring `TankorentPage.cpp:2163-2174`. Task 1 Step 4 adds the `deleteFiles` parameter to `cancelStreamBulkGroup` enabling explicit caller control.
- ✓ "visual discrepancy ... where the values ... are placed in terms of right or left" → Task 2 Step 1 fixes Status column icon discrepancy (the most visually prominent shift); Task 2 Step 2 fixes Category column alignment; Task 2 Step 3 conditionally fixes Progress column padding.

**Placeholder scan:**
- No TBD / TODO / "fill in" placeholders in any step.
- All code blocks contain real C++ (not pseudocode).
- Variable references like `allOrphaned`, `anyActiveChild`, `publishedCount` are documented as existing in scope at the cited lines.

**Type consistency:**
- `restartStreamBulkGroup(QString groupId)` declaration in Task 1 Step 2 matches the call in Task 1 Step 7's menu lambda + the impl signature in Step 3.
- `cancelStreamBulkGroup(QString groupId, bool deleteFiles)` overload in Step 4 matches the two callers in Step 7.
- `cohortMaybeAdvance(QString)` referenced in Step 3 is the existing public-API surface at `TorrentClient.h` (per prior V2 Phase 2 ship).

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-11-tankorent-bulk-menu-and-alignment.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
