# Tankoban 2 — Session Bootstrap & State Dashboard

This file auto-loads into every Claude Code session in this directory. The dashboard block is **state** (refreshed by Agent 0 at every phase-boundary commit per Rule 13). Rules and protocols live in `agents/GOVERNANCE.md` — this file does not duplicate them.

---

## 30-Second State Dashboard

**As of:** 2026-04-16 (Agent 0 — workflow optimization Tracks 1+2+3+4 all SHIPPED; Agent 3 Option B PERF 3.B SHM-routed swept)

**Active agents:**
- **Agent 1** (Comic Reader) — IDLE, polish mode (`COMIC_READER_FIX_TODO.md` Phase 6 closed)
- **Agent 2** (Book Reader) — IDLE, awaiting Hemanth smoke on 8 batches across `BOOK_READER_FIX_TODO.md` Phases 1+2+3+5
- **Agent 3** (Video Player) — IDLE, `PLAYER_PERF_FIX` CLOSED 2026-04-16 (Phase 1+2+3 Option B shipped, Phase 4 deferred). Next up: `PLAYER_LIFECYCLE_FIX` Phase 1 (sidecar sessionId filter, Agent 7 prototype at `agents/prototypes/player_lifecycle/Batch1.1_SidecarProcess_sessionId_filter.cpp`).
- **Agent 4** (Stream mode) — ACTIVE, `STREAM_UX_PARITY_TODO.md` Phase 2 (Batch 2.5 shipped, 2.6 = Shift+N player shortcut pending)
- **Agent 4B** (Sources) — IDLE, `TANKORENT_HYGIENE_FIX` Phases 1+2+3 SHIPPED + committed
- **Agent 5** (Library UX) — IDLE, last sweep `3b8faa9` verified green
- **Agent 6** (Reviewer) — DECOMMISSIONED 2026-04-16 (do not summon; READY FOR REVIEW lines retired)
- **Agent 7** (Codex prototypes + audits) — IDLE, last delivery `agents/prototypes/{player_lifecycle,stream_lifecycle}/` 2026-04-16

**READY TO COMMIT backlog:** 0 lines (last sweep commit `df73419`)

**Open congresses:** none (Congress 4 archived to `agents/congress_archive/2026-04-16_library-ux-1-1-parity.md` this session)

**Open HELP requests:** none

**Blocked:** none

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
| `STREAM_UX_PARITY_TODO.md` | Agent 4 | Phase 2 batches 2.1-2.5 shipped | Batch 2.6 (Shift+N player shortcut) pending; needs Agent 3 heads-up |
| `STREAM_LIFECYCLE_FIX_TODO.md` | Agent 4 | queued — gated on Agent 3 PLAYER_LIFECYCLE Phase 1 sessionId contract | 5 phases ~11 batches scoped (PlaybackSession + source-switch reentrancy); Agent 7 prototypes at `agents/prototypes/stream_lifecycle/` |
| `PLAYER_LIFECYCLE_FIX_TODO.md` | Agent 3 | Phase 1 starting (contract gate for Agent 4) | 3 phases ~5 batches (sidecar sessionId filter + open/stop fence + VideoPlayer stop identity); Agent 7 prototype available |
| `PLAYER_PERF_FIX_TODO.md` | Agent 3 | CLOSED 2026-04-16 | Phase 1+2+3 Option B shipped; Phase 4 (P1 cleanup) deferred capacity-gated |
| `PLAYER_POLISH_TODO.md` | Agent 3 | Phases 1+2+3+4 PASSED | Phase 5 (subtitles) awaiting Hemanth greenlight |
| `TANKORENT_FIX_TODO.md` | Agent 4B | All 7 phases SHIPPED | smoke pending |
| `TANKORENT_HYGIENE_FIX` | Agent 4B | Phases 1+2+3 SHIPPED + committed | done; data-dir self-healing on next boot |
| `CINEMASCOPE_FIX_TODO.md` | (deprioritized) | — | per `feedback_cinemascope_aspect_deprioritized` — don't actively chase |
| `STREAM_PARITY_TODO.md` | Agent 4 | All 6 phases SHIPPED | closed |
| `STREAM_PLAYBACK_FIX_TODO.md` | Agent 4 | All 3 phases SHIPPED | closed |
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

---

## New Agent / Consultant Onboarding

Path: see `agents/ONBOARDING.md` — 15-minute orientation track that gets a new contributor productive without reading 7 governance files + 50 memories.

---

## Build Quick Reference

- Main app (Release + asset deploy + run): `build_and_run.bat`
- Main app (Debug, MSVC2022 + Qt6.10.2): `build2.bat`
- Sidecar (MinGW, installs to `resources/ffmpeg_sidecar/`): `powershell -File native_sidecar/build.ps1`
- Always: `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1).
