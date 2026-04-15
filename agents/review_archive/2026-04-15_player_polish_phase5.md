# Archive: Player Polish Phase 5 — Subtitle Visibility Consistency

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source:**
- Hemanth-reported subtitle bugs (chat.md:10700-10744) — Bug 1 "subs appear for some videos not others"; Bug 2 "can't change subtitles from context menu"
- `PLAYER_POLISH_TODO.md:221-241` — revised Phase 5 scope after discovery that sidecar's `subtitle_renderer.cpp` already implements libass+PGS end-to-end (original 5-batch build-out obsoleted)

**Outcome:** REVIEW PASSED 2026-04-15.
**Shape:** 1 P0 (sidecar crash vector), 2 P1, 5 P2 initially. P0 + P1 #1 fixed in-session; P1 #2 deferred-with-justification to Batch 5.3 backlog.

---

## Initial review (Agent 6)

Two bug-fix batches against two Hemanth reports. Agent 3's scope revision at TODO:221 accepted — original libass-port is correctly obsoleted by the pre-existing sidecar renderer.

Files reviewed:
- `src/ui/player/VideoPlayer.cpp` — three edit sites (restoreTrackPreferences unconditional send + SetSubtitleTrack visibility-on branch + SetSubtitleTrack "off" branch)
- `src/ui/player/SidecarProcess.cpp:332-344 + :193-197` — tracks_changed handler + sendSetTracks payload shape
- Sidecar `native_sidecar/src/main.cpp` — tracks_changed sub_visibility emit (:394-400), open-time preload (:586-599), handle_set_tracks (:785-860)

### Parity (Present) — 10 features shipped correctly

- Unconditional `sendSetSubVisibility` at restoreTrackPreferences end (fresh files get explicit command).
- Sidecar open-time `preload_subtitle_packets` matches mid-switch path shape exactly.
- SetSubtitleTrack visibility-on mirrors cycleSubtitleTrack pattern.
- Visibility-before-track-change ordering correct in both paths.
- Toast messaging preserved.
- Idempotency gates on both flip-and-send.
- Comment discipline references bug + root cause + fix intent.
- Scope revision at PLAYER_POLISH_TODO.md:221 pivots cleanly from libass-build-out to audit-driven fixes.
- Batch 5.3 on-flag defer is Hemanth-gated (not Agent 3-unilateral).
- Original Batch 5.1-5.5 retained in TODO as historical context.

### Gaps raised

**P0:**

1. **`sendSetTracks("", "off")` at VideoPlayer.cpp:1854 crashes sidecar.** Sidecar `handle_set_tracks` at main.cpp:835 does `std::stoi(new_sub_id)` which throws `std::invalid_argument` on `"off"`. Grep confirms zero try/catch in entire sidecar main.cpp — uncaught → `std::terminate` → sidecar dies mid-playback. The line is also redundant (preceding `sendSetSubVisibility(false)` already achieves user intent). Fix: delete the line.

**P1:**

1. Sidecar emits `tracks_payload["sub_visibility"]` but Qt never consumes it. Field semantics also lie (`!probe->subs.empty()` reports "has subs?" not "visible?"). Recommend dropping emission (honest minimum).

2. Open-time preload hardcodes `probe->subs[0]` — thrown away when restoreTrackPreferences issues set_tracks with user's language preference.

**P2:** (5 items) handle_set_tracks:828 has a parallel `sub_visibility` side-channel Qt doesn't populate; three duplicate visibility-on sites growing not shrinking; per-file-preference branch at :1402-1408 now double-sends with new unconditional send; open-time preload comment doc nit; TODO hygiene on obsoleted Batch 5.1-5.5 markers.

### Questions asked

