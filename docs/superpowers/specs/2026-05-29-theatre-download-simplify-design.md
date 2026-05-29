# THEATRE_DOWNLOAD_SIMPLIFY — Design Spec

- **Lifecycle:** active
- **Date:** 2026-05-29
- **Owner:** Agent 4 (Stream + Tankorent)
- **Status:** Design approved by Hemanth (2026-05-29); auto-pick metrics brainstormed + locked same day; awaiting final spec review → implementation plan
- **Supersedes for Theatre:** the Theatre-facing scope of `TANKORENT_QUALITY_AND_QUEUE` (lane queue, Tankorent-in-show-view, pack badges). That arc's Tankorent-search-engine improvements survive as standalone-Tankorent work; its Theatre integration is parked.
- **Builds on:** `2026-05-29-theatre-download-only-design.md` (Theatre is download-only; this spec defines *how the download UX should feel* now that streaming is gone).

## §1 — Strategic intent

Theatre's download UX grew five different behaviors — single-episode download, season download, auto-most-seeded-per-episode, multi-season, full-series — wired through a source picker, a Tankorent-search sidebar, and an orphaned download panel left over from the Stremio era. Some buttons work, some don't, and at no point does the screen tell you what is happening. Removing the Stremio streaming "safety net" before this UX was solid exposed how undercooked it is.

**The pivot is subtraction, not addition.** The fix for an overwhelming screen is not a new streaming engine on top of it — it is collapsing Theatre's download function to **two actions that behave identically**, with **one source** and **one clear state per episode**. Complexity is re-added later, deliberately, only when a concrete flaw demands it (see §8).

Hemanth verbatim: *"what we need is simplicity. no torrentio list, no other download options. just episode download (auto picked torrent) and season download (auto-picked torrent files)."*

## §2 — Goals

1. **Two actions, total:** *Download* (an episode or a movie) and *Download Season*. Nothing else.
2. **One source:** Torrentio only. No source picker, no results list — the user never sees Torrentio's rows.
3. **Auto-pick:** the app silently selects the best 1080p source via filter-then-rank (§5) and downloads it — no list, no choice.
4. **One state model per episode:** *not downloaded* → *downloading (%)* → *downloaded (play)*. Visible on the tile **and** aggregated on the Downloads page.
5. **Completed downloads land in the library** and play from disk. No auto-play, no interruption.
6. **The confusing surface is deleted** (§6), shrinking the smoke-test surface to a single state machine.

## §3 — User-facing flow (approved shape)

- **Download an episode / movie.** Open a show, click *Download* on an episode (or the movie). The app finds Torrentio sources in the background, picks the best-seeded 1080p, and starts downloading. The tile flips to *downloading* with a percentage. When it completes, the tile becomes *Play* and the file is in your library. Click it → plays from disk.
- **Download a season.** Click *Download Season*. This **fans out into one auto-picked Torrentio download per episode**, run **one at a time, in episode order**. Each episode's tile moves through the same three states independently. Episode 1 is watchable while Episode 2 is still downloading. (It is literally the *Download* button pressed for every episode, queued.)
- **Already-downloaded.** Plays instantly, as today.
- **No picker, no list, no choices** anywhere in this flow.

## §4 — The state model

Each episode/movie tile is in exactly one state, derived from the download engine + `StreamDownloadIndex`:

| State | Meaning | Affordance |
|-------|---------|------------|
| Not downloaded | No file on disk, not in progress | *Download* |
| Downloading | Active transfer | progress % + Cancel |
| Downloaded | File on disk, mapped to this episode | *Play* |

The Downloads page aggregates every *downloading* item as one row/card per show, with the active episode's progress and any episodes still queued behind it. Completed items drop off the active list (they live in the library now).

## §5 — Auto-pick algorithm (filter → rank)

