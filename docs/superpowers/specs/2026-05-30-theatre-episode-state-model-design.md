# THEATRE_EPISODE_STATE_MODEL — Design Spec (Phase 1)

- **Lifecycle:** active
- **Date:** 2026-05-30
- **Owner:** Agent 4 (Stream + Tankorent)
- **Status:** Design brainstormed with Hemanth 2026-05-30; awaiting spec review → implementation plan
- **Builds on:** `2026-05-29-theatre-download-simplify-design.md` (download UX collapsed to per-episode auto-download). This spec fixes the **state model** behind that UX.
- **Two-phase arc.** This is **Phase 1** (disk-first state model + visual language). **Phase 2** (seeder-tiered pack-vs-singles source selection + per-file pause engine) is captured in §9 and gets its own spec after Phase 1 proves out.

## §1 — Why (the architecture problem, not a symptom)

Three patches in one session (download routing → mode stamp → duplicate-resolution) each fixed a symptom and the episode table is **still** confusing. That is the signal to stop patching and fix the architecture (systematic-debugging Phase 4.5).

**Ground truth, verified on disk 2026-05-30:** all 13 Daredevil S2 episodes exist at `Media/TV/Marvels.Daredevil.S02…/…S02E0N.mkv`. Every episode is downloaded. Yet the UI shows **"Queued"** on every row and a **retry icon** on episode 1.

**Root cause — TWO independent state systems paint the same row from two different sources, and they disagree:**
1. **Status text column** (`renderEpisodeStateChip`, StreamDetailView.cpp:1530) reads `StreamDownloadIndex::Entry::State` (the download index / "substrate"). Pending → "Queued".
2. **Action icon column** (`resolveRowState` + `actionIconForState`, StreamDetailView.cpp:84-110) reads a **different** model — a `RowState` enum driven by the legacy **bulk-cohort** machinery (`cohortHit`/`cohortState`/`streamBulkSnapshotForImdbSeason`), a leftover from the Stremio/pack-mapping era. Failed/unknown → retry icon.

Neither system consults the **one fact that is always true: is the episode's file on disk?** (Playback already does — `filePathFor` + `QFileInfo::exists` at line 1373 — which is why play works while the status lies.) Both systems derive state from a fragile chain of download *events* (metadata→piece→finished). For an **already-complete / resumed** torrent (the season pack was seeding when Hemanth returned) those events don't re-fire, so rows stay stuck at their initial Pending="Queued" even though the files are right there.

**The fix:** one **disk-first** state derivation, feeding BOTH the status text and the action control. Rip out the second (cohort `RowState`) system for Theatre. One source of truth, one place to reason about, no event-chain fragility.

Hemanth verbatim: *"the UI's wiring is all over the place and confusing."* And on the fix: source of truth = **the disk**; *"if I'm downloading the entire season, the pause applies to the season. if I'm downloading a single episode, the pause applies to a single episode"* (pause is per-episode/per-file — qBittorrent-style file priority).

## §2 — Goals (Phase 1)

1. **One source of truth for episode state: the disk.** File present → **Downloaded**, unconditionally. Engine/index consulted ONLY for items not yet on disk (in-progress %, paused, failed).
2. **One state derivation** feeds both the status column and the action control. Delete the parallel cohort `RowState` path for Theatre.
3. **A clear, unambiguous visual language** (§4) — distinct affordances for download / pause / resume / **Play** / options, with no icon reused for two meanings.
4. **A right-click context menu** (§5): Cancel download, Delete from disk, Show in folder.
5. **Downloaded episodes read as Downloaded and STAY that way** across navigation, season-switch, and app restart — because the truth is re-derived from disk each paint, not cached from events.

## §3 — The state model (the core)

**Five states, derived per episode in priority order:**

| State | Derivation (first match wins) | Status text | Action control |
|-------|------------------------------|-------------|----------------|
| **Downloaded** | episode's file exists on disk (`filePathFor` → `QFileInfo::exists`) | *(blank)* | **`Play`** text button |
| **Downloading** | not on disk; engine has an active transfer covering it, not paused | `Downloading N%` | **pause** icon |
| **Paused** | not on disk; transfer exists but paused (per-episode/per-file) | `Paused N%` | **resume** icon |
| **Failed** | not on disk; transfer errored | `Failed` | **retry** icon |
| **Not downloaded** | none of the above | *(blank)* | **download** icon |

**Authority rule:** disk existence is checked FIRST and wins over any index/engine state. A file on disk is Downloaded even if a stale index entry says Pending — this single rule subsumes the whole duplicate-entry / stale-Pending bug class (the prior `pickBestEntry` patch becomes redundant for the *display*; it can stay as a harmless tiebreak for the not-on-disk case or be removed in cleanup).

**Where it lives:** one pure-ish helper — `EpisodeDownloadState deriveEpisodeState(imdbId, season, episode)` — that takes (a) a disk check via `StreamDownloadIndex::filePathFor` and (b) the in-progress transfer state from `TorrentClient` for not-on-disk items, and returns one enum + progress%. Both the status-render and the action-control builder call THIS, nothing else. The legacy `resolveRowState`/`RowStateInput`/`actionIconForState` cohort path (StreamDetailView.cpp:68-166) is removed for Theatre.

