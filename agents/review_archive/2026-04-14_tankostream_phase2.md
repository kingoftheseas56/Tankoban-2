# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankostream Phase 2 — Addon Manager UI (Batches 2.1 AddonManagerScreen, 2.2 AddAddonDialog, 2.3 AddonDetailPanel)
Reference spec: `STREAM_PARITY_TODO.md` Phase 2 (planning doc). Cross-refs the Phase 1 APIs (`AddonRegistry::installByUrl`/`uninstall`/`setEnabled`/`list` + `AddonDescriptor` shape) that Phase 2 consumes.
Objective: ship the install / enable / disable / uninstall UI so `stream_addons.json` is never hand-edited in normal use
Files reviewed:
- `src/ui/pages/stream/AddonManagerScreen.h/.cpp` (2.1 + 2.3 wiring into StreamPage stack index 3)
- `src/ui/dialogs/AddAddonDialog.h/.cpp` (2.2 install-by-URL modal)
- `src/ui/pages/stream/AddonDetailPanel.h/.cpp` (2.3 right-side detail pane)
- `src/ui/pages/StreamPage.cpp:6, :130-135, :176-188, :270-275` (entry-point wiring: Addons button + stack layer + showAddonManager slot)

Date: 2026-04-14

### Scope

Phase 2 UI surface against its STREAM_PARITY_TODO.md spec. Relies on Phase 1 (PASSED) APIs — cross-ref is whether Phase 2 calls the registry API correctly, not whether the registry itself is correct. Out of scope: addon-catalog discovery UI (explicitly skipped per Phase 2 scope; relevant only to hypothetical future "Discover More Addons" UI); catalog aggregation (Phase 3); stream aggregation (Phase 4). Static read only; no build.

### Parity (Present)

**Batch 2.1 — AddonManagerScreen (fourth stack layer)**

- **Fourth layer in `StreamPage` QStackedWidget at index 3.** Spec `STREAM_PARITY_TODO.md:104`. Code: `StreamPage.cpp:130-131` — `m_addonManager = new AddonManagerScreen(m_addonRegistry, this); m_mainStack->addWidget(m_addonManager); // index 3: addons`. Comment literally tags the index. ✓
- **Back button returns to browse.** Code: `StreamPage.cpp:133-135` connects `backRequested → [switch stack to browse index]`. `AddonManagerScreen::buildUI` at `:212-216` wires the Back button to emit `backRequested`. ✓
- **Installed addon list with 32×32 async logo, name, version, enabled toggle, "OFFICIAL" badge, description.** Spec `STREAM_PARITY_TODO.md:105`. Code: `AddonRowWidget` at `AddonManagerScreen.cpp:29-108`. Logo is `QLabel` fixed 32×32 (`:44`), async-loaded via `loadLogoAsync` with per-widget `QHash<QString, QPixmap>` cache (`:267-310`, cache at `m_logoCache`). First-letter fallback before the network response lands (`:50-52`). Name + version + OFFICIAL badge shown conditionally in the title row (`:63-78`); description below with word-wrap (`:82-88`). Enabled toggle is a right-aligned QCheckBox (`:92-96`). ✓
- **Toggle wiring respects registry rejection.** Code: `AddonManagerScreen.cpp:173-180` — if `m_registry->setEnabled(...)` returns false, `QSignalBlocker`-guarded revert of the checkbox state. Defensive; the Phase 1 registry currently always returns true for non-protected + honours protected for enable/disable, so the guard is belt-and-suspenders. ✓
- **Logo async-fetch with dangling-widget guard.** `QPointer<QLabel> guard(target)` at `AddonManagerScreen.cpp:289`; reply callback checks `if (guard)` before setting the pixmap. Protects against the row being destroyed mid-fetch (e.g. `addonsChanged → refresh() → list rebuild`). ✓
- **Re-render on `AddonRegistry::addonsChanged`.** Connection at `:121-122`. Refresh preserves previous selection by id at `:137-140`. Protected tile re-selects as well because id-based restore is format-agnostic. ✓
- **Add-addon flow is dialog-based and refreshes on close.** Code: `AddonManagerScreen.cpp:125-132` — clicking the "+ Add addon" button emits `addAddonRequested`; a slot-wired lambda opens `AddAddonDialog` modally and calls `refresh()` afterward. Whether the dialog was accepted or rejected, the refresh normalises state. ✓

