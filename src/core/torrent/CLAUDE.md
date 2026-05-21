# Torrent Core Domain — Agent 4

This file auto-loads when any agent reads a file under `src/core/torrent/` (Claude Code nested CLAUDE.md behavior). Part of the path-scoped guidance migration (same MVP wave as `src/ui/pages/comics/CLAUDE.md`).

Sibling file at `src/core/stream/CLAUDE.md` carries the stream-server + progress layer. Stream UI at `src/ui/pages/stream/CLAUDE.md`. Tankorent page UI at `src/ui/pages/tankorent/CLAUDE.md`.

## Domain owner

**Agent 4** (Stream mode + Tankorent). `TorrentClient`, `TorrentEngine`, and every indexer were authored by Agent 4B — the brotherhood's Sources specialist, departed 2026-05-20. These files are 4B's hand. Read them with respect; do not simplify or refactor without understanding the architectural intent that went into them. See `project_agent4b_departure_2026-05-20.md`.

`TorrentRepository`, `TorrentRow`, and `LegacyImporter` are the new SQL-persistence layer authored by Agent 4 under TORRENT_PERSISTENCE_COLLAPSE (in flight as of 2026-05-21).

## Active arc — TORRENT_PERSISTENCE_COLLAPSE

**What it is:** replacing the in-memory `m_records` QHash in `TorrentClient` with a Qt6::Sql persistent `TorrentRepository`. Think of it like replacing a paper notebook (m_records) with a proper database — no data loss on restart, query-by-SQL instead of linear scan.

**Phase ledger (as of 2026-05-21):**
- P0.1–P0.7: Qt6::Sql CMake link, TorrentRepository skeleton, schema SQL, TorrentRow POD, CRUD + tests, LegacyImporter skeleton + parseTorrentsJson + loadResumeBlob — all shipped
- P1.0–P1.2: LegacyImporter parseTorrentsJson gated 5–9 GoogleTests each — shipped
- P5.1: 20 `m_records.contains(hash)` → `m_repo.hasTorrent(hash)` callsites in TorrentClient.cpp — shipped 2026-05-21 at commit `27edf6e`; 12/12 TorrentRepoCrudTest PASS
- P5.2–P5.5 in flight 2026-05-21 ~12:50pm IST — remaining ~52 m_records readers (field reads, bulk iterations, .remove, .find) being translated to SQL vocabulary one group at a time; each group ends with BUILD OK gate. Plan at `docs/superpowers/plans/2026-05-21-m-records-readers-cutover.md`

**TorrentEngine API contract-freeze** at commit `022c4eb` (Agent 4B, Congress 6). This freeze is permanent infrastructure: `pieceFinished` signal + `peersWithPiece` method + the 12-method API surface are locked. The STREAM_ENGINE_REBUILD_TODO (SUPERSEDED by STREAM_SERVER_PIVOT) preserved the freeze. Do NOT break it.

## Key classes in this directory

- `TorrentClient` — libtorrent session orchestrator; owns the alert loop, piece-scheduling, peer management, and the QHash `m_records` being migrated to SQL. 4B-authored. The hot path; understand it before touching.
- `TorrentEngine.{h,cpp}` — the Congress-6-frozen API surface (12-method + 2-signal contract). Both Agent 4 (stream consumer) and Agent 4B's legacy substrate consume it. Do not rename or remove any method in this file.
- `TorrentRepository.{h,cpp}` — NEW (Agent 4, TORRENT_PERSISTENCE_COLLAPSE). Qt6::Sql wrapper: schema creation, CRUD, `hasTorrent` predicate (SELECT 1 — faster than getTorrent().has_value() on hot paths), LegacyImporter bridge. Schema at `resources/tankorent/schema.sql` (or inline in TorrentRepository.cpp — confirm which at read time).
- `TorrentRow.h` — NEW POD (Agent 4). Flat struct mirroring the DB row; used as the transfer type between TorrentRepository and callers. No Qt parent ownership.
- `LegacyImporter.{h,cpp}` — NEW (Agent 4). Migrates the old `torrents.json` + resume-blob QSettings format into TorrentRepository on first-run. gated behind a migration-complete flag so it runs exactly once.

