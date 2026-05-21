# SEQUENTIAL_DOWNLOADS_FIX — Strict 1-at-a-Time Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline single-agent — single-file change with smoke verification, no need for subagent fan-out). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enforce strict 1-at-a-time torrent downloads across the entire Tankoban session — when a season dispatches N episodes, episode 1 downloads first, episodes 2..N sit queued by libtorrent's own scheduler; on completion of episode 1, libtorrent auto-promotes episode 2; rinse + repeat. Per Hemanth 2026-05-21 ~7:45pm IST: *"episode by episode sequential is an absolute must."*

**Architecture:** Single-line change to libtorrent session settings — `active_downloads = 10 → 1`. libtorrent's session-level queue handles promotion automatically (FIFO by `queue_position()` which defaults to add-order). No new state machine, no app-level queue, no Queued enum addition; the existing libtorrent `queued_for_download` state already surfaces in the UI via `torrent_status::state` mapping. Tradeoff: REVERSES the 2026-04-19 stream-priority optimization (TorrentEngine.cpp:344-347 comment explicitly raised the cap to give stream torrents priority over prior library-mode downloads). Hemanth's "absolute must" overrides the streaming-priority intent — both are sequential now.

**Tech Stack:** libtorrent-rasterbar 2.x (`settings_pack::active_downloads`), Qt6, C++20. No new dependencies. Uses existing `TorrentEngine::setQueueLimits` API for runtime tunability (already exposed via `QueueLimitsDialog`).

---

## File Structure

**Single-file change** — the entire core fix is one line. The rest is verification + smoke + comment-rewrite.

- Modify: `src/core/torrent/TorrentEngine.cpp:368` — change `active_downloads = 10` to `active_downloads = 1`
- Modify: `src/core/torrent/TorrentEngine.cpp:344-347` — rewrite the comment block to document the new sequential-by-default invariant + Hemanth-driven design intent
- Verify: `out\tankoctl.exe stream-get-torrents --active` after dispatching a multi-episode season (smoke evidence)

**No new files. No tests added** — the libtorrent setting is a black box; we don't test libtorrent's queue behavior, we trust + verify it. Tankoban's existing `TorrentRow.state` painter (`Active` / `Paused` / `Completed` / etc) already reflects what libtorrent reports via `TorrentClient::onTorrentUpdate`. Smoke is the verification.

---

## Invariants to preserve

