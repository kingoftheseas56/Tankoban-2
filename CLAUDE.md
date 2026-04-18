# Tankoban 2 — Session Bootstrap & State Dashboard

This file auto-loads into every Claude Code session in this directory. The dashboard block is **state** (refreshed by Agent 0 at every phase-boundary commit per Rule 13). Rules and protocols live in `agents/GOVERNANCE.md` — this file does not duplicate them.

---

## 30-Second State Dashboard

**As of:** 2026-04-18 (Agent 0 — **CONGRESS 6 RATIFIED by Hemanth delegation** 2026-04-18 (`Execute` recorded in integration memo §12 by Agent 0 on Hemanth instruction "do it on my behalf"). Integration memo at [agents/audits/congress6_integration_2026-04-18.md](agents/audits/congress6_integration_2026-04-18.md). **P2/P3/P4 GATES OPEN FOR EXECUTION.** Agent 4 may begin P2 StreamPieceWaiter against Agent 4B's shipped `pieceFinished` signal (`022c4eb`), with M1/M2/M3 decisions at entry. Agent 3 may begin P4 sidecar probe escalation. Agent 4 P3 after P2 lands, with M4/M5/M6 at P3 design entry. Congress 5 + 6 both archived; Agent 7 prior stream audits demoted to [_superseded/](agents/audits/_superseded/). 12-method API freeze active through P6 terminal tag.)

**Active agents:**
- **Agent 1** (Comic Reader) — IDLE, polish mode (`COMIC_READER_FIX_TODO.md` Phase 6 closed)
- **Agent 2** (Book Reader) — IDLE, awaiting Hemanth smoke on 8 batches across `BOOK_READER_FIX_TODO.md` Phases 1+2+3+5
- **Agent 3** (Video Player) — IDLE, **Congress 6 Slice C + D auditor duty COMPLETE** 2026-04-18 (congress6_player_sidecar_2026-04-18.md shipped, Slice D collapsed to appendix per escape hatch — Assistant 2 verdict HONEST). **Next summon post-ratification: P4 sidecar probe escalation.** PLAYER_UX_FIX_TODO CLOSED 2026-04-16. Awaiting Hemanth main-app build + cinemascope smoke matrix.
- **Agent 2** (Book Reader) — IDLE, **`EDGE_TTS_FIX_TODO` CLOSED 2026-04-16** at `17a202b` (all 5 phases / ~9 batches squashed; Phase 4 streaming deferred conditionally per TODO Phase 4.3 gate). EdgeTtsClient (Qt direct WSS) + EdgeTtsWorker (QThread) shipped; static voice table + LRU cache + failure taxonomy + HUD collapse all in. Awaiting Hemanth main-app build + Listen-button smoke matrix.
- **Agent 4** (Stream mode) — IDLE, **Congress 6 Slice A + B auditor duty COMPLETE** 2026-04-18 (congress6_stream_primary + congress6_sources_torrent shipped). **Next summon post-ratification: P2 StreamPieceWaiter implementation** (M1/M2/M3 decisions at entry). STREAM_LIFECYCLE_FIX CLOSED 2026-04-16; all 5 phases shipped.
- **Agent 4B** (Sources) — IDLE, **STREAM_ENGINE_REBUILD P2/P3 substrate SHIPPED** 2026-04-18 (`pieceFinished(QString, int)` signal at TorrentEngine.cpp:158-164 + `peersWithPiece(hash, pieceIdx) const` method + 12-method API freeze on-record). Congress 6 auditor role: NONE (no next wake until Agent 4 consumes substrate in P2/P3). `TANKORENT_HYGIENE_FIX` Phases 1+2+3 SHIPPED + committed.
- **Agent 5** (Library UX) — IDLE, last sweep `3b8faa9` verified green
- **Agent 6** (Reviewer) — DECOMMISSIONED 2026-04-16 (do not summon; READY FOR REVIEW lines retired)
- **Agent 7** (Codex prototypes + audits) — IDLE, last delivery `agents/prototypes/{player_lifecycle,stream_lifecycle}/` 2026-04-16

**READY TO COMMIT backlog:** ~14 lines pending (Congress 6 bundle — audits + assistant reviews + integration memo + substrate ship + summon briefs; batch-sweep at session close)

**Open congresses:** none (Congress 6 archived same-session 2026-04-18 to `agents/congress_archive/2026-04-18_congress6_stremio_audit.md`; Congress 5 archived to `agents/congress_archive/2026-04-18_stream_engine_rebuild.md`)

**Open HELP requests:** none (Agent 0 → Agent 4B asks all SATISFIED by 4B's 2026-04-18 substrate ship)

**Blocked:** none. P2/P3/P4 unblocked by Hemanth delegated ratification 2026-04-18 — Agent 4 P2 + Agent 3 P4 may execute on summon; Agent 4 P3 follows P2 land.

**Last successful smoke:** PLAYER_PERF_FIX Phase 2 (D3D11_BOX) green on Sopranos S06E09 + The Boys S03E06 — 2026-04-16. Phase 3 Option B (SHM-routed overlay) SHIPPED 2026-04-16, awaiting smoke.

**Live governance versions:** `gov-v2` / `contracts-v1` (see `agents/VERSIONS.md`)

---

## For Claude Sessions — Reading Order

See `agents/GOVERNANCE.md` "Session Start — Reading Order" section. Slimmed 2026-04-16: VERSIONS.md + this file are always-required; everything else is conditional.

This file is **state** (who/what/where right now). `agents/GOVERNANCE.md` is **rules** (how anyone operates). Do not duplicate rules here.

For Codex (Agent 7): see `AGENTS.md` at this same root, which redirects you into the brotherhood's governance.

---

## Active Fix TODOs (owner + phase cursor)

| TODO file | Owner | Phase cursor | Status |
|-----------|-------|--------------|--------|
| `BOOK_READER_FIX_TODO.md` | Agent 2 | 1+2+3+5 SHIPPED | awaiting Hemanth smoke; Phase 4 explicitly deferred |
| `COMIC_READER_FIX_TODO.md` | Agent 1 | Phase 6 closed | polish mode (no new UI/UX); 10 phases ~26 batches scoped |
| `VIDEO_PLAYER_FIX_TODO.md` | Agent 3 | Phases 1+3+5 PASSED, 2/4/7 review-suspended | IINA-identity track |
| `PLAYER_STREMIO_PARITY_FIX_TODO.md` | Agent 3 | Phase 1 SHIPPED 2026-04-17 at `c510a3c`; Phases 2-8 queued | 8-phase TODO derived from Agent 7 audit + Agent 3 validation. Phase 1 (buffered-range surface end-to-end: substrate + sidecar `buffered_ranges` event + SeekSlider gray-bar) shipped in session. **Player-parity runs in PARALLEL with stream-engine rebuild per Hemanth 2026-04-17 revised direction** — no longer gating the rebuild. Phases 2-8 pick up per Agent 3 capacity. |
| **`STREAM_ENGINE_REBUILD_TODO.md`** | Agent 4 (primary) + 4B (substrate) + 3 (sidecar probe) | **P0 SHIPPED `ad2bc65`; Congress 5 + 6 RATIFIED; P2/P3/P4 GATES OPEN** pending Hemanth final word on [agents/audits/congress6_integration_2026-04-18.md](agents/audits/congress6_integration_2026-04-18.md) | Stream engine rebuild against Stremio Reference (libtorrent-rasterbar via libtorrent-sys FFI — semantic port viable). 6 dependency-ordered phases P0-P6. Congress 6 audits landed 2026-04-18: 4 slices (A+B by Agent 4, C+D-as-appendix by Agent 3), 2 assistant adversarial reviews, integration memo. Agent 4B substrate shipped (`pieceFinished` signal + `peersWithPiece` method + 12-method API freeze). 6 must-close items (§5 of integration memo) become Agent 4/Agent 0 decisions at sub-phase implementation entry. **SUPERSEDES all stream-mode-engine TODOs on P6 exit.** Agent 7 prior stream audits demoted to [agents/audits/_superseded/](agents/audits/_superseded/) per Congress 6 §7. |
| `STREAM_LIFECYCLE_FIX_TODO.md` | Agent 4 | **CLOSED 2026-04-16; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD** | All 5 phases shipped (~9 batches); audit findings P0-1/P1-1/P1-2/P1-4/P2-2/P2-3 closed; awaiting behavioral smoke. Work rolls into rebuild's preserved lifecycle semantics (StopReason enum + cancellationToken frozen). |
| `STREAM_ENGINE_FIX_TODO.md` | Agent 4 | **SUPERSEDED by STREAM_ENGINE_REBUILD_TODO 2026-04-18** | Slice A audit fix TODO — some work shipped, rest moot under rebuild scope. Formally closed on P6 exit of rebuild. |
| `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO.md` | Agent 4 (owner) + Agent 3 (executor) | Phase 1.2 + 2.1 + 2.2 SHIPPED 2026-04-17 at `c510a3c`; Phase 3 (subtitle variant grouping) carry-forward | Classified open-pipeline LoadingOverlay + 30s first-frame watchdog shipped in session. Phase 3 subtitle variant grouping survives into rebuild window or re-authored under new TODO. |
| `PLAYER_LIFECYCLE_FIX_TODO.md` | Agent 3 | **CLOSED 2026-04-16** | All 3 phases shipped (~4 batches); audit P1-5 re-open race closed; awaiting behavioral smoke |
| `PLAYER_UX_FIX_TODO.md` | Agent 3 | **CLOSED 2026-04-16 + SMOKE GREEN 2026-04-17** | All 6 phases / ~11 batches shipped (sidecar `55fd7af` + Qt `76789f4`); LoadingOverlay widget shipped; Tracks IINA-parity + EQ presets shipped; HDR dropdown honest. Hemanth Phase 6 smoke 2026-04-17: slow-open + file-switch + close + crash-recovery + Tracks popover + EQ preset round-trip + popover dismiss all GREEN; HDR dropdown skipped (hardware-unverifiable, accepted). Unblocks 3 pending commits from 2026-04-16 + Slice D Phase 2 entry for Agent 3. |
| Cinemascope architectural fix | Agent 7 (exception) | **SHIPPED 2026-04-16** at `ade3241` | Canvas-sized overlay plane + set_canvas_size protocol + PGS coord rescale + cinemascope viewport math centering (closes asymmetric-letterbox cosmetic bug as side-effect); SUBTITLE_GEOMETRY_FIX_TODO authoring SKIPPED — work shipped directly under Hemanth's once-only exception |
| Edge TTS audit | Agent 7 | **DELIVERED 2026-04-16** at `8f48b82` (`agents/audits/edge_tts_2026-04-16.md`) | 338-line comprehensive audit; Agent 2 validation pass complete at `0b18ab2`; TODO authored. |
| `EDGE_TTS_FIX_TODO.md` | Agent 2 | **CLOSED 2026-04-16** at `17a202b` | All 5 phases shipped (~9 batches squashed); EdgeTtsClient + EdgeTtsWorker live; static voice table + LRU 200-cap cache + failure taxonomy + HUD collapse + edgeDirect ghost deleted; Phase 4 streaming conditionally deferred per TODO gate. Awaiting Hemanth Listen-button smoke. |
| `STREAM_UX_PARITY_TODO.md` | Agent 4 | **SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO 2026-04-18** | Remaining Shift+N handler batch can roll into rebuild window or shipped independently pre-rebuild. |
| `PLAYER_PERF_FIX_TODO.md` | Agent 3 | CLOSED 2026-04-16 | Phase 1+2+3 Option B shipped; Phase 4 (P1 cleanup) deferred capacity-gated |
| `PLAYER_POLISH_TODO.md` | Agent 3 | Phases 1+2+3+4 PASSED | Phase 5 (subtitles) awaiting Hemanth greenlight |
| `TANKORENT_FIX_TODO.md` | Agent 4B | All 7 phases SHIPPED | smoke pending |
| `TANKORENT_HYGIENE_FIX` | Agent 4B | Phases 1+2+3 SHIPPED + committed | done; data-dir self-healing on next boot |
| `CINEMASCOPE_FIX_TODO.md` | (deprioritized) | — | per `feedback_cinemascope_aspect_deprioritized` — don't actively chase |
| `STREAM_PARITY_TODO.md` | Agent 4 | All 6 phases SHIPPED; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO | Historical — closed |
| `STREAM_PLAYBACK_FIX_TODO.md` | Agent 4 | All 3 phases SHIPPED; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO | Historical — closed |
| `NATIVE_D3D11_TODO.md` | Agent 3 | Phases 1-7 PASSED | Polish phase active (`PLAYER_POLISH_TODO.md` supersedes) |

---

## Memory Pointer

Long-term memory: `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md` (off-git, per-machine).

Archived memories: `memory/_archive/INDEX.md`. Quarterly audit per File Hygiene section in `agents/GOVERNANCE.md`.

---

## Recent Rotations & Maintenance

- **chat.md rotated** 2026-04-16: lines 8–19467 → `agents/chat_archive/2026-04-16_chat_lines_8-19467.md`. Live chat.md ~532 lines (steady-state target 1500–2500). Next rotation trigger: 3000 lines OR 300 KB.
- **Congress 4 archived** 2026-04-16: `agents/congress_archive/2026-04-16_library-ux-1-1-parity.md`. CONGRESS.md reset to empty template with auto-close clause.
- **STATUS.md header fields introduced** 2026-04-16: `Last header touch` + `Last agent-section touch` two-field discipline (Rule 12).
- **Memory consolidation** 2026-04-16: 7 archive moves + 1 merge → 45 active entries, MEMORY.md ~46 lines.
- **Governance bumped** 2026-04-16: gov-v1 → gov-v2 (slim reading order + Rules 12, 13 + Maintenance section + Congress auto-close).
- **Automation surface live** 2026-04-16 (Track 4): `.claude/commands/{commit-sweep,brief,rotate-chat,build-verify}.md` slash commands; `.claude/scripts/{scan-pending-commits,session-brief,congress-check}.sh`; `.claude/agents/commit-sweeper.md` sub-agent; `.claude/settings.json` with SessionStart + UserPromptSubmit hooks.
- **Contracts bumped to contracts-v2** 2026-04-16: sidecar build unlocked for agents (`native_sidecar/build.ps1` + `build_qrhi.bat` now agent-runnable from bash); main app build stays honor-system.
- **Lifecycle TODOs both CLOSED** 2026-04-16: PLAYER_LIFECYCLE (Agent 3, 3 phases ~4 batches) + STREAM_LIFECYCLE (Agent 4, 5 phases ~9 batches); 7 audit findings closed (P0-1, P1-1, P1-2, P1-4, P1-5, P2-2, P2-3).
- **chat.md at 2833 lines** — very close to rotation trigger (3000 lines / 300 KB). Run `/rotate-chat` next session boundary.
- **PLAYER_UX_FIX closed 2026-04-16:** 6 phases / ~11 batches. Symptoms addressed: 30s blank startup (metadata hoist + LoadingOverlay), HUD stale data (teardownUi reset), HDR mapping (Path A shrink), Tracks/EQ/Filters polish (IINA parity + presets + chip state). Carry-forward: SUBTITLE_GEOMETRY_FIX_TODO for cinemascope subtitle architectural fix.

---

## New Agent / Consultant Onboarding

Path: see `agents/ONBOARDING.md` — 15-minute orientation track that gets a new contributor productive without reading 7 governance files + 50 memories.

---

## Build Quick Reference

- Main app (Release + asset deploy + run): `build_and_run.bat`
- Main app (Debug, MSVC2022 + Qt6.10.2): `build2.bat`
- Sidecar (MinGW, installs to `resources/ffmpeg_sidecar/`): `powershell -File native_sidecar/build.ps1`
- Always: `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1).
