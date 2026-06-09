# IDLE_PROGRESS_SCAN FIX TODO

**Author:** Agent 5
**Date:** 2026-06-07
**Owner:** Agent 4 (torrent core)

**STATUS (2026-06-09, Agent 4):** P1 (parsed-pack cache) + P2 (off-GUI scan via QtConcurrent)
IMPLEMENTED + BUILD OK (agent4 lane) + 102/102 tankoban_tests GREEN + **Codex threading review =
APPROVE, no blocking findings** (verdict: `agents/audits/idle_progress_scan_p2_review_verdict.txt`).
Applied Codex's one low-pri hardening — stale-result guard now compares `season` too. Reviewer
independence: regardless of who produced P2 (Codex-drafted Jun 7, or a prior Opus instance Jun 9),
the gate is met — Codex APPROVE is a cross-model review, and an independent first-principles Opus
threading analysis (done before consulting Codex) reached the same verdict. A DeepSeek cross-check
was attempted but the `engine.py grunt` verb passes the packet path (not contents) and DeepSeek's CLI
resolves it from the wrong CWD — known helper limitation (obs 14306/12649), not a substantive block.
**P3 (Hemanth smoke under a big active pack) PENDING.**

---

## §1 — Strategic intent

The app still goes **"Not Responding" at idle — with NO video player open** — whenever
downloads are active. Hemanth, 2026-06-07: *"but the app has not responding issues even
when I don't open the video player at all. Why is that?"*

Root cause is a **leftover** of the app-open freeze fix (`fd00690`, 2026-06-06). That fix
killed the per-piece *SQL* flood on the GUI thread (`onPieceFinished` → `getTorrent` per
piece) via piece-meta cache + debounce. But the debounced path still runs a **full
per-file progress scan on the GUI thread** every debounce window, for every active
torrent — and the cost of that scan **scales with the number of files in the torrent.**

