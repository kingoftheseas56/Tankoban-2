# Three Small Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship 3 small bug fixes Hemanth surfaced from prior smokes — (1) movie Download button stays hidden in some state (he saw it gated on library membership; the actual gate may be stream-load state, to be confirmed in Task 1 investigation), (2) Sources sidebar shows 4K torrents even when his Torrentio addon is configured for 1080p, (3) episode rows show "-" status after he clicks Download until he exits + reopens the detail view.

**Architecture:** Each fix is independent — single-file scope (mostly), no cross-task dependencies. Tasks 1 + 3 are UI/signal-wire fixes in `StreamDetailView`. Task 2 starts with investigation: the user-facing "1080p preference" is a Torrentio addon webpage config (not a Tankoban setting), so the bug shape is either (a) Tankoban is stripping/normalizing the addon manifest URL and losing the user's config params, or (b) the streams come from a different addon than Torrentio, or (c) it's a no-fix finding for Hemanth's addon webpage. Investigation outcome determines whether Task 2 ships code or just a finding.

**Tech Stack:** Qt6 / C++20 / CMake-Ninja-MSVC. No new dependencies. No new tests (Tankoban TDD policy: smoke-first for UI plumbing per CLAUDE.md Tier-2 skill discipline).

**Brotherhood contract notes:**
- Agents do NOT `git commit` directly. Each task ends with appending a `READY TO COMMIT - [Agent 4, ...]` line to `agents/chat.md` per Rule 11. Agent 0 batches commits via `/commit-sweep` later.
- Build verify is `build_check.bat` from repo root (compile-only, agent-safe). Expected: `BUILD OK` near the end + exit 0. On failure: 30-line cl.exe tail prints automatically.
- ASCII-only sweep on the diff is the last step before each RTC.
- No `Tankoban.exe` spawn, no MCP, no `build_and_run.bat`. Smokes are described as user-actions for Hemanth to execute, not driven from the implementer subagent.
- One-fix-per-rebuild per `feedback_one_fix_per_rebuild.md`. Each task is its own commit boundary.

---

## File Structure

**Files modified:**
- `src/ui/pages/stream/StreamDetailView.cpp` (Task 1: movie Download visibility gate; Task 3: kick episode refresh on download-dispatch)
- `src/ui/pages/stream/StreamDetailView.h` (Task 3: may declare a kick helper if logic is shared)
- Task 2: investigation only — file scope TBD by investigation findings. May touch `src/core/stream/addon/AddonRegistry.cpp` or `src/core/stream/addon/AddonTransport.cpp` if Tankoban is stripping addon URL config params. Or may produce a no-code finding for Hemanth.

Each task has independent scope. Tasks 1 + 3 are mechanical fixes once the gates are located. Task 2 is genuinely investigative.

---

## Task 1: Movie Download Button Visibility Gate

**Files:**
- Investigate + Modify: `src/ui/pages/stream/StreamDetailView.cpp`

**Background:** Hemanth's Smoke Test 3a from 2026-05-17 ~3:50pm: "Movie auto-download button NOT visible because movie wasn't in library yet (Add-to-Library gates appearance)." That was Agent 4's hypothesis at the time, NOT confirmed. The visible-on-screen gate at line 1238 (`m_movieActionRow->setVisible(isMovie)`) only checks `m_currentType == "movie"` — it does NOT check library state. So the real gate is somewhere else, likely a stream-load-state guard.

Goal: find the real gate; fix it so movies render the Download button consistently regardless of library state (movies should be downloadable BEFORE Add-to-Library, matching how series work).

- [ ] **Step 1: Locate the real gate**

Run this grep in the implementer subagent's working directory:

```
Grep pattern: m_movieDownloadBtn.*setVisible|m_movieDownloadBtn.*setEnabled|m_movieDownloadBtn->show|m_movieDownloadBtn->hide  in src/ui/pages/stream/StreamDetailView.cpp
Grep pattern: m_lastChoices|m_movieStreams|onStreamLoadComplete|onMovieStreamsLoaded  in src/ui/pages/stream/StreamDetailView.cpp
Grep pattern: m_library->has\\(m_currentImdb\\)  in src/ui/pages/stream/StreamDetailView.cpp
```

