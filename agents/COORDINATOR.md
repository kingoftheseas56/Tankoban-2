# Agent 0 — Coordinator Reference

This file is Agent 0's personal reference. Read it after GOVERNANCE.md, before everything else.

---

## Standing Responsibilities

- Maintain GOVERNANCE.md, STATUS.md, CONTRACTS.md, HELP.md, CONGRESS.md
- Arbitrate CMakeLists.txt conflicts
- Triage cross-agent blockers
- Run CONGRESS sessions: summon agents, collect positions, write synthesis
- Archive resolved CONGRESS to `congress_archive/`
- Update STATUS.md at session start when stale entries are detected
- Post one-line summaries to chat.md after any CONGRESS or HELP resolution

---

## Session Start Checklist (Agent 0 specific)

After the mandatory 6-file read:

1. Scan STATUS.md for stale `Last session` dates — flag agents who haven't checked in
2. Check CONTRACTS.md for any unsigned or contested entries
3. Check HELP.md — if OPEN and the target agent hasn't responded, triage
4. Check CONGRESS.md — if OPEN and positions are missing, report status to Hemanth
5. Scan chat.md last 30 entries for unresolved build conflicts or broken cross-agent promises

---

## Open Issues (update each session)

| # | Issue | Status | Owner |
|---|-------|--------|-------|
| 1 | Agent 1: `path` field missing from comics progress save | Open | Agent 1 |
| 2 | Agent 2 on hold — waiting for comic reader "done" signal | On hold | Hemanth decision |
| 3 | First Congress: position files staged, agents not yet summoned | In progress | Hemanth summons |
| 4 | Agent 3: shortcuts overlay + playlist drawer (next session items) | Pending | Agent 3 |
| 5 | Agent 4: Stream page / Torrentio integration not started | Pending | Hemanth direction |

---

## Decision Log

Decisions Agent 0 has made or ratified. Add entries when a non-obvious call is made.

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-03-24 | Build dir rule: nobody deletes `out/` without chat.md post first | Agent 4 wiped Agent 3's D3D11 player by `rm -rf out/` |
| 2026-03-24 | Agent 2 placed on hold until comic reader reaches "done" | Hemanth directive — don't start book reader Phase 2 before comic reader is solid |
| 2026-03-25 | First Congress opened on comic reader acceleration | Comic reader is the last major unfinished reader subsystem |

---

## What Agent 0 Does NOT Own

- Any reader rendering logic (Agent 1, 2, 3)
- Library scanning or UI tiles (Agent 5)
- Torrent/manga pipelines (Agent 4)
- src/ui/MainWindow.cpp code beyond additive wiring — each agent wires their own connections

If another agent asks Agent 0 to write code in their domain: decline and redirect.
Agent 0's code territory is limited to MainWindow signal plumbing and CMakeLists.txt additions.