Per [TorrentClient.cpp:2961-2969](src/core/torrent/TorrentClient.cpp#L2961-L2969),
`processPieceFinishedProgress()` (always invoked on the main/GUI thread — `onPieceFinished`
is a `Qt::QueuedConnection` from the AlertWorker thread, connection site
[TorrentClient.cpp:552-554](src/core/torrent/TorrentClient.cpp#L552-L554)) does, on each tick:

1. `m_engine->torrentFiles(hash)` — pulls the **entire file list** as a `QJsonArray`
2. `StreamPackParser::parsePack(files, imdbId, season)` — **re-parses the whole list**
3. `m_engine->torrentFileProgress(hash)` — per-file downloaded-bytes for **every file**
4. loops every episode/file updating `StreamDownloadIndex`

For a movie (1-2 files) this is cheap. For a **One Piece season pack (hundreds of
episodes/files)** it is a heavy O(files) scan + JSON re-parse, on the GUI thread, repeated
every debounce window per downloading torrent. Several big active packs ⇒ the GUI thread
keeps getting tied up ⇒ Windows "Not Responding." This is the same disease as `fd00690`,
a milder strain: the first fix treated the SQL symptom, this scan is the leftover.

**Secondary contributor (separate owner, noted for context):** the animated
`GlassBackground` ([GlassBackground.cpp](src/ui/GlassBackground.cpp)) repaints continuously
while the window is active — constant GUI-thread paint at idle. Already under a "heaviness
audit 2026-06-06" (live edits in the working tree to `GlassBackground.*` + `HangWatchdog.*`).
Out of scope for this TODO; flagged so the two aren't conflated.

---

## §2 — Phase breakdown

1. **P1** — Cache the per-torrent file-list parse so `parsePack` doesn't re-run every tick — Owner: Agent 4 — Wakes: ~1
2. **P2** — Move the per-file progress scan off the GUI thread (compute on a worker, marshal only the small per-episode `{imdbId, season, episode, pct}` results back) — Owner: Agent 4 — Wakes: ~1-2
3. **P3** — Verify under a big active season pack (One Piece) that the GUI thread stays responsive; capture before/after evidence — Owner: Agent 4 + Agent 5 (observability/dump) — Wakes: ~1

---

## §3 — Deliverables per phase

### P1 deliverables
- A per-infoHash cache of the parsed pack (`ParsedPack`) keyed off the file list, invalidated only when the file list changes (file count / first-add). The pack layout is immutable once metadata resolves, so it should be parsed **once per torrent**, not once per tick.
- `processPieceFinishedProgress` reads the cached `ParsedPack` instead of calling `parsePack` every time.
- Build verification (`build_check.bat`) + `tankoban_tests` green (TorrentRepoCrudTest et al.).

### P2 deliverables
- The `torrentFiles` / `torrentFileProgress` gather + percentage math runs off the GUI thread (worker thread or `QtConcurrent`, matching the existing freeze-fix pattern used for Books `validateAll` / poster cleanup).
- Only the final small result set (per-episode pct ints) is marshaled back to the GUI thread to call `m_streamDownloadIndex->updateEpisodeProgress(...)`.
- The `pieceFinished` **signal contract stays untouched** (Congress-6 frozen, `022c4eb`).
- Build + tests green.

### P3 deliverables
- Smoke: resume a real multi-hundred-file pack (One Piece) downloading, app on the Stream/Theatre page, no video. Confirm: GUI thread not pegged, no "Not Responding," progress bars still update live.
- Evidence: `tankoctl log-mark` correlation + a procdump/`!runaway` capture showing the main thread is no longer the hot thread inside `parsePack` / `torrentFiles` (mirror the `idle_spin_rootcause_2026-06-05.md` capture recipe).

---

## §4 — Acceptance criteria

- **P1:** `parsePack` is provably called once per torrent (not per tick) — verify via a counter/log-mark under an active download; build + tests green.
- **P2:** under an active big pack, the main thread is no longer the hottest thread in a procdump `!runaway`; `processPieceFinishedProgress` heavy work is off the GUI thread; build + tests green.
- **P3:** Hemanth smoke — open app with One Piece (or equivalent big pack) downloading, no video, app stays responsive for ≥60s. Hemanth verdict "no more freezing at idle."

---

## §5 — Hemanth ratification questions

1. **Is "progress bars update at most ~once per second" acceptable?** — Recommended: YES. Per-piece granularity is invisible to the eye; a 250-500ms-to-1s debounced update is smooth and removes nearly all the load. (This is already the debounce intent; P1/P2 just make each tick cheap *and* off-thread.)
2. **No new UI, no behavior change — pure responsiveness fix?** — Recommended: YES. This is a perf/threading fix only; download behavior, progress accuracy, and the Stream UI all stay identical.

---

## §6 — Ownership

- Primary owner: **Agent 4** (torrent core — `TorrentClient` / `TorrentEngine` / `TorrentRepository`).
- Cross-agent contributors: **Agent 5** (diagnosis author; can supply procdump/`!runaway` evidence + `tankoctl log-mark` correlation for P3).
- Codex Trigger D scope: P2's off-thread refactor is a good candidate for a scoped Codex/cross-model review since it touches threading — route the diff through `codex-review` before merge (threading change = mandatory review per brotherhood norm).

---

## §7 — Dependencies

- Blocked by: nothing — code paths are all present and root-caused.
- Blocks: clean idle responsiveness; unblocks reliable GUI dev-bridge smokes (main-thread starvation also causes the dispatch "series metadata timeout" noted in `idle_spin_rootcause_2026-06-05.md`).
- Memory references: `project_idle_open_spin_torrent_not_player_2026-06-05`, `feedback_app_hang_torrents_db_corruption`, `project_agent4b_departure_2026-05-20`, `feedback_libtorrent_windows_backslash_separator`.

---

## §8 — Risks

1. **Touching the frozen `pieceFinished` signal.** Mitigation: do NOT modify the signal or its connection. Only change what `processPieceFinishedProgress` does *after* the event arrives. (Congress-6 freeze `022c4eb`.)
2. **Thread-safety of `m_engine->torrentFiles/torrentFileProgress` off the GUI thread.** Mitigation: confirm these are safe to call from a worker (libtorrent handle access); if not, gather the raw libtorrent data on the existing safe thread and do only the *parse + math* off-GUI. Verify against `C:\tools\libtorrent-source\`.
3. **Cache staleness if file count changes mid-life** (rare — file list is fixed after add). Mitigation: invalidate the parsed-pack cache on the same events that clear `m_pieceMetaCache` (`clearPieceMetaCache` / remove path, [TorrentClient.cpp:2899-2906](src/core/torrent/TorrentClient.cpp#L2899-L2906)).
4. **Shared-tree git destructiveness.** Mitigation: commit early; never `reset --hard` (see `feedback_shared_tree_git_is_cross_agent_destructive`).

---

## §9 — Wake budget

- P1: ~1 wake (cache the parse — small, contained).
- P2: ~1-2 wakes (off-thread marshal — the real work + review).
- P3: ~1 wake (smoke + evidence).
- Total: ~3-4 wakes.

---

## §10 — Anti-patterns to avoid

1. **DO NOT** modify the `pieceFinished` signal, its emit site, or the `Qt::QueuedConnection` wiring (frozen `022c4eb`).
2. **DO NOT** "fix" this by widening the debounce window to hide the cost — that just makes progress bars lag without removing the GUI-thread work. Make each tick *cheap and off-thread* instead.
3. **DO NOT** re-introduce per-tick SQL (`getTorrent`) — that was `fd00690`'s whole point; keep using `m_pieceMetaCache`.
4. **DO NOT** conflate with the `GlassBackground` repaint (separate heaviness audit, separate owner).
5. **DO NOT** claim green without a procdump `!runaway` showing the main thread is no longer the hot thread under a big active pack (eyes-on evidence, not "BUILD OK").

---

## §11 — Evidence pointers

- Root-cause report (original per-piece SQL flood): `agents/night_ops/idle_spin_rootcause_2026-06-05.md` (capture recipe: procdump64 `-c 15 -s 2`, cdb `!runaway`).
- Crash inventory: `agents/night_ops/crash_inventory_2026-06-03.md` (#1 RESOLVED note).
- Fix that handled the SQL strain: commit `fd00690` (cache piece-meta + debounce).
- Live code: [TorrentClient.cpp:2941-2996](src/core/torrent/TorrentClient.cpp#L2941-L2996) (`processPieceFinishedProgress`), [TorrentClient.cpp:3809-3849](src/core/torrent/TorrentClient.cpp#L3809-L3849) (`onPieceFinished` debounce).
- Memory: `project_idle_open_spin_torrent_not_player_2026-06-05`.

---

## §12 — Close criteria

Idle "Not Responding" no longer reproduces with a big active season pack downloading and
no video open. P3 smoke green + Hemanth verdict. Progress bars still update live. Build +
`tankoban_tests` green. Threading diff cross-model reviewed.

---

## §13 — Standing contracts

- The per-torrent **parsed-pack cache** becomes the canonical place to read pack layout on hot paths (parse once per torrent, never per tick) — same spirit as `m_pieceMetaCache`.
- **No heavy work on the GUI thread in download hot paths** — gather/parse/compute off-thread, marshal only small results back. This is the durable pattern (also matches `feedback_app_hang_torrents_db_corruption` item (c)).

---

## §14 — Archive trigger

When closed, move to `agents/_archive/todos/IDLE_PROGRESS_SCAN_FIX_TODO.md` and update the
CLAUDE.md / ACTIVE_TODOS "Active Fix TODOs" list to mark it CLOSED.
