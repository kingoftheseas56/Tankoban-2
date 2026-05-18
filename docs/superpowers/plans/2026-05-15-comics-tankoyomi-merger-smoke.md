# COMICS_TANKOYOMI_STREAM_MERGER Smoke-Check Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to walk this plan in-session task-by-task. This is a smoke-test plan, not a code-implementation plan — each task is "act + verify + capture evidence", and the "failing test" / "implementation" / "passing test" TDD shape of writing-plans doesn't apply. Per-step checkbox (`- [ ]`) syntax still used for tracking.

**Goal:** Exhaustively smoke-check every feature shipped by the COMICS_TANKOYOMI_STREAM_MERGER 9-phase arc + the post-audit Trigger D Tier 1/2/3 fixes against the brainstorm + plan + audit specs. Compile every observed regression, glitch, or vision-misalignment into a structured findings md so the next wake can patch from a single document.

**Architecture:** Three-channel MCP smoke harness — `out/tankoctl.exe` for app-state queries (fast, structural), `pywinauto-mcp` for UIA invocation (clicks, key sends, focus), `windows-mcp` for visual confirmation (Screenshot, Shortcut). Dual evidence per finding: structural state (UIA tree / tankoctl get-state) + visual proof (screenshot). Backend assertions sourced from `out/*.log` tails. Bugs land in a new findings md keyed by phase/feature, prioritized P0/P1/P2 by user-visibility impact.

**Tech Stack:** `out/tankoctl.exe` (dev bridge, named pipe TankobanDevControl), `mcp__pywinauto-mcp__*` (UIA), `mcp__windows-mcp__*` (visual + keyboard), Bash for filesystem + log inspection, Edit for findings md authorship.

---

## Pre-Flight: Environment + Lock + Rules

### Task 0: Pre-flight environment check

**Files:**
- Read: `out/tankoctl.exe` (verify exists post-build)
- Read: `agents/chat.md` (claim MCP LOCK before any desktop action)
- Create: `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md` (will be authored progressively)

- [ ] **Step 1: Verify MCP servers are connected**

Check `pywinauto-mcp` and `windows-mcp` are both online by running a no-op tool call against each. If `pywinauto-mcp` is in "connecting" state, wait up to 60s; if still down after that, plan continues with `windows-mcp` + `tankoctl` only and the findings md flags any test that needed UIA-specific invocation as "deferred — UIA unavailable".

- [ ] **Step 2: Verify no other agent is on the desktop (Rule 19 LANE LOCK)**

Read tail of `agents/chat.md` for any active `MCP LOCK` claim by another agent. If clear, append:

```
MCP LOCK — Agent 1, COMICS_TANKOYOMI_STREAM_MERGER smoke matrix walkthrough, 2026-05-15 ~HH:MMam/pm. Driving desktop via tankoctl + pywinauto-mcp + windows-mcp. Expected duration ~30-60 min. Will append MCP LOCK RELEASED at close.
```

- [ ] **Step 3: Verify clean Tankoban state**

Run `taskkill //F //IM Tankoban.exe 2>&1`. Ignore "not running" errors. Confirm no stale instance.
Run `taskkill //F //IM ffmpeg_sidecar.exe 2>&1` defensively.

- [ ] **Step 4: Author findings-md skeleton**

Create `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md` with the structure below. Fill it progressively as findings emerge. Empty sections under each phase mean "no findings observed" at close.

