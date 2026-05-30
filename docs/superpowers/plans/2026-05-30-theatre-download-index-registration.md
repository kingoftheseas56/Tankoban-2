# Theatre Download Index Registration — Fix Plan (THEATRE_EPISODE_STATE_MODEL P1.6)

> **For the executing agent (Agent 9 / Codex):** scoped src/ fix in Agent 4B's
> completion/registration code. Use superpowers:executing-plans + superpowers:systematic-debugging.
> Build + test + smoke; **do NOT push to master** — post READY TO MERGE, Agent 4 (Opus) reviews first.

## Context — why this exists

THEATRE_EPISODE_STATE_MODEL Phase 1 (committed locally, NOT pushed: `afd0cb1`,
`edf5f9c`, `4f5e720`, `34a01c9`, `3298e00`, `5b042a9`) made every Theatre episode
row derive its state **disk-first** from `StreamDownloadIndex`. It works — Daredevil:
Born Again S2 correctly shows **Play** on every downloaded episode (Hemanth-verified
on screen 2026-05-30).

But the disk-first display is only as good as the **index**. The old code masked an
index-completeness gap by reading the engine's cohort state directly; disk-first
honestly surfaces it. Two download types never get a **Complete** record in
`StreamDownloadIndex`, so they paint wrong:

1. **Whole-season-pack torrents** (e.g. "One.Piece.2023.S01.COMPLETE") → every
   episode shows the **download arrow** despite being downloaded + published.
2. **Single-episode downloads** (e.g. Invincible S4E01) → stuck showing a stale
   **"NN%"** after the download has actually finished (torrent is *seeding*).

The display layer is correct and OUT OF SCOPE. This plan fixes the **registration**
so the index becomes authoritative; the existing display then lights them up.

## Root cause (pinned by Agent 4 / Opus)

**Gap 1 — season-pack path computation.** `TorrentClient::backfillStreamDownloadIndex()`
([src/core/torrent/TorrentClient.cpp:2058](../../../src/core/torrent/TorrentClient.cpp#L2058))
and the on-publish registerEpisode path (same file, ~line 2270 — grep
`registerEpisode` / `Publishing` → `Published` transition) compute the episode file
path as **`destinationRoot + canonicalFilename`** (flat). A season-pack torrent nests
its files in the torrent's own content subfolder
(`destinationRoot/<torrent-folder>/One Piece - S01E01 ....mkv`), so `QFileInfo::exists`
on the flat path is **false** → counted as `skippedNoFile` → `registerEpisode` never
called. Bulk groups confirm: One Piece S1 items are all `itemState=Published` with
`canonicalFilename` set, but `get-downloads` has no One Piece entries.

**Gap 2 — single-episode downloads never register on completion.** Single-episode
Theatre downloads (dispatched via `singleEpisodeDownloadRequested` → empty
`streamGroupId`, NOT a cohort) are bare torrent records carrying imdb/season/episode.
The on-publish `registerEpisode` path is **cohort-only** (walks `m_streamBulkGroups`),
so a finished single episode never gets a Complete entry. Its only index trace is a
stale Pending/Downloading entry (if any), which sticks. The movie equivalent is
already handled (`streamMovieDownloadSnapshot`, season=0); episodes have no equivalent.

## The fix (two parts, both in TorrentClient.cpp)

### Part A — resolve the real (possibly nested) file for cohort items
- In `backfillStreamDownloadIndex()` AND the on-publish path, when the flat
  `destinationRoot/canonicalFilename` does not exist, **resolve the actual file**:
  prefer the torrent's own file list for that item (libtorrent `torrent_info`
  file paths — honor the `[\\/]` separator foot-gun, see
  `feedback_libtorrent_windows_backslash_separator`), falling back to a bounded
  recursive search for `canonicalFilename` under `destinationRoot`. Register the
  resolved path. Keep it idempotent (`filePathFor` guard already present).
- Verify it does NOT double-register or churn correctly-registered per-episode
  packs (Daredevil: Born Again must stay Play).

### Part B — register single-episode downloads on completion
- Add a registration hook for bare single-episode stream torrents: on the
  torrent reaching seeding/finished (the completion alert path that already drives
  `torrentCompleted`), if the record has imdb + season>0 + episode>0 and a file on
  disk, call `m_streamDownloadIndex->registerEpisode(...)` (the same call the cohort
  path uses). Reconcile any stale Pending/Downloading entry for that (imdb,season,
  episode) to Complete (registerEpisode's highest-quality-wins dedup handles the
  path; confirm it flips state).
- Also cover the **already-finished-but-unregistered** case (like the current stuck
  Invincible, finished while the app was killed): extend the one-shot reconcile so a
  single-episode torrent that is **seeding/complete** with a file on disk and an
  imdb/season/episode identity gets registered, mirroring `backfillStreamDownloadIndex`
  but over torrent records rather than cohort groups.

## Files
- **Modify:** `src/core/torrent/TorrentClient.cpp` (backfill + on-publish path + a
  single-episode completion/reconcile hook). `TorrentClient.h` only if a new private
  helper decl is needed. **Do not** touch the frozen `TorrentEngine` API (Congress 6,
  `022c4eb`).
- **Reference (consumer, already correct — do NOT change):**
  `src/ui/pages/stream/StreamDetailView.cpp` `episodeDisplayState()` — disk-first gatherer.
- **Index API:** `src/core/stream/StreamDownloadIndex.{h,cpp}` —
  `registerEpisode(imdbId, season, episode, canonicalPath, sourceGroupId, fileSizeBytes)`,
  `filePathFor`, `bestEntryForEpisode`, `entriesForImdb`.

## Verification (acceptance)
1. Build: `build_check.bat` → BUILD OK, verify `out\Tankoban.exe` mtime advanced
   (kill the app FIRST — `feedback_verify_exe_mtime_after_build`).
2. `out\tankoctl.exe get-downloads` now lists **One Piece S1** episodes + the
   single Invincible S4E01 with their real on-disk paths.
3. Relaunch → Theatre → **One Piece S1** = every episode shows **Play**;
   **Invincible S4E01** = **Play** (not "NN%"); **Daredevil: Born Again S2** still
   Play (no regression). Hemanth confirms by eye.
4. If `tankoban_tests` gains pure-logic coverage for a path-resolution helper, add it.

## Discipline
- gov-v13 flat-on-master, **no worktrees**. One change → one rebuild → verify mtime.
- `taskkill //F //IM Tankoban.exe` before every rebuild (Rule 1).
- Honor 4B's hand — understand the publish/cohort state machine before editing; this
  is the single most completion-sensitive file in the stream stack.
- **Do NOT push / merge.** Post `READY TO MERGE — [Agent 9 (DeepSeek V4-Pro), ...]`
  to agents/chat.md; Agent 4 (Opus) reviews the diff before master (mandatory
  reviewer pass). Sign all work `Agent 9 (DeepSeek V4-Pro)`.
