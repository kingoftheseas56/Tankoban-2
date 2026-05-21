# Stream Core Domain — Agent 4

This file auto-loads when any agent reads a file under `src/core/stream/` (Claude Code nested CLAUDE.md behavior). Part of the path-scoped guidance migration (same MVP wave as `src/ui/pages/comics/CLAUDE.md`).

Sibling file at `src/ui/pages/stream/CLAUDE.md` carries the stream UI widgets. Torrent persistence layer at `src/core/torrent/CLAUDE.md`.

## Domain owner

**Agent 4** (Stream mode + Tankorent). Tankorent ownership inherited from Agent 4B on 2026-05-20. The Stremio stream-server subprocess layer (`src/core/stream/stremio/`) was authored under the STREAM_SERVER_PIVOT arc and is Agent 4's primary active surface here. Agent 4B's hand is present throughout the addon registry + libtorrent-era code — honor it.

## Active arc — STREAM_SERVER_PIVOT

Vision: off the libtorrent C++ engine onto Stremio's Rust `stream-server` binary (`resources/stream_server/`) as a subprocess + REST adapter. P0+P1+P2A+P2B all GREEN (2026-04-24).

**Key classes:**
- `stremio/StreamServerProcess` — subprocess lifecycle: spawn, health-check, shutdown. Wraps the Rust `stremio-runtime.exe` binary at `resources/stream_server/`.
- `stremio/StreamServerClient` — REST client to the local stream-server HTTP surface (127.0.0.1 scope, Windows Firewall inbound rule required — see foot-gun note below).
- `stremio/StreamServerEngine` — orchestrates Process + Client; implements the engine interface consumed by `StreamAggregator`.
- `StreamProgress` — schema_version=1 ship at `ad2bc65`; carries torrent progress state as structured JSON between layers.

**Legacy-flag rollback window:** `TANKOBAN_STREAM_BACKEND={legacy,server}` CMake option preserved through P4. After P4 ships, the legacy path is slated for removal.

**FOOT-GUN: Windows Firewall Cancel = BLOCK rules.** If Hemanth clicks "Cancel" (not "Allow") on the Windows Firewall prompt when Tankoban first runs the stream-server, Windows silently adds BLOCK rules. Symptom: `peers > 0` but `dlSpeed = 0` for 30+ seconds. Fix: delete the BLOCK rule via Windows Defender Firewall → Inbound Rules → find `stremio-runtime.exe` entry with Action=Block → delete it. This is the first thing to check on any "stream just hangs" report. See `feedback_stream_server_firewall_gotcha.md`.

**DO NOT rcedit `stremio-runtime.exe`.** Node SEA (Single Executable Application) breaks on offset rewrite → segfault on launch. See `feedback_nodejs_sea_rcedit_trap.md`.

## Other key classes in this directory

- `CatalogAggregator` / `MetaAggregator` / `SubtitlesAggregator` — addon fan-out + result merge layer; consume `AddonRegistry` + `AddonTransport`
- `StreamAggregator` + `UnifiedPackSearchEngine` — top-level stream orchestration; fan-out across sources + dedup
- `BulkSourceCollector` + `BulkPackVerifier` + `StreamBulkPlan` — THEATRE_DOWNLOAD_OVERHAUL substrate; bulk-download planning + verification
- `StreamLibrary` + `UnifiedProgressStore` + `StreamDownloadIndex` — in-library persistence; tracks what is downloaded + in-progress
- `PackClassifier` + `QualityScorer` + `TitleMetadataEstimator` — pack heuristics; quality ranking + title parsing
- `StreamRescueScanner` — recovery path for stuck or incomplete downloads
- `StreamTelemetryWriter` — writes `out/stream_telemetry.log` (structured event stream; `log-mark` correlation available via v1.9 dev-bridge)
- `addon/AddonRegistry` + `addon/AddonTransport` + `addon/Manifest` + `addon/Descriptor` — Stremio addon layer; catalog + stream + subtitle endpoint contracts
- `CalendarEngine` — airing calendar data; consumed by `src/ui/pages/stream/CalendarScreen`

## Reference apps

