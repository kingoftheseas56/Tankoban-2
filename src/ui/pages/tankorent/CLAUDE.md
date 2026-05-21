# Tankorent UI Domain — Agent 4

This file auto-loads when any agent reads a file under `src/ui/pages/tankorent/` (Claude Code nested CLAUDE.md behavior). Part of the path-scoped guidance migration (same MVP wave as `src/ui/pages/comics/CLAUDE.md`).

Engine layer at `src/core/torrent/CLAUDE.md`. Stream UI (where Tankorent search results now surface) at `src/ui/pages/stream/CLAUDE.md`.

## Domain owner

**Agent 4** (Stream mode + Tankorent). Tankorent page ownership inherited from Agent 4B on 2026-05-20. Every file here was 4B-authored. Honor the hand. See `project_agent4b_departure_2026-05-20.md`.

## What this directory is today

The Tankorent tab UI — torrent detail tabs (`TorrentGeneralTab`, `TorrentTrackersTab`, `TorrentPeersTab`, `TorrentFilesTab`) + `TorrentPropertiesWidget` — is the *standalone* Tankorent page surface. **The search functionality that originally lived in `TankorentPage` has already merged INTO Stream mode** (`project_tankorent_stream_integration_closed.md` — CLOSED 2026-05-16, Hemanth-verified Daredevil S2 with 11 real packs).

Think of this directory as the "detail inspector" side of Tankorent — what you see when you click into a specific torrent to examine trackers, peers, file listing, and general stats. The discovery + search surface now lives in `src/ui/pages/stream/`.

As of 2026-05-21, `TankorentPage` itself (`src/ui/pages/TankorentPage.{h,cpp}`) was rewired: it is now a **consumer** of `TankorentSearchService` (commit `a324919`) rather than owning the indexer dispatch logic directly. The page UI shell stays; the engine is in `src/core/`.

## Active arcs touching this surface

**TANKORENT_HYGIENE_FIX** — Phases 1+2+3 SHIPPED + committed; data-dir self-healing on next boot. Done.

**TANKORENT_FIX_TODO** — All 7 phases SHIPPED. Smoke pending. Open cosmetic: ExtraTorrents `0 B` Size display bug. 1337x harvester deep-dive deferred (env-dead). Tankorent list-view "Download" column: consumer shape defined, Agent 4 owns delivery to the THEATRE_DOWNLOAD_OVERHAUL pipeline.

**TORRENT_PERSISTENCE_COLLAPSE** (engine-side, in flight) — as this arc closes out the `m_records` → SQL migration in `TorrentClient`, the detail-tab widgets here will eventually read from `TorrentRepository` rather than live `m_records` snapshots. This is a future-phase concern; the UI widgets in this directory are not yet consuming SQL data directly.

## Load-bearing memories (read when touching this domain)

- `project_tankorent_as_foundation_vision.md` — Tankorent UI form may fade; indexer engine is permanent infrastructure. Custom indexer chip is never removable.
- `project_tankorent_stream_integration_closed.md` — search NOW lives in Stream mode; this directory owns the detail/inspector surface only
- `project_agent4b_departure_2026-05-20.md` — every file here was 4B-authored; honor it
- `feedback_plan_first_zero_errors.md` — non-trivial tasks: write the plan first

## Dev-bridge surface

No dedicated tankorent-UI tankoctl prefix. Torrent-level inspection via `stream-get-torrents` (v1.1) + `stream-get-bulk-groups`. Cross-mode `library-*` for library-tile state. Full catalog via `out\tankoctl.exe ping`.

## Build / MCP lane discipline (gov-v7)

Changes here require main-app rebuild (`build_check.bat`). Claim `build` lane via lease registry before touching this tree. MCP smokes for tab interactions claim `mcp` lane. Use `## MCP LANE` + `## BUILD LANE` companion headings in chat.md for human narrative; `out\tankoctl.exe lease-get <lane>` is the machine-truth per gov-v7.

## Activation contract

Auto-loads when any agent reads a file under `src/ui/pages/tankorent/`. You are reading 4B's tab-inspector UI. Cross-domain edits without Agent 4 sign-off are a Rule 14 violation.
