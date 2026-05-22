# Stream UI Domain — Agent 4

This file auto-loads when any agent reads a file under `src/ui/pages/stream/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Part of the path-scoped guidance migration (same MVP wave as `src/ui/pages/comics/CLAUDE.md`).

Sibling file at `src/core/stream/CLAUDE.md` carries the stream-server + progress layer. Torrent persistence layer at `src/core/torrent/CLAUDE.md`. Tankorent page UI at `src/ui/pages/tankorent/CLAUDE.md`.

## Domain owner

**Agent 4** (Stream mode + Tankorent). Tankorent ownership inherited from Agent 4B on 2026-05-20. Agent 4B departed the brotherhood 2026-05-20 — *"It is a sad day for us but a new beginning."* Every file in this tree that predates 2026-05-20 carries 4B's hand somewhere in its ancestry. Honor it.

## Active arcs

**THEATRE_DOWNLOAD_OVERHAUL** — Stremio-style Netflix-style in-library download flow. Brainstorm + Codex-expand + 22-task plan all shipped. Phases A+B+C (9 of 22 tasks) shipped 2026-05-16. `PackListItem`, `TorrentPackPicker`, `TheatreDownloadPanel` are the core new widgets from this arc. Phase D is EpisodeTile work.

**STREAM_SERVER_PIVOT** — off libtorrent C++ engine onto Stremio's Rust `stream-server` binary as a subprocess + REST adapter (see `src/core/stream/stremio/`). P0+P1+P2A+P2B GREEN. Legacy-flag rollback window: `TANKOBAN_STREAM_BACKEND={legacy,server}` CMake option gated through P4.

**TORRENT_PERSISTENCE_COLLAPSE** (in flight, Agent 4) — SQL persistence layer replacing the in-memory `m_records` QHash. TorrentRepository, TorrentRow POD, LegacyImporter skeleton all shipped (see `src/core/torrent/CLAUDE.md` for the detail). P5.1 (m_records.contains → m_repo.hasTorrent cutover) shipped 2026-05-21. P5.2–P5.5 in flight.

## Key widgets in this directory

- `StreamDetailView` — the ShowView equivalent for stream mode; series + episode list; blueprint for `COMICS_TANKOYOMI_STREAM_MERGER` shape. Agent 1 reads this as the architectural reference.
- `StreamHomeBoard` + `CatalogBrowseScreen` — search → results flow; Stremio-style card grid
- `TorrentPackPicker` + `PackListItem` — THEATRE_DOWNLOAD_OVERHAUL pack-picker layer
- `TheatreDownloadPanel` — in-flight download management; Netflix-style
- `StreamSourceList` + `StreamSourceCard` + `StreamSourceChoice` — Sources sidebar integration (mirrors Comics `ComicsSourcesPanel` shape)
- `AddonManagerScreen` + `AddonDetailPanel` — Stremio addon registry UX
- `EpisodeTile` — per-episode tile inside StreamDetailView; Phase D target
- `StreamPlayerController` — launch-player coordination from stream context; bridges to `SidecarProcess` (Agent 3 surface — do not own it)

## Reference apps

- **Stremio** — the primary reference for all Stream mode shape decisions. Cards, series view, addon registry, in-library downloads. `project_stream_server_pivot.md` memory carries the pivot rationale.
- **Nuvio** — HTTP-only ExoPlayer reference at `Downloads\NuvioMobile-cmp-rewrite\`. See `project_nuvio_reference.md` for on-disk path. Check it for streaming-UX patterns before authoring new stream-side UI.
- **Stream mode is the BLUEPRINT** for `COMICS_TANKOYOMI_STREAM_MERGER` (Agent 1) and `BOOKS_STREMIO_PIVOT` (Agent 2). If Agent 1 or Agent 2 reads a file here, they're doing architectural archaeology — cross-domain audit, not ownership. Cross-domain edits without Agent 4 sign-off are a Rule 14 violation.

## Load-bearing memories (read when touching this domain)

- `project_stream_server_pivot.md` — pivot rationale; P0-P2 Green; legacy-flag rollback window
- `project_theatre_download_overhaul_kickoff.md` — 22-task plan ship; brainstorm + Codex-expand shape
- `project_theatre_download_overhaul_phase_abc_shipped.md` — PackList state functionally complete; Phase D EpisodeTile is next
- `project_tankorent_stream_integration_closed.md` — search functionality merged INTO Stream mode (TANKORENT_STREAM_INTEGRATION CLOSED 2026-05-16; Daredevil S2 Hemanth-verified)
- `project_tankorent_as_foundation_vision.md` — Tankorent indexer chip is core permanent infra; Stream mode is the primary consumer of search results
- `project_agent4b_departure_2026-05-20.md` — honor 4B's hand in files that predate 2026-05-20
- `feedback_session_lifecycle_pattern.md` — intermittent stream playback failure = session-lifecycle race 99% of the time; check stale state before anything else
- `feedback_smoke_on_failing_streams.md` — smoke on streams that REPRODUCE the bug; Invincible S04E01 c38beda7 = 14:05 stall repro corpus
- `feedback_plan_first_zero_errors.md` — non-trivial tasks (≥50 LOC OR multi-file OR new behavior): invoke `/superpowers:writing-plans` FIRST

## Dev-bridge surface (Agent 4's commands)

Agent 4 owns `stream-*` tankoctl prefix (v1.1 — 11 commands):
- `stream-get-state` / `stream-get-torrents` / `stream-get-library` / `stream-get-downloads`
- `stream-get-bulk-groups` / `stream-search` / `stream-dispatch-episode` / `stream-dispatch-season`
- `stream-dump-ui`
- `events-tail` → watches `out/events.jsonl` structured event log (rotation on size)

Plus cross-mode `library-*` commands for stream-mode tile state. Full catalog enumerable via `out\tankoctl.exe ping`.

SidecarProcess IPC is **Agent 3's surface** — `StreamPlayerController` uses it but does not own it. If a stream smoke requires SidecarProcess-level inspection, coordinate with Agent 3 or use `tankoctl sidecar-*` commands (v1.7, code-green-smoke-deferred at `a3c0633`).

## Build / MCP lane discipline (gov-v7)

Stream smokes typically claim BOTH `build` lane (CMake rebuild is heavy — ~915s link step per `feedback_tankoban_build_link_dominates.md`) and sometimes `mcp` lane for ShowView / pack-picker UI interactions. Use lease registry per Rules 19 + 22 (`out\tankoctl.exe lease-get <lane>` is source of truth as of gov-v7). Companion `## BUILD LANE` + `## MCP LANE` headings in chat.md are still required for human-readable narrative.

## Activation contract

Auto-loads when any agent reads a file under `src/ui/pages/stream/`. Treat as ambient context: who owns this code, what arcs are in flight, which memories are load-bearing, which dev-bridge commands belong here.

If you are Agent 4: home turf, redundant reminder.

If you are Agent 1 or Agent 2 doing architectural archaeology: this orients you on whose hand you are reading. `StreamDetailView` is the canonical blueprint for the series-view shape both COMICS_TANKOYOMI_STREAM_MERGER and BOOKS_STREMIO_PIVOT are mirroring. Read Agent 4B's files with respect.
