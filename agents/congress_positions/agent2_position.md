# Agent 2 (Book Reader) — Congress Position
# First Congress: Comic Reader Acceleration

**Your lane: Reader state machine, navigation UX, end-of-volume behavior, volume/series flow**

---

## Step 1: Read these files IN THIS ORDER before writing anything

**Primary — Tankoban Max (JavaScript):**
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\state_machine.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\input_keyboard.js`
- `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\input_pointer.js`
- Any file in `C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\reader\` that handles end-of-volume, series navigation, or the volume navigator overlay

**Secondary — Groundwork (PyQt6):**
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\readers\comic_reader.py`
  Focus on: navigation logic, end-of-chapter/volume handling, any overlay or dialog that appears at volume boundaries, series navigation

**Current implementation:**
- `src\ui\readers\ComicReader.h`
- `src\ui\readers\ComicReader.cpp`

Do not write a single word of your position until you have read all of the above.

---

## Step 2: Your assignment

You built the book reader. You understand reader state machines, navigation contracts, and boundary behavior. Your job is to read how Max and Groundwork handle comic reader navigation — then compare to Agent 1's current implementation — and write specific fixes and solutions in real C++.

**Part A — Rough edges already implemented (find the gaps and fix them)**

For each navigation/state feature already in ComicReader, compare to Max and Groundwork. Where behavior differs or is incomplete, document it and write the fix.

Areas to check:
- End-of-volume overlay — what does Max show when the last page is reached? What buttons, what keyboard shortcuts, what happens if the user presses Next again past the overlay? Compare to our current end-of-volume overlay. Does it match?
- Volume/series navigation — how does Max move between volumes in a series? Does it auto-advance? Does it prompt? Does it preload the next volume? Compare to our next/prev volume behavior.
- Volume navigator (O key) — what does Max's volume picker look like and how does it behave? How does the user select a volume and land on which page? Does our implementation match?
- Page jump / goto dialog — does Max have a "go to page N" dialog? How is it triggered? Do we have one?
- Progress save timing — when does Max save reading position? On every page turn? On exit? On a timer? Compare to when we save.
- Reader open behavior — when reopening a previously-read volume, does Max scroll to the last page automatically? What is the exact restore behavior?

For each gap, use this format:

```
## Rough Edge: [name]
What Max does: ...
What Groundwork does: ...
What we have: ...
The gap: ...
Fix:
[C++ code — specific, compilable, with file and approximate line reference]
```