- **Stremio** — canonical reference for the addon protocol (`Manifest`, `ResourcePath`, `StreamInfo`). The pivot's pre-built `stremio-runtime.exe` SHA-256-pinned binary at `resources/stream_server/` is Stremio's own Rust source, not a fork.
- **Nuvio** — HTTP-only ExoPlayer reference at `Downloads\NuvioMobile-cmp-rewrite\`. See `project_nuvio_reference.md`. Useful for streaming pipeline patterns before authoring new aggregator code.
- `C:\tools\libtorrent-source\` — libtorrent RC_2_0 on-disk; scheduler map and peer-selection logic reference for the legacy engine path and for `TorrentClient` understanding. See `reference_libtorrent_source.md`.

## Load-bearing memories (read when touching this domain)

- `project_stream_server_pivot.md` — pivot rationale + P0-P2 Green state; 127.0.0.1 scope; Windows Firewall rule; SHA-256 pin
- `project_stream_rebuild_gate.md` — SUPERSEDED 2026-04-24 by pivot; P0 StreamProgress schema (ad2bc65) preserved; historical only
- `project_stream_stall_fix_closed.md` — STREAM_STALL_FIX CLOSED 2026-04-19 (4 phases); Congress 7 B3 inherits scheduler work
- `project_tankostream_parity.md` — all 3 stream tracks SHIPPED (PARITY P1-6, UX_PARITY 5+, PLAYBACK_FIX 3 + WSADuplicateSocket)
- `project_stremio_tuning_ab_result.md` — Experiment 1 APPROVED: 65% stall ↓, 89.5% cold-open ↑, 86.3% p99 wait ↓
- `project_theatre_download_overhaul_kickoff.md` — 22-task plan; Phases A+B+C shipped
- `project_theatre_download_overhaul_phase_abc_shipped.md` — PackList state complete; Phase D next
- `project_agent4b_departure_2026-05-20.md` — honor 4B's hand in addon + libtorrent-era files
- `feedback_session_lifecycle_pattern.md` — intermittent stream failure = session-lifecycle race 99% of the time
- `feedback_time_update_frozen_pts_during_stall.md` — time_update IPC flows at 1Hz with frozen PTS during stalls; not "pipeline silent"
- `feedback_stream_failed_hypotheses.md` — do NOT retry: request_queue_time 10→3 + setSequentialDownload(false) both regressed
- `feedback_smoke_on_failing_streams.md` — smoke on streams that REPRODUCE the bug
- `feedback_stream_server_firewall_gotcha.md` — Windows Firewall Cancel = BLOCK; peers > 0 + dlSpeed = 0 = the tell
- `feedback_nodejs_sea_rcedit_trap.md` — never rcedit stremio-runtime.exe (Node SEA breaks)
- `feedback_libtorrent_windows_backslash_separator.md` — libtorrent file_path(i) uses `\\` on Windows; split on `[\\/]`, never assume POSIX
- `reference_libtorrent_source.md` — RC_2_0 at `C:\tools\libtorrent-source\` with scheduler map

## Dev-bridge surface

Agent 4 owns `stream-*` tankoctl prefix (v1.1). Stream-core smokes use:
- `stream-get-state` / `stream-get-torrents` / `stream-get-downloads` / `stream-get-bulk-groups`
- `events-tail` → `out/events.jsonl` for structured telemetry (use `log-mark <label>` around smoke steps to correlate across all 4 log streams)
- `log-mark <label>` (v1.9) — writes correlation marker to `sidecar_debug_live.log` + `stream_telemetry.log` + `events.jsonl` + `ipc_latency.log` simultaneously; the headline v1.9 unlock for multi-log smoke correlation

## Build / MCP lane discipline (gov-v7)

Stream core changes always require a main-app rebuild (`build_check.bat`). Claim `build` lane via lease registry before touching this tree. Heavy-link step (~915s) — do not batch multiple stream-core changes into one build window without a plan. Sidecar (`native_sidecar/build.ps1`) is Agent 3's domain; stream-server binary is a pre-built vendored artifact — do not rebuild it from source.

## Activation contract

Auto-loads when any agent reads a file under `src/core/stream/`. Treat as ambient context on who owns this code, what arcs are in flight, and which memories gate any change here.

If you are Agent 3 reading `StreamServerProcess` or `StreamTelemetryWriter`: note that SidecarProcess IPC is your surface but the stream-server REST layer is Agent 4's. Coordinate on boundary.

If you are Agent 1 or Agent 2 doing architectural archaeology: `StreamAggregator` + `CatalogAggregator` are the fan-out + dedup patterns your arcs should mirror. Cross-domain edits without Agent 4 sign-off are a Rule 14 violation.
