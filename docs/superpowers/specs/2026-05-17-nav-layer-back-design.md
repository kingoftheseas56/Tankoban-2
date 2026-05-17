# Layer-Based Navigation Redesign — Design Spec

**Date:** 2026-05-17
**Owner:** Agent 5 (Library UX + Theme)
**Status:** Phase 1 brainstorm complete (16 product picks locked); awaiting Hemanth review before writing-plans
**Workflow:** Brainstorm (this doc) -> writing-plans -> executing-plans
**Supersedes:** [`docs/superpowers/specs/2026-05-14-global-nav-history-design.md`](2026-05-14-global-nav-history-design.md)
  (the browser-style flat-history model adopted three days ago is REPLACED in full)

---

## 1. Goal

Hemanth verbatim (2026-05-17 brief, three pieces):

> **(A)** I don't want the back and forward buttons in the top bar to function just like the Windows file manager back and forth or the back and forth on a browser like Google Chrome because after using it, I realized it kind of got confusing. And I think it could be better if the back and forth button actually functioned like going back to the direct layer that is behind the page. ... At a top-level mode root, back is a no-op. There is no back layer behind the mode itself.
>
> **(B)** nah fuck the front button, remove it. doesn't serve purpose with our new navigation style.
>
> **(C)** When I open the comic series view in comic mode, if I click back on it, it does not work. ... If I click on Comics, it does not matter which page I am on; it should take me back to the main comic library. This has been the navigation experience for Tankoban, not just for this app, but it is an essential part of all our predecessors, including Tankoban Max, Tankoban Max Butterfly, and Tankoban QT Groundworks.

**The three pieces fold into one product shape:** layer-based per-mode Back, no Forward, mode pills are end-all-be-all hard resets.

---

## 2. Pre-spec context — the four scaffolding rules locked at brief time

These are NOT brainstorm questions; Hemanth ratified them in the brief and they constrain everything below.

1. **Back is layer-based** — always goes UP one level in the current mode's UI hierarchy. Not browser-history-flat. Stream show view -> Stream home. Comic series view -> Comic home. Comic series view reached from search results -> search results layer (NOT Comic home — search IS the layer behind in that path).
2. **Back is scoped per mode** — cross-mode jumps (clicking the Theatre pill from inside Comic mode) do NOT add to any back stack. Back inside Stream never traverses into Comic.
3. **Forward button is physically removed** — not "disabled cosmetic," not "stays as-is." Gone from the topbar entirely. Alt+Right unbound. Mouse btn 5 inert.
4. **Mode pills are end-all-be-all hard resets** — clicking any pill from ANY state (deep view, search, modal, etc.) returns that mode to its root home view. Pills are NOT history events; they are hard resets that wipe (or reset) the per-mode stack to [root].

---

## 3. Product picks from brainstorm (16 picks, Batches A-D)

### Batch A — Back-button disambiguation

- **A1 (re-entry path):** Back walks the path you JUST took. Library -> Search -> Result -> Series, Back goes to Search Results. If you backed out earlier and re-entered via a different path, the earlier visit is forgotten — the new path is the live one.
- **A2 (search results as layer):** Search results IS a full layer. Library -> Search -> Result -> Series, Back goes Series -> Search Results -> Library (three steps).
- **A3 (back tooltip):** Hovering Back chevron shows a destination-name tooltip ("Back to Search Results" / "Back to Library"). Same precedent as Tankoban-Max.
- **A4 (bottom-of-stack):** At mode root, Back chevron is grayed out (low opacity, no hover effect) and click is a silent no-op.

### Batch B — Forward-button removal

- **B1 (topbar layout):** Back chevron stays at its current X position. The Forward slot is removed and the brand label shifts ~42px left to close the gap. No relayout.
- **B2 (Alt+Right):** Unbound cleanly. Becomes inert app-wide. No remap.
- **B3 + B4 (mouse buttons 4 and 5):** Both inert. Tankoban explicitly does nothing with thumb-button input. If a power-user mouse fires either, no app-level action; OS-default behavior may pass through.

### Batch C — Mode-pill behavior

- **C1 (pill on root):** Silent no-op. No scroll-to-top, no rescan, no refresh. Identical to today's fresh-launch behavior.
- **C2 (pill + modal):** Block until modal closed. Pill click is ignored while a modal dialog is up; the user must close the modal (X / Esc / Cancel) first. Prevents accidental discard of mid-task input.
- **C3 (pill + reader/player) - reframed per D4:** Moot in practice. The Comic Reader and Video Player are fullscreen takeovers that HIDE the topbar entirely; the pills are not reachable while either is open. The reader/player has its own internal back affordance for exit.
- **C4 (pill + player) - same as C3:** Moot. Fullscreen takeover hides the topbar.

