# THEATRE_DOWNLOAD_ONLY — Design Spec

- **Lifecycle:** active
- **Date:** 2026-05-29
- **Owner:** Agent 4 (Stream + Tankorent)
- **Status:** Design approved by Hemanth (2026-05-29); awaiting spec review → implementation plan
- **Architecture map:** the Explore pass this wake (stream-server layer, consumers, play flow, download path, blast radius) — see §7.

## §1 — Strategic intent

Theatre is the odd mode out: Comics and Books are **download-exclusive** (fetch to disk, then read), while Theatre also does **live torrent streaming** through a bundled Stremio stream-server subprocess (`stremio-runtime.exe`). That streaming layer is the single most fragile thing in the app — this wake it hung indefinitely on "Resolving metadata." Investigation found **8 orphaned `stremio-runtime` processes squatting all 5 of its ports (11470–11474)** (the subprocess isn't reliably killed on exit, so orphans accumulate) — a real, proven failure mode. But **clearing them did NOT restore streaming**, which means there is *additional* fragility in the same layer that we never fully root-caused. That is itself the argument: a subsystem we can't even reliably diagnose, on top of a firewall foot-gun (Cancel = silent BLOCK rules) and a Node-SEA binary-rewrite trap, is not worth keeping when the mode can run without it.

**The pivot:** make Theatre **download-only**, like Comics and Books. You find a show, pick a source, it downloads to disk, you watch the local file. Remove the Stremio stream-server entirely. This unifies the app around one coherent identity — *Tankoban: download and own your comics, books, and films; watch/read them in a Netflix-looking shell* — and **deletes an entire class of bug permanently** (no subprocess → no port contention, no zombie leak, no firewall trap).

Hemanth verbatim: *"it's not exactly stremio for videos, books and manga. it's a stremio for downloading and watching/reading everything. the videos will be on your disk."*

**Deferred (explicit non-goals, see §4):** ephemeral / play-while-downloading playback is parked until there's a clean solution; direct-URL (non-torrent) playback is dropped (unused).

## §2 — Goals

1. **Theatre plays only local files.** Opening an episode/movie that isn't downloaded routes to the **download flow**, not streaming.
2. **The Stremio stream-server is gone** — subprocess layer, the ~22 MB bundled binary, and all CMake/resource wiring removed.
3. **The "Resolving metadata" hang is structurally impossible** afterward (nothing to resolve through; no subprocess, no ports).
4. **Everything you browse with is untouched** — catalog, search, show/episode metadata, source-finding (Cinemeta + Torrentio + subtitles) all stay.
5. **The download + local-play path is reused, not rebuilt** — it already works and is independent of the stream-server.

## §3 — User-facing flow (approved shape)

- **Browsing:** identical to today — catalog, search, posters, show/episode lists.
- **Playing something not yet downloaded:** click episode/movie → the **source picker** (existing `TorrentPackPicker`: quality / seeders / size) → pick one → it **downloads** (libtorrent), appears on the **Downloads page** with progress → when complete, **plays from disk** (instant, offline, no buffering). *(Approved: "choose source, then download.")*
- **Already-downloaded:** plays instantly, exactly as today.
- **No streaming, no "Resolving metadata," no buffering spinner** — the only "wait" is the download, surfaced as normal download progress.

## §4 — Non-goals (this arc)

- **Ephemeral / progressive play-while-downloading.** Deferred until a clean solution exists (Hemanth's call). The hard real-time piece-prioritization problem the stream-server solved only returns if/when we do this; we will design it deliberately later, likely as sequential-download + play-the-downloaded-range via libtorrent (no Stremio server).
- **Direct-URL (non-torrent) playback.** Some Stremio addons return a direct web link; Hemanth's setup (Torrentio) never produces these, so the direct-URL code branch is dropped for consistency.
- **Polished download-only UI emphasis.** Hemanth: *"after the transition is done, we just need to add more polished UI to emphasise the download-only nature."* That is a **separate follow-on arc** (see §9), not built here. This arc is the mechanical transition.
- **Comics / Books / Tankorent** — untouched.

## §5 — Sequencing (two phases)

**Phase 1 — Behavior cutover (kills the bug immediately).**
- Reroute the play path: opening a not-downloaded item → source picker → download, instead of the stream-server polling loop.
- Stop creating/starting `StreamServerEngine` (no subprocess spawned ever).
- `StreamPlayerController` play path becomes: downloaded → play local file; not-downloaded → route to download flow. Remove the magnet/stream-server polling loop and the "Resolving metadata" status.
- **Outcome:** streaming can no longer hang; Theatre is download-only in behavior even before the code is deleted.

**Phase 2 — Dead-code + binary removal (clean-up).**
- Delete `src/core/stream/stremio/` (StreamServerProcess / StreamServerClient / StreamServerEngine).
- Delete `resources/stream_server/` (the ~22 MB `stremio-runtime.exe` + `server.js` + bundled ffmpeg DLLs).
- Remove the CMake source/header entries + the resource-copy block for the stream-server bundle.
- Remove the vestigial `TANKOBAN_STREAM_BACKEND` flag and stream-server lifecycle telemetry events.
- Simplify `StreamTypes` playback-mode (only local-file play remains).
- **Gate:** clean-from-scratch `build_check` BUILD OK (gov-v11) + the app launches and plays a downloaded file.

*(Phases are sequenced so the user-visible fix lands first; the deletion is mechanical follow-up. Engineering call — not Hemanth's to adjudicate.)*

## §6 — Components: removed / rewritten / kept

**Removed** (`§7` blast radius has exact paths):
- `src/core/stream/stremio/` — 3 classes (Process/Client/Engine).
- `resources/stream_server/` — binary bundle.
- CMake entries (sources, headers, resource-copy block); `TANKOBAN_STREAM_BACKEND` flag.

**Rewritten:**
- `StreamPlayerController` — drop `StreamServerEngine` member + polling loop + "Resolving metadata" path; play = local-file, else route-to-download.
- `StreamPage` — stop creating/owning `StreamServerEngine`; episode play → download flow.
- `StreamTypes` — collapse playback modes to local-file only.
- Stream telemetry — drop engine-lifecycle events.

**Kept unchanged (the parts that already work, independent of the stream-server):**
- Meta/discovery: `MetaAggregator`, `AddonRegistry`, `CatalogAggregator`, `StreamAggregator` (source finding), `SubtitlesAggregator`.
- Download: `TorrentClient` (libtorrent), `BulkSourceCollector`, `StreamBulkPlan`, `TheatreDownloadPanel`, `TorrentPackPicker`, `StreamDownloadIndex`, `TransferQueue`.
- Local-file play: `playLocalFileRequested` → `MainWindow::onPlayLocalFileFromStreamRequested` → `VideoPlayer::openFile`.

## §7 — Removal blast radius (from the architecture map)

| Category | Action | Scope |
|----------|--------|-------|
| Stremio subprocess | DELETE | `src/core/stream/stremio/StreamServer{Process,Client,Engine}.{h,cpp}` |
| Stremio binary bundle | DELETE | `resources/stream_server/` (runtime exe + server.js + ffmpeg DLLs + LICENSE) |
| CMakeLists.txt | EDIT | remove the 3 source entries, 3 header entries, and the stream_server resource-copy block |
| `StreamPlayerController.{h,cpp}` | REWRITE | remove engine + poll loop + metadata-resolving status; local-or-download play |
| `StreamPage.{h,cpp}` | REWRITE | remove engine creation/ownership; episode play → download |
| `StreamTypes.h` | SIMPLIFY | one playback mode (local file) |
| `StreamTelemetryWriter` | TRIM | drop engine_started/stopped/metadata events |
| `TANKOBAN_STREAM_BACKEND` | REMOVE | vestigial legacy/server flag |

**Verified independent / no change:** MetaAggregator, AddonRegistry, CatalogAggregator, StreamAggregator, the entire download pipeline, and the local-file play path. The plan phase will grep each rewritten file's callers to confirm no dangling references after removal.

## §8 — Testing / smoke

- Open a show → pick episode → **source picker appears** → pick → **download starts** (visible on the Downloads page with progress) → completes → **plays from disk**.
- Already-downloaded episode/movie → plays instantly.
- **No `stremio-runtime` process ever spawns** after launch or play (process-list check) — the zombie/port-contention class is gone.
- **No "Resolving metadata" state** appears anywhere.
- Movie path = same as series (single source pick → download → play).
- **gov-v11 hard gate:** clean-from-scratch `build_check` BUILD OK after Phase 2, in an isolated lane/worktree (the tree is multi-agent-contended — build in isolation, not shared `out/`).

## §9 — Follow-on (separate future arc, NOT this one)

**THEATRE_DOWNLOAD_ONLY_UI_POLISH** — once the transition lands, polish the UI to *emphasise* the download-only nature so it feels intentional and Netflix-like, not like a downgraded streamer: prominent "Download" affordances, download-state badges on tiles, "in your library" framing, clean progress surfaces. Hemanth-requested; gets its own brainstorm → spec → plan when this arc closes.

## §10 — Coordination

- **Agent 0's `REPO_STRUCTURE_CLEANUP`** explicitly defers its source-move passes (incl. relocating `StreamPage` into `src/ui/modes/`) until a quiet tree with no active arcs in those files (their §7/§8). This arc makes the stream files a **hot active arc**, which correctly keeps that move parked. Post a heads-up in `chat.md` when this arc kicks off so the stream-file reorg stays deferred until we're done.
- **Agent 1** owns the COMICS_TANKOYOMI_STREAM_MERGER arc, for which **Stream mode is the architectural blueprint**. Removing live streaming changes that reference — give Agent 1 a heads-up; the *download-and-local-play* shape is arguably an even cleaner blueprint for the Comics merger, but it's their call how it ripples.

## §11 — Risks

1. **Hidden stream-server dependency surfaces during deletion.** Mitigation: Phase 1 (behavior cutover) lands first and is exercised before any deletion; Phase 2 greps every caller of the removed symbols before deleting.
2. **Multi-agent build contention.** Mitigation: build + verify in an isolated lane/worktree, never shared `out/` (this wake's lesson — [[feedback-no-concurrent-builds-same-out-dir]]).
3. **Losing the instant-play feel.** Accepted + deferred (§4); mitigated later by the progressive-play follow-on if we choose to.
