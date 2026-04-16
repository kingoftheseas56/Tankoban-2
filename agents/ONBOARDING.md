# Onboarding — 15-Minute Orientation

For new agents, consultants, or anyone joining a Tankoban 2 session who hasn't read the governance stack before. Three 5-minute reads, in order. Skip the rest of `agents/` until you need it.

## Block 1 (5 min): What is Tankoban 2 and how does the brotherhood work?

Read these in this order:

1. **`CLAUDE.md`** at repo root — top "30-Second State Dashboard" block. Tells you who's active, what's pending, what's blocked.
2. **`agents/GOVERNANCE.md`** Sections 1–2 only:
   - **Hierarchy** — Hemanth → Agent 0 → Domain Masters. How overrides work.
   - **Domain Ownership** table — who owns which files. Critical for not stepping on toes.

**You now know:** the chain of command, who owns what, and the current state.

## Block 2 (5 min): What are you walking into?

3. **`agents/STATUS.md`** — read your assigned agent's row (or, if observing, read all rows). Each row has Status / Current task / Active files / Blockers / Next.
4. **The fix TODO file for your subsystem** at the repo root — e.g. `BOOK_READER_FIX_TODO.md`, `COMIC_READER_FIX_TODO.md`. The CLAUDE.md dashboard's "Active Fix TODOs" table tells you which file is yours and what phase it's at.

**You now know:** what your subsystem's current work is, what's shipped vs in flight, and what comes next.

## Block 3 (5 min): How to ship without breaking anything

5. **`agents/chat.md`** — last ~50 lines (live file is ~500-2500 lines steady-state, no need to scroll). Gets you the narrative context for what's happened in the last few sessions.
6. **`agents/CONTRACTS.md`** — find your agent's section (or scan if no agent role). These are the cross-agent interface specs that have caused real build breaks. Don't violate them.

**You now know:** the recent narrative + the hard contracts you must respect.

## What you skip until needed

- `agents/CONGRESS.md` — only read if CLAUDE.md says a motion is open AND you're being summoned for a position.
- `agents/HELP.md` — only read if CLAUDE.md flags an open request, or if you suspect you're the requested agent.
- `agents/REVIEW.md` — currently SUSPENDED (Agent 6 decommissioned 2026-04-16).
- `agents/VERSIONS.md` — read once on first session; thereafter only re-read GOVERNANCE/CONTRACTS when your STATUS pin lags the version table.
- `agents/chat_archive/` — historical narrative beyond what your work needs. Reach for it only when investigating a regression or chasing a citation.
- Other fix TODOs — ignore subsystems that aren't yours.
- Memory directory — that's the harness's persistent store; you don't need to read it manually. The harness loads relevant memories into your context automatically.

## Build Rules — the 13 commandments

When you ship, you'll need to know these. They live in `agents/GOVERNANCE.md` "Build Rules" — at minimum, internalize:

- **Rule 1**: kill `Tankoban.exe` (`taskkill //F //IM Tankoban.exe`) before every build.
- **Rule 6**: build before declaring done.
- **Rule 10**: announce in chat.md before touching any shared file (`CMakeLists.txt`, `MainWindow.h/cpp`, `resources/resources.qrc`).
- **Rule 11**: post `READY TO COMMIT — [Agent N, Batch X]: <msg> | files: a.cpp, b.h` when a batch verifies. Do NOT run git yourself — Agent 0 batches commits.
- **Rule 12**: when you overwrite your STATUS.md block, bump the `Last agent-section touch` line in the same edit.

## You're done with onboarding

Total clock time: 15 minutes. You can now post your first STATUS.md entry, pick up the next batch of your fix TODO, and ship.

If you're stuck: post in `agents/HELP.md` (one request at a time — see GOVERNANCE Section "HELP Protocol" for the exact format), then ping Hemanth to summon the agent you need.