Expected: surface 1-3 candidate gate sites where `m_movieDownloadBtn` (or its enclosing `m_movieActionRow`) is toggled. Likely candidates: (a) `refreshMovieLocalChip()` around line 1231-1247, (b) some `onLoadComplete`-style slot that enables it after streams arrive, (c) some library-state-aware refresh.

- [ ] **Step 2: Read the gate site(s) end-to-end**

Read 20 lines around each candidate site. Look for any condition that ties visibility/enabled to `m_library` or `inLibrary` or similar membership state. Note the exact file:line of the offending check.

- [ ] **Step 3: Reproduce the bug intent in plain language**

In a comment block IN the implementer's RTC (not in code), write 1-2 sentences describing what the current gate does and what it should do. Example: "Currently `m_movieDownloadBtn` is hidden when X. The bug is that movies-not-in-library skip this load path, so Download never shows. Fix: decouple from library state — show button when (a) media is movie, (b) at least one stream candidate is available; library state is separate."

- [ ] **Step 4: Apply the fix**

Two likely shapes:

**Shape A — Library-state gate is real and should be removed:**
If you find a check like `if (!m_library || !m_library->has(m_currentImdb))` near the movie-action setVisible logic, remove that check. The button should be visible based on media-type + stream-load state only, NOT library membership.

**Shape B — Stream-load gate prevents pre-Add-to-Library load:**
If the streams only load for library titles, the fix is in the stream-load path (find where `m_movieStreams` or `m_lastChoices` is populated for movies — confirm it fires for non-library movies). If it doesn't fire, add a load trigger that's not library-gated.

Pick the smaller change. If Shape A, apply directly. If Shape B, prefer triggering the existing load path on showEntry for movies regardless of library state.

The actual code edit depends on what you find. The constraint: ~10-20 LOC max. If the fix balloons past 30 LOC, stop and report DONE_WITH_CONCERNS to the controller for re-scoping.

- [ ] **Step 5: Build verify**

```
build_check.bat
```

Expected: `BUILD OK`. If failure: read the 30-line cl.exe tail and diagnose.

- [ ] **Step 6: ASCII sweep on the diff**

```powershell
$diff = git diff src/ui/pages/stream/StreamDetailView.cpp 2>$null
$nonAscii = $diff | Select-String -Pattern '[^\x00-\x7F]'
if ($nonAscii) { Write-Host "FOUND NON-ASCII:"; $nonAscii | Select-Object -First 5 } else { Write-Host "ASCII CLEAN" }
```

Expected: `ASCII CLEAN`.

- [ ] **Step 7: Post RTC for Task 1**

Append this template (with the actual file:line + Shape A/B + change description filled in) to the end of `agents/chat.md`:

```
## Agent 4 - Movie Download button visibility fix - 2026-05-18

READY TO COMMIT - [Agent 4, Movie Download button visibility fix per docs/superpowers/plans/2026-05-18-three-small-fixes.md Task 1. Bug shape: <fill in from investigation>. Real gate located at src/ui/pages/stream/StreamDetailView.cpp:<line>. Fix: <Shape A or Shape B description>. ~N LOC modified. build_check.bat BUILD OK. ASCII sweep clean on diff. Smoke matrix for Hemanth: (a) open Theatre + search any movie NOT in your library (e.g. "Spider-Man" or any title you haven't added) + click into detail view -> Download button visible immediately, no Add-to-Library required; (b) click Download -> sources panel populates + auto-Download fires same as today; (c) movies already in library still work as before.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/stream/StreamDetailView.cpp, agents/chat.md
```

- [ ] **Step 8: Report DONE to controller**

Report DONE with `git diff --stat` evidence. If the investigation surfaced a different root cause than expected (e.g. the gate is library-state but in an unexpected file, or the visibility logic is more tangled than 20 LOC can fix), report DONE_WITH_CONCERNS with details + the file:line of the surprise.

---

## Task 2: 4K Quality Leak in Sources Sidebar — Investigation + Conditional Fix

**Files:**
- Investigate: `src/core/stream/addon/AddonRegistry.cpp`, `src/core/stream/addon/AddonTransport.cpp`, `src/core/stream/MetaAggregator.cpp` (whichever owns Stremio addon manifest fetching + stream queries)
- Modify (CONDITIONAL on findings): TBD — likely the addon URL pipeline if Tankoban is stripping config params