1. **No regression on already-active torrents at the moment of change.** Pre-existing Active torrents stay active (libtorrent doesn't re-cap mid-flight unless you call `set_settings` with active_downloads < current active count — which DOES cap them; this is the warm-cache behavior to verify in smoke).
2. **User-facing `QueueLimitsDialog` continues to work.** Existing `setQueueLimits(maxDownloads, maxUploads, maxActive)` API is unchanged; the dialog still lets power-users override the default. The change here is to the DEFAULT only, not the API contract.
3. **TorrentEngine API freeze (Congress 6 `022c4eb`) is untouched.** No method signatures change. The 12-method API stays intact.
4. **request_queue_time stays at 10.** That's a separate streaming-priority setting (line 373) unrelated to active_downloads count.

---

## §5 Open questions — Hemanth ratification BEFORE execution

These are design questions that the implementation depends on. Pause for Hemanth nod before T1 fires.

- **Q1 — SCOPE OF CAP**: 1-at-a-time GLOBALLY (every torrent including manga, books, video) or only STREAM-MODE video?
  - Proposed: **Global**. Simpler implementation (one libtorrent setting), matches Hemanth's verbatim "absolute must" framing.
  - Tradeoff if global: a long-running book download will block a video season dispatch from starting; user has to wait for the book to finish OR manually pause it.
  - Alternative: per-category cap (1 video at a time + N manga concurrent). Way more code, but more flexible.
- **Q2 — REVERSAL OF 2026-04-19 STREAM-PRIORITY**: TorrentEngine.cpp:344-347 comment explicitly raised `active_downloads` from 5 → 10 to *"prevent the stream torrent from being queued behind prior library-mode downloads."* This change REVERSES that. Confirm OK?
  - Proposed: **Confirm reversal**. Hemanth's "absolute must" overrides; the new behavior IS that stream torrents queue too.
- **Q3 — QueueLimitsDialog DEFAULT**: should the existing user-facing QueueLimitsDialog ship a new default of 1 (matching the engine), or stay at the old default (so the dialog and engine disagree)?
  - Proposed: **Update dialog default to match (1)**. Otherwise the dialog says "Max active downloads: 10" while the actual cap is 1 — user-confusing.
- **Q4 — VISIBLE "QUEUED" STATE IN UI**: libtorrent reports `queued_for_download` state; does it already surface in StreamDetailView's episode-row status chip? If not, do we want to add a visible "Queued" pill in this TODO (Phase 1.b) or defer to a future polish arc?
  - Proposed: **Verify during smoke (T5)**. If libtorrent's queued state already paints something user-readable, no extra work needed. If it shows up as blank or as "Active 0%", add a Phase 1.b task to map `queued_for_download` → a visible chip.

---

## Tasks

### Task 1: Apply the single-line change + rewrite the comment block

**Files:**
- Modify: `src/core/torrent/TorrentEngine.cpp` (lines ~344-370 area)

- [ ] **Step 1: Edit TorrentEngine.cpp — replace the comment block + the active_downloads line**

The current comment block at lines 344-347 (within the larger session-settings block) reads:

```cpp
    //   active_downloads 5→10, active_seeds 5→10, active_limit 10→20:
    //       prevents the stream torrent from being queued behind prior
    //       library-mode downloads when multiple torrents are active.
    //       A streaming app should treat stream adds as priority.
```

And the actual setting at line 368 is:

```cpp
    sp.set_int(lt::settings_pack::active_downloads, 10);
```

Replace the comment block with:

```cpp
    //   active_downloads = 1 (was 10 pre-SEQUENTIAL_DOWNLOADS_FIX 2026-05-21):
    //       strict 1-at-a-time across the entire Tankoban session per
    //       Hemanth 2026-05-21 ~7:45pm IST ("episode by episode sequential
    //       is an absolute must"). libtorrent's session scheduler queues
    //       any torrent above the cap into state queued_for_download and
    //       auto-promotes the next queued one when the active one
    //       completes. FIFO by add-order via queue_position(). This
    //       REVERSES the 2026-04-19 stream-priority optimization (5→10
    //       bump that prevented stream torrents from queuing behind
    //       library-mode downloads) — under the new contract stream
    //       torrents queue too. Tradeoff acknowledged; the absolute-must
    //       framing won.
    //   active_seeds 10, active_limit 20: unchanged. Seeding is independent
    //       of download contention; the active_limit cap is the outer ring
    //       and stays loose so completed torrents can keep seeding without
    //       blocking new download dispatches.
```

And replace the active_downloads line at ~368 with:

```cpp
    sp.set_int(lt::settings_pack::active_downloads, 1);
```

active_seeds (line 369) and active_limit (line 370) stay at 10 and 20 respectively.

- [ ] **Step 2: Run build_check.bat to verify clean compile**

Run: `./build_check.bat`
Expected: `BUILD OK` — comment-only + integer-literal change, no surface API touched, should pass first-try.

- [ ] **Step 3: Run tankoban_tests to verify no regression**

Run: `cmd /c "<vcvars> && cmake --build out --config Release --target tankoban_tests"`
Then: `out\tankoban_tests.exe`
Expected: existing test suite results unchanged from pre-change baseline. The libtorrent settings change doesn't surface in any pure-logic test path.

Note: at the time of writing this plan, pre-existing brotherhood-debt failures at HEAD are `PickBestBookFileTest.FileInSubdir_MovedToRoot` (Agent 2 Jr, `c7acf74`) + `LocalFandomCatalogLoader::loadFromFile` link error (Agent 1, fandom catalog territory). Those are NOT caused by this change and should NOT be fixed in this TODO scope. Verify these are the only failures pre + post and the delta is zero new failures.

- [ ] **Step 4: Commit**

```bash
git add src/core/torrent/TorrentEngine.cpp
git commit -m "SEQUENTIAL_DOWNLOADS_FIX: strict 1-at-a-time via libtorrent active_downloads=1

Single-line change to TorrentEngine session settings:
active_downloads 10 -> 1 per Hemanth 2026-05-21 ~7:45pm IST
('episode by episode sequential is an absolute must').

libtorrent's session-level queue handles promotion automatically:
torrents above the cap go into state queued_for_download, FIFO by
queue_position() (add-order default); on torrentCompleted, the next
queued one auto-promotes to Active.

REVERSES the 2026-04-19 stream-priority optimization (5->10 bump that
gave stream torrents priority over library-mode downloads). Under the
new contract, stream torrents queue too. Tradeoff acknowledged.

active_seeds (10) + active_limit (20) unchanged — seeding is
independent of download contention.

No new state machine added. No Queued enum on TorrentRow. The existing
libtorrent state mapping in TorrentClient::onTorrentUpdate surfaces
queued state through the existing UI status painter.

build_check.bat BUILD OK; tankoban_tests pre-existing failures
unchanged (Agent 2 Jr book file-walk test + Agent 1 fandom catalog
link error; both pre-date this commit).

T2 smoke fires next: dispatch Community S5 (13 episodes) -> verify
via tankoctl stream-get-torrents that 1 shows Active + 12 show
queued; complete the active one -> verify next auto-promotes.
"
```

### Task 2: Live smoke under BUILD LANE (Hemanth-driven UI step)

**Files:** None (verification only)

- [ ] **Step 1: Agent claims BUILD LANE in chat.md + launches Tankoban via build_and_run.bat**

Per `feedback_agent_launches_app.md`, agent runs `build_and_run.bat` themselves in the background; Hemanth's role starts at the UI click step.

Post chat.md banner:
```
## BUILD LANE — Agent 4 — SEQUENTIAL_DOWNLOADS_FIX live smoke

Claimed YYYY-MM-DD ~HH:MMpm IST. Strict 1-at-a-time verification:
dispatch Community S5 -> 1 active + 12 queued -> complete 1 -> next
promotes. Will release on completion.
```

Then: `Bash run_in_background: true` with `./build_and_run.bat`.

- [ ] **Step 2: Wait for window to pop, then register lease + start monitoring**

Once Tankoban is alive, agent registers `out\tankoctl.exe lease-get build` for the formal gov-v7 lease (chat.md banner is the human-readable companion).

Tail `out/events.jsonl` and snapshot `out\tankoctl.exe stream-get-torrents --all` for baseline.

- [ ] **Step 3: Hemanth dispatches Community S5 via the existing Download button**

Hemanth-facing one-liner: *"Click Theatre → Community → Season 5 → Download (the normal one, not anything fancy). I'll watch the queue."*

That's the click. Nothing else — no purple button (it's gone post-Phase-1-revert), no Find Sources, just the regular Download.