**Batch 2.2 — AddAddonDialog (install-by-URL)**

- **Modal, fixed 500×300, manifest-URL QLineEdit + Install button + status label.** Spec `STREAM_PARITY_TODO.md:113`. Code: `AddAddonDialog::AddAddonDialog` at `:15-31` calls `setModal(true)` + `setFixedSize(500, 300)`. Layout has title, help text, URL input, status label, Cancel + Install buttons (`:90-143`). Default button is Install (`:137`). ✓
- **Install → `AddonRegistry::installByUrl`.** Code: `onInstallClicked` at `:33-57` validates input is non-empty + has scheme, stashes `m_pendingUrl`, sets the busy state (disables input + buttons at `:145-150`), shows "Installing addon…" status, then calls `m_registry->installByUrl(url)`. ✓
- **Success → close + refresh.** Code: `onInstallSucceeded` at `:59-73` clears `m_pendingUrl`, shows "Installed: {name}" then calls `accept()`. Parent's post-exec `refresh()` (`AddonManagerScreen.cpp:131`) picks up the new row. ✓
- **Failure → inline error.** Code: `onInstallFailed` at `:75-88` restores interactive state, shows "Error: {message}" in the status label (with bold/higher-contrast styling from `showStatus(msg, true)`). ✓
- **Pending-install tracking is idempotent.** Both success and failure handlers gate on `m_pendingUrl.isValid()` so stale signals from an earlier cancelled dialog can't misfire. ✓
- **Trigger wired on the AddonManagerScreen header.** Spec `STREAM_PARITY_TODO.md:115`. Code: `AddonManagerScreen.cpp:224-229` — "+ Add addon" button in header connected to `addAddonRequested`. ✓

**Batch 2.3 — AddonDetailPanel (right-side detail pane)**

- **Embedded inside AddonManagerScreen, visible on row select.** Spec `STREAM_PARITY_TODO.md:120`. Code: `AddonManagerScreen.cpp:261-262` adds `m_detail = new AddonDetailPanel(m_registry, this)` in the body horizontal layout; `:258-259` connects `currentRowChanged → updateDetailForRow`; `:193-200` pushes the selected descriptor into the panel. Placeholder text ("Select an addon to view details") at `AddonDetailPanel.cpp:143-146` shows when nothing is selected. ✓
- **Logo, name, version, transport URL, description.** Code: `setDescriptor` at `AddonDetailPanel.cpp:63-100` populates all five. Transport URL is `TextSelectableByMouse` (`:198`) so users can copy-paste the URL they installed. ✓
- **Capabilities as read-only chips: types[], resources[], catalogs[].** Spec `STREAM_PARITY_TODO.md:122`. Code: `refillChips` at `AddonDetailPanel.cpp:262-301` enumerates `type:X`, `resource:X`, `catalog:type/id` chips. Layout is a 2-column grid (`:291-300`) — compact. Empty-state chip "No declared capabilities" at `:284-288` for bare manifests. ✓
- **Enable/Disable toggle + DANGER-styled Uninstall.** Spec `STREAM_PARITY_TODO.md:123-124`. Code: Enable toggle at `:229-234`; Uninstall QPushButton at `:237-251` with the DANGER stylesheet (red `#e53935` text, red-tinted bg, red border) + precedent-cite comment at `:236` referencing `ContextMenuHelper::addDangerAction`. Uninstall confirmation dialog at `:123-132` (QMessageBox::question, default No). ✓
- **Protected flag hides Uninstall + shows note.** Spec `STREAM_PARITY_TODO.md:124` ("`protected=true` addons hide Uninstall. Enable/Disable still works"). Code: `setDescriptor` at `:90-91` — `m_uninstallBtn->setVisible(!d.flags.protectedAddon)` + `m_protectedNote->setVisible(d.flags.protectedAddon)`. The protected note reads "Protected addon — uninstall disabled" at `:253-256`. Enable toggle is left unconditionally active so the user can still disable Cinemeta/Torrentio. ✓
- **setDescriptor(std::nullopt) correctly shows placeholder and hides content.** Code: `:63-72` swaps visibility of `m_placeholder` and `m_contentRoot`. `AddonManagerScreen::refresh` at `:147-148, :157` calls this on registry-empty or list-empty. ✓
- **QSignalBlocker around programmatic enable-toggle updates.** Code: `:84-87` — prevents `toggled` signal from firing when we're just reflecting state into the UI on selection change. Same pattern as AddonManagerScreen's row-level toggle. ✓
- **Own cache for the 64×64 detail logo.** Code: `loadLogoAsync` at `:303-343` mirrors the manager-screen fetcher with `QHash<QString, QPixmap> m_logoCache` local to the panel, larger target size, `QPointer<QLabel> guard` against widget destruction mid-fetch. ✓