**Re-derivation, not caching:** state is recomputed from disk+engine on every populate / season-switch / re-entry, and on the existing `entryStateChanged` + the 1Hz progress poll already in `populateEpisodeTable`. No state is remembered across views (that's what let "100%" revert to "Queued").

## §4 — Visual language (Hemanth-specified)

Per-episode action control (the kColAction cell), one of:
- **Download** — SVG download-arrow. Not-downloaded → click starts the download.
- **Pause** — SVG pause icon. Shown while Downloading → click pauses *this episode* (Phase 2 wires the per-file mechanism; Phase 1 may pause the owning transfer).
- **Resume** — SVG play-circle (the "continue download" glyph). Shown while Paused → click resumes.
- **Play** — a small **text button literally labelled "Play"** (the deliberate rare-English-text exception Hemanth approved). Shown when Downloaded. Chosen because a play *icon* would collide with the resume-download glyph; text removes all ambiguity.
- **Retry** — SVG retry-arrow. Shown only on Failed.

Status text column: `Downloading N%` / `Paused N%` / `Failed` / blank (Downloaded + Not-downloaded both blank — the action control carries the meaning).

No emojis, gray/black/white, SVG icons only (house style, `feedback_no_color_no_emoji`).

## §5 — Context menu (right-click an episode row)

- **Cancel download** — only when Downloading/Paused. Stops + removes the in-progress transfer for this episode; does not touch already-finished files.
- **Delete from disk** — only when Downloaded. **Destructive** — deletes the episode's file from disk + evicts its index entry, returning the row to Not-downloaded. Behind a confirm dialog (Hemanth chose delete-on-remove). Reclaims space.
- **Show in folder** — when on disk. Opens the OS file manager at the episode's file.

(Items show contextually by state; a Not-downloaded row's menu may be empty or just offer Download.)

## §6 — Components: rewritten / removed / kept

**Rewritten (`src/ui/pages/stream/StreamDetailView.cpp`):**
- New `deriveEpisodeState(...)` — the single disk-first derivation (§3).
- `populateEpisodeTable` / `refreshSubstrateStatesForActiveSeason` — call `deriveEpisodeState` for both the status cell and the action control; one pass, one source.
- The kColAction control builder — render Download/Pause/Resume/Play(text)/Retry per derived state (§4), replacing `actionIconForState(RowState)`.
- Add the §5 context menu (`setContextMenuPolicy` + handler) to the episode table.

**Removed (Theatre state path):**
- `RowState` enum, `RowStateInput`, `resolveRowState`, `actionIconForState` (StreamDetailView.cpp:68-166) — the legacy cohort-driven second system.
- Cohort reads for per-row state (`streamBulkSnapshotForImdbSeason` for the table's row state). NOTE: the season-level **Download/Pause/Continue Season** button (`onDownloadSeasonClicked`) also reads cohort snapshots — Phase 1 leaves that button's logic alone unless it directly blocks the rewrite; flag at plan time.

**Kept unchanged:**
- `StreamDownloadIndex::filePathFor` + `QFileInfo::exists` (already the play/owned-check; now also the state authority).
- The download trigger (`startAutoDownload`) and play path (`beginPlayOrDownload`) from the prior arc.
- `TorrentClient` progress/completion signals (feed the in-progress states only).

## §7 — How we know it works

Smoke is **Agent 4's** job (tankoctl + disk inspection + relaunch). Hemanth's check is the taste call: *is it clear now.*

- Open Daredevil S2 (all files on disk) → **every episode shows the `Play` text button, no "Queued", no retry icon.** ← the exact bug, fixed.
- Click an episode → it plays from disk.
- Start a fresh not-downloaded episode → row shows download icon → click → `Downloading N%` + pause icon → completes → `Play` button. Leave the view and return → still `Play` (not reverted).
- Right-click a downloaded episode → Delete from disk → confirm → file gone, row returns to download icon.
- Right-click → Show in folder → opens at the file.
- gov-v11 hard gate: clean `build_check` BUILD OK; pure-logic `deriveEpisodeState` covered by `tankoban_tests`.

## §8 — Risks

1. **Disk check on every row paint** could be slow for big seasons. Mitigation: `filePathFor` is an in-memory index lookup; `QFileInfo::exists` is one stat per visible row (≤~25). Acceptable; cache within a single populate pass if measured slow.
2. **Removing the cohort RowState path** might be read elsewhere. Mitigation: grep all `resolveRowState`/`actionIconForState`/`RowState::` callers before deleting; the season button (§6) is the known one — handle or scope-defer.
3. **Delete-from-disk is destructive.** Mitigation: confirm dialog; delete only the specific episode file + its index entry, never a directory.
4. **Phase 1 pauses the owning transfer, not the single file yet** (per-file priority is Phase 2). If pausing one episode of a pack visibly pauses siblings, that's a known Phase-1 limitation — surface it honestly; Phase 2 fixes it.

## §9 — Phase 2 (deferred — separate spec, NOT this arc)

Captured so it's not lost:
1. **Seeder-tiered source selection** — well-seeded **complete-series / season pack** → use it; else **fan out to best-seeded single-episode torrents, bulk**. (Hemanth: recent shows lack packs → per-episode bulk; ≥6-month / completed shows have packs that out-seed singles → pack wins.)
2. **Per-file pause** — pause/resume one episode inside a multi-file pack via libtorrent **file priority = 0** (qBittorrent-style), so §4's per-episode pause is literal even for packs.
3. **Cinemeta per-file mapping** robustness for packs (each file → its real episode identity).

## §10 — Coordination

- gov-v13: flat-on-master, **Path A commit** (Agent 4 posts `READY TO COMMIT`, Agent 0 batches). No worktrees.
- `StreamDetailView.{cpp,h}` + `StreamDownloadIndex.{cpp,h}` remain a HOT active arc — Agent 0's REPO_STRUCTURE source-move on StreamPage/StreamDetailView stays deferred until this closes.
- Tankorent search index untouched (still preserved for future streaming, per the prior arc).