### Batch D — Edge cases

- **D1 (cross-mode return):** Library Home (mode reset). Cross-mode pill click resets the destination mode to its root. Per-mode back stack for the entered mode is wiped to [root].
- **D2 (persistence across restart):** Wiped. Per-mode back stacks reset to [root] on every launch. No nav_history.json. A session is a session.
- **D3 (deep-link entry):** Inferred chain. If Tankoban opens directly into Reader via a notification click (or future URL handler), the inferred Comics back stack is [Library, Series, Reader] so the user can navigate out naturally. Closing the reader exits to the inferred Series view (or Library if Series synthesis is not possible). NOTE: per D4 the topbar is hidden inside the reader so this inferred chain is consumed by the reader's own internal back button, not by the topbar chevron.
- **D4 (reader / player role):** Fullscreen overlays OUTSIDE the back stack. Opening one does NOT push a stack entry. The topbar is HIDDEN entirely. The reader's / player's own internal back affordance is the only nav out. Mode pills are not visible during reader/player. This simplifies C3 / C4 to "moot".

---

## 4. Architecture pick — Approach A, PerModeNavController

New class `PerModeNavController` in `src/ui/`. Replaces the existing `NavHistory` + `INavStateProvider` machinery entirely (the 2026-05-14 browser-style design is retired in full).

**Shape:**

- `PerModeNavController` owns `QHash<QString /*pageId*/, QStack<LayerEntry>>` — one stack per mode.
- Each registered mode page emits `enteredLayer(LayerEntry)` and `exitedLayer()` signals when its in-page mode flips (Library <-> SearchResults <-> SeriesView, Browse <-> CatalogBrowse <-> Detail, etc.).
- Controller listens, pushes/pops the corresponding mode stack.
- Topbar Back chevron asks the controller: `controller->canGoBack(currentPageId)` (returns bool); `controller->backDestinationLabel(currentPageId)` (returns tooltip string for A3); `controller->goBack(currentPageId)` (walks one layer; controller fires a `restoreLayer(LayerEntry)` signal that the active page handles).
- Mode-pill click handler calls `controller->resetMode(targetPageId)` which clears that mode's stack to [root]; cross-mode pill click then `activatePage(targetPageId)` follows.

**Why A over the alternatives (preserved here for plan context):**

- B (refactor existing NavHistory): bending a flat cursor-based browser stack into per-mode layer stacks is more rewrite than rebuild; future readers see "browser history with extra steps" and get confused.
- C (per-page ownership, no central controller): tooltip-of-destination requires polymorphic dispatch across 5 page types; cross-mode pill resets require 5 separate resetStack methods; testing per-mode behavior means testing 5 pages independently. The shared concern is not page-specific, so centralization wins.

**Files affected (estimate):**

- NEW: `src/ui/PerModeNavController.{h,cpp}` (~150-200 LOC)
- NEW: `src/ui/LayerEntry.h` (struct: pageId, layerKind, label, opaque-JSON state-blob)
- MODIFIED: `src/ui/MainWindow.{h,cpp}` (back-chevron wire, forward-chevron removal, pill click handler refactor, controller instantiation)
- MODIFIED: 5 page files (`ComicsPage.{h,cpp}`, `StreamPage.{h,cpp}`, `BooksPage.{h,cpp}`, `VideosPage.{h,cpp}`, `TankorentPage.{h,cpp}`) to emit `enteredLayer` / `exitedLayer` on their in-page transitions
- DELETED: `src/ui/NavHistory.{h,cpp}`, `src/ui/INavStateProvider.h`, all `captureNavState` / `restoreNavState` implementations on pages, the persistence file path code, the AppData nav_history.json blob format. Forward chevron QPushButton + Alt+Right QShortcut + mouse-button-5 handler. The 2026-05-14 spec deferred to the same-day rename `_2026-05-14-global-nav-history-design.md.superseded.md`.

---

## 5. Phase 0 status (what just shipped this wake, NOT part of the new redesign)

Two restore-the-broken-contract patches landed today (Agent 5, 2026-05-17 chat.md sweep pending):