**Cross-cutting — StreamPage integration**

- **`m_addonRegistry` constructed before clients.** Code: `StreamPage.cpp` somewhere prior to line 130 (not shown here, but confirmed via grep for `m_addonRegistry = new AddonRegistry`). Clients receive the registry pointer for their AddonTransport-based lookups. ✓
- **Entry button in search bar.** Code: `StreamPage.cpp:176-188` — `m_addonsBtn = new QPushButton("Addons", m_searchBarFrame)`, `setToolTip("Manage installed addons")`, click connected to `showAddonManager`. See P2 gap about text-button vs spec's "gear icon" below.
- **`showAddonManager` calls refresh before switching stack index.** Code: `StreamPage.cpp:270-275` ensures the UI is up-to-date the moment the user lands. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Entry point is a text button "Addons", not the spec's gear icon.** Spec `STREAM_PARITY_TODO.md:106` — "Entry: gear icon (28x28) in browse header of StreamPage." Code: `StreamPage.cpp:176-181` constructs `QPushButton("Addons")` with `setFixedHeight(32)`. Agent 4's STATUS.md open-debt line already owns this as "Proper gear SVG icon (polish)". Functional parity (one-click → manager screen) is intact; visual parity deferred. Flagging only so the debt is mirrored in the review record — no action required now.
- **DANGER-red Uninstall button tension with `feedback_no_color_no_emoji`.** `feedback_no_color_no_emoji` says "Strictly gray/black/white UI… no colored text." `AddonDetailPanel.cpp:237-251` renders the Uninstall button in `#e53935` red. **However**, STREAM_PARITY_TODO.md:123 explicitly says "Uninstall (DANGER styled — use `ContextMenuHelper::addDangerAction` pattern or equivalent styling)" — the spec that Agent 4 is reviewed against authorizes the red styling by name. The TODO postdates the no-color memory so it represents a more recent Hemanth decision for this specific surface. Agent 4's STATUS flags the tension explicitly "pending explicit `feedback_no_color_no_emoji` ruling by Hemanth". Not a gap in this review (Agent 4 shipped what the TODO asked for) — noting so Hemanth can rule on whether the `feedback_no_color_no_emoji` memory should get a "DANGER surfaces are exempt" clause or whether the Uninstall button should swap to grayscale-with-warning-icon. One-line revert either way.
- **`AddonRegistry::installFailed` emits inconsistent URL forms** (carry-over from Phase 1 observations, lands in Phase 2 UI consumer). Code: `AddonRegistry.cpp:311` emits on "Invalid addon URL" with the *raw* input URL (pre-normalize), while `:317, :368, :407` emit with the *normalized* URL. `AddAddonDialog::onInstallFailed` at `:75-88` works around this with `Q_UNUSED(inputUrl)` and self-tracks via `m_pendingUrl.isValid()`. Works because the dialog is modal and single-shot. If the install-by-URL flow is ever re-used in a non-modal surface (e.g., a future "batch-install from a list of URLs" feature), the URL-arg inconsistency would start to matter. Agent 4's STATUS owns this. No action now.
- **Logo cache is per-widget instance, not shared.** `AddonManagerScreen` has its own `m_logoCache`; `AddonDetailPanel` has its own. Each screen re-fetches logos on construction. Small efficiency miss — one shared cache in `AddonRegistry` (or a standalone `AddonLogoCache` singleton) would halve network round-trips on first open of the manager. Per-surface for now; consolidate later if it ever matters.
- **`onInstallSucceeded` doesn't call `setBusy(false)` before `accept()`.** `AddAddonDialog.cpp:59-73` leaves the Install button disabled when the dialog closes. Invisible because `accept()` closes the dialog immediately, but asymmetric vs `onInstallFailed` which does call `setBusy(false)`. Cosmetic.
- **Capability chips display `catalog:{type}/{id}` not `catalog:{name}`.** `AddonDetailPanel.cpp:279-282` builds the catalog chip from `type + id`. Stremio's user-facing catalog surfaces show the human-readable `name`. For Cinemeta with `id=top`, the chip reads `catalog:movie/top` — correct but terse; the manifest also carries `name="Top Movies"` which would be friendlier. Stylistic choice; `id` is stable for diffing, `name` is nicer for the user. Flagging for awareness; no action required.
- **Periodic manifest refresh not implemented.** Agent 4's STATUS owns this as "Periodic manifest refresh for seeded addons (deferred beyond Phase 4)." If Cinemeta updates its manifest (e.g., adds a new catalog entry), Tankoban never re-fetches because `seedDefaults` runs only on first-launch. For the test + Phase 6 timelines, won't be noticed. Deferral acknowledged.