- [ ] **Step 4: Verify 1 active + 12 queued via tankoctl**

Run: `out\tankoctl.exe stream-get-torrents --all` immediately after dispatch.
Expected: One torrent in Active state, 12 in queued_for_download state.

(If `--all` returns all torrents flat without grouping, filter on imdb=tt1439629 + season=5 to isolate the dispatch.)

If the count shows multiple Active or all queued, the cap is misbehaving. Diagnostic: `tankoctl logs 50 | grep active_downloads` or read out/sidecar_debug_live.log for any session-settings warnings.

- [ ] **Step 5: Wait for episode 1 to complete (or fast-finish by pre-seeding a small torrent if smoke target is too big)**

When the active episode completes (or is canceled/removed), verify:
- The completed torrent transitions to Completed state
- The next queued torrent auto-promotes to Active
- Episode count: 1 (Completed) + 1 (Active) + 11 (queued)

Hemanth-facing prompt for visual: *"Episode 1's downloading. Tell me when you see the percentage either hit 100% or when episode 2's row goes from 'queued' to 'downloading'."*

- [ ] **Step 6: Verify UI surface (Q4 from §5)**

Visually check StreamDetailView's episode rows. Does episode 2-13 show a "Queued" pill / chip / dimmed state, or does it look identical to the dimmed "Download" idle state?

If the queued state is visually indistinguishable from idle, that's the F1 carry-through finding for v1.b — add a Phase 1.b task to map `queued_for_download` → a visible chip (probably an amber pill or a "Queued" text label in the Status column).

- [ ] **Step 7: Stop Tankoban + release BUILD LANE + close-out RTC**

Run: `powershell -NoProfile -File scripts/stop-tankoban.ps1`
Then: `out\tankoctl.exe lease-release build` (if Tankoban's still up before kill) OR just post the chat.md RELEASED banner.

Post chat.md close-out RTC documenting:
- Smoke verdict (PASS / PARTIAL / FAIL)
- Active vs queued counts observed at dispatch + post-completion
- UI surface verdict (Queued state visible / invisible / partially visible)
- Any unexpected behavior (e.g., already-active library torrents getting downgraded mid-flight)

---

## Self-Review Checklist

1. ✅ **Spec coverage:** Hemanth's "episode by episode sequential is an absolute must" — single setting flip from 10 → 1 covers the requirement. §5 questions Q1-Q4 surface design ambiguity (scope of cap, reversal acknowledgment, dialog default, UI visibility).
2. ✅ **No placeholders:** Both tasks have concrete code blocks. The smoke task has specific tankoctl commands + expected outputs + Hemanth-facing one-liners.
3. ✅ **Type consistency:** No new types introduced. Single integer literal change.
4. ✅ **Invariants preserved:** Pre-existing Active torrents covered in §Invariants Q1; QueueLimitsDialog API covered Q2; TorrentEngine API freeze covered Q3; request_queue_time covered Q4.

## Risks

- **libtorrent cap-mid-flight behavior:** if Tankoban is currently running multiple active downloads when the new cap takes effect (i.e., the binary restarts with active_downloads=1 while libtorrent state was 10 Active from a prior session), libtorrent MAY downgrade some to queued. Verify in smoke; if downgrades break in-flight torrents, add a one-time migration that explicitly preserves currently-Active torrents at startup before applying the new cap.
- **User confusion when cap=1 but UI shows "Max active downloads: 10" in QueueLimitsDialog:** Q3 ratification covers this. If Hemanth says "keep dialog at 10," document the discrepancy + plan a Phase 1.b dialog default update.
- **Long-running non-video downloads blocking video season dispatches:** Q1 (global vs stream-only scope) addresses this. If Hemanth chooses global scope (proposed), the smoke evidence should explicitly call out this behavior so it's not a surprise later.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-21-sequential-downloads.md`.

REQUIRED SUB-SKILL on execution: `superpowers:executing-plans`. Inline single-agent (single file change + Hemanth-in-loop smoke; no parallelism, no subagent fan-out).

§5 ratification is the **hard gate** — Hemanth must answer Q1-Q4 before T1 fires. If any answer requires scope change (e.g., per-category cap instead of global), revisit the file structure + tasks.