**Background:** Hemanth has Torrentio addon configured for 1080p (via Torrentio's webpage config). Tankoban's Theatre Sources sidebar still shows 4K torrents. There's no `preferredQuality` or `maxHeight` setting in Tankoban's UI (verified via grep — only QualityScorer in the legacy TorrentPackPicker uses quality scoring, and that's being deleted in Phase G). So the user's quality config is purely on the addon's side. The bug is either: (a) Tankoban strips/normalizes the addon manifest URL and loses the user's config token, (b) the streams come from a non-Torrentio addon, or (c) this is a Torrentio webpage config issue (user-fixable, not a Tankoban bug).

Goal: trace the addon-config pipeline and determine which of (a)/(b)/(c) is the real cause. Ship a fix if (a). Document and close as no-fix if (b) or (c).

- [ ] **Step 1: Grep the addon manifest URL pipeline**

```
Grep pattern: manifestUrl|addonUrl|configuredUrl|addon_config|manifest.json  in src/core/stream/addon/
Grep pattern: torrentio|Torrentio  in src/core/stream/
Grep pattern: streamsResource|addon.*streams  in src/core/stream/addon/
```

Expected: surface the addon-registry code that stores user-installed addons + the transport that fetches `streams/<type>/<id>.json` from each addon. Note any URL stripping, normalization, or config-token handling.

- [ ] **Step 2: Read the addon-fetch path end-to-end**

Read AddonRegistry.cpp + AddonTransport.cpp end-to-end (or the equivalent files surfaced in Step 1). Look for:
- Is the user's installed addon manifest URL stored AS-IS (full URL with config token) or normalized to a root?
- Is the streams endpoint called as `<manifestRoot>/stream/<type>/<id>.json` (correct) or `<manifestRoot>/stream/<type>/<id>.json` WITHOUT the config token?

Torrentio's URL pattern: `https://torrentio.strem.fun/<config-token>/manifest.json` — the config token is part of the path. If Tankoban replaces this with `https://torrentio.strem.fun/manifest.json` (no token), the user's config is lost.

- [ ] **Step 3: Check live addon storage**

If accessible, check Hemanth's current addon config — likely stored in QSettings or a JSON file under `%LOCALAPPDATA%\Tankoban\` or similar. Find the file via:

```
Grep pattern: stream/addons|addons.json|installedAddons  in src/core/stream/addon/AddonRegistry.cpp
```

Look at the on-disk format. If you can read it (e.g. `%LOCALAPPDATA%\Tankoban\addons.json`), do so and verify whether the Torrentio entry has the user's config token in its URL.

- [ ] **Step 4: Form hypothesis**

Based on Steps 1-3, the bug is one of:

**(a) URL stripping bug:** Tankoban strips the config token from the manifest URL somewhere → 1-line fix to preserve it.

**(b) Wrong addon:** Sources are coming from a different addon (e.g. Cinemeta, OpenSubs) not Torrentio → no fix needed, document.

**(c) Addon webpage config issue:** The user's Torrentio webpage hasn't been re-saved, OR Torrentio's filter doesn't apply to old cached results → user-fixable, document for Hemanth.

State which hypothesis you're going with + the evidence in the RTC.

- [ ] **Step 5: Apply fix (CONDITIONAL — only for hypothesis (a))**

If hypothesis (a): apply the smallest fix that preserves the full manifest URL through the streams-fetch call. Verify the streams endpoint URL is constructed correctly (e.g., `<manifestUrl-without-/manifest.json>/stream/<type>/<id>.json`). Build verify + ASCII sweep + RTC.

If hypothesis (b) or (c): skip Step 5. Go straight to Step 6 with a no-code RTC.

- [ ] **Step 6: Build verify + ASCII sweep (if code changed in Step 5)**

```
build_check.bat
```

Expected: `BUILD OK`.

```powershell
git diff src/core/stream/ 2>$null | Select-String -Pattern '[^\x00-\x7F]' | Select-Object -First 5
```

Expected: zero output (ASCII CLEAN).

- [ ] **Step 7: Post RTC for Task 2 (one of two templates based on outcome)**

**If hypothesis (a) and fix shipped:**
```
## Agent 4 - 4K quality leak Sources sidebar fix - 2026-05-18

READY TO COMMIT - [Agent 4, 4K quality leak Sources sidebar root-cause + fix per docs/superpowers/plans/2026-05-18-three-small-fixes.md Task 2. Root cause: <describe how Tankoban stripped/lost the Torrentio config token>. Fix: <describe the URL-preservation change>. Files: <list>. ~N LOC modified. build_check.bat BUILD OK. ASCII sweep clean. Smoke matrix for Hemanth: (a) confirm your Torrentio config (https://torrentio.strem.fun/<config-token>/manifest.json) is still in your installed addons after rebuild; (b) open any movie in Theatre that previously showed 4K torrents in Sources; (c) verify Sources now only shows torrents matching your Torrentio quality config.] | Skills invoked: [/superpowers:systematic-debugging, /superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: <list>, agents/chat.md
```

**If hypothesis (b) or (c) — no-fix finding:**
```
## Agent 4 - 4K quality leak Sources sidebar investigation - 2026-05-18

NOTE - [Agent 4, 4K quality leak Sources sidebar investigation per docs/superpowers/plans/2026-05-18-three-small-fixes.md Task 2. Finding: NOT a Tankoban bug. <Describe the actual root cause: either streams are coming from a non-Torrentio addon, or the user's Torrentio webpage config needs re-saving, or Torrentio's filter only applies to fresh results.> Evidence: <list grep findings + addons.json contents if accessible + observed stream sources>. No code change. For Hemanth: <one-paragraph plain-language explanation of what's happening + what he can do (e.g. visit https://torrentio.strem.fun, re-pick 1080p, install updated config) or whether this is moot>.] | files: agents/chat.md
```

- [ ] **Step 8: Report DONE to controller**

Report DONE with the hypothesis + evidence. Include `git diff --stat` if you shipped a fix; otherwise note "no code change" + the finding summary.

---

## Task 3: Episode Row Refresh on Download Start

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

**Background:** Hemanth's smoke: clicking the season-header Download (or context-menu Download) fires the torrent dispatch, but the episode-row Status column stays "-" until he exits + reopens the show. The poll timer `m_bulkPollTimer` at line 158 + `refreshEpisodeBulkProgress()` slot at line 1263 polls every 1s, but it only starts polling when bulk activity is detected, AND it requires that the activity is already visible in the snapshot. The first dispatch + the snapshot's first appearance can have a race window where the immediate post-dispatch state isn't reflected.

Goal: kick `refreshEpisodeBulkProgress()` immediately after a download dispatch (from this view) so the row updates without waiting for the next poll tick. This is a small additive call — no refactor.

- [ ] **Step 1: Find all dispatch sites in StreamDetailView.cpp**

```
Grep pattern: emit theatreTopSeededDownloadRequested|emit theatreDownloadRequested|emit streamBulkDownloadRequested  in src/ui/pages/stream/StreamDetailView.cpp
```

Expected: surface 2-3 emit sites. Per yesterday's grep, lines 525 + 635 emit `theatreTopSeededDownloadRequested` + `theatreDownloadRequested`. There may be a third for the Sources context-menu Download.

- [ ] **Step 2: Read each dispatch site + 5 surrounding lines**

For each emit site, read the 5 lines before + after. Note the function name they sit inside. Likely candidates: `onMovieDownloadBtnClicked` (line ~525), `onSeasonHeaderDownloadClicked` (line ~635), and `onSourceContextMenuDownloadRequested` (if present).

- [ ] **Step 3: Locate the kick target**

`refreshEpisodeBulkProgress()` at line 1263 is the slot we want to call immediately after dispatch. Note its signature: takes no args, no return.

- [ ] **Step 4: Apply the kick at each dispatch site**

After each `emit theatreTopSeededDownloadRequested(...)` or `emit theatreDownloadRequested(...)` line in `StreamDetailView.cpp`, append a one-line call:

```cpp
emit theatreTopSeededDownloadRequested(/* args */);
// Kick episode-bulk-progress refresh immediately so the per-row Status
// column updates without waiting for the 1Hz poll tick (THREE_SMALL_FIXES
// 2026-05-18 Task 3 - Hemanth's smoke showed row stuck at "-" until view
// re-render).
QTimer::singleShot(0, this, &StreamDetailView::refreshEpisodeBulkProgress);
```

The `QTimer::singleShot(0, ...)` delays the refresh by one event-loop tick — by the time it fires, the torrent client has had a moment to register the new group in its snapshot, so the refresh sees the new row. Calling `refreshEpisodeBulkProgress()` synchronously immediately might miss the snapshot update by a microsecond.

If `<QTimer>` isn't already included in `StreamDetailView.cpp`, add `#include <QTimer>` to the Qt includes block (alphabetical placement).

Apply this same kick after EVERY dispatch site found in Step 1. If 3 sites, 3 kicks (one per site). Each is 5 LOC including the comment.

- [ ] **Step 5: Build verify**

```
build_check.bat
```

Expected: `BUILD OK`. Failure mode: missing `<QTimer>` include — add it.

- [ ] **Step 6: ASCII sweep**

```powershell
git diff src/ui/pages/stream/StreamDetailView.cpp 2>$null | Select-String -Pattern '[^\x00-\x7F]' | Select-Object -First 5
```

Expected: zero output.

- [ ] **Step 7: Post RTC for Task 3**

```
## Agent 4 - Episode row refresh on download-start - 2026-05-18

READY TO COMMIT - [Agent 4, Episode row refresh on download-start per docs/superpowers/plans/2026-05-18-three-small-fixes.md Task 3. Hemanth's smoke 2026-05-17: clicking Download from the season header (or context-menu per-episode) fires the torrent dispatch correctly but the per-row Status column stays "-" until view exit + re-enter. Root cause: m_bulkPollTimer at StreamDetailView.cpp:158 polls at 1Hz and only kicks refreshEpisodeBulkProgress on tick; the first dispatch races the snapshot update. Fix: after each emit theatreTopSeededDownloadRequested / emit theatreDownloadRequested in StreamDetailView.cpp, add a QTimer::singleShot(0, this, &StreamDetailView::refreshEpisodeBulkProgress) kick - delays by one event-loop tick so the torrent client snapshot has registered the new group before the refresh reads it. 3 dispatch sites kicked (movie + season-header + context-menu). ~15 LOC added across 3 sites + 1 include. build_check.bat BUILD OK. ASCII sweep clean. Smoke matrix for Hemanth: (a) open any show + click season-header Download -> episode rows update Status column immediately to "Queued"/"<N>%" instead of staying at "-"; (b) right-click an episode + pick context Download -> that single row updates immediately; (c) movies hit the same kick path -> Status updates without re-render.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/stream/StreamDetailView.cpp, agents/chat.md
```

- [ ] **Step 8: Report DONE to controller**

Report DONE with `git diff --stat`. If you found more than 3 dispatch sites, note the count + which functions you kicked from.

---

## Self-review pass (done by plan-writer)

**Spec coverage:** Task 1 → movie Download visibility (Fix 1 from Hemanth's parked list). Task 2 → 4K leak investigation (Fix 2). Task 3 → episode row refresh (Fix 3). All 3 covered. ✓

**Placeholder scan:** Task 2 contains `<list>` and `<describe>` placeholders inside the RTC templates — those are intentional (the implementer fills them based on actual findings). Task 1 Step 7 has `<line>` and Shape A/B placeholders — also intentional, the actual line + shape is determined by Step 1-4 investigation. These are NOT plan failures; they are "fill in your findings here" markers in the RTC template, which is honest about Task 1 + 2 being investigative-first. ✓

**Type consistency:** All slot names (`refreshEpisodeBulkProgress`), member names (`m_movieActionRow`, `m_movieDownloadBtn`, `m_bulkPollTimer`), and signal names (`theatreTopSeededDownloadRequested`, `theatreDownloadRequested`) match across tasks where referenced. ✓

**Ambiguity check:** Task 1 has a real ambiguity — the exact gate is unknown. The plan acknowledges this by structuring Steps 1-3 as investigation + Step 4 as conditional Shape A/B fix. Task 2 same — explicit investigation-then-conditional-fix. Task 3 has no ambiguity — the kick is straightforward additive code. ✓

**Scope sanity:** All 3 tasks should fit in ~15-30 LOC each, single-file mostly. No new tests (smoke-first). No new files. Bundle of 3 tasks = ~60-90 LOC total, 3 RTCs (one per task). Fits in one wake comfortably.
