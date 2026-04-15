# Archive: VIDEO_PLAYER_FIX Phase 1 — Subtitle Off path validation + unification

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `VIDEO_PLAYER_FIX_TODO.md:56-92` — Phase 1 batch list + exit criteria
- **Secondary (audit):** `agents/audits/video_player_2026-04-15.md` — audit P1 #7 (contradicting subtitle Off code paths)

**Outcome:** REVIEW PASSED 2026-04-16 first-read pass. 0 P0, 0 P1, 7 non-blocking P2 + 2 optional clarifications. Audit P1 #7 closed.

---

## Scope

Two batches: 1.1 validation-only (observation, zero code change) + 1.2 unification. Batch 1.1 verdict at TODO:76 confirmed `PARTIAL` — Paths 4 (TrackPopover) + 5 (SubtitleMenu) both latent-crash via `std::stoi("off")` at sidecar main.cpp:850, matching my own prior Phase 5 P0 finding. Batch 1.2 consolidates Phase 5's inline context-menu fix + closes the two latent paths via a canonical `setSubtitleOff()` helper + transitive rewrite at `SidecarProcess::sendSetSubtitleTrack(-1)`.

---

## Parity (Present)

### Canonical helper
- `VideoPlayer::setSubtitleOff()` declaration at VideoPlayer.h:173 with crash-vector docstring.
- Implementation at VideoPlayer.cpp:1348-1357: `m_subsVisible = false` + `sendSetSubVisibility(false)` + toast. Track selection preserved.

### Three Off-path reroutings
1. **Path 1 (context-menu Off)** consolidated at VideoPlayer.cpp:2283-2290 — `setSubtitleOff()` call supersedes Phase 5 P0 inline fix.
2. **Path 4 (TrackPopover Off id==0)** at VideoPlayer.cpp:895-901 — direct `setSubtitleOff()` call.
3. **Path 5 (SubtitleMenu Off)** rides transitive fix at SidecarProcess.cpp:509-524 — `sendSetSubtitleTrack(-1)` rewritten from `sendSetTracks("", "off")` to `sendSetSubVisibility(false)`. `m_activeSubIndex = -1` + `subtitleTrackApplied(-1)` emissions preserved for API stability.

### Coherency fix
- `subVisibilityChanged` handler extension at VideoPlayer.cpp:227-238 — mirrors sidecar-emitted visibility to `m_subsVisible`. Closes the Path 5 coherency gap (pre-fix, SubtitleMenu-driven Off left `m_subsVisible` stale-true, breaking the next `T` toggle). Handler also clears subtitle overlay text on visibility=false.

### Wire-level guarantee
- Grep confirms zero `sendSetTracks(..., "off")` call sites remain main-app-side.
- `std::stoi("off")` at sidecar main.cpp:850 is unreachable by any main-app-originated payload.

### Scope-restraint
- SubtitleMenu.cpp + TrackPopover.cpp explicitly untouched per TODO:86. Consolidation happens where all paths converge (VideoPlayer + SidecarProcess), not at each caller.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (all non-blocking):

1. Sidecar-side `std::stoi` hardening deferred — TODO:84 acknowledges, worth adding as defense-in-depth in a future sweep.
2. `toggleSubtitles()` at :1341-1346 doesn't route through `setSubtitleOff()` (minor hygiene — functionally equivalent today).
3. `m_subsVisible` writes fan out across 5 sites. Could consolidate into a single private setter. P2 hygiene.
4. Overlay text clearing on visibility=false correctly colocated in `subVisibilityChanged` handler (fires for all off-transitions including sidecar-driven).
5. TODO:86 scope-fence respected — TrackPopover signal-consumer code lives in VideoPlayer, not a fence violation.
6. `sendSetSubtitleTrack(-1)` return-value + emission semantics preserved (API contract intact).
7. Comment density is high — could consolidate into single "see setSubtitleOff" reference in future cleanup.

---

## Agent 6 verdict

- [x] All P0 closed (audit P1 #7 class-of-bug closed; crash vector unreachable)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, VIDEO_PLAYER_FIX Phase 1]** 2026-04-16 first-read pass.

Tight, well-scoped fix that consolidates what was previously fragmented across three entry points into a single canonical helper + a transitive rewrite at the sidecar-communication layer. Zero files touched outside the declared scope-fence.

**Optional clarifications:**
1. Was Batch 1.1 validation actual runtime repro or code-path analysis? Either justifies the fix.
2. Suggest flagging sidecar `std::stoi` hardening in active open-debt list for a future grep-friendly sweep.