Structure:
```markdown
# COMICS_TANKOYOMI_STREAM_MERGER Smoke Findings — 2026-05-15

By Agent 1 (Claude), smoke walkthrough, 2026-05-15.

Scope: post-implementation smoke of the 9-phase arc + Trigger D audit fixes
against the brainstorm (§1-§21) and plan (Tasks 1-54) specs.

Drives: `out/tankoctl.exe` + pywinauto-mcp UIA + windows-mcp visual.
Logs sourced from `out/*.log`.

## Executive Summary

[Count + severity breakdown filled at close]

## Findings

### Phase 1 — Data contracts + source registry
### Phase 2 — Library store + provenance
### Phase 3 — Search takeover
### Phase 4 — Detail hero + Add/Remove
### Phase 5 — Chapter rows + downloader + tile chips
### Phase 6 — Provenance edge handling
### Phase 7 — Root-change fallback
### Phase 8 — Old Tankoyomi surface removed
### Phase 9 — Polish + nav + final smoke
### Trigger D — Post-audit Tier 1/2/3
### Vision-alignment + cross-cutting

[Per finding template — see Task 1 below for the shape]

## Smoke Coverage Report

[Tick-list of every test in this plan + pass/fail/skip]

## Open Questions for Hemanth

## Closing Posture
```

---

## Build + Launch

### Task 1: Build + launch via build_and_run.bat

**Files:**
- Run: `build_and_run.bat` (repo root) — builds Release + launches Tankoban with dev-control flag
- Inspect: `out/tankoctl.exe` (must exist post-build)
- Inspect: `out/*.log` (live tail during launch)

- [ ] **Step 1: Run `build_and_run.bat`**

`cmd.exe //C ".\build_and_run.bat"`

Run in background. Wait for one of: Tankoban window appearing (visible via `mcp__pywinauto-mcp__automation_windows` list), `out/tankoctl.exe ping` succeeding, or 20-minute timeout.

If BUILD FAILED, capture the last 40 lines of build output into the findings md as a P0 ("smoke blocked — build broken") and abort the rest of the plan. Restart from Task 1 only if the build is fixed.

- [ ] **Step 2: Verify dev bridge is reachable**

`out\tankoctl.exe ping`

Expected: JSON reply with `{ "schema": "tankoban.dev.v1", ... }` in <200ms. If exit=2 ("cannot connect"), the dev-control bridge isn't bound — either Tankoban didn't launch or it launched without `--dev-control`. Re-launch via `build_and_run.bat` (the batch file sets `--dev-control` automatically). Findings: P0 "dev bridge unreachable, smoke harness blocked".

- [ ] **Step 3: Verify first-boot one-time backup migration**

Per Phase 8 Task 47, on first launch after the merger ships, `runOneTimeTankoyomiBackup` should:
- Create `<dataDir>/.comics_merger_migration_done` marker
- If `manga_downloads.json` existed pre-merger: copy to `manga_downloads.json.pre-merger-backup`, remove original
- Same for `manga_history.json`

Find dataDir via tankoctl get-state (look for any dataDir field) OR via `%APPDATA%\Tankoban\` Windows default. List directory contents:
```bash
ls -la "${APPDATA}/Tankoban/" 2>&1 | grep -E "comics_merger|manga_downloads|manga_history"
```

Expected (if pre-merger state existed): marker file present, .pre-merger-backup files present, originals gone.
Expected (clean install, no pre-merger state): marker file present, no backup files (nothing was there to back up), originals not present.

If marker is absent AND originals still present → P0 "migration didn't run".
If marker present AND originals also present → P1 "migration ran but original-removal failed (post-Critical-fix variant of data-loss bug)".
If backup files exist but marker is absent → P0 "migration crashed between copy and marker-write".

- [ ] **Step 4: Snapshot launch state via tankoctl get-state**

`out\tankoctl.exe get-state`

Capture the JSON reply. Verify:
- `activePageId` is one of the valid page IDs
- `navButtons` list includes "comics", "books", "videos", "stream", "tankorent", "tankolibrary" — and CRITICALLY does NOT include "tankoyomi" (Phase 8 Task 46 dropped it from the sidebar)

If "tankoyomi" appears in navButtons → P0 "Phase 8 Task 46 sidebar removal incomplete".

- [ ] **Step 5: Open Comics page via dev bridge**

`out\tankoctl.exe open-page comics`

Expected: reply confirms activePageId=comics. Take screenshot via `mcp__windows-mcp__Screenshot` to confirm Comics tab is visually selected and the library grid renders.

---

## Section A: Vision-Alignment Smokes (brainstorm §1-§10)

### Task 2: Sidebar has no Tankoyomi entry

**Files:** none (verification only)

- [ ] **Step 1: UIA dump the SidebarDrawer**

If pywinauto-mcp is online: `mcp__pywinauto-mcp__automation_elements` with a filter for SidebarDrawer's children. Look for any element whose Name or AutomationId contains "tankoyomi" (case-insensitive).

If pywinauto-mcp is offline: fall back to `out\tankoctl.exe get-state` and confirm navButtons doesn't contain "tankoyomi" (already done in Task 1 Step 4 — re-verify here).

Expected: zero Tankoyomi-related elements. Sidebar shows Tankorent + TankoLibrary as the only Sources entries.

- [ ] **Step 2: Screenshot the sidebar**

`mcp__windows-mcp__Screenshot` — full Tankoban window. Crop or visually inspect the left-edge sidebar drawer.

Expected: visible items Comics / Books / Videos / Stream / Tankorent / TankoLibrary. No "Tankoyomi" item.

If a Tankoyomi label or icon is visible → P0 "Phase 8 sidebar removal didn't update the rendered UI".

### Task 3: Comics library — folder tiles + Continue strip render

**Files:** none

- [ ] **Step 1: tankoctl get-videos comics** — wait, that's videos-specific. Use a UIA query against the Comics page's tile strip instead.

`mcp__pywinauto-mcp__automation_elements` filter for elements under the Comics page tile container. Capture the count + first few tile titles.

- [ ] **Step 2: Screenshot Comics page**

`mcp__windows-mcp__Screenshot`.

Expected: visible library tiles (whatever existed pre-arc), no placeholder/shimmer states. If a Continue Reading strip is shown at the top, verify it renders without errors.

If the Comics page is blank, error-overlayed, or shows "loading..." indefinitely → P0 "Comics page broken at landing".

- [ ] **Step 3: Inspect Continue strip provenance (audit P1-2)**

For each Continue-strip tile visible in the screenshot, check whether any has a "Tankoyomi" chip badge. The audit fix (Tier 1) sets provenance via `getByCanonicalPath` lookup in `refreshContinueStrip`.

This step DEPENDS on a Tankoyomi-origin series existing in the user's library AND having recent read progress. If the user's data set doesn't include such a series, this test is deferred to after Task 6 (Add to library) when we manually create one.

Expected (if applicable): Tankoyomi-origin Continue-strip items have the "Tankoyomi" chip top-left.

If a Tankoyomi-origin item is missing the chip → P1 "Continue-strip provenance regression (audit P1-2 didn't ship)".

---

## Section B: 9-Step Smoke Matrix (plan line 3190)

### Task 4: Step 1 — Open Comics, library + Continue strip baseline

Already covered by Task 3. Mark it ✅ in the smoke coverage report if Task 3 was clean.

### Task 5: Step 2 — Search-bar takeover

**Files:** none (UI verification)

- [ ] **Step 1: Locate + click the search bar**

`mcp__pywinauto-mcp__automation_elements` filter for `QLineEdit` under the Comics page (likely AutomationId `comicsSearchBar` or similar — look up exact ID). Click it.

- [ ] **Step 2: Verify placeholder text**

Read the placeholder text via UIA. Expected: `"Search Tankoyomi"`.

If placeholder is unchanged from pre-arc (e.g., `"Search series and volumes"`) → P0 "Phase 3 Task 19 search-bar repurpose didn't ship".

- [ ] **Step 3: Type "berserk" and press Enter**

`mcp__pywinauto-mcp__automation_keyboard` — type the string. Send Enter via `mcp__windows-mcp__Shortcut` (key=Return).

- [ ] **Step 4: Verify mode flip**

Wait ~3 seconds for network responses (scrapers fan out to WeebCentral + ReadComicsOnline).

`out\tankoctl.exe get-state` — confirm the active widget changed (m_stack moved from library/grid to searchTakeover index 2). Tank state might not expose the inner stack index; fall back to UIA + screenshot.

`mcp__windows-mcp__Screenshot` — confirm:
- Library grid is hidden
- Two-section result strip visible (a "Manga" section + a "Comics" section, per `MangaResult::type` split)
- Each section has up to `kInitialCap=6` tiles + a "Show more" button if more results
- A status line shows e.g. "Searching..." then settles to "N results"

If only one section renders OR results aren't split → P1 "search takeover two-section split regression".

If status stays at "Searching..." indefinitely → P1 "scraper fan-out signal wire broken (Phase 3 disconnect/reconnect race?)".

If search bar takeover doesn't fire at all (library grid stays visible) → P0 "Phase 3 search transition broken".

- [ ] **Step 5: Verify Back arrow on the search takeover**

Look for a Back button in the search-takeover header. Click it.

Expected: returns to library mode (m_stack back to index 0). UIA confirms library grid is visible again. Screenshot confirms.

If Back doesn't work → P1 "search takeover Back path broken".

### Task 6: Step 3 — Click a result tile → detail view (P1-1 cover, P1-3 row open prep)

**Files:** none

- [ ] **Step 1: Re-enter search mode with "berserk"** (per Task 5 if needed)

- [ ] **Step 2: Click the first Manga-section result tile**

UIA: find the first tile in the Manga section. Click via `mcp__pywinauto-mcp__automation_mouse` or pywinauto-mcp invoke.

- [ ] **Step 3: Verify detail view opens**

`out\tankoctl.exe get-state` — should show the detail view active (m_stack index 3).

Screenshot. Expected hero layout:
- Cover image (180×270 left-edge)
- Title (large heading)
- Meta line (status, year, etc.)
- Synopsis (multi-line, word-wrapped)
- Genres line
- "Add to library" button (top-right, since this series isn't in the library yet)
- "← Back" button (top-left)
- Below the hero: "CHAPTERS" header + Range + N-selected buttons + chapter table

**P1-1 audit fix verification:** the cover label MUST NOT be blank. The audit's Tier 2 fix wired `setCoverFromPath` + `loadCoverFromUrl` so cover loads either from cache or from preview.thumbnailUrl.

If cover is blank for >5 seconds → P1 "cover loading regression (Tier 2 P1-1 fix didn't ship)". Capture screenshot.

If meta/synopsis/genres are also blank → wait another 5s (network detail-fetch might be in flight). If still blank after 15s → P1 "detailReady wire broken".

- [ ] **Step 4: Verify chapter table populates**

After detailReady, the chapter table should populate with rows. Each row has 4 columns: checkbox, indicator, chapter name, date.

Indicator state for not-yet-downloaded chapters = NotDownloaded (visually a download-arrow icon).

If chapter table stays empty after 30s → P1 "chaptersReady wire broken or no chapters returned".

### Task 7: Step 4 — Add to library → tile with Tankoyomi chip

**Files:** none

- [ ] **Step 1: Click "Add to library" button**

UIA find by AutomationId `AddRemoveLibraryBtn`. Click via pywinauto-mcp.

- [ ] **Step 2: Verify Toast + button text morph**

Wait ~1 second. Screenshot.

Expected: Toast saying "Added <Title> to your library" near the bottom of the window. Button text changes to "Remove from library".

If Toast doesn't fire OR fires but says something else → P1 "P1-5 audit fix regression — toast should only fire on real success".

If button doesn't morph → P1 "Add path's isInLibrary() not flipping post-add".

- [ ] **Step 3: Click Back, return to library**

UIA click the Back button. Screenshot the library grid.

Expected: a new tile appears in the library with:
- The series title
- The cover (per P1-1 audit fix, persisted to `ComicsLibraryRecord::coverPath`)
- A "Tankoyomi" chip top-left
- No "DOWNLOADING" chip (nothing's downloading yet)

If new tile is missing → P0 "library record not surfacing in rebuildTiles".

If cover is blank on the tile → P1 "ComicsLibraryRecord::coverPath persistence regression (audit P1-1 Tier 2)".

If Tankoyomi chip is missing → P0 "TileCard::setProvenance painter regression (Phase 5 Task 33)".

- [ ] **Step 4: Verify the on-disk library record**

Inspect `<dataDir>/comics_library.json`:
```bash
cat "${APPDATA}/Tankoban/comics_library.json"
```

Expected: JSON has `schemaVersion: 1` and a `records` array containing the just-added series. The record should have:
- sourceId (e.g., "weebcentral")
- seriesId
- title
- origin: "tankoyomi"
- rootFolder, seriesFolderName, canonicalSeriesPath all populated
- coverPath populated (per P1-1)
- addedAt (qint64 ms-epoch)

If file missing → P0 "ComicsTankoyomiLibrary persistence broken".
If schemaVersion missing or different → P1 "schema-version invariant regression".

- [ ] **Step 5: Verify the sidecar**

`canonicalSeriesPath/.tankoyomi-meta.json` should exist with sourceId/seriesId/title/createdAt/schemaVersion fields.

```bash
cat "<canonicalSeriesPath>/.tankoyomi-meta.json"
```

If missing → P1 "SidecarMeta write didn't fire on Add (Phase 4 Add path)".

### Task 8: Step 5 — Open the new tile, download a chapter

**Files:** none

- [ ] **Step 1: Click the new Tankoyomi-origin tile in the library**

UIA: find by tile title. Click. (This is the critical P0-1 audit fix verification — the click must route to the new ComicsTankoyomiDetailView, NOT the old SeriesView.)

- [ ] **Step 2: Verify routing — detail view, not SeriesView**

Screenshot. Compare against Task 6's detail view layout (cover/title/meta/synopsis/chapters table).

Expected: the SAME detail view layout from Task 6 appears.

If SeriesView (the folder-imported UI, different layout) appears instead → P0 "audit P0-1 fix regression — Tankoyomi tile bypassed the centralized opener".

- [ ] **Step 3: Click the download arrow on the first chapter**

UIA find the indicator widget in row 0, column 1. Click.

- [ ] **Step 4: Verify state transitions**

Wait ~1 second. Screenshot.

Expected:
- Indicator state changes from NotDownloaded → Queued → Downloading
- The tile in the BACKGROUND library (visible if you Back out, but for now just trust the chip) should gain a "DOWNLOADING" chip

Use `mcp__pywinauto-mcp__automation_elements` to introspect the indicator's state property if exposed. Otherwise trust visual.

If indicator stays NotDownloaded → P0 "downloader.startDownload wire broken".
If indicator goes to Errored immediately → P1 "scraper.fetchPages failure or network error" — capture log.

- [ ] **Step 5: Wait for chapter completion + verify P0-2 audit fix**

Wait up to 60 seconds for the chapter to finish downloading (small chapters complete in 5-30s).

When complete:
- Indicator should flip to Downloaded (filled-in icon)
- `<dataDir>/manga_downloads_index.json` should now contain an entry for this chapter (per audit P0-2 fix — `chapterCompleted` signal → `registerChapter`)

```bash
cat "${APPDATA}/Tankoban/manga_downloads_index.json"
```

Expected: entries array includes `(sourceId, seriesId, chapterId, canonicalPath, addedAt, fileSizeBytes)` for the just-downloaded chapter.

If indicator stays Downloading after 60s → P1 "download stuck — check logs".
If indicator flips to Downloaded but index.json has no new entry → P0 "audit P0-2 fix regression — chapterCompleted signal not firing OR not wiring to registerChapter".

- [ ] **Step 6: Verify the library tile DOWNLOADING chip cleared post-completion**

Back out to library. Screenshot.

Expected: the tile no longer has the DOWNLOADING chip.

If chip persists → P1 "refreshTileChips not triggered on downloadCompleted".

### Task 9: Step 5b — Verify open-already-downloaded-chapter (audit P1-3)

**Files:** none

- [ ] **Step 1: Re-enter the detail view for the just-downloaded series**

Click the tile.

- [ ] **Step 2: Click the indicator on the just-downloaded chapter**

Expected: ComicReader opens the chapter for offline reading.

This is the audit P1-3 fix — onChapterRowClicked/onIndicatorClicked check `m_downloadIndex->filePathFor` and emit `openComicRequested` on hit.

Screenshot to confirm ComicReader window opens with the chapter's first page.

If the click just queues a re-download → P1 "audit P1-3 fix regression — already-downloaded branch missing".
If the click does nothing → P1 "openComicRequested signal not connected".

- [ ] **Step 3: Close the reader, return to detail view**

Send Escape or close button.

### Task 10: Step 6 — Right-click series header → context menu

**Files:** none

- [ ] **Step 1: Right-click the hero region of the detail view**

UIA find the hero widget by AutomationId `ComicsDetailHero`. Right-click via `mcp__pywinauto-mcp__automation_mouse`.

- [ ] **Step 2: Verify context menu appears**

Screenshot. Expected menu items (from Phase 5 Task 37):
- Pause
- Resume
- Restart
- Retry
- Cancel
- Remove (in danger style — typically red text)

If menu doesn't appear → P1 "context menu wire broken".
If menu items are wrong (different actions / missing items / extra items) → P1 "context menu spec drift".

- [ ] **Step 3: Click "Pause" while a download is active**

(Optionally start another chapter download first via Task 8 Steps 3-4, then come back here while it's still in flight.)

Expected: the active chapter's indicator state freezes (Downloading → Queued? Or stays at Downloading frozen? — verify against actual MangaDownloader::pauseSeries semantics).

Capture the indicator state before and after the Pause click. Note any divergence from expected pause semantics in findings.

- [ ] **Step 4: Click outside the menu to dismiss**

UIA click somewhere neutral. Menu should disappear.

### Task 11: Step 7 — Remove confirmation dialog (Phase 6 + audit polish)

**Files:** none

- [ ] **Step 1: Click "Remove from library" on the detail view**

UIA find AddRemoveLibraryBtn. Click.

- [ ] **Step 2: Verify three-button QMessageBox appears**

Screenshot. Expected modal with:
- Title: "Remove from library"
- Body: "Remove <Title> from your library?"
- Buttons: "Remove (keep files)" (AcceptRole, DEFAULT focus per Phase 6 polish), "Remove and delete files" (DestructiveRole), "Cancel"

If dialog doesn't appear → P0 "Phase 6 Task 41 Remove confirmation didn't ship".
If only one button (the old silent-delete) → P0 "Phase 6 Task 41 regression".
If default button is the delete button (Enter would delete files) → P0 "Phase 6 polish Critical fix regression — destructive-Enter footgun".

- [ ] **Step 3: Press Escape to cancel**

`mcp__windows-mcp__Shortcut` Escape key. Dialog should dismiss. The series should still be in the library.

If dialog stays or series is removed → P1 "Cancel/Escape path broken".

- [ ] **Step 4: Re-trigger the dialog, click "Remove (keep files)"**

Click Remove from library again. In the dialog, click the keep-files button.

Expected:
- The library record is removed (verify via `cat comics_library.json` — no entry for this series anymore)
- The sidecar `.tankoyomi-meta.json` is removed (verify via filesystem check on canonicalSeriesPath)
- The series folder remains on disk (the user kept files)
- The downloaded chapter file remains on disk
- The `manga_downloads_index.json` evicted the entries (per audit P0-2 wiring — `evictBySeries`)
- The tile disappears from the library

If folder was deleted on keep-files → P0 "keep-files path destructively deleted (CRITICAL data-loss bug)".
If folder remains but record + sidecar persist → P1 "Remove path failed to drop record/sidecar".
If index entries persist → P1 "evictBySeries didn't fire from Remove path".

- [ ] **Step 5: Re-add the series for the next test**

Re-enter search, find the same title, click Add to library (per Task 7). This restores state for Tasks 12+.

### Task 12: Step 8 — Source-failure toast

**Files:** none (UI verification + network manipulation)

- [ ] **Step 1: Disable network**

Use `mcp__windows-mcp__PowerShell` to disable the active network adapter:
```powershell
Get-NetAdapter | Where-Object Status -eq 'Up' | Disable-NetAdapter -Confirm:$false
```

Wait 5 seconds for network stack to settle.

- [ ] **Step 2: Trigger a fresh search**

Re-enter the search bar, type a NEW query (e.g., "naruto"), press Enter.

- [ ] **Step 3: Verify per-source error toasts**

Wait ~10 seconds for scrapers to time out.

Expected: Toast(s) fire on the window saying "WeebCentral didn't respond" and "ReadComicsOnline didn't respond" (one per failing scraper, per Phase 3 Task 20).

Screenshot to capture. If only one toast fires or no toasts → P1 "Phase 3 source-failure toast wire broken".

- [ ] **Step 4: Re-enable network**

```powershell
Get-NetAdapter | Where-Object Status -eq 'Disabled' | Enable-NetAdapter -Confirm:$false
```

Wait 10 seconds for connectivity to restore. Verify a test ping works (`ping -n 1 8.8.8.8`).

### Task 13: Step 8b — Offline detail-view banner + disabled indicators (audit P1-4)

**Files:** none

- [ ] **Step 1: Disable network again**

Same as Task 12 Step 1.

- [ ] **Step 2: Click into an in-library Tankoyomi-origin series**

Open the detail view for the series we re-added in Task 11 Step 5.

- [ ] **Step 3: Wait for source error**

The detail view should attempt fetchDetail / scraper.search → fails → `onSourceError` fires.

- [ ] **Step 4: Verify offline banner + disabled indicators (audit P1-4)**

Screenshot. Expected:
- Banner above the chapter table: "Couldn't refresh chapter list from Tankoyomi — showing cached state." (or similar wording from `m_offlineBanner`)
- Chapter rows where the chapter is NOT yet downloaded: indicator is disabled (greyed out) and has a tooltip when hovered (e.g., "Connect to the internet to download this chapter")
- Chapter rows where the chapter IS downloaded: indicator stays enabled (readable offline)

If banner shows but indicators are not disabled → P1 "audit P1-4 fix regression — m_sourceOffline state machine didn't propagate".
If banner doesn't show at all → P1 "offline-banner wire broken".

- [ ] **Step 5: Click a downloaded chapter while offline**

Expected: ComicReader opens (per P1-3 fix — already-downloaded chapters open even while offline).

If click is rejected or shows error → P1 "audit P1-3 fix didn't preserve offline-readable contract".

- [ ] **Step 6: Re-enable network**

Per Task 12 Step 4.

### Task 14: Step 9 — Manual file deletion → indicators revert

**Files:** none

- [ ] **Step 1: Identify the downloaded chapter's file path**

From Task 8 Step 5's `manga_downloads_index.json` capture, note the `canonicalPath` of the downloaded chapter.

- [ ] **Step 2: Delete the chapter file via PowerShell**

```powershell
Remove-Item "<canonicalPath>" -Force
```

- [ ] **Step 3: Reopen the detail view (or trigger showEvent)**

Back out to library, click the tile again. The detail view's showEvent should call `MangaDownloadIndex::validateAll` (per Phase 5 Task 30 + Phase 7 P7 wiring) which re-checks every entry's on-disk presence.

- [ ] **Step 4: Verify indicator reverts to NotDownloaded**

Screenshot. The previously-downloaded chapter's indicator should now show NotDownloaded (download-arrow icon).

If indicator stays Downloaded → P1 "validateAll didn't fire OR didn't remove the entry".

- [ ] **Step 5: Verify index.json no longer contains that entry**

```bash
cat "${APPDATA}/Tankoban/manga_downloads_index.json"
```

Expected: the entry for the deleted chapter is gone.

If entry persists → P1 "validateAll didn't evict missing files".

- [ ] **Step 6: Verify the library record + tile persist**

The series should still be in the library (record persists). The tile should still render with Tankoyomi chip (no DOWNLOADING chip since no active download).

If the entire series disappeared → P0 "validateAll over-eager — removed the library record on a chapter-file delete".

---

## Section C: Audit-Driven Extra Checks (Trigger D Tier 1 + 2 + 3)

### Task 15: Centralized opener routes ALL 5 sites (audit P0-1 + P1-8)

**Files:** none

- [ ] **Step 1: List-view activation routing**

Switch the Comics page to list view (look for a toggle button or shortcut — Ctrl+L is the typical shortcut; check existing keybindings).

Find a Tankoyomi-origin series in the list. Press Enter to activate (per `LibraryListView::itemActivated`).

Expected: routes to ComicsTankoyomiDetailView (the Stream-style hero), NOT to SeriesView.

If routes to SeriesView → P0 "audit P0-1 fix incomplete — list-view bypass remains".

- [ ] **Step 2: Tile double-click routing**

Switch back to grid view. Double-click a Tankoyomi-origin tile.

Expected: routes to ComicsTankoyomiDetailView.

If routes to SeriesView → P0 "audit P1-8 fix incomplete — double-click bypass remains".

- [ ] **Step 3: Tile context menu "Open" routing**

Right-click a Tankoyomi-origin tile. Look for an "Open" action in the menu. Click it.

Expected: routes to ComicsTankoyomiDetailView.

If "Open" doesn't appear → P2 "context-menu Open action missing" (might not be implemented at all).
If routes to SeriesView → P1 "audit P1-8 fix incomplete — tile-context bypass remains".

- [ ] **Step 4: Multi-select "Open first selected" routing**

Select a Tankoyomi-origin tile via shift-click or ctrl-click. Right-click → "Open first selected" (or similar wording).

Expected: routes to ComicsTankoyomiDetailView.

If routes to SeriesView → P1 "audit P1-8 fix incomplete — multi-select bypass remains".

- [ ] **Step 5: Folder-origin tile (negative test)**

Click a folder-imported (non-Tankoyomi) series tile.

Expected: routes to the OLD SeriesView (the folder-imported UI), since `originHint` is "folder" not "tankoyomi".

If routes to ComicsTankoyomiDetailView → P0 "centralized opener over-eager — folder tiles wrongly routed to Tankoyomi UI".

### Task 16: Collision disambig (audit P1-6)

**Files:** none

- [ ] **Step 1: Add a series via search**

Re-enter search, find "Berserk" on WeebCentral, Add to library. Note the canonicalSeriesPath in `comics_library.json` — should be `<root>/Berserk`.

- [ ] **Step 2: Search the same title from a different source**

Re-enter search. If the same title "Berserk" exists on ReadComicsOnline, click that result. Click Add to library.

Expected: the disambig kicks in. The second record's canonicalSeriesPath should be `<root>/Berserk (ReadComicsOnline)` (audit P1-6 fix — `uniqueSeriesFolderName` appends source name on collision).

Verify:
```bash
cat "${APPDATA}/Tankoban/comics_library.json"
```

Expected: two records exist, with DIFFERENT canonicalSeriesPath values.

If second record overwrites the first → P0 "audit P1-6 fix regression — m_canonicalToKey collision".
If both records share the same canonicalSeriesPath → P0 "disambig didn't fire".

If the same title doesn't exist on both sources, this test is deferred. Note in findings.

### Task 17: Search stale-result race fix (audit P1-7)

**Files:** none

- [ ] **Step 1: Type a slow query, then immediately a fast query**

Type "one piece" (likely many results, slow), press Enter. Wait 0.5-1 second. Without waiting for results to populate, type "naruto" (clear bar first), press Enter.

- [ ] **Step 2: Wait for results to settle (~10s)**

- [ ] **Step 3: Verify only "naruto" results render**

Screenshot. Inspect the two result strips. Every visible tile should be a "naruto"-related series.

If any tile is a "one piece" result (the prior query) → P1 "audit P1-7 fix regression — m_searchGeneration not gating late arrivals".

### Task 18: Continue-strip "Open series" routing (Tier 1 minor open thread)

**Files:** none

- [ ] **Step 1: If Continue strip has a Tankoyomi-origin item**

This depends on the user having reading progress on a Tankoyomi-origin series. If not present, skip + note.

Right-click the Continue-strip tile. Look for an "Open series" or "Open library entry" action. Click.

Expected: routes through the centralized opener to ComicsTankoyomiDetailView.

If routes to ComicReader directly (skipping series page) → potentially intentional (Continue strip's primary action is "resume reading"); check whether this is a regression or by-design.

Note finding as P2 "Continue-strip Open-series routing — clarify intent" if ambiguous.

### Task 19: Nav-state Back routing (Phase 9 Task 52)

**Files:** none

- [ ] **Step 1: Library → Search → Library**

From library, click search bar, type "berserk", Enter. Back arrow on search takeover.

Expected: lands on library mode (m_mode = Library, m_stack index 0).

- [ ] **Step 2: Library → Search → Detail (from search) → Back → Search**

From library: search "berserk". From search: click a result. From detail: Back.

Expected: lands on SEARCH mode (m_stack index 2 with results still rendered), NOT library.

If lands on library → P0 "Phase 9 Task 52 Back routing regression — m_enteredDetailFrom not tracked through search-origin".

- [ ] **Step 3: Library → Detail (from tile) → Back → Library**

From library: click a Tankoyomi-origin tile. From detail: Back.

Expected: lands on library mode.

If lands on search → P0 "Phase 9 Task 52 Back routing regression — Library origin mis-tagged".

- [ ] **Step 4: Global nav history Back (cross-page)**

Navigate Comics → Books page → Comics (use sidebar or NavHistory back-arrow). Verify Comics page restores its prior mode (e.g., if was in Detail mode, should restore Detail; if was in Library, should restore Library).

This exercises Phase 9 Task 50/51 (captureNavState / restoreNavState 3-mode discriminator).

If page restores to wrong mode → P1 "Phase 9 Task 50/51 nav-state regression".

### Task 20: Sidecar rewrite-if-missing (Phase 6 Task 39)

**Files:** none

- [ ] **Step 1: Delete the sidecar of an in-library Tankoyomi-origin series**

```bash
rm "<canonicalSeriesPath>/.tankoyomi-meta.json"
```

- [ ] **Step 2: Open the detail view for that series**

UIA: click the tile from library.

- [ ] **Step 3: Verify sidecar was regenerated**

```bash
cat "<canonicalSeriesPath>/.tankoyomi-meta.json"
```

Expected: file exists again with sourceId/seriesId/title/createdAt/schemaVersion fields populated.

If file is still missing → P1 "Phase 6 Task 39 sidecar-rewrite-if-missing regression".

### Task 21: Renamed-folder recovery (Phase 6 Task 40)

**Files:** none

- [ ] **Step 1: Rename the canonical folder of an in-library Tankoyomi-origin series**

Pick one — e.g., rename `<root>/Berserk` → `<root>/Berserk Renamed`. The sidecar moves with the folder.

```powershell
Rename-Item "<canonicalPath>" "Berserk Renamed"
```

- [ ] **Step 2: Open the detail view**

Click the tile. NOTE: the tile's stored canonicalPath is still the old name; clicking may fail to navigate. If clicking from library fails, use the search-then-Open-from-library path or trigger via the dev bridge if available.

- [ ] **Step 3: Verify the library record relocated**

```bash
cat "${APPDATA}/Tankoban/comics_library.json"
```

Expected: the record's `canonicalSeriesPath` and `seriesFolderName` updated to reflect the renamed folder (per `findAndRelocateByIdentity`).

If record still points to old path → P1 "Phase 6 Task 40 cross-root recovery regression".

### Task 22: Empty-Comics-root guard (Phase 5 polish I1)

**Files:** none

- [ ] **Step 1: Remove all Comics roots from settings**

Navigate to Settings (gear icon or hotkey). Remove every Comics root folder. (If only one is configured, remove it.) Apply.

- [ ] **Step 2: Try to add a series from search**

Re-enter search, find a series, click Add to library.

Expected: Toast says "Add a Comics folder before adding to library" (per Phase 5 review I1 polish). No library record gets created (verify with `cat comics_library.json` showing no new entry).

If Add succeeds with no root → P0 "audit Phase 5 I1 polish regression — half-populated record write".
If Toast text is different → P2 "Toast wording drift".

- [ ] **Step 3: Restore Comics root for subsequent tests**

Add back a Comics root folder. Apply.

---

## Section D: Below-The-Waterline State Checks

### Task 23: comics_library.json invariants

**Files:**
- Inspect: `${APPDATA}/Tankoban/comics_library.json`

- [ ] **Step 1: Validate JSON structure**

```bash
cat "${APPDATA}/Tankoban/comics_library.json" | python -m json.tool
```

Expected: valid JSON. Top-level keys: `schemaVersion` (= 1) and `records` (array).

- [ ] **Step 2: For each record, verify fields**

Required fields per ComicsLibraryRecord schema:
- sourceId, seriesId, title, origin (= "tankoyomi"), rootFolder, seriesFolderName, canonicalSeriesPath, coverPath (may be empty pre-Tier-2-fix but should be populated for any series added post-fix), addedAt (qint64), lastValidatedAt (qint64)

For records added BEFORE the Tier 2 P1-1 cover fix, coverPath may be empty — that's expected for legacy entries. For records added AFTER the fix, coverPath should be a real file path.

Note any records with structural problems in the findings.

### Task 24: manga_downloads_index.json invariants

**Files:**
- Inspect: `${APPDATA}/Tankoban/manga_downloads_index.json`

- [ ] **Step 1: Validate JSON structure**

```bash
cat "${APPDATA}/Tankoban/manga_downloads_index.json" | python -m json.tool
```

Expected: valid JSON. Top-level keys: `schemaVersion` (= 1) and `entries` (array).

- [ ] **Step 2: For each entry, verify fields**

Required: sourceId, seriesId, chapterId, canonicalPath, addedAt, fileSizeBytes.

Verify: every entry's `canonicalPath` actually exists on disk.

```bash
# For each canonicalPath: ls -la "<path>" — should succeed
```

If an entry's canonicalPath doesn't exist on disk → `validateAll` should have evicted it. If it persists, P1 "validateAll incomplete".

### Task 25: Sidecar invariants across library

**Files:** none

- [ ] **Step 1: For each record's canonicalSeriesPath, verify .tankoyomi-meta.json exists and matches**

For each record in `comics_library.json`:
```bash
cat "<canonicalSeriesPath>/.tankoyomi-meta.json"
```

Verify: sidecar's sourceId, seriesId, title match the record. schemaVersion = 1. createdAt populated.

If sidecar missing for any in-library record → P2 "Phase 6 Task 39 didn't catch this case" (could be a pre-Phase-6 race) — log finding.
If sidecar contents mismatch record → P1 "sidecar drift" — likely a write-race we missed.

### Task 26: Thread-safety smoke (audit P2-3)

**Files:** none

- [ ] **Step 1: Trigger rapid Add then Remove**

This is hard to smoke via UI alone — would need rapid programmatic adds. Defer to log inspection.

- [ ] **Step 2: Inspect logs for mutex contention or deadlock warnings**

```bash
grep -i "mutex\|deadlock\|lock contention\|race" out/*.log
```

Note any findings.

---

## Section E: Output md Compilation + Close

### Task 27: Compile findings md final pass

**Files:**
- Edit: `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md`

- [ ] **Step 1: Walk every Task 2-26 outcome**

For each Task, mark in the Smoke Coverage Report section as:
- ✅ PASS (matched expected)
- ❌ FAIL (deviation observed, finding added)
- ⏭️ SKIPPED (precondition not met, e.g., no Tankoyomi-origin series in user data)
- ⚠️ PARTIAL (some steps passed, some failed)

- [ ] **Step 2: Per finding, fill the template**

For each ❌ FAIL or ⚠️ PARTIAL outcome, add a structured entry:
```markdown
#### [Phase X — Feature Name] [Pn-m] One-line title

- **Source of truth:** brainstorm §X / plan line Y / audit file:line / phase RTC at chat.md:Z
- **Observed:** what the smoke saw (with screenshot path / log tail / state dump)
- **Expected:** what the spec / audit said should happen
- **Severity:** P0 (ship blocker / data loss / vision misalignment) / P1 (user-visible regression) / P2 (polish)
- **Evidence:**
  - Screenshot: `agents/audits/smoke_evidence/<timestamp>_<test-id>.png`
  - Log tail: `agents/audits/smoke_evidence/<timestamp>_<test-id>.log`
  - State dump: paste tankoctl get-state JSON
- **Hypothesis:** likely root cause (file:line if you can pin it; otherwise "investigate")
```

- [ ] **Step 3: Author Executive Summary**

At the top of the md:
- Count of findings by severity (P0 / P1 / P2 totals)
- 3-5 most load-bearing items
- Headline vision-alignment verdict ("ships as-is" / "fixes needed before user-facing" / "blockers exist")

- [ ] **Step 4: Author Closing Posture**

One paragraph: is the arc shippable post-fixes? Are any P0s genuinely v1-blockers? What's the recommended action for the next wake?

- [ ] **Step 5: Capture the findings-md path in chat.md**

Append a non-RTC NOTE line:
```
NOTE — Agent 1, smoke walkthrough complete 2026-05-15 ~HH:MMam/pm. Findings at agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md. Counts: P0=N, P1=M, P2=K. MCP LOCK RELEASED.
```

### Task 28: Close-out + MCP LOCK release

**Files:**
- Edit: `agents/chat.md` (append MCP LOCK RELEASED)

- [ ] **Step 1: taskkill Tankoban defensively per Rule 17**

```bash
taskkill //F //IM Tankoban.exe 2>&1
taskkill //F //IM ffmpeg_sidecar.exe 2>&1
```

Optionally: `powershell -NoProfile -File scripts/stop-tankoban.ps1` (handles both).

- [ ] **Step 2: Release MCP LOCK in chat.md**

Append:
```
MCP LOCK RELEASED — Agent 1, COMICS_TANKOYOMI_STREAM_MERGER smoke complete 2026-05-15 ~HH:MMam/pm.
```

- [ ] **Step 3: Report back to Hemanth**

Final reply structure:
- Headline counts (P0 / P1 / P2)
- 3-5 most load-bearing items (one-liner each)
- Path to findings md
- Recommendation: "next wake we patch" / "next wake we patch + re-smoke" / "ship as-is, P2s in a backlog sweep"

---

## Evidence-Capture Conventions

Per finding, store evidence under `agents/audits/smoke_evidence/`:

- Screenshots: `<HHMM>_task<N>_<short-name>.png` (use `mcp__windows-mcp__Screenshot` then save the bytes via PowerShell or Edit's binary path)
- Log tails: `<HHMM>_task<N>_<short-name>.log` (Bash: `tail -n 200 out/<logname>.log > agents/audits/smoke_evidence/<filename>`)
- State dumps: paste the tankoctl get-state JSON inline in the finding entry (small enough)

Don't try to screenshot every passing test — only failures + ambiguous cases. PASS tests get a one-line tick in the Smoke Coverage Report.

## MCP Tool Discipline (per CLAUDE.md + memory)

- **tankoctl FIRST** for app-state queries — `out\tankoctl.exe get-state / get-videos / get-player / logs`. 5-10× faster than UIA tree walks. Per `project_windows_mcp_live.md` + REPO_HYGIENE Phase 3.
- **pywinauto-mcp SECOND** for UIA invocation (clicks, key sends by AutomationId, structural element queries). Tankoban auto-publishes 100% AutomationId coverage via `objectName()`.
- **windows-mcp LAST RESORT** for screenshots (visual confirmation), `Shortcut` for compound keys (Enter, Escape, Ctrl+L, etc.), `PowerShell` for network manipulation + filesystem commands.
- **Pixel `Click` is the absolute last resort** — prefer dev-bridge or UIA-invoke first. Per `feedback_mcp_coord_system.md`, derive coords from pywinauto-mcp UIA tree, never visually from a downscaled Screenshot.
- **Eyes-on-screen for visual claims** — per `feedback_dev_bridge_visual_blindspot.md`, tankoctl state proves signal-emit, not paint. Combine state + screenshot for visual UI claims.
- **MCP LANE LOCK (Rule 19)** — one agent drives the desktop. Claim in chat.md before, release after.
- **Logs under `out/`** — per `feedback_logs_under_out_directory.md`. Never `cat _player_debug.txt` at repo root.

## Out of Scope (defer to dedicated arcs)

- Stream / Tankorent / TankoLibrary / Books / Videos pages — only Comics + Tankoyomi-merger surface.
- Performance profiling (refreshTileChips debounce concern is a known v2 follow-up, not v1 smoke target).
- v2-deferred features per audit Missed Opportunities (auto-copy on root change, volume/story-arc grouping, etc.).
- Multi-instance / single-instance launch behavior.
- Reader (ComicReader) internals — only smoke that it OPENS when an already-downloaded chapter is clicked.

## Spec Coverage Cross-Reference

| Spec / Audit Section | Smoke Task |
|---|---|
| brainstorm §3.1 search flow | Task 5, 12, 17 |
| brainstorm §3.2 series detail | Task 6, 7, 10 |
| brainstorm §3.3 library/downloads | Task 8, 9, 14 |
| brainstorm §3.4 Tankoyomi badge | Task 7 Step 3, Task 3 Step 3 |
| brainstorm §3.5 folder vs badged distinction | Task 15 Step 5 |
| brainstorm §3.6 sources sealed v1 | Task 5 (registry fan-out visible) |
| brainstorm §3.7 persistence + clean-slate migration | Task 1 Step 3, Task 23, Task 24 |
| brainstorm §3.8 out of scope | Task 2 (no Tankoyomi sidebar entry) |
| brainstorm §11-§16 Codex review-expand | Tasks 20, 21, 22 (provenance edges) |
| brainstorm §17 Tankoyomi-side ABSORB | Task 8 (ChapterDownloadIndicator + ChapterRangeDialog reused) |
| brainstorm §20 nav state 3-mode | Task 19 |
| Plan Task 39 (sidecar rewrite) | Task 20 |
| Plan Task 40 (cross-root recovery) | Task 21 |
| Plan Task 41 (Remove confirmation) | Task 11 |
| Plan Task 43 (root-change fallback) | Task 22 |
| Plan Task 47 (one-time backup) | Task 1 Step 3 |
| Plan Task 50-52 (nav state) | Task 19 |
| Audit P0-1 (centralized opener) | Task 8 Step 2, Task 15 |
| Audit P0-2 (chapter index producer) | Task 8 Step 5, Task 24 |
| Audit P1-1 (cover loading) | Task 6 Step 3, Task 7 Step 3 |
| Audit P1-2 (Continue-strip badge) | Task 3 Step 3 |
| Audit P1-3 (open downloaded chapter) | Task 9, Task 13 Step 5 |
| Audit P1-4 (offline indicators) | Task 13 |
| Audit P1-5 (toast guard) | Task 22 Step 2 |
| Audit P1-6 (collision disambig) | Task 16 |
| Audit P1-7 (search race) | Task 17 |
| Audit P1-8 (all open-paths centralized) | Task 15 |
| Audit P2-3 (thread-safety refactor) | Task 26 |
| Audit P2-4 (comment rot) | none (compile-time only, doesn't smoke) |

## Self-Review

**Spec coverage:** Every brainstorm §3.x section + plan Phase X + audit P-tier item maps to a smoke task above. The Cross-Reference table confirms this.

**Placeholder scan:** No "TBD" / "investigate later" / "add appropriate error handling" anywhere. Each task has concrete actions + expected results + failure modes + evidence-capture protocol.

**Type consistency:** UI element AutomationIds (`AddRemoveLibraryBtn`, `ComicsDetailHero`, `ComicsSearchShowMore`, etc.) match the actual `setObjectName` calls in the implementation (verified during the Trigger D audit). Method names (`registerChapter`, `validateAll`, `filePathFor`, `getByCanonicalPath`, `evictBySeries`, `findAndRelocateByIdentity`, `chapterCompleted`, `openComicRequested`, `setProvenance`, `setDownloadingChip`) match the code surface.

**Scope sanity:** 28 tasks, each averaging 3-6 bite-sized steps. Total expected wall-clock 45-90 minutes. PASS-heavy tasks are quick (UIA query + screenshot + ✅); FAIL tasks add evidence-capture overhead (~2 min each). If FAIL count is high, the wall-clock stretches.

If you find a Task whose precondition is impossible (e.g., Task 16's cross-source collision test requires the same title to exist on both WeebCentral AND ReadComicsOnline), mark it SKIPPED + note the precondition gap in findings. Don't fabricate a result.