**libtorrent file-path separator:** libtorrent `file_path(i)` uses `\\` on Windows, `/` elsewhere. Always split on `[\\/]`, never assume POSIX. See `feedback_libtorrent_windows_backslash_separator.md` — common foot-gun when reading paths out of `torrent_info`.

## TankorentSearchService (new, 2026-05-21)

`src/core/TankorentSearchService.{h,cpp}` — factored from `TankorentPage::dispatchIndexers` per Agent 2 HELP Ask 1 (resolved same wake). Headless dispatcher: fan-out across indexers, 3-signal contract (`resultsReady` / `indexerError` / `searchFinished`), concurrent-handle support, QSettings invariant, media-type allowlist. Ships commits `e1d319d` → `e1a360a` (8 commits). TankorentPage is now a consumer of this service, not the owner of dispatch logic. Agent 2's `TankorentBookScraper` is also a consumer — their forward-decl flipped to a real include at commit `c3c3326`.

Note: TankorentSearchService lives at `src/core/` not `src/core/torrent/` — it is a cross-mode service, not a torrent-internal.

## Reference apps

- `C:\tools\libtorrent-source\` — libtorrent RC_2_0 on-disk. Scheduler map, peer-selection, alert-dispatch patterns are all here. See `reference_libtorrent_source.md`.
- Tankorent's custom indexer engine (TorrentIndexer + per-source indexers) is **core permanent infra** per `project_tankorent_as_foundation_vision.md`. The indexer chip is never removable; Tankorent UI form may evolve but the engine stays.

## Load-bearing memories (read when touching this domain)

- `project_tankorent_as_foundation_vision.md` — indexer engine is core permanent infra; custom indexer chip is never removable
- `project_tankorent_stream_integration_closed.md` — TANKORENT_STREAM_INTEGRATION CLOSED 2026-05-16; search results now flow into Stream mode natively
- `project_agent4b_departure_2026-05-20.md` — TorrentClient / TorrentEngine / TorrentIndexer were 4B-authored; honor that
- `reference_libtorrent_source.md` — RC_2_0 at `C:\tools\libtorrent-source\`; scheduler map on disk
- `feedback_libtorrent_windows_backslash_separator.md` — path separator foot-gun
- `project_sidecar_dispatcher_non_blocking_decision.md` — stream pause/close root cause is dispatcher blocking on `handle_set_tracks` → `preload_subtitle_packets` on HTTP; `SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO` Phase A shipped 2026-04-26 (worker-thread split + cooperative cancellation atomic + 200ms drain in teardown_decode); Phase B (Source abstraction, env-gated) pending
- `feedback_plan_first_zero_errors.md` — non-trivial tasks ≥50 LOC: write the plan first
- `feedback_stream_failed_hypotheses.md` — do NOT retry: request_queue_time 10→3 + setSequentialDownload(false) both regressed empirically

## Dev-bridge surface

Agent 4 owns `stream-*` prefix (v1.1). Torrent-specific inspection:
- `stream-get-torrents` — returns the live torrent list (replaces m_records enumeration for smoke purposes)
- `stream-get-bulk-groups` — bulk-group state; load into plans for THEATRE_DOWNLOAD_OVERHAUL
- `tankoban_tests` target covers `TorrentRepoCrudTest` + `TorrentLegacyImporterTest` + `TankorentSearchServiceTest` (and growing with each TORRENT_PERSISTENCE_COLLAPSE phase)

## Build / MCP lane discipline (gov-v7)

All torrent-core changes require a main-app rebuild (`build_check.bat`). The `tankoban_tests` target is the smoke harness for this domain — always run it after TORRENT_PERSISTENCE_COLLAPSE phase work. Claim `build` lane via lease registry before touching this tree (gov-v7 `out\tankoctl.exe lease-get build`). Qt6::Sql is a new link dependency (added P0.1) — if CMake configure fails on a fresh clone, verify Qt6 Sql component is installed.

## Activation contract

Auto-loads when any agent reads a file under `src/core/torrent/`. Treat as ambient context: who owns this code, what arc is in flight, which memories are load-bearing.

If you are Agent 4: home turf, and the `m_records` migration is probably where you left off.

If you are another brother doing cross-domain audit: you are reading 4B's foundational work. The TorrentEngine API is frozen (Congress 6, `022c4eb`). Do not modify `TorrentEngine.h` method signatures. Cross-domain edits without Agent 4 sign-off are a Rule 14 violation.