1. Did Hemanth actually click Context Menu → Subtitles → Off during smoke? (Agent 3: no, not yet smoked.)
2. Was `sub_visibility` field intended to be consumed? (Agent 3: intended defense-in-depth; didn't follow through.)
3. Batch 5.3 refactor plan? (Agent 3: fold all duplication into 5.3 if Hemanth flags the T-key bug.)
4. Preload-only-when-no-preference considered? (Agent 3: yes, chose against at design; now convinced the right fix is Qt-side.)
5. TODO hygiene at :244? (Agent 3: yes please add the explanatory note.)

---

## Agent 3 response (fixes)

**P0 #1 — ACCEPTED + FIXED.** Deleted `sendSetTracks("", "off")` at VideoPlayer.cpp. Kept `sendSetSubVisibility(false)` in both state-flipping and already-hidden branches (idempotent cover for post-crash-recovery edge + state-divergence cases). Track selection stays intact. Comment references review date + Agent 6.

**P1 #1 — ACCEPTED + FIXED (sidecar rebuild required).** Dropped `tracks_payload["sub_visibility"]` emission at sidecar main.cpp:399. Comment references review + explains why the field is gone.

**P1 #2 — JUSTIFIED DEFER.** Right fix is Qt-side semantic change to `restoreTrackPreferences` (always resolve to concrete sub_id + always send set_tracks), which touches track-list cache timing — non-trivial. Bundled with P2 #1 + P2 #2 + P2 #3 into a coherent visibility-logic refactor to land if/when Batch 5.3 opens on Hemanth's T-key flag.

**P2s — all noted with appropriate dispositions.** P2 #1 (side-channel dup) comment-both-sides queued with 5.3. P2 #2 (three-site dup) fold into 5.3 helper. P2 #3 (per-file-pref redundant send) bundled with 5.3 refactor. P2 #4 (comment doc nit) batched with other doc cleanups. P2 #5 (TODO hygiene) accepted — Agent 3 or Agent 0 on next sweep.

---

## Agent 6 re-verdict

**Both fixes code-verified:**

- **P0 #1 fix at [VideoPlayer.cpp:1865-1886](src/ui/player/VideoPlayer.cpp#L1865-L1886)** — `sendSetTracks("", "off")` deleted. `sendSetSubVisibility(false)` kept in both branches; the idempotent already-hidden-branch send is a defensible over-engineering for post-crash-recovery + state-divergence edges. Comment references review date + Agent 6 so future readers understand the empty-looking branch. Crash vector eliminated. ✓

- **P1 #1 fix at sidecar main.cpp:394-402** — `tracks_payload["sub_visibility"]` emission removed. No behavior change on Qt side (Qt never consumed it anyway); dead code eliminated. Comment at :394-401 references this review. ✓

**P1 #2 defer accepted.** Agent 3's rationale is correct: fresh-file-no-preference cases genuinely rely on the open-time preload today, and the right fix is a Qt-side semantic change to `restoreTrackPreferences` that touches track-list cache timing. Bundling P1 #2 + P2 #1 + P2 #2 + P2 #3 into a Batch 5.3 visibility-logic refactor (triggered by Hemanth's T-key flag) is clean scope.

**P2 dispositions all sound.** Idempotent-send over-engineering in P0 fix is noted as deliberate defensive cover, not a concern.

**Hemanth Q1 note for operator:** Phase 5 smoke (including Context Menu → Subtitles → Off) has NOT yet been run. On sidecar rebuild + smoke, this path will be the first empirical validation that the crash vector is gone. If smoke crashes, there's an edge I missed; if smoke passes, P0 is fully closed.

### Verdict

- [x] All P0 closed (P0 #1 FIXED — delete line 1854 shipped)
- [x] All P1 closed or justified (P1 #1 FIXED; P1 #2 justified-defer to Batch 5.3 backlog)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, Player Polish Phase 5]** 2026-04-15.

Phase 5 closes. Batch 5.3 on-flag deferred per Hemanth-gated trigger. Visibility-logic refactor (P1 #2 + P2 #1/#2/#3) bundles into 5.3 if/when it opens. Sidecar rebuild required for P1 #1 fix to land; main-app P0 fix ships on its own.
