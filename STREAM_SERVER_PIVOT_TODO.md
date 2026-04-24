# Stream Server Pivot TODO — Tankoban 2

**Authored 2026-04-24 by Agent 0.** Derived from Hemanth's 15:34 decision + Agent 4's handoff at `agents/chat.md` 2026-04-24 15:40 block (lines 608-651) + Wake-4 `STREAM_STALL_AUDIT_RERUN` evidence at `agents/audits/evidence_{sidecar,player_debug,stream_telemetry}_stream_stall_audit_rerun_141745.*`. Supersedes `STREAM_ENGINE_REBUILD_TODO.md` scope on P5 commit.

---

## Context

Stream mode has consumed more hours than the rest of Tankoban combined. 4 wakes of STREAM_HTTP_PREFER (piece count / gate shape / session_params / head range), 2 Congresses (5 rebuild + 6 API freeze), 1 APPROVED A/B experiment (TANKOBAN_STREMIO_TUNE — 65% stall reduction), STREAM_STALL_FIX CLOSED-then-re-opened by the 2026-04-24 VIDEO_QUALITY_DIP audit. Wake-4 fresh smoke 2026-04-24 13:58-14:18 showed HOLY_GRAIL=0 across 4.5 min — ffmpeg probe never completed, 12 `Stream ends prematurely` reconnect loops on pieces just outside the 5 MB head range. The pattern of "fix one layer, surface another" has reached diminishing returns.

**Hemanth's decision 2026-04-24 15:34:** *"we've been at it for weeks, streaming still doesn't work, unless you can magically make libtorrent work for streaming, we have exhausted a lot doing this."*

Stream mode pivots off our custom C++ libtorrent-based engine onto **Stremio's own Rust `stream-server` binary** (perpetus/stream-server — static single Windows exe from GitHub Releases) running as a subprocess alongside ffmpeg_sidecar. REST over `127.0.0.1`. Tankorent (torrent downloads for Tankorent UI) stays on libtorrent — only stream mode pivots.

The pivot doesn't break Congress 8's reference-driven discipline — it *strengthens* it. Instead of porting Stremio's piece-scheduling feature-by-feature and losing at it, we run their reference source compiled as a subprocess.

---

## Objective

Replace `src/core/stream/*` (~3000 LOC) with `StreamServerProcess` subprocess + `StreamServerClient` REST wrapper + `StreamEngineAdapter` signal bridge. StreamPage + VideoPlayer wire unchanged. Tankorent (`src/core/torrent/*`) untouched.

**Success =** Invincible S01E01 Torrentio 1080p reaches HOLY_GRAIL=1 end-to-end in <15s p50 on a healthy swarm, mid-file seek into unbuffered resumes in <5s, zero TorrentEngine API touches, and stream-mode cold-open failure rate drops to zero over a 1-week shipping window.

---

## Non-Goals (explicitly out of scope)

- **Tankorent-side work.** TorrentEngine + Congress 6 12-method API freeze preserved. Tankorent search + download stays on libtorrent. Verified in P4 by Agent 4B sign-off.
- **Rust toolchain dependency.** We download the pre-built Windows exe from `https://github.com/perpetus/stream-server/releases` + SHA-256 pin; we don't `cargo build --release` in CI.
- **Source patches to stream-server.** No flipping `[0.0.0.0]` → `[127.0.0.1]` at `server/src/main.rs:299`; no adding a `--port` CLI arg. Network scope constrained via Windows Firewall inbound rule at install time.
- **StreamPage or VideoPlayer rewrites.** Consumer contracts (`stallDetected` / `stallRecovered` / `bufferedRangesChanged` / `streamReady` signals) preserved via the adapter class. Zero UI-side rewrites.
- **WebTorrent fallback.** If P0 prototype fails, WebTorrent pivot is a separate TODO. Don't pre-author.
- **HLS transcoding.** Stremio's binary ships with it; we don't consume it. Direct-byte-stream only.
- **libmpv wrap.** Orthogonal; covered separately by the groundwork/butterfly libmpv discussion. Not in this TODO.
- **Stream-server crash recovery beyond respawn.** If the binary hard-crashes mid-stream, we respawn + re-add torrent. No playback-checkpoint recovery — that's a follow-on if the crash rate warrants.

---

## Supersession

