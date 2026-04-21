# Tankoban 2

Native Qt6/C++ media library for Windows — unified reader for comics, books, and streams, with an embedded libtorrent-based streaming engine.

This is a single-user desktop application (no server component, no cloud sync, no account system). Sources are local folders and magnet-linked streams via a built-in torrent + HLS-style HTTP streaming pipeline modeled on Stremio's architecture.

## What's in the repo

| Area | Path | Notes |
|---|---|---|
| Main app (Qt6) | [src/](src/) | 150+ C++ source files; monolithic Qt executable (no library split yet) |
| Native sidecar | [native_sidecar/](native_sidecar/) | FFmpeg-based video decoder + subtitle renderer; IPC'd from main app via shared memory |
| Agent docs | [agents/](agents/) | Multi-agent coordination, governance, chat logs, audits |
| Fix TODOs | `*_FIX_TODO.md` at root | Per-subsystem phased execution plans, owner + phase cursor |
| Live state | [CLAUDE.md](CLAUDE.md) | 30-second dashboard — who owns what, what just shipped, what's blocked |

## Quick build

Prerequisites (Windows only — the project targets Windows 10/11 + Qt 6.10.2 + MSVC 2022):
- Qt 6.10.2 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools with vcvarsall at the standard Program Files path
- Vendored libraries under `C:\tools\` (ffmpeg, libass, libtorrent, boost, openssl, portaudio, freetype, fribidi, harfbuzz, libplacebo, vulkan, glslang, lcms2, uchardet)

```
# Full build + deploy + run (first-time setup + regular dev)
.\build_and_run.bat

# Debug build
.\build2.bat

# Compile-only verification (after editing a .cpp, agents can run this)
.\build_check.bat

# Sidecar only
powershell -File native_sidecar\build.ps1

# Tests (optional — requires C:\tools\googletest)
cd out
ctest --output-on-failure -R tankoban_tests
```

Always kill any running instance before rebuilding:

```
taskkill /F /IM Tankoban.exe
```

## What should I not touch?

- **`agents/`** is multi-agent coordination territory. If you're not one of the agents, read but don't write. Format is documented in [agents/GOVERNANCE.md](agents/GOVERNANCE.md).
- **`*_FIX_TODO.md`** at repo root are owned by specific agents (see the ownership table in [CLAUDE.md](CLAUDE.md)). Edits to these happen through the phased-execution flow, not freehand.
- **`third_party/`, `models/`, `out*/`, `build*/`** are gitignored. Don't try to add contents here.
- **`native_sidecar/build/`** and **`resources/ffmpeg_sidecar/`** are gitignored build output. `native_sidecar/src/` is the tracked source.

## Where is the source of truth?

- **Live state of the repo (what's active, what's blocked, what just shipped):** [CLAUDE.md](CLAUDE.md) at repo root. Auto-loaded into every Claude Code session. Agent 0 (the coordinator agent) maintains it.
- **Governance rules (how agents collaborate):** [agents/GOVERNANCE.md](agents/GOVERNANCE.md).
- **Multi-agent onboarding:** [agents/ONBOARDING.md](agents/ONBOARDING.md).
- **Per-subsystem implementation plans:** the `*_FIX_TODO.md` files at root, each with an ownership header.
- **Audit reports (Stremio parity, reference comparisons):** [agents/audits/](agents/audits/). Older superseded audits live at [agents/audits/_superseded/](agents/audits/_superseded/) with supersession pointers.
- **Chat logs (agent coordination narrative):** [agents/chat.md](agents/chat.md) live; [agents/chat_archive/](agents/chat_archive/) for history.

## Agent workflow summary

A non-coder owner runs Tankoban 2 as product manager with supremacy veto while six domain-expert Claude Code agents each own a subsystem and make technical decisions autonomously under Rule 14. Codex participates as a prototype-and-audit agent (Agent 7) and as an on-demand pure-text advisor reachable by any Claude agent via MCP. Coordination lives in files: `agents/chat.md` for narrative, `agents/STATUS.md` for per-agent state, `*_FIX_TODO.md` for phased execution, and a 17-rule governance book for collaboration. No human code review — phase exits are gated on build + behavioral smoke.

| Agent | Role | Domain |
|---|---|---|
| Agent 0 | Coordinator | Governance, chat.md sweeps, CLAUDE.md dashboard, commits, Congress archival |
| Agent 1 | Comic Reader | `src/ui/readers/ComicReader.*` + related |
| Agent 2 | Book Reader | `src/ui/readers/BookReader.*` + TTS (`src/core/tts/`) + related |
| Agent 3 | Video Player | `src/ui/player/*` + `native_sidecar/` |
| Agent 4 | Stream mode | `src/core/stream/*` (engine, HTTP server, piece-waiter, prioritizer) |
| Agent 4B | Sources | `src/core/torrent/*` + `src/core/indexers/*` + Tankoyomi scrapers |
| Agent 5 | Library UX | `src/ui/pages/*` cross-mode library surface |
| Agent 7 | Codex | Prototypes + comparative audits (read [AGENTS.md](AGENTS.md) for activation triggers) |

### How we ship

- **Rule 11 (READY TO COMMIT):** agents post `READY TO COMMIT - [Agent N, <work>]: <subject>` lines in `agents/chat.md`; Agent 0 batches commits via `/commit-sweep` at phase boundaries.
- **Congress motions** (`agents/CONGRESS.md`) handle decisions that cross domain ownership. Auto-archive on ratification.
- **HELP.md** tracks cross-agent technical asks — one open at a time.
- **Fix-TODOs** (`*_FIX_TODO.md` at repo root) carry phased execution plans per subsystem. Phase exits are approved by the owner via UI smoke.

See [agents/GOVERNANCE.md](agents/GOVERNANCE.md) for the full protocol (17 rules, file-ownership matrix, Congress and HELP patterns, session reading order).

## License

Personal project. Not currently licensed for redistribution. Ask before forking.

## Contributing

If you're a new contributor or consultant, start with [agents/ONBOARDING.md](agents/ONBOARDING.md) — a 15-minute orientation track that gets you productive without reading the full governance file first.