**Part B — Unimplemented navigation features (what Max and Groundwork have that we don't)**

Look at Max's state machine and navigation flow for features not yet in our reader. For each:
- Describe what it does
- Decide: worth implementing? (exclude: auto-scroll, auto-flip — blacklisted)
- If yes: write the C++ approach

Things to look for (not exhaustive — read the code):
- Bookmarks — does Max have them? How does it store and navigate to them?
- Reading history — does Max track which volumes have been read/finished?
- Any state that Max tracks that we have no equivalent for in ComicReader

Use this format:

```
## Unimplemented Feature: [name]
What Max does: ...
What Groundwork does: ...
Implement? Yes / No / Modified
C++ approach:
[C++ code or detailed spec]
```

---

## Step 3: When you are done

Post ONE LINE in `agents/chat.md`:

`Agent 2 congress position complete — [N] rough-edge fixes, [M] unimplemented features. See congress_positions/agent2_position.md.`

Then stop. Do not build anything. Do not touch Agent 1's files. Do not post anywhere else.

---

## Your Specific Checklist (from master_checklist.md)

These are your assigned items. Address every one. Use the master checklist for Max/Groundwork file references.

**Rough Edges:**
- R1: Progress payload completeness — CONTRACTS.md requires page, pageCount, path. Max also saves: maxPageIndexSeen, finished, finishedAt, knownSpreadIndices, knownNormalIndices, updatedAt, y (scroll offset). Groundwork also exposes: scroll_fraction(), get_spread_overrides(), get_coupling_state(). What should our payload include and how do we add these fields?
- R2: Coupling state persistence — Max/Groundwork save coupling mode + phase + confidence so auto-detection doesn't re-run on re-open. Does ours save this?
- R3: End-of-volume overlay key interception — Max remaps ALL nav keys (Space, Enter, ArrowRight, PageDown, Backspace) to overlay-specific actions while the overlay is open. Does our overlay fully block and remap keys?
- R4: Volume navigator live search — Max/Groundwork filter the volume list live on every keystroke. Does ours?

**Missing Features:**
- M1: Go-to-page dialog (Ctrl+G) — floating input overlay, 1-based page number, Enter jumps, Escape cancels. Max: openGotoOverlay lines 579–603. Groundwork: GoToPageOverlay lines 868–916. Give the C++ QWidget implementation.
- M2: Keyboard shortcuts help overlay (K key) — centered panel listing all key bindings in two columns. Max: toggleKeysOverlay(). Groundwork: KeysHelpOverlay lines 806–865. [Agent 1 may have started this — check and complement if so.]
- M3: Bookmarks (B key) — toggle current page index in a sorted list, show toast. Max: saved in progress payload. Groundwork: _toggle_page_bookmark() lines 2419–2423 with page_bookmarks: list[int]. [Agent 1 may have started this — check.]
- M4: Instant Replay (Z key) — jump back ~30% of viewport height, crossing one page boundary backward if needed. Max: instantReplay() lines 565–626.
- M5: Clear Resume (R key) — delete saved progress for current volume, show "Resume cleared" toast. Max: clearResume() lines 1189–1194.
- M6: Manual checkpoint save (S key) — immediate progress save (bypass debounce timer), show "Checkpoint saved" toast. Max: saveProgressNow() lines 1148–1186.
- M7: Auto-finish detection — when maxPageIndexSeen reaches the last page index, set finished:true in the next save. Max: scheduleProgressSave() lines 1083–1088.
- M8: Max page index seen tracking — track highest page ever reached; only ever moves forward. Used by continue strip for completion percentage. Max: computeMaxPageIndexSeenNow() lines 1001–1017.
- M9: Series settings per-seriesId — store reader preferences (mode, portrait width, coupling, zoom) under a series key, not per-volume. On open, apply series settings if they exist. Max: open.js loadSeriesSettings/saveSeriesSettings.
- M10: Home/End keys — Home = page 0, End = last page. Max: input_keyboard.js lines 464–477. Groundwork: Key_Home/Key_End lines 2484–2492.
- M11: Alt+Left / Alt+Right — request previous/next volume from keyboard. Groundwork: lines 2392–2400.
- M12: "Resumed" / "Ready" toast on volume open — show which one based on whether saved progress exists. Max: open.js openBook() lines 83–140.

---

*(Write your position below this line)*

---

## Rough Edge: R1 — Progress payload missing 5+ critical fields

**What Max does:**
`scheduleProgressSave()` (state_machine.js:1055–1095) saves:
`pageIndex`, `y`, `settings`, `pageCount`, `bookMeta` (title/series/seriesId/path), `maxPageIndexSeen`, `knownSpreadIndices`, `knownNormalIndices`, `updatedAt`, `finished`, `finishedAt`.
Coupling is embedded in `settings.twoPageCouplingNudge`.

**What Groundwork does:**
Saves equivalent fields including scroll_fraction, coupling_mode, coupling_phase, spread_overrides, page_bookmarks.

**What we have:**
`ComicReader.cpp:993–1000` saves only `page` and `pageCount`. No `path` field.

**The gap:**
1. `path` is absent — this **violates CONTRACTS.md** which calls it "non-negotiable."
2. No `maxPageIndexSeen` — continue strip shows wrong completion %.
3. No coupling state — auto-detection re-runs on every open.
4. No `finished`/`finishedAt` — context menus "Mark all as read" won't work.
5. No `updatedAt` — continue strip can't sort by recency.
6. No `knownSpreadIndices`/`knownNormalIndices` — spread detection restarts from zero.

**Fix (`ComicReader.cpp`, replace `saveCurrentProgress()`):**
```cpp
void ComicReader::saveCurrentProgress()
{
    if (!m_bridge || m_pageNames.isEmpty()) return;

    // Read prior save to preserve finished/finishedAt and grow maxPageIndexSeen monotonically
    QJsonObject prev = m_bridge->progress("comics", itemIdForPath(m_cbzPath));

    int maxSeen = qMax(prev["maxPageIndexSeen"].toInt(0), m_currentPage);
    // In double-page mode, also count the left page of the current pair
    if (m_isDoublePage) {
        auto* pair = pairForPage(m_currentPage);
        if (pair && pair->leftIndex >= 0)
            maxSeen = qMax(maxSeen, pair->leftIndex);
    }

    QJsonArray spreadsArr, normalsArr;
    for (int i = 0; i < m_pageMeta.size(); ++i) {
        if (m_pageMeta[i].decoded) {
            if (m_pageMeta[i].isSpread) spreadsArr.append(i);
            else normalsArr.append(i);
        }
    }

    QJsonArray bookmarksArr;
    for (int b : m_bookmarks) bookmarksArr.append(b);

    QJsonObject data;
    data["page"]               = m_currentPage;
    data["pageCount"]          = m_pageNames.size();
    data["path"]               = m_cbzPath;                      // CONTRACTS.md requirement
    data["maxPageIndexSeen"]   = maxSeen;
    data["knownSpreadIndices"] = spreadsArr;
    data["knownNormalIndices"] = normalsArr;
    data["updatedAt"]          = QDateTime::currentMSecsSinceEpoch();
    data["couplingMode"]       = m_couplingMode;
    data["couplingPhase"]      = m_couplingPhase;
    data["bookmarks"]          = bookmarksArr;

    // Preserve finished/finishedAt across saves
    if (prev.contains("finished"))   data["finished"]   = prev["finished"];
    if (prev.contains("finishedAt")) data["finishedAt"] = prev["finishedAt"];

    // Auto-finish: once the user has seen the last page, mark done
    int total = m_pageNames.size();
    if (total > 0 && maxSeen >= total - 1 && !data["finished"].toBool(false)) {
        data["finished"]   = true;
        data["finishedAt"] = prev.contains("finishedAt")
                             ? prev["finishedAt"]
                             : QDateTime::currentMSecsSinceEpoch();
    }

    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
}
```
Add `#include <QDateTime>` to the includes at the top of ComicReader.cpp.

---

## Rough Edge: R2 — Coupling state not restored on re-open

**What Max does:**
`openBook()` (open.js:106–117) loads `saved.knownSpreadIndices` and seeds `knownSpreadIndexSet` before the first frame draws. Coupling phase is restored from `settings.twoPageCouplingNudge`.

**What Groundwork does:**
`load_book()` reads `coupling_mode` and `coupling_phase` from JSON and sets them before building canonical units.

**What we have:**
`restoreSavedPage()` (ComicReader.cpp:1002–1010) only reads `page`. The coupling fields `m_couplingMode`, `m_couplingPhase`, `m_couplingResolved` are reset to defaults in `openBook()` (line 498–502) and never restored.

**The gap:**
Every re-open runs auto-coupling from scratch. For volumes that were manually set to "shifted" phase, this is wrong and slow.

**Fix (replace `restoreSavedPage()`):**
```cpp
int ComicReader::restoreSavedPage()
{
    if (!m_bridge) return 0;
    QJsonObject data = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    if (data.isEmpty()) return 0;

    // Restore coupling state so auto-detection doesn't re-run
    QString savedMode  = data["couplingMode"].toString();
    QString savedPhase = data["couplingPhase"].toString();
    if (!savedMode.isEmpty() && savedMode != "auto") {
        m_couplingMode       = savedMode;
        m_couplingPhase      = savedPhase.isEmpty() ? "normal" : savedPhase;
        m_couplingConfidence = 1.0f;
        m_couplingResolved   = true;
        // Rebuild pairing immediately with restored phase so first frame is correct
        if (m_isDoublePage) {
            invalidatePairing();
            buildCanonicalPairingUnits();
        }
    }

    // Restore bookmarks
    m_bookmarks.clear();
    for (const auto& v : data["bookmarks"].toArray())
        m_bookmarks.insert(v.toInt());

    int page = data["page"].toInt(0);
    if (page >= m_pageNames.size()) page = 0;
    return page;
}
```

---

## Rough Edge: R3 — End-of-volume overlay missing PageDown key

**What Max does:**
`input_keyboard.js:227`:
```js
if (e.code === 'Space' || e.code === 'Enter' || e.code === 'ArrowRight' || e.code === 'PageDown') {
```
`PageDown` advances (next volume or replay) just like `Space`.

**What Groundwork does:**
Same — `Key_PageDown` is in the advance group.

**What we have:**
`ComicReader.cpp:1646`:
```cpp
case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Right:
```
`Key_PageDown` is absent.

**The gap:**
PageDown during the end overlay does nothing. Inconsistent with page-flip navigation where PageDown = next page.

**Fix (one-line change in `keyPressEvent`):**
```cpp
// ComicReader.cpp:1646 — add Qt::Key_PageDown
case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter:
case Qt::Key_Right: case Qt::Key_PageDown:
    hideEndOverlay();
    if (m_endNextBtn->isVisible()) nextVolume();
    else showPage(0);
    break;
```

---

## Rough Edge: R4 — Volume navigator search selection update incomplete

**What Max does:**
When text is typed, the visible list updates and selection jumps to the top result.

**What Groundwork does:**
`VolumeNavigatorOverlay._on_search()` — filters list and always sets selection to the first visible item.

**What we have:**
`ComicReader.cpp:454–462` only updates selection if the current item is hidden. If the user types and the current volume is still visible but not the best match, selection stays on the old item. When text is cleared, selection stays on the last filtered result instead of restoring to the current volume.

**Fix (replace the connect lambda in `buildUI()`):**
```cpp
connect(m_volSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
    QListWidgetItem* firstMatch = nullptr;
    int currentSeriesIdx = m_seriesCbzList.indexOf(m_cbzPath);
    for (int i = 0; i < m_volList->count(); ++i) {
        auto* item = m_volList->item(i);
        bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match && !firstMatch) firstMatch = item;
    }
    if (!text.isEmpty() && firstMatch) {
        m_volList->setCurrentItem(firstMatch);
    } else if (text.isEmpty() && currentSeriesIdx >= 0
               && currentSeriesIdx < m_volList->count()) {
        m_volList->setCurrentItem(m_volList->item(currentSeriesIdx));
        m_volList->scrollToItem(m_volList->currentItem());
    }
});
```

---

## Unimplemented Feature: M1 — GoToPage key-blocking gap

**What Max does:**
`input_keyboard.js:51–68` — when gotoOverlay is open, ALL keys are blocked except Escape (close) and Enter (commit). Navigation keys cannot leak through to the reader.

**What Groundwork does:**
Same — overlay consumes all key events.

**What we have:**
GoToPage dialog is implemented (`showGoToDialog()`, `ComicReader.cpp:1351`). Ctrl+G works. Enter commits. But in `keyPressEvent`, the gotoOverlay check (line 1671) only handles Escape. Other keys like Space/Right/Left fall through to the switch and trigger page navigation while the dialog is open.

**Implement? Yes**

**Fix (add blocking gate at the top of `keyPressEvent`, before the vol overlay check):**
```cpp
if (m_gotoOverlay && m_gotoOverlay->isVisible()) {
    if (event->key() == Qt::Key_Escape) { hideGoToDialog(); }
    event->accept(); // swallow ALL other keys while goto is open
    return;
}
```

---

## Unimplemented Feature: M2 — Keyboard shortcuts help overlay (K key)

**What Max does:**
`toggleKeysOverlay()` (input_keyboard.js:103) — centered panel listing all key bindings. K opens, K or Escape closes. Swallows all other keys while open.

**What Groundwork does:**
`KeysHelpOverlay` (comic_reader.py:806–865) — centered QWidget with two-column key/description layout, fixed size, dismissed by K or Escape.

**Implement? Yes**

**C++ approach:**

In `ComicReader.h`, add:
```cpp
QWidget* m_keysOverlay = nullptr;
void toggleKeysOverlay();
```

New method (lazy-created on first K press):
```cpp
void ComicReader::toggleKeysOverlay()
{
    if (!m_keysOverlay) {
        m_keysOverlay = new QWidget(this);
        m_keysOverlay->setObjectName("KeysHelp");
        m_keysOverlay->setStyleSheet(
            "#KeysHelp { background: rgba(0,0,0,220); border: 1px solid rgba(255,255,255,36);"
            " border-radius: 8px; }");
        auto* layout = new QVBoxLayout(m_keysOverlay);
        layout->setContentsMargins(18, 14, 18, 14);
        layout->setSpacing(2);
        auto* title = new QLabel("Keyboard Shortcuts", m_keysOverlay);
        title->setStyleSheet(
            "color: white; font-size: 14px; font-weight: 700; background: transparent;");
        layout->addWidget(title);
        layout->addSpacing(4);
        static const QVector<QPair<QString,QString>> shortcuts = {
            {"H",              "Toggle HUD"},
            {"M",              "Cycle reader mode"},
            {"O",              "Volume navigator"},
            {"K",              "Toggle this help"},
            {"B",              "Bookmark current page"},
            {"S",              "Save checkpoint now"},
            {"R",              "Clear resume"},
            {"Z",              "Instant replay"},
            {"F",              "Cycle fit mode"},
            {"I",              "Toggle LTR/RTL (double page)"},
            {"P",              "Coupling nudge (double page)"},
            {"F11",            "Toggle fullscreen"},
            {"Ctrl+G",         "Go to page"},
            {"Alt+Left/Right", "Prev/next volume"},
            {"Space/Right",    "Next page"},
            {"Left",           "Prev page"},
            {"Home/End",       "First/last page"},
            {"Esc",            "Close overlay / back"},
            {"Right-click",    "Spread override / settings"},
        };
        for (const auto& [key, desc] : shortcuts) {
            auto* row = new QWidget(m_keysOverlay);
            auto* rl  = new QHBoxLayout(row);
            rl->setContentsMargins(0,0,0,0); rl->setSpacing(10);
            auto* kl = new QLabel(key, row);
            kl->setFixedWidth(120);
            kl->setStyleSheet(
                "color: rgba(255,255,255,200); font-size: 12px; font-weight: 600; background: transparent;");
            auto* dl = new QLabel(desc, row);
            dl->setStyleSheet(
                "color: rgba(255,255,255,160); font-size: 12px; background: transparent;");
            rl->addWidget(kl); rl->addWidget(dl, 1);
            layout->addWidget(row);
        }
        m_keysOverlay->adjustSize();
    }
    if (m_keysOverlay->isVisible()) {
        m_keysOverlay->hide(); setFocus();
    } else {
        m_keysOverlay->move((width()  - m_keysOverlay->width())  / 2,
                            (height() - m_keysOverlay->height()) / 2);
        m_keysOverlay->show(); m_keysOverlay->raise();
    }
}
```

Key-blocking gate in `keyPressEvent` (after vol overlay block, before end overlay block):
```cpp
if (m_keysOverlay && m_keysOverlay->isVisible()) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_K)
        toggleKeysOverlay();
    event->accept(); return;
}
```

Add to keyPressEvent switch:
```cpp
case Qt::Key_K: toggleKeysOverlay(); break;
```

---

## Unimplemented Feature: M3 — Bookmarks (B key)

**What Max does:**
`saved.bookmarks` — QJsonArray of page indices in the progress payload. Toggled inline, shows toast. No dedicated bookmark UI.

**What Groundwork does:**
`_toggle_page_bookmark()` (comic_reader.py:2419–2423) — toggles current page in `self.page_bookmarks: list[int]`, shows toast, calls debounced save.

**Implement? Yes**

In `ComicReader.h`, add:
```cpp
QSet<int> m_bookmarks;
void toggleBookmark();
```

New method:
```cpp
void ComicReader::toggleBookmark()
{
    if (m_pageNames.isEmpty()) return;
    if (m_bookmarks.contains(m_currentPage)) {
        m_bookmarks.remove(m_currentPage);
        showToast(QString("Bookmark removed — page %1").arg(m_currentPage + 1));
    } else {
        m_bookmarks.insert(m_currentPage);
        showToast(QString("Bookmarked page %1").arg(m_currentPage + 1));
    }
    saveCurrentProgress();
}
```

`data["bookmarks"]` is included in the updated `saveCurrentProgress()` (R1 fix) and restored in `restoreSavedPage()` (R2 fix). Field name matches CONTRACTS.md `bookmarks: QJsonArray of chapter indices`.

Add to keyPressEvent switch:
```cpp
case Qt::Key_B: toggleBookmark(); break;
```

---

## Unimplemented Feature: M4 — Instant Replay (Z key)

**What Max does:**
`instantReplay()` (state_machine.js:565–626) — jumps back 30% of viewport height. Crosses page boundary if needed. Designed for auto-scroll / long-strip mode.

**What Groundwork does:**
Same. In double-page mode (no y-coordinate), goes to previous spread.

**Implement? Yes — simplified for our architecture (no y-scroll state in page-flip modes)**

```cpp
void ComicReader::instantReplay()
{
    if (m_pageNames.isEmpty()) return;
    if (m_isScrollStrip) {
        if (auto* vbar = m_scrollArea->verticalScrollBar()) {
            int dist = static_cast<int>(m_scrollArea->viewport()->height() * 0.30);
            vbar->setValue(qMax(0, vbar->value() - dist));
        }
    } else {
        prevPage();
    }
    showToast("Replay");
}
```

Add `void instantReplay()` to `ComicReader.h` private methods.

Add to keyPressEvent switch:
```cpp
case Qt::Key_Z: instantReplay(); break;
```

---

## Unimplemented Feature: M5 — Clear Resume (R key)

**What Max does:**
`clearResume()` (state_machine.js:1189–1194) — clears progress entry, shows `toast('Resume cleared')`.

**What Groundwork does:**
Equivalent.

**Implement? Yes**

```cpp
void ComicReader::clearResume()
{
    if (!m_bridge || m_cbzPath.isEmpty()) return;
    QJsonObject reset;
    reset["page"]      = 0;
    reset["pageCount"] = m_pageNames.size();
    reset["path"]      = m_cbzPath;
    reset["finished"]  = false;
    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), reset);
    m_bookmarks.clear();
    showToast("Resume cleared");
}
```

Note: if CoreBridge exposes `clearProgress(domain, key)`, use that instead. The reset-to-zero above is sufficient either way.

Add `void clearResume()` to `ComicReader.h`.

Add to keyPressEvent switch:
```cpp
case Qt::Key_R: clearResume(); break;
```

---

## Unimplemented Feature: M6 — Manual Checkpoint Save (S key)

**What Max does:**
`saveProgressNow()` (state_machine.js:1148–1186) — bypasses debounce, saves immediately, shows `toast('Checkpoint saved')`.

**What Groundwork does:**
Equivalent.

**Implement? Yes — trivial since our save is already synchronous**

```cpp
void ComicReader::saveCheckpoint()
{
    saveCurrentProgress();
    showToast("Checkpoint saved");
}
```

Add `void saveCheckpoint()` to `ComicReader.h`.

Add to keyPressEvent switch:
```cpp
case Qt::Key_S: saveCheckpoint(); break;
```

---

## Unimplemented Feature: M7 + M8 — Auto-finish detection + Max page index seen

**What Max does:**
`computeMaxPageIndexSeenNow()` (state_machine.js:1001–1017) — in two-page mode, counts both visible pages. Stored as `maxPageIndexSeen`. When `maxEver >= pageCount - 1`, sets `finished = true` in the same save (lines 1083–1087).

**What Groundwork does:**
Equivalent.

**Implement? Yes — both covered by the R1 fix above.**

The updated `saveCurrentProgress()` already:
1. Tracks `maxSeen = qMax(prev["maxPageIndexSeen"].toInt(0), m_currentPage)` (M8)
2. In double-page mode, includes the left-page index in maxSeen
3. Sets `finished = true` when `maxSeen >= total - 1` (M7)

No additional code needed beyond R1.

---

## Unimplemented Feature: M9 — Series settings per-seriesId

**What Max does:**
`loadSeriesSettings(seriesId)` / `saveSeriesSettings(seriesId, settings)` (open.js:120–143) — stores `portraitWidthPct`, `scrollMode`, `twoPageCouplingNudge`, etc. under a per-series key. Applied on every `openBook()`.

**What Groundwork does:**
Equivalent.

**Implement? Modified — wire the three most impactful fields; defer the full system**

```cpp
QString ComicReader::seriesSettingsKey() const {
    if (!m_seriesName.isEmpty())
        return "reader_series/" + QString(QCryptographicHash::hash(
            m_seriesName.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    return "reader_series/" + QString(QCryptographicHash::hash(
        QFileInfo(m_cbzPath).absolutePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

void ComicReader::saveSeriesSettings() {
    if (m_cbzPath.isEmpty()) return;
    QSettings s;
    QString key = seriesSettingsKey();
    s.setValue(key + "/portraitWidthPct", m_portraitWidthPct);
    s.setValue(key + "/readerMode",       static_cast<int>(m_readerMode));
    s.setValue(key + "/couplingPhase",    m_couplingPhase);
}

void ComicReader::applySeriesSettings() {
    QSettings s;
    QString key = seriesSettingsKey();
    if (!s.contains(key + "/portraitWidthPct")) return;
    setPortraitWidthPct(s.value(key + "/portraitWidthPct", 78).toInt());
    // Reader mode and coupling phase are applied via restoreSavedPage coupling restore (R2)
    // Full multi-field expansion deferred post-congress
}
```

Call `applySeriesSettings()` in `openBook()` before `showPage(startPage)`. Call `saveSeriesSettings()` at the end of `saveCurrentProgress()`. Add `#include <QSettings>` if not already present.

---

## Unimplemented Feature: M10 — Home/End keys

**Status: Already implemented.** `ComicReader.cpp:1668–1669`:
```cpp
case Qt::Key_Home: showPage(0); break;
case Qt::Key_End: showPage(m_pageNames.size() - 1); break;
```
No action needed.

---

## Unimplemented Feature: M11 — Alt+Left / Alt+Right for volume navigation

**What Max does:**
Volume nav from the O-key overlay or end-overlay buttons.

**What Groundwork does:**
`_request_adjacent_volume(-1/+1)` (comic_reader.py:2392–2400) — `Alt+Left` = previous volume, `Alt+Right` = next volume directly, without opening the overlay.

**Implement? Yes — two lines, no header changes needed**

```cpp
// In keyPressEvent, BEFORE the main switch, after all overlay gates:
if (event->modifiers() & Qt::AltModifier) {
    if (event->key() == Qt::Key_Left)  { prevVolume(); event->accept(); return; }
    if (event->key() == Qt::Key_Right) { nextVolume(); event->accept(); return; }
}
```

---

## Unimplemented Feature: M12 — "Resumed" / "Ready" toast on volume open

**What Max does:**
`open.js:297`: `toast(saved ? 'Resumed' : 'Ready')` — fires after the first frame draws.

**What Groundwork does:**
Equivalent behavior.

**What we have:**
`openBook()` calls `showToolbar()` and `showPage(startPage)` but shows no toast.

**Implement? Yes**

```cpp
// In ComicReader::openBook(), after showPage(startPage) and showToolbar():
bool hadProgress = false;
if (m_bridge) {
    QJsonObject saved = m_bridge->progress("comics", itemIdForPath(cbzPath));
    hadProgress = !saved.isEmpty() && saved.contains("updatedAt");
}
QTimer::singleShot(250, this, [this, hadProgress]() {
    showToast(hadProgress ? "Resumed" : "Ready");
});
```

---

## Summary

**Rough edges: 4**
- R1: Progress payload missing `path` + 5 fields — CONTRACTS violation, breaks continue strip, breaks context menu mark-as-read
- R2: Coupling state not saved/restored — auto-detection re-runs every open
- R3: End overlay missing `Qt::Key_PageDown` — one-line fix
- R4: Volume navigator search selection incomplete — fix always jumps to first match

**Unimplemented features: 10**
- M1: GoToPage key-blocking gap — background keys leak while dialog is open
- M2: Keys help overlay (K key) — full QWidget implementation provided
- M3: Bookmarks (B key) — full implementation provided, feeds into CONTRACTS
- M4: Instant Replay (Z key) — simplified C++ port for our architecture
- M5: Clear Resume (R key) — implementation provided
- M6: Manual checkpoint save (S key) — implementation provided
- M7+M8: Auto-finish + maxPageIndexSeen — covered by R1 fix, no extra work
- M9: Series settings per-seriesId — skeleton provided, defer full system
- M11: Alt+Left/Right volume navigation — two-line fix
- M12: "Resumed"/"Ready" toast — implementation provided
- M10: Home/End — already done

**Priority order for Agent 1:**
R1 first (CONTRACTS violation) → R2 (coupling restore) → M12 (toast) → M2 (keys overlay) → M3 (bookmarks) → M4/M5/M6 (Z/R/S keys) → M11 (Alt nav) → R3 (PageDown one-liner) → R4 (vol search) → M1 (goto blocking) → M9 (series settings)