**This TODO supersedes `STREAM_ENGINE_REBUILD_TODO.md` scope on P5 commit** — the point at which the C++ engine is deleted. Until P5, both live side-by-side guarded by `TANKOBAN_STREAM_BACKEND={legacy,server}` CMake option (see §Legacy-flag rollback window below). The StreamProgress schema-version hardening shipped at `ad2bc65` (REBUILD P0) is preserved — it's mode-agnostic.

**Related-but-not-superseded TODOs** (survive pivot unchanged):
- `STREAM_HTTP_PREFER_FIX_TODO.md` — addon/debrid HTTP fallback. Bridges through the adapter.
- `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO.md` — LoadingOverlay state enrichment. Consumer contract preserved.
- `STREAM_STALL_UX_FIX_TODO.md` — CLOSED 2026-04-22. Already shipped.
- `PLAYER_STREMIO_PARITY_FIX_TODO.md` — player-side parity (seek, bufferedRanges surface). Stays, feeds the adapter.

---

## Reference slate

**Stremio stream-server source** (already on disk): `C:\Users\Suprabha\Downloads\Stremio Reference\stream-server-master\` — Rust, 5 crates. Key files:
- `server/src/main.rs:203-299` — route binding + port 11470 + CORS setup
- `server/src/routes/stream.rs` — `GET /stream/{infoHash}/{fileIdx}` + `GET /{infoHash}/{fileIdx}` handlers
- `server/src/routes/engine.rs` — `POST /create` + `GET /remove/{infoHash}` + `GET /list`
- `server/src/routes/system.rs:15-41` — `GET /stats.json` + `GET /{hash}/stats.json` response shape
- Shutdown: `main.rs:309-327` — graceful on Ctrl+C / shutdown_rx, force-exit 1s after (libtorrent hang workaround)

**Stremio binary (pre-built):** `https://github.com/perpetus/stream-server/releases` — `stream-server-windows-amd64.exe`, ~50 MB static.

**Tankoban subprocess pattern to mirror:** `src/ui/player/SidecarProcess.{h,cpp}` — intentional-shutdown flag, crash signal via `onProcessFinished`, seq counter, session-id, session-filtered IPC at parser layer (see `SidecarProcess.h:228`).

**Stream-engine signal consumers:** `src/ui/pages/StreamPage.cpp` — bindings to `StreamEngine::stallDetected`, `stallRecovered`, `bufferedRangesChanged`, `streamReady`. Adapter emits these exact signals from stats-poll response shape.

**Congress 6 API freeze:** `agents/congress_archive/2026-04-18_congress6_stremio_audit.md` §5 — 12-method TorrentEngine list. Honored by Agent 4B sign-off in P4.

**Congress 8 §3 pairing:** `agents/congress_archive/2026-04-23_reference_driven_player_bug_closure.md` §3. Stream-HTTP-lifecycle row evolves from "Agent 4 + Stremio/mpv/IINA 3-tier" to "Agent 4 + stream-server-master Rust source as reference for REST contract + stall semantics; legacy C++ stream engine no longer accepts new cites (retired P5)." Sidecar / subtitle / HDR / UX-polish rows unchanged.

---

## Phases

Seven phases. Phase numbers match risk boundaries, not individual deliverables. Every phase has explicit entry criteria + exit criteria. No deletions pre-P0 greenlight.

### P0 — Prototype (Agent 7 Trigger-B, HARD GATE)

**Owner:** Agent 7 (Codex Trigger-B). **Scope:** reference-only prototype in a scratch directory. No src/ changes. No commits to master.

**Entry criteria:** None. Kickoff is immediate.

