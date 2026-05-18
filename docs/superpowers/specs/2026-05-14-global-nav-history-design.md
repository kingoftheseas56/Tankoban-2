# Global Back / Forward Navigation — Design Spec

**Date:** 2026-05-14
**Owner:** Agent 5 (Library UX + Theme)
**Status:** Phase 1 brainstorm complete; awaiting Hemanth final read before Phase 2 (writing-plans)
**Workflow:** Phase 1 brainstorming (this doc) -> Phase 2 writing-plans -> Phase 3 executing-plans

---

## 1. Goal

Hemanth verbatim:

> "Tankoban-Max has forward and backward buttons in the top-left corner, just below the sidebar toggle, with < and > chevrons. They work like a browser's back/forward (and like Windows File Explorer's back/forward). I want this feature implemented in Tankoban 2 by Agent 5."

**Behavior anchor:** browser + Windows File Explorer back/forward. Real history stack with truncate-on-new-nav, keyboard shortcuts, mouse buttons 4 / 5, disabled state on stack ends, full state restore on back-navigation.

**Placement anchor:** Tankoban-Max's topbar left cluster (chevrons immediately right of the sidebar toggle, before the brand label).

---

## 2. Tankoban-Max reference — what's there, what's not

What Tankoban-Max has for visual placement and styling:

