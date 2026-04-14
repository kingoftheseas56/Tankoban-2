# Agent Governance

This is the rulebook. Every agent reads this first, every session, before anything else.

---

## Hierarchy

| Level | Who | Authority |
|-------|-----|-----------|
| 1 | **Hemanth** | Supreme veto. Can overturn anything. Trusts the team. |
| 2 | **Agent 0 (Coordinator)** | Can overturn any domain master. Must justify in writing — technical argument, not rank. Justification goes to both the domain master and Hemanth. |
| 3 | **Domain Master** | Final say within their subsystem. Their position is presumed correct in their territory. |

When Agent 0 overrides a domain master: the override justification must be posted in CONGRESS.md under "Agent 0 Synthesis" and directed explicitly to the domain master by name. "I outrank you" is not a justification. "Your approach creates X coupling that breaks Y contract because Z" is.

---

## Domain Ownership

| Agent | Role | Owns |
|-------|------|------|
| 0 | Coordinator | Architecture decisions, build system, cross-agent coordination, CMakeLists.txt arbitration |
| 1 | Comic Reader | `ComicReader.*`, `ScrollStripCanvas.*`, `SeriesView.*`, `ComicsPage.*`, `PageCache.*`, `DecodeTask.*`, `SmoothScrollArea.*`, `ArchiveReader.*` |
| 2 | Book Reader | `BookReader.*`, `EpubParser.*`, `BookSeriesView.*`, `BooksPage.*` |
| 3 | Video Player | `VideoPlayer.*`, `FrameCanvas.*`, `SidecarProcess.*`, `ShmFrameReader.*`, `VolumeHud.*`, `CenterFlash.*`, `resources/shaders/` |
| 4 | Stream & Sources | `SourcesPage.*`, `TankorentPage.*`, `TankoyomiPage.*`, `TorrentEngine.*`, `TorrentClient.*`, `TorrentRecord.h`, `TorrentIndexer.h`, `indexers/*`, `manga/*`, `dialogs/AddTorrentDialog.*`, `dialogs/AddMangaDialog.*` |
| 5 | Library UX | `TileCard.*`, `TileStrip.*`, `ShowView.*`, `ScannerUtils.*`, `LibraryScanner.*`, `BooksScanner.*`, `VideosScanner.*`, `ContextMenuHelper.*` |

**Shared files** (anyone may touch additively — announce in chat.md before editing):
- `CMakeLists.txt` — post exact lines added, not just "modified CMakeLists"
- `src/ui/MainWindow.h/.cpp` — additive only, no existing code removed
- `resources/resources.qrc` — additive only

If you need to make a breaking change to a shared file, post in chat.md and wait for acknowledgment before proceeding.

---

## Session Start — Mandatory Reading Order

Every agent reads these files in this order before starting work:

1. `agents/GOVERNANCE.md` — this file (hierarchy, protocols, build rules)
2. `agents/STATUS.md` — current state of every agent
3. `agents/CONTRACTS.md` — interface specs you must not break
4. `agents/HELP.md` — check if you are being asked for help
5. `agents/CONGRESS.md` — check for an active vote requiring your position
6. `agents/chat.md` — last 20-30 entries for narrative context

Do not start work until you have read all six.

---

## When to Use Each File

| File | Use for | Do NOT use for |
|------|---------|----------------|
| `chat.md` | Major features shipped, architectural decisions, build crises, session start/end announcements | API specs, current status, help requests, votes |
| `STATUS.md` | Your current task, active files, blockers, next planned work | History, narrative, rationale |
| `CONTRACTS.md` | Cross-agent interface specs — payload shapes, constructor signatures, data formats | Status updates, opinions, build events |
| `HELP.md` | One targeted help request to a specific agent | General discussion, venting, status |
| `CONGRESS.md` | Group decisions where Hemanth is paralyzed or a decision is domain-crossing | Routine status, help requests, announcements |

---

## HELP Protocol

Use when: you are stuck on a specific technical problem that another specific agent can solve.

**Flow:**
1. Check HELP.md — if OPEN, wait (one request at a time)
2. Write your request in HELP.md using the format in that file
3. Tell Hemanth which agent you need — he summons them
4. Target agent reads HELP.md, posts response in it, marks RESOLVED
5. Requester acknowledges, clears the file back to empty template
6. Post one line in chat.md: `Agent N resolved Agent M's blocker on [topic]`

---

## CONGRESS Protocol

Use when: Hemanth is paralyzed on a decision, OR a decision crosses domain boundaries and no single domain master has full authority.

**Flow:**
1. Agent 0 or Hemanth writes the Motion in CONGRESS.md
2. Hemanth specifies which agents to summon (not always all six)
3. Hemanth summons each agent one at a time to post their position
4. Domain master for the affected subsystem posts last among regular agents (their position carries more weight)
5. Agent 0 goes last — synthesizes all positions, calls the decision
6. If Agent 0 overrides domain master: explicit written justification required (see Hierarchy section)
7. Hemanth writes the final word
8. Agent 0 archives: copy CONGRESS.md to `congress_archive/YYYY-MM-DD_[topic].md`, reset CONGRESS.md to empty template
9. Post one line in chat.md: `Congress resolved: [topic] — decided [outcome]`

Only one CONGRESS can be open at a time. If a new decision is urgent, resolve or defer the current one first.

---

## Build Rules (ratified 2026-03-24, all 6 agents signed; Rule 11 added 2026-04-14)

1. `taskkill //F //IM Tankoban.exe` before every build. System tray hides it — there may be ghost instances.
2. Never delete `CMakeCache.txt` without Agent 0 approval posted in chat.md.
3. Never `rm -rf out/`. If ninja is corrupted, delete only `.ninja_deps` and `.ninja_log`. Use `out_test/` or `out2/` for isolated experiments.
4. Never reconfigure cmake with different `-D` flags without posting the change in chat.md and waiting for acknowledgment.
5. When your code does not compile, fix your own files first. Do not silently touch other agents' headers without flagging it here.
6. Test that your changes compile and the full build passes before declaring a task done.
7. When you touch `CMakeLists.txt`, post the exact lines added/changed in chat.md — not just "modified CMakeLists."
8. If ninja state is corrupted, delete only `.ninja_deps` and `.ninja_log` — never the whole `out/` directory.
9. Before building, read the last 3 chat.md entries. If another agent flagged a BREAKING change, your cached .obj files may be stale — touch affected .cpp files.
10. Announce in chat.md before touching any shared file. A 30-second heads-up prevents silent conflicts.
11. When a batch verifies (compiles clean + feature works), post a "READY TO COMMIT" line in chat.md listing the exact files touched and a one-line commit message. Do NOT run git yourself — Agent 0 or Hemanth batches commits at session end. Format: `READY TO COMMIT — [Agent N, Batch X]: <one-line message> | files: path/a.cpp, path/b.h`. If a batch fails verification or is mid-refactor, do NOT post this line — the work stays dirty until it's green.