- **0a (initial Phase 0):** Wired `ComicsPage::navigationRequested` signal + connected to `NavHistory::recordNavEvent("comics")` in MainWindow + emit-with-guard in 5 mode-flippers (showLibraryMode, showSearchMode, onSearchResultActivated, openSeriesByPath, onDetailBack) + `QScopedValueRollback` guard in `restoreNavState`. Added `resetToRoot()` public forwarder on ComicsPage and StreamPage. Added `MainWindow::resetActivePageToRoot()` with `qobject_cast` polymorphic dispatch. Pill click handler routes same-page through resetActivePageToRoot instead of activatePage's early-return.
- **0b (Phase 0 follow-up, this wake post-Hemanth-callout):** Added the missing emit in `ComicsPage::openSeriesByAnilistId` (the Phase 10 method for BOOKMARKED + DOWNLOADED tile clicks that the initial 0a pass missed because it was added one day after Phase 9 and the audit only walked Phase 9 transitions).

After 0a + 0b, the Back chevron correctly enables in deep Comics states. But it walks ACROSS modes (Comics deep -> Back -> Stream home) because the underlying NavHistory is still flat / cross-mode-walking — that is the Phase 1 problem this spec replaces.

**Phase 0c (a small hotfix that could land before Phase 1):** scope NavHistory.back() to skip cross-mode entries (~5-10 LOC patch). Buys Hemanth a working in-mode Back today, ahead of the full Phase 1 rewrite. Hemanth-pending decision — recommendation in §8 below.

---

## 6. Out of scope

- Reader / player content design (Agent 1 owns Comic Reader; Agent 3 owns Video Player). Their internal back buttons are unchanged.
- Theme system tokens (coordinate as flag; chevron + tooltip visual styling lands as part of this scope only).
- The Comics-mode-Tankoyomi merger Agent 1 is brainstorming separately (different arc; this nav spec just consumes whatever ComicsSeriesView ends up being).
- Modal dialog VISUAL design (this scope only wires the C2 "block pill while modal open" behavior).
- Search bar / sort dropdown / library filters internal behavior (the spec only governs whether they participate in the back stack — they do not; the search bar is part of the Library layer, sort is sub-layer state captured by the Library entry's blob).

---

## 7. Acceptance criteria

After Phase 1 ships, the verification matrix from the 2026-05-17 brief (items a-i) must all pass under live MCP-driven smoke:

- **(a)** Comic series view -> Back -> Comic library home.
- **(b)** Comic series view -> Click Comics pill -> Comic library home.
- **(c)** Comic search results -> Back -> Comic library home.
- **(d)** Comic search results -> Click Comics pill -> Comic library home.
- **(e)** Stream show view -> Click Theatre pill -> Stream root.
- **(f)** Books deep state -> Click Books pill -> Books root (or no-op if no deep state exists; document that).
- **(g)** Theatre deep state -> Click Theatre pill -> Theatre root.
- **(h)** Inside Tankorent (sidebar drawer) -> Click Theatre pill -> Theatre root.
- **(i)** Any modal state -> pill click is BLOCKED (C2); reader/player state -> pills not visible (D4).

Plus the architectural invariants:

- Forward chevron entirely removed from topbar markup (B1).
- Alt+Right keyboard shortcut unbound (B2).
- Mouse buttons 4 and 5 inert (B3 + B4).
- Back tooltip names the destination (A3).
- Back at mode root grayed out + silent (A4).
- App restart wipes all per-mode stacks (D2).
- No `nav_history.json` written or read.

---

## 8. Phase 0c hotfix decision — Agent 5 recommendation

Land Phase 0c as a 5-10 LOC patch to `NavHistory::back()` that skips cross-mode entries. Unblocks Hemanth's in-mode Back today; Phase 1 then replaces NavHistory entirely with PerModeNavController. The hotfix is throwaway code — deleted as part of Phase 1's NavHistory deletion — but the cost of leaving it out is "Back walks to Theatre when you press it from Comics" which is the exact bug Hemanth flagged.

**Awaiting Hemanth ratify or override** (Rule 14 implementation territory, but Hemanth's experience of the broken contract is product-territory enough to confirm).

---

## 9. Bug-diagnosis log (carry-forward for plan context)

Two distinct bugs in today's working state:

- **The openSeriesByAnilistId emit gap** — closed in Phase 0b today. Root cause: a new method added in Phase 10 (one day after my Phase 0a audit's reference Phase 9) was missed because the audit grep was scoped to Phase 9 transitions. Fix: emit `navigationRequested()` at the top of the method, guarded by `!m_inNavRestore`.
- **The cross-mode walking back stack** — open. Root cause: `NavHistory` is a single flat list with a cursor; `recordNavEvent` does not segregate by pageId; `back()` decrements the cursor without checking whether the target entry is in the current mode. Fix lands either as Phase 0c (skip-cross-mode patch in `NavHistory::back()`) or as Phase 1 (delete NavHistory, replace with PerModeNavController).

Both bugs traced via /superpowers:systematic-debugging this wake; full evidence at `agents/audits/smoke_evidence/0300..0322_*.png`.