- Markup: [C:\Users\Suprabha\Desktop\Tankoban-Max\src\index.html](file:///C:/Users/Suprabha/Desktop/Tankoban-Max/src/index.html) lines 53-56 define `libBackBtn` / `libForwardBtn` as `iconBtn` siblings of `libMenuBtn` and `refreshBtn`. The forward button has the `disabled` HTML attribute baked in at markup time.
- Icons: [C:\Users\Suprabha\Desktop\Tankoban-Max\src\ui\icons.js](file:///C:/Users/Suprabha/Desktop/Tankoban-Max/src/ui/icons.js) lines 10-11 wire `libBackBtn` -> `chevron-left`, `libForwardBtn` -> `chevron-right` (Lucide).
- Element refs in JS shell: [C:\Users\Suprabha\Desktop\Tankoban-Max\src\domains\shell\core.js](file:///C:/Users/Suprabha/Desktop/Tankoban-Max/src/domains/shell/core.js) lines 144-146.
- Styling: [C:\Users\Suprabha\Desktop\Tankoban-Max\src\styles\styles.css](file:///C:/Users/Suprabha/Desktop/Tankoban-Max/src/styles/styles.css) lines 483-528. The `.tbLeft` flex cluster runs left-to-right with 6px gap; `.iconBtn:disabled` is `opacity:.45; cursor:default`.

What Tankoban-Max does NOT have for behavior:

- Back button handler at [shell_bindings.js](file:///C:/Users/Suprabha/Desktop/Tankoban-Max/src/domains/shell/shell_bindings.js) lines 1193-1206 is per-mode delegation only — it calls `window.booksApp.back()` / `window.videoApp.back()` / clicks the series-back affordance. No history stack lives in the shell; each mode's app has its own private back-affordance and the topbar Back just delegates.
- Forward button has zero click handlers anywhere in the Max codebase. The button is purely cosmetic — `disabled` at markup time, never enabled by any code path.

**Implication:** Tankoban-Max is the placement and visual-styling anchor. It is NOT the behavior anchor. Hemanth's verbal model — proper browser/Explorer history — is what defines the behavior. Tankoban 2 must build the real machinery Max never had.

---

## 3. Hemanth's product picks from brainstorm

Twelve plain-language scenarios were posed; one pick each.

### 3.1 Nav-event granularity (Q1)

A nav event is a top-level page switch OR opening a detail view. Sub-views inside a detail (season tabs, sort changes, search typing) do NOT push history entries. Browser-typical.

Example: `Home -> Videos library -> The Boys detail (Season 3) -> click Season 4 tab -> hit Back -> lands at Videos library` (the Season 4 click was sub-view, not tracked).

### 3.2 Restore depth on Back-navigation (Q2)

Full state restore. When Back lands on a previously-visited page, that page restores scroll position + sort + filters + search input. Browser-style. Each navigable page is responsible for capturing and restoring its own state via the contract in §4.2.

### 3.3 StreamPage interplay (Q3)

Unified — Stream's existing local back-stack (`m_navStack` + `showEntryRaw` machinery in [src/ui/pages/StreamPage.h](src/ui/pages/StreamPage.h)) folds into the global history. Stream's existing `NavEntry` struct becomes the *shape of Stream's state blob* in the global stack. The global controller is the single source of truth. Forward works inside Stream same as everywhere else.

### 3.4 Persistence (Q4)

Persist across sessions. Both stacks (back + forward) survive an app restart. On launch, the previous session's history is restored along with the cursor position. Stale entries (entries referencing data that was deleted between sessions) are filtered out — the page's `restoreNavState` returns false and the controller transparently skips that entry.

### 3.5 Keyboard shortcuts (Q5)

`Alt + LeftArrow` -> Back. `Alt + RightArrow` -> Forward. No other shortcuts. Backspace is deliberately not wired (Chrome removed it in 2016 because of accidental nav while typing).

### 3.6 Mouse buttons 4 / 5 (Q6)

`Qt::BackButton` -> Back. `Qt::ForwardButton` -> Forward. Standard browser convention. Wired at the QMainWindow level via `mousePressEvent` override so they fire from any page context.

### 3.7 Stack cap (Q7)

100 entries. Oldest evicted from the front when full. Roughly browser-typical. Memory cost is ~100 KB at full capacity (negligible).

### 3.8 Cross-page coordination (Q8)

Agent 5 owns all UI work for this feature across every page in the app. Other agents retain primary ownership of engine, scraper, player, and reader internals. If a nav-state hook needs to touch primary-ownership code (e.g., changing how Tankorent's search engine returns results), Agent 5 stops and asks the owning agent. Otherwise Agent 5 ships and posts to `agents/chat.md` to keep owners informed.

### 3.9 Placement (Q9)

Topbar left cluster reorders to `[hamburger] [<] [>] [Brand "Tankoban"]`. Chevrons sit immediately right of the hamburger button, before the brand label. Matches Tankoban-Max's `[hamburger][Back][Forward][...]` ordering verbatim while preserving Tankoban 2's brand label.

### 3.10 Reader / player Back behavior (Q10)

When ComicReader, BookReader, or VideoPlayer is open as a full-screen surface, Back closes the reader / player. Returns the user to the page that opened it. The reader / player itself is NOT a history entry — it's a modal overlay. Forward is disabled while the reader / player is open.

Esc and the close-X continue to work the same way they do today (parallel affordances, all three call into the same close path).

### 3.11 Forward-stack truncate-on-new-nav (Q11)

Wiped. Standard browser and File Explorer behavior. When the user navigates somewhere new from a position mid-history, the forward entries above the cursor are dropped.

### 3.12 Modal dialog Back behavior (Q12)

Back is a no-op while a modal dialog is open (Add Torrent, source picker, settings popup, Add From URL, etc.). Chevrons gray out while the modal has focus. Esc / close-X / outside-click close the modal. Once the modal closes, chevrons re-enable.

This is intentionally different from §3.10 reader/player. The reasoning: readers/players are immersive surfaces where Back-closes-it mirrors "exit fullscreen". Modals are mid-task input surfaces where Back accidentally cancelling the modal would be destructive.

### 3.13 Rule-14 bake-ins (technical, not Hemanth-product)

- **Startup state:** both chevrons disabled on first launch until the user makes their first nav event.
- **Accessibility:** `accessibleName` set to "Back" / "Forward"; `accessibleDescription` includes the keyboard shortcut hint.
- **Disabled-state visual:** opacity 0.35, cursor stays default arrow (no pointing-hand on disabled).
- **State-capture timing:** lazy — each page's state is captured at the moment of navigation away. Within-page interactions (typing, scrolling, filtering) update the page's internal state continuously but are not pushed to the history until the user actually navigates.
- **Long-press / right-click on chevron showing a history dropdown:** NOT in v1.
- **No topbar Refresh button** (Hemanth's verbal mentioned only chevrons; Tankoban already has per-page Scan affordances).
- **Brand label click behavior:** unchanged (currently a passive label).

---

## 4. Proposed architecture

Three pieces. One substrate class, one contract, one data shape.

### 4.1 NavHistory class

New file: `src/ui/NavHistory.{h,cpp}`. Singleton owned by `MainWindow` (passed to pages via getter; pages must not own the instance).

Public surface (concept):

- `pushEntry(QString pageId, QJsonObject stateBlob)` — called by `MainWindow` after a nav event fires. Captures the current page's state into the previous entry first, then pushes the new entry, truncating any forward entries above the cursor.
- `back()` / `forward()` — shift cursor; emit `entryRequested(NavEntry)`.
- `canGoBack() const` / `canGoForward() const` — for chevron enable / disable.
- `captureCurrent()` — explicit "snapshot the active page right now" call. Used before navigation events and at app-quit time.

Signals (concept):

- `backAvailableChanged(bool)`, `forwardAvailableChanged(bool)` — for chevron state and shortcut enable state.
- `entryRequested(const NavEntry& entry)` — emitted by `back()` / `forward()`. `MainWindow` listens, activates the target page, then hands the state blob to the page's `restoreNavState`.

Storage:

- `QVector<NavEntry> m_stack` — bounded at 100, evict from front.
- `int m_cursor` — current entry index. `-1` when stack is empty.
- Persistence as in §4.5.

The class is plain Qt — no third-party deps, no MOC tricks beyond `Q_OBJECT` for the signals.

### 4.2 INavStateProvider — the page contract

New file: `src/ui/INavStateProvider.h`. Pure interface (Qt-idiom: virtual methods, no Q_OBJECT — implementing classes have their own).

```cpp
class INavStateProvider {
public:
    virtual ~INavStateProvider() = default;

    // Snapshot the page's current user-visible state.
    // Called by NavHistory just before this page is navigated away from,
    // and on app-quit time. Must be cheap (no I/O, no thread waits).
    virtual QJsonObject captureNavState() const = 0;

    // Restore the page to the state described by blob.
    // Called by NavHistory after Back / Forward lands on this page.
    // Return true if restore succeeded; false if the blob references
    // data that no longer exists (e.g., a deleted show id). On false,
    // NavHistory drops this entry and tries the next one in the stack.
    virtual bool restoreNavState(const QJsonObject& blob) = 0;
};
```

The blob is opaque to NavHistory. Each page defines its own blob schema. Examples:

- VideosPage blob: `{"view":"library","scrollY":250,"sort":"recent","filter":"tv","search":""}`.
- VideosPage detail blob: `{"view":"detail","showId":"the-boys","seasonTab":3}` (seasonTab persists across this page's own captures even though it's sub-view).
- TankoyomiPage blob: `{"view":"search","query":"naruto","sourceFilter":"mangadex","scrollY":40}`.
- StreamPage blob: `{"view":"detail","showId":"daredevil","stremioId":"tt0316067"}` — derived from Stream's existing `NavEntry` struct fields.

Implementations: VideosPage, BooksPage, ComicsPage, StreamPage, TankorentPage, TankoyomiPage, TankoLibraryPage. Each one inherits both QWidget (or its current base) and INavStateProvider. Multiple inheritance is C++-clean here because INavStateProvider is a non-Q_OBJECT pure interface.

### 4.3 Cross-page event flow

When user fires a nav event (sidebar page button click, detail tile click, Stream library->detail transition):

1. `MainWindow` calls `m_navHistory->captureCurrent()`. NavHistory asks the active page for its `captureNavState()` and writes the result into the current entry.
2. `MainWindow` calls `m_navHistory->pushEntry(targetPageId, freshStateBlob)`. NavHistory truncates any forward entries above the cursor, evicts the oldest entry if at capacity, pushes the new entry, advances the cursor.
3. `MainWindow` performs the page activation as it does today (existing `activatePage` path).
4. `NavHistory` emits `backAvailableChanged(true)` (since stack now has at least one entry behind cursor); `forwardAvailableChanged(false)`.

When Back / Forward fires (chevron click, Alt+arrow, mouse 4 / 5):

1. `NavHistory::back()` (or `forward()`): captures current page state into the current entry first (so restore-on-return works), shifts cursor, emits `entryRequested(target)`.
2. `MainWindow` slot on `entryRequested`: activates `target.pageId` (no-op if already there); calls `targetPage->restoreNavState(target.stateBlob)`.
3. If `restoreNavState` returns false: NavHistory drops the entry and tries the next one in the same direction. Repeats until success or stack-end (chevron disables).

### 4.4 StreamPage interplay resolution

The global stack is the ONLY source of truth for navigation. Stream's existing `m_navStack` (the QStack used as a back-stack) is retired as a navigation mechanism — but the surrounding code is not deleted wholesale.

What is removed:

- `m_navStack` as a back-stack. The QStack member can be removed, or kept as an empty unused field if removal would ripple too far in Phase 3; either way it stops being read or written for navigation purposes.
- `StreamPage::goBack()` (if exposed) and any in-page Back affordance that is not the topbar chevron. From the user's perspective there is one Back: the topbar chevron, the keyboard shortcut, and the mouse thumb button.

What is retained and re-fitted:

- The `NavEntry` struct itself (with its `Kind`, show context fields, etc.) is preserved as a documentation-of-shape — its fields are the natural inputs to Stream's `captureNavState` blob. The struct can stay in the header.
- `showEntryRaw(const NavEntry& entry)` continues to exist as Stream's internal "render this view" dispatcher. It is now called by `restoreNavState` to reproduce the requested view, with no stack push of its own.

How a user-initiated nav event flows:

1. User clicks a show tile in Stream library.
2. StreamPage emits a "navigation requested" signal (new — narrow surface; signature includes the would-be `NavEntry` shape).
3. MainWindow's slot runs the standard flow from §4.3: ask current page (StreamPage itself) for its current state via `captureNavState` -> writes into the current global entry; pushes a new global entry tagged `{pageId:"stream", stateBlob:{view:"detail", showId:...}}`.
4. MainWindow then calls back into StreamPage with the new blob via `restoreNavState`, which routes through `showEntryRaw` to render the detail view.

How a Back / Forward navigation flows:

- Global controller decides which entry to land on.
- If target.pageId is "stream", MainWindow activates StreamPage (no-op if already there) and calls `restoreNavState(target.stateBlob)`.
- `restoreNavState` translates the blob into the equivalent `NavEntry` and routes through `showEntryRaw`. Forward works the same way.

Risk: this is the heaviest slice. StreamPage shipped the P6 NavEntry::Detail work last week. The refactor must preserve all of P6's behavior. Mitigation: ship the StreamPage hook as a single commit with smoke before declaring done; post heads-up to Agent 4 in `agents/chat.md` before starting.

### 4.5 Persistence

File: `nav_history.json` in `QStandardPaths::AppDataLocation` (same directory as `torrents.json`, `progress.json`, etc.).

Schema (sketch):

```json
{
  "schemaVersion": 1,
  "cursor": 4,
  "entries": [
    {"pageId":"videos","stateBlob":{...},"timestamp":1747200000000},
    {"pageId":"videos","stateBlob":{"view":"detail","showId":"the-boys"},"timestamp":1747200080000},
    ...
  ]
}
```

Write timing: on `MainWindow::closeEvent` (after `captureCurrent()` flushes the active page's latest state). Single atomic write via `QSaveFile` (matches the existing `JsonStore` discipline in the codebase). No background thread; persistence is one write per shutdown.

Read timing: in `NavHistory`'s constructor, before chevron wiring. If parse fails (corrupted file, schema mismatch, missing fields), start with empty stack and overwrite on next quit. Stale entries are NOT pre-filtered at load time — they're filtered lazily when Back / Forward lands on them (the page's `restoreNavState` decides whether the blob is still valid).

Eviction: the cap (100) is enforced on push, not on load. If on launch we somehow load 110 entries (e.g., from a future version that had a higher cap), we trim from the front to 100 silently.

### 4.6 Keyboard + mouse bindings

Keyboard: two `QShortcut` instances registered on `MainWindow`. Both have `Qt::ApplicationShortcut` context so they fire from any focused widget except text inputs that consume Alt+arrow themselves (Qt's default text-input behavior already absorbs plain LeftArrow / RightArrow for cursor movement; Alt+arrow is unused there).

Mouse: `MainWindow::mousePressEvent` override. Check `event->button()` for `Qt::BackButton` / `Qt::ForwardButton`. Call `m_navHistory->back()` / `forward()`. `event->accept()` to prevent propagation.

Both bindings are disabled by `NavHistory`'s `backAvailableChanged` / `forwardAvailableChanged` signals (no firing on empty stacks).

---

## 5. Per-page work list (decomposes into Phase 2 tasks)

All under Agent 5 per §3.8. Posted to `agents/chat.md` per slice. Owners flagged where Agent 5 must defer if engine/scraper/player/reader internals would be touched.

Substrate (no per-page implementation):

1. NavHistory class — `src/ui/NavHistory.{h,cpp}`. Stack, cursor, push, back, forward, captureCurrent, persistence load / save, eviction.
2. INavStateProvider interface — `src/ui/INavStateProvider.h`. Pure-virtual header.
3. Topbar chevrons — wire two QPushButton instances into `MainWindow::buildTopBar()` at the position from §3.9. SVG icons already exist. QSS scoped under `#TopBarBackBtn` / `#TopBarForwardBtn`. Re-balance the `mirrorTopBarSlotWidths()` invariant after the topbar grows.
4. Keyboard shortcuts — register Alt+Left / Alt+Right QShortcuts.
5. Mouse buttons — override `MainWindow::mousePressEvent` for Qt::BackButton / Qt::ForwardButton.
6. Reader / player overlays — in `MainWindow`, intercept Back when ComicReader / BookReader / VideoPlayer is the active surface; route to the existing close-path. Forward disabled while the surface is open. Modal dialogs gate Back / Forward via `QApplication::activeModalWidget()` check.

Per-page hooks (one slice each; ordered by simplicity):

7. ComicsPage — captureNavState + restoreNavState. State: scroll, sort, search input, selected tile. No detail page in this domain (ComicReader is the modal overlay).
8. BooksPage — same shape as ComicsPage.
9. VideosPage + ShowView — VideosPage has a library view + a ShowView (show detail). Both contribute to state; the blob's `view` field distinguishes them. ShowView's season tab selection is captured (sub-view inside the same detail entry).
10. TankorentPage — captureNavState + restoreNavState. State: search input + filter chips + scroll. **Engine boundary:** if `TorrentClient` / `TorrentEngine` integration would be touched, defer to Agent 4B and HELP-request the engine-side hook.
11. TankoLibraryPage — same shape as TankorentPage. Source plugin code untouched.
12. TankoyomiPage + MangaDetailView — Tankoyomi has both library / search view and a manga detail (MangaDetailView). Both contribute. **Scraper boundary:** scraper code untouched; only UI state is captured.
13. StreamPage + StreamDetailView — the heaviest slice. Re-fits `m_navStack` as `captureNavState`'s internal storage. Routes nav events through the global controller. Preserves all P6 behavior. Heads-up to Agent 4 in chat.md before starting.

Each slice is one commit (or one bundled small group) with build verification.

---

## 6. Visual spec

Topbar left cluster layout in `MainWindow::buildTopBar()`:

```
[hamburger]  [<]  [>]    Tankoban
   28x24    28x24 28x24
```

Spacing matches the existing iconBtn convention used by `m_hamburgerBtn` — 6px gap between adjacent icon buttons. A larger gap (12px) before the brand label to separate the navigation cluster from the brand identity.

Buttons:

- `QPushButton` instances named `m_backBtn` + `m_forwardBtn` (object names `TopBarBackBtn` + `TopBarForwardBtn`).
- Fixed size 28x24 (matching `m_hamburgerBtn`).
- Cursor: `Qt::PointingHandCursor` when enabled; `Qt::ArrowCursor` when disabled.
- Focus policy: `Qt::NoFocus` (same as other topbar icon buttons).
- Icons: `:/icons/chevron_left.svg` + `:/icons/chevron_right.svg` — already shipped, stroke `#c6c6c6`, 16x16 icon size.

QSS (scoped under object names):

```css
QPushButton#TopBarBackBtn,
QPushButton#TopBarForwardBtn {
    background: transparent;
    border: none;
    padding: 0;
}
QPushButton#TopBarBackBtn:hover:!disabled,
QPushButton#TopBarForwardBtn:hover:!disabled {
    background: rgba(255, 255, 255, 0.06);
    border-radius: 4px;
}
QPushButton#TopBarBackBtn:disabled,
QPushButton#TopBarForwardBtn:disabled {
    /* Opacity rule via Qt requires the icon-color trick;
       implementation will use QIcon::Disabled mode + the
       same SVG. Result: visibly dim chevron, no border,
       no hover affordance. */
}
```

Tooltips:

- Back: `"Back (Alt+Left)"`
- Forward: `"Forward (Alt+Right)"`

Accessibility (`accessibleName` + `accessibleDescription`):

- Back: `"Back"` + `"Navigate to the previous page. Keyboard: Alt+LeftArrow."`
- Forward: `"Forward"` + `"Navigate to the next page. Keyboard: Alt+RightArrow."`

Left-slot width: the topbar's `mirrorTopBarSlotWidths()` invariant currently sets `m_topBarLeftSlot->setFixedWidth(m_topBarRightSlot->sizeHint().width())`. Adding two 28-wide buttons + 6+6+12 px of gaps grows the left slot by ~80px. If the new left slot exceeds the right slot's sizeHint, the mirror logic needs to either (a) widen the right slot to match, or (b) flip the mirror direction (right slot mirrors left). Implementation will pick whichever preserves the current centered topbar look.

---

## 7. Risk + rollback

**Highest risk: StreamPage refactor (§5 item 13).** Stream's P6 work shipped a week ago and is recently active. The refactor must not regress any P6 behavior. Mitigations:

- Single commit per the StreamPage slice; small, surgical, focused.
- Smoke pass before declaring done: open Stream library, click a show, hit Back, see library restored; click show again, hit Forward, see detail restored; cross-page Back into Comics, Forward into Stream detail.
- Heads-up to Agent 4 in `agents/chat.md` before the slice starts (per §3.8 coordination).
- Recovery: this slice is `git revert`-able as a single commit. The Stream P6 code is preserved in history and can be restored independently of any other slice.

**Persistence file corruption.** On launch, if `nav_history.json` is malformed or schema mismatches, fall back to empty stack and overwrite on next quit. No user-visible error; first-launch behavior. Verified by injecting a malformed file in a smoke pass.

**Stale entries after between-session deletions.** If Hemanth deletes a show / book / manga between sessions, a Back-navigation that lands on that entry's blob would fail to restore the missing detail. The contract handles this: the page's `restoreNavState` returns false, NavHistory drops the entry, tries the next one. From Hemanth's perspective, Back "skips over" the deleted-content entry — same as a browser back-navigating past a closed tab.

**Topbar width imbalance.** Adding ~80px to the left slot may push the page-pill switcher in the center to look unbalanced. Mitigation: the existing `mirrorTopBarSlotWidths` invariant gets adjusted to widen the right slot to match. Visual verification on Hemanth's smoke pass.

**Modal dialog edge cases.** A modal closed via Esc / X while Back is technically queued: not a real race — Back is synchronous; the modal-active gate is checked at click / shortcut time, not later. No race window.

**Rollback for the whole feature.** Every per-page hook is additive (new methods on existing classes). Reverting the feature is a single `git revert` of the substrate commit + reverting each per-page slice. The per-page hooks are independent of each other (each page tested standalone in Phase 3). No cross-slice coupling beyond the shared `INavStateProvider` header.

---

## 8. Open questions deferred to Phase 2

These are implementation-level decisions Agent 5 will make during Phase 2 (writing-plans):

- Exact field set in each page's state blob (some fields like Tankoyomi's `sourceFilter` aren't observable from the spec alone — verifying on the source code during plan-writing).
- Concrete signal / slot wiring between MainWindow and NavHistory vs static singleton access vs DI.
- Whether `MainWindow::buildPageStack` needs splitting for the per-page hook registration loop or if pages register themselves in their own ctor.
- ShowView season-tab persistence policy (sub-view internal to the entry, captured at navigation time; details on whether ShowView itself owns the persistence or VideosPage proxies it).
- Whether the StreamPage refactor preserves `goBack()` as a public method (for Stream-internal use) or removes it entirely.
- Phase 3 sequencing: substrate-first ship vs substrate-and-Videos-together ship.

None of the above changes the product behavior Hemanth ratified in §3. All are internal C++ shape calls (Rule 14).