### Answers to Agent 4's disclosed open debts

Agent 4's STATUS lists five Phase 2-adjacent open debts; confirming disposition on each:

1. **Gear SVG icon** — P2 above; cosmetic, deferred, acceptable.
2. **DANGER-red Uninstall pending Hemanth ruling** — P2 above; TODO authorizes, memory opposes, Hemanth's call.
3. **`installFailed` inconsistent URL forms** — P2 above; dialog tolerates, works today, document for future reuse.
4. **`normalizeManifestUrl` duplication** — Phase 1 P2 carried forward, no new surface touches in Phase 2.
5. **Periodic manifest refresh** — P2 above; out-of-scope per Phase 4 deferral.

None of these upgrade to P1.

### Questions for Agent 4

1. **Gear icon vs "Addons" text button.** Is the gear-icon swap blocked on finding/commissioning an SVG asset, or is it purely a prioritization choice? If blocked on the asset, happy to flag the dependency in the review so it doesn't get lost.
2. **DANGER-red ruling.** Do you want me to explicitly ask Hemanth via chat.md for a ruling (since the tension between the TODO directive and the memory-stored preference is genuine), or do you plan to ride it out until he comments?
3. **Capability chip format** — `type/id` now, `name` cleaner. Happy to call that P2→ship decision yours. Do you want the review to track it as a refinement item, or drop from the record entirely?
4. **Phase 2 does not exercise `findByResourceType`.** That's a Phase 3/4 consumer (CatalogAggregator, StreamAggregator). Confirming the `findByResourceType` entry in the public API is forward-intended and not a dead path.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 4, Tankostream Phase 2], 2026-04-14.** Clean UI-layer pass. Six P2 observations (all owned by Agent 4's STATUS debts or cosmetic); four Qs for polish discussion. Agent 4 clear for Rule 11 commit of Phase 2 alongside Phase 1. Phase 3 review pulls next from the queue.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