The auto-pick is the entire intelligence behind the one-tap experience — with no list shown, **the scorer *is* the product.** It runs as **hard filters, then a ranking** — more explainable and debuggable than a single blended score. It reuses the existing pure-logic `QualityScorer` (resolution + release-type + log-scaled seeder health, already unit-tested, already powering the picker we're deleting) plus the `seeders` and `sizeBytes` that `StreamAggregator` already parses from Torrentio result titles. No new source plumbing.

**Step 1 — Hard filters (exclude, no exceptions):**
- Resolution ≠ 1080p → excluded. *(Hemanth: "1080p or nothing.")*
- 0 seeders → excluded (dead torrent).
- CAM / TS / TELESYNC (camcorder rips) → excluded.
- If nothing survives → surface **"no source found"** on the tile; never auto-pick a different resolution.

**Step 2 — Rank survivors by seeder health** (log-scaled: 50→500 seeders matters, 500→1500 barely does). Highest health wins. This decides the pick in the overwhelming majority of cases — the popular release simply wins. *Seeders is treated as a crowd-sourced summary of correctness, encode quality, group reputation, and not-fake-ness; it is the primary signal precisely because it already bundles what separate factors would re-measure.*

**Step 3 — Size guardrail, only for the weakly-seeded tail.** Seeders negate size doubt: a well-seeded release is trusted regardless of size and **never reaches this step.** Only when the surviving candidates are all low-seeded do we consult **implied bitrate** (`sizeBytes` ÷ runtime, runtime from Cinemeta) and drop releases outside a sane 1080p band — rejecting a low-seed 200MB re-encode or a low-seed 12GB mislabeled-4K remux *when seeder count can't disambiguate.*

**No UI at any step.** Selection is silent; the candidate list is never shown.

**Worked examples (the model behaving):**
- *One Piece [SubsPlease] 1080p, 1.4GB/24min, 1200 seeders* → passes filters, wins on seeders; size never consulted. ✅
- *1080p 180MB, 4 seeders* → passes filters; low-seed tail → implied bitrate ≈ re-encode → dropped. ✅
- *1080p 13GB/24min, 6 seeders* → low-seed tail → implied bitrate ≈ remux → dropped. ✅

**The real reliability lever is title parsing, not factor count.** Resolution, CAM tags, and size are all read from Torrentio's messy, inconsistent title strings. A missed "1080p" or "CAM" read breaks a filter far more often than any missing metric would. Test coverage concentrates on the parser; the scorer is pure-logic and logs its filter/rank decision for every pick, so mispicks are diagnosable.

## §6 — What gets deleted

Removal is the bulk of this arc. Each item below is UI the user will never have to learn or test again:

- The **source picker** (`TorrentPackPicker` / `PackListItem`) — no picking anymore.
- The **Tankorent-search section inside the Theatre show view** (Tankorent-as-source-addon sidebar) — Theatre uses Torrentio only.
- **Multi-season** and **full-series** download buttons.
- **Auto-download-season-by-most-seeded-per-episode** as a distinct mode (its behavior is now just *Download Season*).
- The **orphaned `TheatreDownloadPanel`** built around Stremio+Tankorent aggregation via `UnifiedPackSearchEngine`.

The standalone **Tankorent page is untouched** and remains reachable on its own (Hemanth: "leave it for now"). It just no longer feeds Theatre.

## §7 — Components: kept / rewritten / removed

**Kept (the byte-movers and identity stores — already work):**
- `TorrentClient` (libtorrent) — the single shared download engine. **New requirement:** downloads must be **tagged by originating mode** so Theatre's downloads appear only on Theatre's Downloads page, never in the standalone Tankorent page's list.
- `StreamDownloadIndex` — maps `imdbId`+season+episode (and movie) → file path; the source of truth for the *downloaded* state and for "play from disk."
- `MetaAggregator` / `StreamAggregator` (Torrentio source finding) / `SubtitlesAggregator` — browsing + source fetch stay. `StreamAggregator` already parses `seeders` + `sizeBytes` from result titles (§5 inputs).
- `QualityScorer` — pure-logic resolution/release-type/seeder-health scorer; reused as the auto-pick brain (§5). The picker UI it currently feeds is deleted; the scorer is kept.
- Local-file play: `beginPlayOrDownload` → `playLocalFileFromStreamRequested` → `VideoPlayer::openFile`.
- `StreamDownloadsPage` (shipped 2026-05-25) — becomes the simple aggregate Downloads view.

**Rewritten:**
- `StreamPage` download handlers — `beginPlayOrDownload` not-owned branch becomes: fetch Torrentio sources → auto-pick best-seeded 1080p → `TorrentClient::startDownload`. The single-episode path (`onSingleEpisodeDownloadRequested`) drives this; `onDirectDownloadRequested`/`onAddToTankorentRequested` source-card paths are removed.
- *Download Season* — a minimal **per-show sequential queue**: enqueue every episode's auto-pick, run one at a time in order, advance on completion. (Reuse the `TransferQueue` primitive if it fits cleanly; otherwise a small purpose-built queue. No reorder/bump/lanes UI in v1.)
- **Progress wiring** — connect `TorrentClient` per-torrent progress/completion signals back to (a) the episode tile state and (b) the Downloads page. This is the wire that was severed in the Stremio removal; restoring it is what makes the UI stop being silent.
- `EpisodeTile` — render the three-state affordance.

**Removed:** see §6.

## §8 — Deferred, on purpose (written down so it is not lost)

These are **known future needs**, parked until the simple core is trustworthy. Re-added deliberately, each on a concrete trigger:

1. **Audio / dub quality selection.** Auto-pick-by-seeders *will* eventually grab a bad-audio release — a Russian dub with no English, or (especially in anime) a single-audio release when dual-audio exists. **This is the first complexity we expect to re-add.** Trigger: the first time auto-pick lands a watch-blocking audio mismatch. Adds as a new §5 filter/factor, not a separate mechanism.
2. **Release-group reputation, codec (x265/x264), HDR/DV detection** — *deliberately* not added in v1. Seeders already proxies most of what these measure (popular releases come from good groups, in sane codecs); adding them now is speculative over-tuning. Re-added per-factor only when a concrete mispick demands it.
3. **Resolution fallback (720p when no 1080p).** v1 is "1080p or nothing" (§5). A fallback policy is parked until obscure/old-content "no source found" reports prove it's worth the added branch.
4. **Tankorent packs as an alternate source** (the parked Theatre scope of `TANKORENT_QUALITY_AND_QUEUE`).
5. **Streaming / play-while-downloading** — buildable natively (sequential-download + progressive play via libtorrent, no Stremio binary); consciously parked as a later choice, not a rescue.
6. Season-**pack** torrents (one torrent, many files, Cinemeta-per-file mapping) — explicitly *not* how *Download Season* works in v1; v1 fans out per-episode instead.

## §9 — How we know it works

Smoke is **Agent 4's job** (tankoctl + pywinauto driving the app), not Hemanth's. Hemanth's only check is the taste call: *does the screen feel calm.*

- Click *Download* on an episode → tile goes *downloading %* → completes → tile goes *Play* → plays from disk. No picker ever appears.
- Click *Download Season* → episodes download one at a time in order; E1 is playable while E2 is still going.
- A Theatre download **does not appear** in the standalone Tankorent page.
- The Downloads page shows active downloads with live progress; completed items move to the library.
- No multi-season / full-series / source-picker / Tankorent-sidebar UI exists in Theatre anymore.
- gov-v11 hard gate: clean-from-scratch `build_check` BUILD OK in an isolated lane.

## §10 — Sequencing (phases)

1. **Auto-pick + single-episode download end-to-end.** Torrentio fetch → best-seeded-1080p pick → download → tile state → progress wiring → completion → library → play. (Restores a *working, visible* single download. Kills the immediate pain.)
2. **Mode tagging + Downloads-page separation.** Tag Theatre downloads; Theatre Downloads page shows only Theatre's; nothing leaks into the Tankorent page.
3. **Download Season fan-out.** Per-show sequential queue over per-episode auto-picks.
4. **Deletion pass.** Remove the picker, Tankorent-in-Theatre sidebar, multi-season/full-series buttons, orphaned panel. Grep every caller before removing.

Phase 1 lands the user-visible fix first; later phases simplify and clean up.

## §11 — Coordination

- **`TANKORENT_QUALITY_AND_QUEUE`** Theatre scope is parked (this spec supersedes it for Theatre). Its standalone-Tankorent search-quality improvements (Nyaa parity, indexer parity) remain valid as their own work if pursued.
- **Agent 0** holds the `ffmpeg_sidecar.exe` deploy fix (downloaded files can't play until it's deployed next to the app) — this spec assumes that lands in parallel.
- **`REPO_STRUCTURE_CLEANUP`** keeps its StreamPage source-move parked — these files stay a hot active arc through this work. Heads-up posted in chat.md at kickoff.
- **Agent 1** (`COMICS_TANKOYOMI_STREAM_MERGER`, Stream mode = blueprint): the blueprint is now *download-and-local-play, two actions* — arguably a cleaner reference than the old streaming shape. Heads-up; their call how it ripples.

## §12 — Risks

1. **Torrentio returns no usable 1080p source.** Per §5 ("1080p or nothing"), the tile surfaces a clear **"no source found"** state rather than silently grabbing a wrong-resolution or junk release. Accepted trade-off: obscure/old content with only 720p reports no source until the §8.3 fallback is added.
2. **Auto-pick lands bad audio** (the deferred §8.1 case). Accepted for v1; first re-added complexity.
3. **Title-parse miss is the likeliest bad-pick cause.** A misread resolution / CAM tag / size from a messy Torrentio title breaks a filter more often than any missing metric. Mitigation: the title parser gets the bulk of unit-test coverage; the pure-logic scorer logs its filter/rank decision for every pick so mispicks are diagnosable (§5).
4. **Deletion surfaces a hidden dependency.** Mitigation: deletion is Phase 4, after the new path is proven; grep every caller of removed symbols first.
5. **Multi-agent build contention.** Build + verify in an isolated lane/worktree, never shared `out/`.