**Work:**
1. Download `stream-server-windows-amd64.exe` from GitHub Releases to `C:\tools\stream-server\`. Compute SHA-256.
2. Spawn the binary as a subprocess from a minimal Qt main.cpp or Rust/Python scratch harness. Verify it binds to `127.0.0.1:11470` (after firewall rule — see below) or accept `0.0.0.0:11470` for prototype-only.
3. `POST /create` with Invincible S01E01 Torrentio 1080p magnet (infoHash `ae017c71...`). Parse response.
4. `GET /stream/{infoHash}/{fileIdx}` — feed the byte-stream URL to `ffmpeg -i <url> -f null -` OR our actual `ffmpeg_sidecar` binary. Observe HOLY_GRAIL=1 (first frame decoded) within 30s on a healthy swarm.
5. **Seek test** — request the stream URL with HTTP Range header at byte offset corresponding to mid-file. Verify stream-server reprioritizes piece fetch (check `stats.json` for piece-priority shift). First byte of new range must arrive within 10s. **This test is mandatory** — byte-range semantics on re-request are the first thing that could break quietly.
6. Deliverable: `agents/prototypes/stream_server/` scratch folder + `agents/audits/stream_server_prototype_2026-04-NN.md` report. Report includes: HOLY_GRAIL time, seek latency, crash count, memory footprint, any REST contract surprises, any hardcoded-path issues (e.g. `~/.stremio-server` piece store colliding with Stremio Desktop).

**Exit criteria (ALL must pass):**
- HOLY_GRAIL=1 on Invincible S01E01 in <30s
- Seek reprioritization works (Range header triggers piece-priority shift)
- No crash across 3 consecutive cold-opens
- Binary memory footprint <200 MB during active streaming
- No disk-path collision with Stremio Desktop install (if Hemanth has it)

**If ANY exit criterion fails:** halt the pivot. Post findings in chat.md. Hemanth decides: WebTorrent pivot (separate TODO), stay on C++ engine, or patch + retry. No P1 kickoff until Hemanth ratifies the P0 report with `ratified` / `APPROVES` / `Final Word` / `Execute`.

### P1 — Subprocess + REST client (Agent 4)

**Owner:** Agent 4. **Scope:** two new classes, legacy-flag infrastructure, Windows Firewall rule.

**Entry criteria:** P0 exit criteria ALL PASS + Hemanth ratification on P0 report.

**Work:**
1. **Move `src/core/stream/*` to `src/core/stream/legacy/`.** Add CMake option `TANKOBAN_STREAM_BACKEND={legacy,server}` defaulting to `legacy`. StreamPage constructor picks backend at runtime via `#ifdef`. Legacy path is byte-for-byte identical — just relocated.
2. **Ship `stream-server-windows-amd64.exe` to `resources/stream_server/`.** SHA-256 pin in `resources/stream_server/STREAM_SERVER_VERSION`. Launcher verifies SHA-256 at startup; refuses to spawn on mismatch.
3. **New class `StreamServerProcess`** at `src/core/stream/server/StreamServerProcess.{h,cpp}`. Mirror `SidecarProcess` pattern:
   - `QProcess` subprocess
   - `m_intentionalShutdown` flag (so crash-detection doesn't fire on deliberate stop)
   - `onProcessFinished` signal with exit-code + crash-vs-clean discrimination
   - `m_sessionId` + seq counter
   - Launch on stream-mode entry, stop on stream-mode exit (not app-exit — save startup cost for users who never stream)
   - Port 11470 collision handling: if bind fails, kill prior `stream-server-windows-amd64.exe` PIDs via `taskkill`, retry once; if still fails, surface error + suggest "close Stremio Desktop" to user
4. **Windows Firewall inbound rule** at installer time (or first-run): block external traffic to port 11470 on Tankoban's process handle. Local loopback still works. Document the rule in `resources/stream_server/FIREWALL_RULE.md` for users who hit AV/firewall issues.
5. **New class `StreamServerClient`** at `src/core/stream/server/StreamServerClient.{h,cpp}`. QNAM-based REST wrapper. Methods:
   - `createTorrent(const QString& magnet)` → `POST /create` → returns `infoHash` + `files[]`
   - `streamUrlFor(const QString& hash, int fileIdx)` → constructs `http://127.0.0.1:11470/{hash}/{fileIdx}` (auth-free, local only)
   - `pollStats(const QString& hash)` → `GET /{hash}/stats.json` → returns struct with `download_speed`, `peers`, `have_bytes`, `downloaded_bytes`, piece state
   - `removeTorrent(const QString& hash)` → `GET /remove/{hash}`
   - `listActive()` → `GET /list`
6. Build-verify: sidecar + main app build green on both `-DTANKOBAN_STREAM_BACKEND=legacy` and `-DTANKOBAN_STREAM_BACKEND=server`. Latter may have placeholder StreamPage bindings — that's fine, Phase 2 wires them.

**Exit criteria:** both classes instantiate cleanly, binary spawns + binds + accepts `POST /create` + serves `/stream/{hash}/{idx}` for a known magnet, SHA-256 verify works, port collision handling tested, Firewall rule documented. Both CMake backends still build.

**RTC discipline:** one batch per commit — stream-server binary ship, StreamServerProcess class, StreamServerClient class, CMake backend flag, Firewall rule doc. Avoid mega-commit.

### P2 — Adapter bridge (Agent 4)

**Owner:** Agent 4. **Scope:** signal-emitting adapter between REST client and StreamPage. Split internally 2a + 2b by signal directionality.

**Entry criteria:** P1 exit criteria PASS + at least one successful cold-open smoke on `TANKOBAN_STREAM_BACKEND=server` (no UI binding yet; command-line `POST /create` + byte-stream verify).

**Work (P2a — Outbound command bridge):**
1. **New class `StreamEngineAdapter`** at `src/core/stream/server/StreamEngineAdapter.{h,cpp}`. Same public interface as `StreamEngine` (the legacy class) so StreamPage binding unchanged. Internally calls `StreamServerClient` methods.
2. `StreamPage.start(magnet)` → adapter `POST /create` → on stats-report-ready emit `streamReady(hash, fileIdx, streamUrl)`. Deterministic, one-shot signal. Unlocks end-to-end HOLY_GRAIL observation.
3. Smoke: run the full Invincible S01E01 cold-open on `TANKOBAN_STREAM_BACKEND=server`, observe HOLY_GRAIL=1 end-to-end via StreamPage (not command-line). Record cold-open time.

**Work (P2b — Polling-driven state signals):**
1. Shared poller inside adapter — single `QTimer` calls `pollStats()` on a tunable interval (default 500ms). Emits from the same response parse:
   - `stallDetected(hash)` when `downloadSpeed=0` AND `have_bytes < target` AND no pieces arrived in last N polls (debounce threshold tunable)
   - `stallRecovered(hash)` when `downloadSpeed>0` AND at least one new piece in last poll
   - `bufferedRangesChanged(hash, ranges)` translated from `stats.json` piece state to byte-range list
2. Session-filtering (see `SidecarProcess.h:228` pattern) — stats-poll responses carry session-id; stale responses rejected at parser layer.
3. Empirical poll-cadence tuning (see Risk 5 below) — start at 500ms, compare stall-detection latency vs legacy engine's signal-driven stalls, tighten if under-sensitive or loosen if wasting CPU.

**Exit criteria:** All four StreamEngine signals (`streamReady`, `stallDetected`, `stallRecovered`, `bufferedRangesChanged`) fire correctly on `TANKOBAN_STREAM_BACKEND=server`. StreamPage + VideoPlayer end-to-end test on Invincible shows: cold-open <15s, mid-playback stall detection within 1s of actual stall, buffered range updates visible on seek slider.

### P3 — Telemetry + progress hooks (Agent 4)

**Owner:** Agent 4. **Scope:** preserve `out/stream_telemetry.log` format + `VideoPlayer::time_update` IPC compatibility.

**Entry criteria:** P2 exit criteria PASS.

**Work:**
1. Map `stream-server stats.json` responses to our existing `stream_telemetry.log` format in `StreamEngineAdapter::writeTelemetry`. Preserve column shape so `scripts/runtime-health.ps1` continues working without edits.
2. Continue Watching progress hook — currently via `StreamEngine` telemetry. Re-plumb via `VideoPlayer::time_update` IPC which is mode-agnostic and already emits playback position regardless of backend. Verify, don't rewrite.
3. Progress persistence verification — watch 60s of Invincible on `TANKOBAN_STREAM_BACKEND=server`, close app, reopen, Continue Watching shows the stream at the right offset.

**Exit criteria:** telemetry.log writes identically to legacy engine; `runtime-health.ps1` runs green; Continue Watching round-trips correctly across app restart.

### P4 — Error paths + Tankorent isolation sign-off (Agent 4 + Agent 4B)

**Owner:** Agent 4 (implements) + Agent 4B (signs off). **Scope:** failure-mode UI mapping + Congress 6 API freeze verification.

**Entry criteria:** P3 exit criteria PASS.

**Work (Agent 4 — implementation):**
1. Map error paths to `LoadingOverlay` states:
   - source-all-failed → "No working sources found" (with retry button)
   - torrent-not-reachable → "Stream source unreachable" (after 30s no-peers)
   - stream-server-crashed → "Engine crashed — retrying" (auto-respawn + re-add torrent once; second crash = "Engine failed to start" with Hemanth-report path)
   - cancelled-mid-play → silent transition back to previous page (no error display)
2. Stream-server crash MUST NOT cascade to TorrentEngine. Agent 4 verifies by killing `stream-server-windows-amd64.exe` mid-playback + confirming Tankorent tab still works for unrelated torrent downloads.

**Work (Agent 4B — sign-off):**
1. Agent 4 submits isolation evidence:
   - Full `git diff src/core/torrent/` = empty (no TorrentEngine changes)
   - Full `git diff src/core/torrent/TorrentEngine.h` = empty (API freeze honored)
   - Fresh Tankorent smoke: add + download + seed an unrelated torrent on `TANKOBAN_STREAM_BACKEND=server` while streaming Invincible in parallel
   - Memory sanity: two lt::session instances (Tankorent's + stream-server's) don't interfere at the libtorrent-sys level
2. Agent 4B reviews + posts sign-off line in chat.md: `[Agent 4B, Congress-6 12-method API freeze VERIFIED for STREAM_SERVER_PIVOT P4]`

**Exit criteria:** all 4 error-path UI states tested and verified; Agent 4B sign-off posted; Tankorent smoke PASS on pivot backend.

### P5 — Deletion (Agent 4, terminal-tag commit)

**Owner:** Agent 4. **Scope:** delete legacy C++ engine + remove CMake backend flag.

**Entry criteria:**
- P4 exit criteria PASS
- **One full week of `TANKOBAN_STREAM_BACKEND=server` shipping to Hemanth without rollback to legacy**
- Hemanth explicit approval: `ratified` / `APPROVES` / `Final Word` / `Execute` on a chat.md post proposing the deletion

**Work:**
1. Delete `src/core/stream/legacy/` entirely — `StreamEngine.{h,cpp}`, `StreamHttpServer.{h,cpp}`, `StreamPieceWaiter.{h,cpp}`, `StreamPrioritizer.{h,cpp}`, `StreamSeekClassifier.{h,cpp}`, `StreamSession.{h,cpp}`. ~3000 LOC gone.
2. Delete `StreamEngineStats` / `StreamFileResult` structs (if not already relocated to adapter). Update any remaining consumers to adapter's types.
3. Remove `TANKOBAN_STREAM_BACKEND` CMake option + all `#ifdef` branches. `src/core/stream/server/` becomes `src/core/stream/` (collapse one directory level).
4. Terminal-tag commit: `git tag stream-server-pivot-terminal`. Blame-clean, isolated.

**Exit criteria:** Tankoban builds + runs + streams Invincible + runs Tankorent in parallel, all green, no legacy references anywhere in the tree.

### P6 — Archive + dashboard (Agent 0)

**Owner:** Agent 0. **Scope:** retire `STREAM_ENGINE_REBUILD_TODO` + housekeeping.

**Entry criteria:** P5 exit criteria PASS.

**Work:**
1. Move `STREAM_ENGINE_REBUILD_TODO.md` to `agents/_archive/todos/` (preserve P0 StreamProgress `ad2bc65` as historical cursor; note supersession in archival comment).
2. Update `MEMORY.md` — strike the REBUILD reference from the active TODO line; update `project_stream_server_pivot.md` phase cursor to CLOSED.
3. Update `CLAUDE.md` dashboard — Agent 4 status "IDLE, pivot complete"; Active Fix TODOs table — remove the STREAM_SERVER_PIVOT row.
4. Amend Congress 8 §3 pairing row in the archive file's *addendum* (archive bodies stay frozen per discipline; addendum is a dated note below the archive). Row now reads: *"Stream-HTTP lifecycle → Agent 4, stream-server-master Rust source (perpetus/stream-server) as primary reference for REST contract + stall semantics; mpv source as secondary for sidecar stall-signaling; IINA tertiary for overlay chrome only. Legacy C++ stream engine retired 2026-NN-NN at P5 terminal-tag."*

**Exit criteria:** `/brief` shows no stream-engine-rebuild references; fresh session reading CLAUDE.md dashboards picks up the pivot correctly.

---

## Legacy-flag rollback window (P1–P4)

During P1–P4, `src/core/stream/legacy/*` coexists with `src/core/stream/server/*`. `CMakeLists.txt` exposes `TANKOBAN_STREAM_BACKEND` option (`legacy` | `server`). `StreamPage` picks the implementation at construction:

```cpp
#if defined(TANKOBAN_STREAM_BACKEND_SERVER)
  #include "core/stream/server/StreamEngineAdapter.h"
  using StreamEngineImpl = StreamEngineAdapter;
#else
  #include "core/stream/legacy/StreamEngine.h"
  using StreamEngineImpl = StreamEngine;
#endif
```

This lets us A/B the same magnet through both engines in a single build during bug hunts — much higher value than "git restore to undo." Primary rollback path during P1–P4 is toggling the CMake option + rebuilding. Git-restore stays as emergency escape hatch.

**Delete `legacy/` at P5** once Hemanth confirms a full week of `TANKOBAN_STREAM_BACKEND=server` with zero rollbacks.

---

## Risk surface

10 items — all must be named in the TODO so no surprises at P5 deletion.

1. **Prototype seek semantics.** stream-server's byte-range `/stream/{hash}/{idx}` may not reprioritize piece fetch on Range header re-requests the way our C++ engine did. Seek UX could regress silently. **Mitigated in P0 — seek test is mandatory.**
2. **Binary provenance / supply chain.** Third-party ~50 MB exe. Mitigated: SHA-256 pin in `STREAM_SERVER_VERSION` file, verify at launch (P1).
3. **Disk-path collision.** stream-server may hardcode `~/.stremio-server` for piece store — collides with existing Stremio Desktop install. Mitigated: P0 verifies; P1 patches via env-override if needed (OR accepts shared cache if behaviorally safe).
4. **Port 11470 collision.** Fixed port, no CLI arg. Could collide with user's Stremio Desktop install. Mitigated in P1 — launcher detects + kills prior instance + retries once + user-facing error on second fail.
5. **Poll-cadence tuning.** Edge-triggered stalls (old engine) → level-triggered polling (new). Stall latency bumps to poll interval. Mitigated in P2b — empirical tuning via A/B against legacy backend.
6. **Windows AV / SmartScreen.** Unsigned Rust exe spawn. Mitigated: document first-run Defender prompt in user docs; long-term code-sign (out of scope for this TODO).
7. **Locale / path encoding.** Non-ASCII usernames may break HTTP URL escaping. Mitigated in P2 — test with a non-ASCII user directory in P0/P2 smokes.
8. **libtorrent 1s-hang-on-shutdown.** stream-server force-exits after 1s. Our parent-process-death path must handle this. Mitigated in P1 — `StreamServerProcess::stop()` gives 1.5s grace then force-kills.
9. **Stats.json polling network overhead.** More than signal-driven C++ engine. Contributing to #5 but distinct. Mitigated in P2b — poll cadence tuning + `If-None-Match` ETag if stream-server supports it.
10. **Adapter session-filter bug class.** SidecarProcess's session-filter pattern applies — stale stats-poll responses from a prior stream must not contaminate the current session. Mitigated in P2b — session-id filter at parser layer, mirroring `SidecarProcess.h:228`.

---

## Smoke criteria (what ships per phase)

Each phase exit includes a self-smoke. Hemanth-smoke is only required at P0 (prototype ratification), P4 (Tankorent isolation), and P5 (deletion greenlight).

- **P0 self-smoke:** Invincible S01E01 cold-open + mid-file seek on pre-built binary. Agent 7 drives.
- **P1 self-smoke:** both backends build; `POST /create` + `GET /stream/{}/{}` work from command-line harness. Agent 4 drives.
- **P2 self-smoke:** full Invincible cold-open via StreamPage on `server` backend; stall + buffered-range signals fire. Agent 4 drives.
- **P3 self-smoke:** telemetry.log writes correctly; Continue Watching round-trips. Agent 4 drives.
- **P4 Hemanth-smoke:** stream Invincible + download an unrelated torrent in Tankorent tab, both work in parallel. Agent 4 drives; Agent 4B signs off.
- **P5 Hemanth-greenlight:** one full week of daily use on `server` backend, zero rollbacks, Hemanth explicit approval.

---

## Exit criteria (TODO-level close)

- All 7 phases shipped
- P5 terminal-tag `stream-server-pivot-terminal` tagged
- `src/core/stream/legacy/` deleted
- `STREAM_ENGINE_REBUILD_TODO.md` archived to `agents/_archive/todos/`
- `MEMORY.md` + `CLAUDE.md` + `STATUS.md` reflect pivot-complete
- Congress 8 §3 pairing row evolution documented in archive addendum
- 1-month post-ship zero-rollback retention

---

## Sign-off

Authored 2026-04-24 by Agent 0. Handoff from Agent 4 (chat.md 2026-04-24 15:40). Ratification pending Hemanth read-through + `ratified` / `APPROVES` / `Final Word` / `Execute` phrase. Agent 7 Trigger-B kickoff on ratification.
