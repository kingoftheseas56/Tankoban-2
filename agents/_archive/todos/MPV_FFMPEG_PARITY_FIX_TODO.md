# MPV ↔ ffmpeg Parity Fix TODO — Tankoban 2

**Authored 2026-04-30 by Agent 0 at Hemanth's direct request after Agent 7 Trigger-C audit landed at `agents/audits/ffmpeg_vs_mpv_player_diagnosis_2026-04-30.md` 2026-04-30 ~13:00.**
**Owner: Agent 3 (player domain, HEMANTH-DRIVEN per `feedback_hemanth_driving_player_domain.md`).** Phase 1 stream retrofit overlaps Agent 4 — see §Decisions Q3.

Source intent: Hemanth's verbatim 2026-04-30 corrected framing — "I just wanted to know if we can replace [our ffmpeg] with mpv while maintaining all the existing functionality of the ffmpeg player and while creating new scope for all the possible improvement that could be brought into our player due to it being mpv." Two-pronged scope:

1. **Parity** — what work is required to preserve every existing ffmpeg-player feature on the mpv backend.
2. **Opportunity** — what new capabilities mpv unlocks that the custom ffmpeg sidecar doesn't easily provide, framed as ratifiable scope-additions.

This TODO scopes both halves. The cutover decision (whether mpv actually becomes sole backend, and on what timeline) is **downstream** of this TODO — it happens after parity gates close + Hemanth picks which opportunity items to bring in + Hemanth ratifies cutover separately. No calendar deadline is assumed.

This TODO does **not** recommend ditching ffmpeg. It does not commit to a deprecation deadline. It scopes the work required to make those decisions answerable with concrete data.

---

## Context

The audit answers two halves of the §Q6 decision:

1. **Is the existing mpv integration sound enough to build on?** Audit §3 verdict: **yes.** mpv path uses libmpv render API + Qt-owned `QOpenGLWidget` framebuffer. Same architectural family as Stremio's reference; explicitly NOT the rejected `--wid` child-window pattern from TankobanQTGroundWork. Passes Hemanth's veto line.

2. **Is mpv the right strategic survivor?** Audit §4 verdict: **yes** — but conditional. Most "real mpv" features (hwdec modes, gpu-next, ICC, codec growth, subtitle behaviors, color management) are upstream libmpv responsibilities. On the mpv path, adding them ≈ expose property + observe + emit signal. On the ffmpeg path, adding them = sidecar handler + native pipeline implementation + IPC payload + UI bridge + cross-cutting verification. Strategic asymmetry favors mpv.

The audit's recommendation (§5 Option A) — that mpv is the strategically-correct survivor — flags two gates that must close before any cutover decision is answerable:
- **Stream-mode gate** — current `BackendFactory::chooseFor(true)` returns ffmpeg correctly, but `VideoPlayer` only calls `chooseFor(false)` once at construction (`VideoPlayer.cpp:192-193`) and `setStreamMode(true)` doesn't reselect (`VideoPlayer.cpp:704-831`). The §Q4 stream-mode-locked-to-ffmpeg policy isn't actually enforced per-open. Either fix the lock OR build an mpv stream adapter.
- **Same-file smoke gate** — no fresh side-by-side capture exists. All audit numbers are from old logs. Before deletion, both backends need to be smoked on identical files (subs + seek + track switch + pause/resume + close/reopen + HDR + format coverage) with new captures.

Plus known mpv path incompletions per audit §1: subtitle style + filter batch are stubs (`MpvBackend.cpp:734-766`), some comments still reference the abandoned WGL interop plan, no stream adapter exists.

This TODO breaks the gate-closure work into ratifiable phases.

---

## Objective

This TODO has two halves. **Parity** = what work makes the answer to "can mpv preserve everything ffmpeg does today" go from "unverified" to "yes, demonstrated." **Opportunity** = what work catalogues + scopes the new capabilities mpv enables that ffmpeg doesn't easily.

### Parity half (Phases 1-6)

After Phases 1-6 ship, on a fresh per-file smoke matrix:

1. A user playing any local file gets bit-identical UX across both backends — same HUD, same chips, same right-click menus, same keyboard shortcuts, same fullscreen behavior — regardless of which backend renders.
2. A user playing any stream-mode file gets the same HUD + buffered-range overlay + stall pause/resume behavior on both backends.
3. A user with an HDR file gets tone-mapping behavior subjectively indistinguishable across backends on Hemanth's hardware.
4. Subtitle behavior matches across backends (Y-offset slider, PGS rendering, ASS karaoke, SRT, external subs).
5. Audio behavior matches (track switch, DRC, delay, channel mapping, passthrough where verifiable).
6. Diagnostic surfaces match in shape (`compare-mpv-tanko.ps1` produces apples-to-apples divergence reports for both backends).

### Opportunity half (§New scope mpv enables)

After §New scope is enumerated + Hemanth picks which items to bring in:

7. A documented, ratifiable list of mpv-native capabilities not currently in Tankoban — each scoped as a future fix-TODO candidate (or rolled into a follow-up arc) so the strategic upside is concrete, not handwaved.
8. The picked items have an unblock path that depends only on mpv being the active backend for the user playing — no new infrastructure beyond what Phases 1-6 already require.

### Cutover (downstream decision)

After Phases 1-6 close + §New scope is enumerated, Hemanth makes a SEPARATE ratification: pull the trigger on cutover (delete ffmpeg + flip default to mpv), keep both backends indefinitely, or pause the question. This TODO does NOT commit to any of those outcomes. Phase 7 below is the *mechanics* of cutover IF that path is picked — not a default outcome.

---

## Architecture lock

**End state IF cutover is ratified downstream:** Tankoban ships with libmpv as sole video backend. `IPlayerBackend` interface stays; `MpvBackend` is the only implementation. `SidecarProcess` + `native_sidecar/` deleted. Stream mode runs entirely through `MpvBackend`. Phase 7 below is the mechanics of that cutover.

**End state IF cutover is NOT ratified:** Tankoban keeps both backends; user picks per-file or per-show via existing right-click entries. Phase 7 stays unfired. This TODO closes after Phases 1-6 + §New scope enumeration; cutover question deferred to a future ratification.

**Reference architectural commitments preserved from `MPV_RENDER_API_INTEGRATION_TODO`:**
- libmpv linked in-process (no subprocess)
- libmpv render API → `QOpenGLWidget` (no `wid` child-window embedding)
- Same Qt-owned chrome above the video surface for both backends during the dual-backend window
- `IPlayerBackend` abstraction stays as the single point of contact; deletion at Phase 7 means deleting the ffmpeg implementation, not the interface

**What this TODO does NOT touch:**
- The `IPlayerBackend` interface shape (already settled in MPV_RENDER_API P2).
- libmpv's render approach (`QOpenGLWidget` direct paint already settled in MPV_RENDER_API P5 redux per `user_hardware_intel_uhd_620.md`).
- Hemanth's veto on `--wid` embedding.

---

## Reference slate

- **Source audit:** `agents/audits/ffmpeg_vs_mpv_player_diagnosis_2026-04-30.md` (Agent 7 Trigger-C, 2026-04-30). §1 architecture diagnosis, §2 capability matrix, §3 integration shape verdict, §4 parity-cost analysis, §5 recommendation, §followup evidence requirements.
- **Anchor TODOs:** `MPV_RENDER_API_INTEGRATION_TODO.md` (precedent — §Q pattern + Phase 0 ratification gate + 60-day window setup), `LIBPLACEBO_SINGLE_RENDERER_FIX_TODO.md` (comparable env-gated rollout shape).
- **Anchor memories:**
  - `project_mpv_backend_integration_complete.md` — §Q4/§Q6 anchors, current default behavior, libmpv bundle metadata.
  - `feedback_hemanth_driving_player_domain.md` — direction-picking is Hemanth's; this TODO surfaces decisions, doesn't make them.
  - `feedback_subtitle_position_yoffset_not_libass.md` — known mpv subtitle gap (Y-offset slider). Phase 2 scope.
  - `feedback_lifecycle_parity_with_mainwindow.md` — open/close discipline; backend swap mid-session has the same parity hazard.
  - `user_hardware_intel_uhd_620.md` — Hemanth's dev hardware. Phase 5 already pivoted once for D3D11/GL interop fail; assume more such surprises during stream-mode work.
  - `feedback_no_human_days_in_agentic.md` — phase estimates below in summons, not days.
  - `feedback_fix_todo_authoring_shape.md` — 14-section template mirrored here.
- **Cross-domain anchors:**
  - `project_stream_server_pivot.md` — stream-server architecture; Phase 1 stream-mode work has to integrate with this.
  - `project_sidecar_dispatcher_non_blocking_decision.md` — sidecar's non-blocking dispatcher work pre-dates the §Q6 deletion plan; that work gets deleted at Phase 7 cutover if Option A wins.
- **Existing surfaces (cite into during phase execution, not before ratification):**
  - `src/ui/player/MpvBackend.{h,cpp}` — current libmpv implementation. Subtitle style + filter batch stubs at `:734-766`. Property observation at `:211-227`. Command surface at `:581-779`.
  - `src/ui/player/MpvVideoWidget.{h,cpp}` — `QOpenGLWidget` render surface.
  - `src/ui/player/BackendFactory.{cpp,h}` — `chooseFor(isStreamMode, ...)` policy at `:24-36`.
  - `src/ui/player/VideoPlayer.{h,cpp}` — backend construction at `:192-193`; `setStreamMode` at `:704-831` (does NOT reselect backend — audit-flagged bug).
  - `src/ui/player/SidecarProcess.{h,cpp}` — ffmpeg backend, deletion target at Phase 7.
  - `native_sidecar/` — entire tree, deletion target at Phase 7.
  - `src/ui/pages/StreamPage.{cpp,h}` — stream-mode integration; Phase 1 owner.
  - `out/ipc_latency.log` + `scripts/compare-mpv-tanko.ps1` — current diagnostic surfaces; Phase 5 builds mpv-side equivalents.

---

## Decisions (Rule 14 — Hemanth ratifies wholesale or per-question)

**RATIFIED 2026-04-30 ~14:50pm via AskUserQuestion.** Q1-Q6 wholesale-recommended; Q7 overridden to "permanent archive (never delete)" per Hemanth's preference for cheap-revert hedge over clean-history minimalism.

Picks below now load-bearing for Phase 1+ execution.

### Q1 — Cutover commitment: does this TODO commit to cutover at parity-GREEN, or stays as a downstream-only decision?

**RATIFIED: DOWNSTREAM-ONLY.** This TODO closes parity gates + enumerates new-scope opportunities. The cutover decision (move ffmpeg sources to `agents/_archive/`, flip default to mpv) is a SEPARATE Hemanth ratification that fires only when (a) Phases 1-6 are GREEN AND (b) Hemanth has picked which §New scope items to bring in AND (c) Hemanth ratifies cutover. Phase 7 below is the *mechanics* IF that ratification happens; if it doesn't, Phase 7 stays unfired and this TODO archives after Phase 6 + §New scope.

### Q2 — Pacing: Hemanth-driven cadence (each phase fires on demand) or paced summon stream?

**RATIFIED: HEMANTH-DRIVEN.** Player domain is Hemanth-driven per `feedback_hemanth_driving_player_domain.md`. Each phase fires when you summon Agent 3 for it; no calendar pressure, no auto-scheduled cadence. Honest summon estimate across all phases: **~15-25 summons** total (Phase 1 stream retrofit is the heaviest at 5-8 summons alone). At your typical multi-phase-per-wake pace this likely closes in 5-10 wakes if you stay focused on it; it could stretch indefinitely if you pause for other work. No deadline, no forcing function — your call entirely on tempo.

### Q3 — Per-phase ownership: Agent 3 sole or Agent 3 + Agent 4 joint?

**RATIFIED: Agent 3 owns all phases; Agent 4 consults on Phase 1.** Player domain is HEMANTH-DRIVEN end-to-end per `feedback_hemanth_driving_player_domain.md`; Hemanth drives direction, Agent 3 executes. Phase 1 (stream retrofit) sits at the player↔stream boundary — Agent 3 implements the MpvBackend stream surface, but Agent 4 reviews how it integrates with `StreamPage` + stream-server lifecycle. Joint ownership splits accountability; consultation preserves Agent 3's player-domain throughline.

### Q4 — Phase 6 SW fallback: in scope of THIS TODO, or stays deferred until cutover-ratified?

**RATIFIED: STAYS DEFERRED until cutover is ratified downstream.** With ffmpeg still present, SW fallback isn't load-bearing — ffmpeg IS the fallback for users whose hardware can't run libmpv's GL renderer. SW fallback only becomes load-bearing if/when cutover-to-mpv-only is ratified. Hold this work until that decision lands; if it never lands, Phase 6 never fires.

### Q5 — Parity validation: trust `compare-mpv-tanko.ps1` harness or require fresh side-by-side smokes per phase?

**RATIFIED: HARNESS + HEMANTH SMOKE, per phase.** The harness (log-parser only — feeds two recorded logs, classifies divergence) is necessary but not sufficient. Each phase Hemanth runs a smoke matrix on his hardware against a curated file set covering that phase's scope (subs file for Phase 2, HDR file for Phase 3, etc.); logs feed into `compare-mpv-tanko.ps1` for objective drop/stall/divergence numbers; subjective "does it feel right" verdict stays Hemanth's call.

### Q6 — Default-backend flip during this arc: stay default-ffmpeg until cutover, or flip earlier to drive real-world exposure?

**RATIFIED: STAY default-ffmpeg until cutover is ratified.** Mid-arc flip would force daily playback through an in-progress mpv backend before each phase's gaps are closed; that creates noisy "regression" reports that conflate scope-known incompletions with real bugs. Keep ffmpeg the daily default; mpv stays opt-in via right-click "Use mpv player" entries during the parity arc. Default flips at cutover ratification (downstream of this TODO), or never if cutover doesn't get ratified.

### Q7 — Rollback path IF cutover gets ratified later + a post-cutover regression surfaces?

**RATIFIED: PERMANENT ARCHIVE (never delete).** Hemanth override of the recommended "no hedge" pick. On the cutover commit (if/when fired), the ffmpeg backend sources don't get deleted — they `git mv` to `agents/_archive/native_sidecar/` + `agents/_archive/sidecar_process/` + similar paths. Build wiring (CMakeLists.txt + build_and_run.bat + setup.bat) drops the ffmpeg paths so the archived sources don't compile or ship; they stay in the repo as a cherry-pick / revert reference. Never deleted.

**Implications:**
- Repo carries the archived source forever (heaviest tail option).
- Cheap-revert path always available — `git mv agents/_archive/native_sidecar/ native_sidecar/` + revert CMake drop = ffmpeg backend back in business.
- `agents/_archive/` is already a tracked convention in this repo (see `agents/_archive/todos/`); the new pattern reuses it cleanly.
- Phase 7 mechanics below reflect this: cutover = `git mv` + build-wiring drop, NOT `git rm` of the source tree.
- The archived sources do NOT track upstream FFmpeg changes — they're frozen at cutover-time. If the codebase needs FFmpeg-via-sidecar functionality 6 months post-cutover, the archived sources will be 6-months-stale; revert is still possible but probably wants a freshen pass.

### Phase 2 Decisions — subtitle-specific (Q1-Q4)

**Status: RATIFIED 2026-04-30 ~21:25pm via AskUserQuestion.** All four wholesale-recommended; no overrides. These four subtitle-specific decisions are user-facing visual/UX defaults — legit product calls per Rule 14. Sub-phase ordering / ratification gating / fast-track timing are coordination mechanics handled inside the §Phase 2 ratification gate paragraph above (Agent 0 + Agent 3 calls per Rule 14 + `feedback_coordination_mechanics_not_hemanth.md`), NOT Hemanth questions. Agent 7 audit + Agent 3 validation back the picks.

**Q1 — Position policy default.**

**RATIFIED: Standard (libass-native / mpv `sub-pos`) by default; Force-position as opt-in toggle.** Respects authored ASS layout (signs + karaoke + multi-event) on ~95% of anime files. Hemanth's 2026-04-25 "still too high" complaint was on simple-dialogue files where Standard mode also works after `ass_set_use_margins` tuning. Audit §5(d).7 aligns. Force mode preserves the existing Y-shift hack as opt-in for users who want explicit override.

**Q2 — Background plate default.**

**RATIFIED: Off by default.** Netflix defaults to plate-off; subtle drop-shadow handles contrast on busy frames; opt-in plate available for accessibility. Audit §3.7 + §5(d).5 align.

**Q3 — Font family default.**

**RATIFIED: Arial fallback when authored font missing AND user hasn't picked a font + add font-choice tunable in 2.G UI.** Three options in the tunable: "default (Arial fallback when authored font missing)" / "readable sans (Source Sans 3 or similar)" / "authored font (use embedded ASS font, paired with 2.E attachment fix)." Arial stays as the universal-fallback floor. **Agent 0 voice-of-reason flag carried into 2.G implementation: UI must clarify Arial is the floor not the ceiling** — tooltip on the "default" option must say "Arial only used when authored font missing; pick 'readable sans' or 'authored font' for best-possible." Audit §3.3 streaming-product-font dimension + Crunchyroll-respect via "authored font" + accessibility via "readable sans."

**Q4 — Force-authored-styling toggle default.**

**RATIFIED: Off by default** per audit §5(d).7. Anime ASS authored styles (signs, karaoke, alternate styles per character) are the user-visible value of fan-subbed releases; Tankoban doesn't override unless user explicitly opts in per-session. The toggle exists for users who want consistent typography across mixed content but is not the default.

---

## Phases

### Phase 0 — Decisions (Hemanth ratifies)

7 questions above. Wholesale ratification or per-question. **Phase 1 hard-blocked on §Decisions lock + Agent 7 audit-derived per-phase scope fold-in.** No src/ touched.

**Acceptance:**
- Hemanth answers all 7 OR ratifies all PROPOSED picks wholesale.
- Agent 0 folds audit §2 capability matrix into Phase 2-4 scope detail post-ratification (per audit dependency block).

**Files:** none.

**Estimate:** 1 ratification cycle (no summons, conversational).

---

### Phase 1 — Stream-mode retrofit (HEAVIEST PHASE) ✅ CLOSED 2026-04-30

**Status (2026-04-30 ~20:05pm):** All sub-phases shipped + Hemanth eyes-on-screen verified. Sub-phase RTCs at `agents/chat.md` lines: 2107 (Pre-1.A right-click parity foundation) → 2123 (Pre-1.A.1 hotfix MpvVideoWidget swap) → 2128 (1.A §Q4 lock fix + 1.A.3 FORCE_MPV precedence) → 2140 (1.B mpv stream probe evidence) → 2145 (1.C cache-state property wiring) → 2155 (1.E + 1.E.1 transparent stall semantics + buffering-overlay dismiss-timing). 1.D engine-driven stall-path m_inStallPause gate folded into the Phase 1 close RTC. 1.F HUD title verified GREEN via Hemanth's prior screenshot showing `Invincible (2021) · S01E01 · It's About Time`. 1.G Stremio reference dig skipped — sub-phases shipped cleanly without it. Memory: `project_mpv_ffmpeg_parity_p1_complete.md`. Awaits Hemanth fire on Phase 2 — subtitle parity (mpv libass default styling vs Tankoban bundled subtitle_renderer.cpp custom outline+font; visible delta flagged during 1.E re-smoke).

**Why:** Per audit §2 + §4, mpv path has zero stream-mode integration. `BackendFactory::chooseFor(true)` returns ffmpeg correctly, but `VideoPlayer.cpp:192-193` only calls `chooseFor(false)` at construction and `setStreamMode(true)` (`VideoPlayer.cpp:704-831`) doesn't reselect — the §Q4 lock is broken in practice. Two routes; Hemanth picks during Phase 0:

**Route A (smaller scope):** Fix the lock. `setStreamMode(true)` re-invokes `BackendFactory::chooseFor` and swaps backend mid-session if the saved preference was mpv. Stream files always route through `SidecarProcess`. mpv never plays stream content. Acceptable IF Phase 7 keeps ffmpeg-stream-only as a permanent split — which contradicts §Q1 deletion. So Route A is rejected if Q1 ratifies deletion.

**Route B (Phase 1 actual scope):** Build the mpv stream adapter. Teach `MpvBackend` to accept stream-server HTTP URLs, integrate buffered-range reporting, hook stall pause/resume into mpv's `paused-for-cache` semantics, mirror `StreamPage`'s lifecycle expectations.

**Scope (Route B, ratified at Q1=deletion):**
- Cross-reference Agent 7 audit §2 stream-mode row + §4 streaming-friendly subsection for known gaps.
- Cross-reference `project_stream_server_pivot.md` for stream-server REST/URL surface.
- Investigate Stremio reference at `C:\tools\stremio-shell\` — their mpv-side stream-server bridge is the closest existing pattern. Audit notes the local path was missing at audit time; Hemanth either populates it or Agent 3 web-fetches Stremio's `mpv.cpp` upstream.
- Per-feature scope filled in post-Phase-0 ratification.

**Files (preliminary, refined post-Phase-0):**
- `src/ui/player/MpvBackend.{h,cpp}` (stream URL handling, buffered-range hooks, stall paused-for-cache wiring)
- `src/ui/player/VideoPlayer.cpp` (`setStreamMode` reselects backend; the broken lock fixes either way)
- `src/ui/player/BackendFactory.{h,cpp}` (per-open reselection API)
- `src/ui/pages/StreamPage.cpp` (verify integration, no behavior change if mpv adapter mirrors ffmpeg shape)

**Acceptance:**
- A stream-mode file plays via mpv backend with buffered-range overlay + stall pause/resume working. Visually + behaviorally equivalent to ffmpeg-backed stream playback.
- `compare-mpv-tanko.ps1` on a known-stalling file shows mpv stall recovery within tolerance of ffmpeg.
- Existing ffmpeg stream playback unregressed (smoke same file via right-click "Use ffmpeg player").
- BUILD GREEN.

**Estimate:** 5-8 summons. Largest single phase. May surface scope work that pushes other phases.

---

### Phase 2 — Subtitle quality (BEST-POSSIBLE bar, BOTH backends) ✅ CLOSED 2026-04-30

**Status (2026-04-30 ~23:30pm):** Phase 2 closed end-to-end across one wake (~22:00-23:30pm). 7 sub-phases shipped (~330 LOC across 14 files: 3 sidecar + 11 main app); 3 build-green checkpoints; MCP smoke matrix walked under "skies are clear" with 4 PNG evidence files at `agents/audits/evidence_phase2_*`. Sub-phase RTC anchors at `agents/chat.md` lines: Task 1+2 corpus+ratification close at chat.md:2351 (Q1-Q4 wholesale ratified + 7-slot corpus locked) → Task 3 (2.E ffmpeg attachment pass — `AVMEDIA_TYPE_ATTACHMENT` branch + `ass_add_font` wiring; ~81 LOC sidecar-only) ship → Task 4 (2.D mpv style bridge — Q5 visual-spec startup constants + sendSetSubStyle 3-property bridge; ~30 LOC main-app-only) ship → Task 5 (2.F positioning policy — `PositionMode { Standard, Force }` enum + atomic + conditional libass+PGS render paths + new IPC + VideoPlayer dispatch + SettingsPopover Force-position checkbox; ~140 LOC across 8 files) ship → Task 6 (2.G UI/persistence first slice — Sub size +/- buttons mirroring existing audio/sub-delay row shape + persistence; ~80 LOC across 4 files) ship → Task 7 close gate at chat.md:[Task 7 RTC]. Memory: `project_mpv_ffmpeg_parity_p2_complete.md` (sibling to P1 close memory). Plan archive-ready at `~/.claude/plans/mpv-ffmpeg-parity-phase2-subtitles.md`. Optional follow-ups deferred per `feedback_player_minimalism_pattern.md`: Task 6.B (outline-thickness slider), 6.C (background-plate toggle), 6.D (font-choice dropdown), force-authored explicit toggle, max-line for SRT — all fire on Hemanth demand. Phase 3 (HDR + tone-mapping parity) + Phase 4 (audio polish) remain on Hemanth-driven fire.

**Phase 2 visible Hemanth-side wins (4 user-facing changes):**
1. Anime ASS files render in their authored fonts, not Arial fallback (Task 3 — Vinland Roboto + Saiki FRAHVIT/framd visually confirmed via MCP smoke evidence).
2. Authored ASS layout (signs, karaoke, multi-event) wins by default (Task 5 Standard mode — Saiki credit-roll multi-event ASS at 2:44 with 8+ \pos events preserved verbatim).
3. Settings popover gains Force-position checkbox to opt-into the 2026-04-25 Y-offset hack (Task 5).
4. Settings popover gains Sub size +/- buttons + persistence (Task 6 first slice).

---

**Why:** Reframed 2026-04-30 ~20:20pm per Hemanth verbatim: *"I want the subtitles to be the best they can, so unless our ffmpeg has the best subtitles possible, the plan needs an update."* The bar is **best-possible subtitle rendering benchmarked against Netflix + Crunchyroll**, not parity-with-whatever-Tankoban-currently-ships. If the ffmpeg path falls short of best-possible, both backends get raised. Validated by Agent 7 audit `agents/audits/subtitle_quality_audit_2026-04-30.md` (211 lines, MATCHES STANDARD / DEVIATES framing per `feedback_audit_framing_standard_not_better_worse.md`) + Agent 3 same-turn validation pass at chat.md:2227 (5 high-leverage cites verified, 3 hypotheses confirmed, one §2.6 nit captured below).

**Sub-phase ladder (lifted from audit §6 with 2.A marked DONE):**

- **2.A audit ✅ DONE 2026-04-30** — Agent 7 audit shipped + Agent 3 validation pass closed the audit→validate gate per `feedback_audit_validation_same_turn.md`.
- **2.B ratify visual spec ✅ DONE 2026-04-30 ~21:05pm** — Q1-Q4 wholesale-ratified by Hemanth ("go with the proposed options you've given"). Picks: Q1 Standard default + Force opt-in / Q2 background-plate off / Q3 Arial fallback + font-choice tunable in 2.G / Q4 force-authored off. Memory: `project_mpv_ffmpeg_parity_p2_decisions.md`.
- **2.C build evidence corpus ✅ DONE 2026-04-30 ~21:25pm** — Agent 3 ffprobe-walked `Media\TV\` per Hemanth verbatim "pick whatever you want from the library and the same for stream mode." 7 slots covered by 6 files (slot 3+4 doubles up on Vinland S02E01: rich ASS authoring + 5+ embedded fonts). Locked at chat.md:2351.
- **2.D mpv style bridge ✅ DONE 2026-04-30 ~22:30pm** — `MpvBackend.cpp:148-166` initializeMpv gains 6 setOpt calls for Q5 visual-spec constants (sub-color #FFFFFFFF / sub-border-color #FF000000 / sub-shadow-color #80000000 / sub-shadow-offset 1.0 / sub-border-style outline-and-shadow / sub-ass-override no); `MpvBackend.cpp:820-846` sendSetSubStyle stub replaced with sub-font-size + sub-margin-y + sub-border-size property writes. ~30 LOC main-app-only. **`sub-scale` NOT re-wired** per Agent 3 validation §2.6 nit (already plumbed at MpvBackend.cpp:870 from prior Task 6 wiring chain).
- **2.E ffmpeg attachment pass ✅ DONE 2026-04-30 ~22:15pm** — `native_sidecar/src/demuxer.cpp:620-640` gains `AVMEDIA_TYPE_ATTACHMENT` branch + `AttachmentFont` struct extraction; `subtitle_renderer.cpp` gets `add_font()` wrapping `ass_add_font`; `main.cpp` wires post-probe iteration BEFORE `load_embedded_track` per libass binding-at-track-parse-time requirement. ~81 LOC sidecar-only. Visually confirmed via MCP smoke (Vinland OP CJK credits + stylized authored fonts vs pre-fix Arial fallback / box placeholders).
- **2.F positioning policy pass ✅ DONE 2026-04-30 ~22:45pm** — Sidecar: `PositionMode { Standard, Force }` enum + atomic + conditional libass + PGS render paths gated on mode. Main app: IPlayerBackend.h adds sendSetSubtitlePositionMode pure-virtual; SidecarProcess maps to set_sub_position_mode IPC; MpvBackend logs one-time warning on Force (Force is ffmpeg-only — mpv would need libmpv render-hook, out of Phase 2 scope per plan Step 5.7); VideoPlayer adds m_subPositionMode + setSubPositionMode + onSidecarReady restore; SettingsPopover adds Force-position checkbox row with QSignalBlocker-protected setter. ~140 LOC across 8 files.
- **2.G UI/persistence first slice ✅ DONE 2026-04-30 ~22:55pm** — Sub size +/- buttons in SettingsPopover (5th row mirroring existing audio/sub-delay/sub-position +/- shape) + Sub size persistence (videoPlayer/subtitleSize QSettings + restore on backend ready). `VideoPlayer::adjustSubtitleSize` clamps 0.5..2.0 + std::round-snap-to-0.1 + qFuzzyCompare dedupe. ~80 LOC across 4 main-app files. Closes Task 4's deferred sub-scale persistence flag specifically. **Task 6.B/C/D follow-ups DEFERRED** per `feedback_player_minimalism_pattern.md`: outline-thickness slider, background-plate toggle, font-choice dropdown, force-authored explicit toggle, max-line for SRT — all fire on Hemanth demand.
- **2.H same-file backend smoke ✅ DONE 2026-04-30 ~23:30pm** — MCP smoke matrix walked under "skies are clear" with 4 PNG evidence files at `agents/audits/evidence_phase2_*`. Pulls 1+2 (Vinland fonts + Saiki multi-event ASS + Settings popover content + Sub size buttons + Force toggle) strong VISUAL PASS. Pulls 3+4 (Sopranos PGS + FORCE_MPV JoJo SRT) STRUCTURAL/MECHANICAL PASS — visual SRT-on-mpv + PGS-Force-flips-position deferred to future Hemanth subtitle smoke.

**Files:**
- `src/ui/player/MpvBackend.{h,cpp}` — 2.D mpv property bridge.
- `src/ui/player/SubtitlePopover.{h,cpp}` — 2.G UI surface (primary candidate).
- `src/ui/player/SettingsPopover.{h,cpp}` — 2.G UI surface (alternative; Agent 3 picks per Rule 14).
- `native_sidecar/src/demuxer.cpp` — 2.E attachment-stream branch + font extraction.
- `native_sidecar/src/subtitle_renderer.cpp` — 2.E `ass_add_font` integration call site.

**Acceptance:**
- 2.B visual spec ratified (Q1-Q4 + small tunables list locked).
- 2.D: ~7 mpv `sub-*` properties wire-tested; styling matches the 2.B spec on ASS + SRT corpus.
- 2.E: anime ASS file with embedded fonts renders authored typography on the ffmpeg path (no silent Arial fallback).
- 2.F: Standard mode default; Force mode opt-in toggle preserved; Saiki karaoke renders without Y-shift hack interfering with authored MarginV.
- 2.G: tunables surface present in popover; persistence across restarts.
- 2.H: Hemanth subjective GREEN on both backends across the 2.C corpus.
- BUILD GREEN both main app and sidecar.

**Estimate:** 6-10 summons honest. (Reframed up from 2-4 per audit fold-in: mpv property bridge is 7×1-line `setDouble` calls, attachment pass is ~30-50 LOC sidecar-only, UI work depends on Q3+Q4+2.B answers, smoke matrix is multi-iteration.)

**Ratification gate (Agent 0/3 coordination call per Rule 14, NOT a Hemanth question):** Phase 2 hard-blocked on §Phase 2 Decisions Q1-Q4 + 2.B visual spec ratification before 2.C kickoff. Mirrors Phase 1's gate-on-ratification shape. Visual-spec answers cascade through 2.D (mpv property bridge defaults) + 2.G (tunable surface scope) — without ratification these sub-phases ship with assumed defaults that may need rework. EXCEPTION: 2.E ships independently when Hemanth fires it (mechanical sidecar fix, no Decisions dependency — see 2.E line above).

---

### Phase 3 — HDR + filters parity

**Why:** Per audit §2 HDR row + §4 ffmpeg-path-extension cost analysis: ffmpeg path runs libplacebo via `GpuRenderer` with Tankoban-owned tone-mapping shader state + ICC handling + custom HDR metadata routing. `LIBPLACEBO_SINGLE_RENDERER_FIX_TODO` P2 ships SDR through the same pipeline (env-gated). mpv path has libmpv's gpu/gpu-next renderer with its own tone-mapping; current MpvBackend command surface exposes tone mapping + HDR peak (`MpvBackend.cpp:581-779`) but parity with the ffmpeg-path's specific tone-mapping function selector + ICC behavior + HDR metadata semantics is unverified.

Plus Tankoban-side filter chain: cinemascope crop, anamorphic correction, letterbox handling all wire through `setFilters` + `sendRawFilters` IPC on the ffmpeg path. mpv path maps to `vf=` / `af=` raw filter strings — semantically different surface.

**Scope (refined post-Phase-0 with audit §2 fold-in):**
- HDR file smoke matrix: HDR10 + Dolby Vision (if available). Compare tone-map output between backends on Hemanth's display.
- ICC profile handling parity check (mpv has `icc-profile-auto` / related options).
- Filter chain semantic mapping: cinemascope crop = `crop` mpv vf; anamorphic = `dsize` / `setdar` semantics; letterbox = display-aspect handling.
- Tone-mapping function selector — verify mpv exposes the equivalent set (reinhard / bt2390 / clip / mobius / linear / hable per `gpu_renderer.cpp`).
- Decision point: keep mpv on libmpv's `gpu` (matches what's bundled per `resources/libmpv/windows/VERSION.txt`) OR opt into `gpu-next` Vulkan path.

**Files:** `src/ui/player/MpvBackend.{h,cpp}` (HDR + filter command parity), `src/ui/player/VideoPlayer.{h,cpp}` (filter chain UI wiring if it routes through backend-specific paths). Audit-derived expansion.

**Acceptance:**
- HDR file plays with comparable tone-mapping quality on both backends.
- Cinemascope crop + anamorphic correction work identically via right-click filter menu on both backends.
- ICC behavior verified on Hemanth's display.
- BUILD GREEN.

**Estimate:** 3-5 summons.

---

### Phase 4 — Audio parity

**Why:** Per audit §2 audio row: ffmpeg path has custom audio decoder/output + DRC compressor (`af=acompressor`) + AV sync clock + stream-stall handling in sidecar. mpv path inherits libmpv's audio + sync + DRC + stall machinery. AudioPopover routes through `IPlayerBackend` since MPV_RENDER_API P2; verify each control's behavior matches across backends.

**Scope (refined post-Phase-0 with audit §2 fold-in):**
- Audio track switching parity (`aid` property vs `set_tracks` IPC).
- Channel mapping behavior (5.1 / 7.1 surround source on stereo output, etc.).
- Audio passthrough — audit explicitly flags "not proven in files/logs reviewed" for both backends. Verify on a known DTS / Dolby Digital source if Hemanth has one.
- Audio delay slider parity (`audio-delay` property).
- DRC toggle parity (`af=acompressor` mpv vs ffmpeg path's filter wiring).
- Speed adjustment + AV sync behavior under speed change.

**Files:** `src/ui/player/MpvBackend.{h,cpp}` (audio command parity), `src/ui/player/AudioPopover.{h,cpp}` (verify wiring). Audit-derived expansion.

**Acceptance:**
- Audio track switch works identically on both backends.
- DRC toggle audibly identical on both backends on a loud-quiet test file.
- Audio delay slider behavior identical.
- Audio passthrough verified (or explicitly flagged unverified if Hemanth has no test source).
- BUILD GREEN.

**Estimate:** 2-3 summons.

---

### Phase 5 — IPC + diagnostics parity

**Why:** Per audit §2 + audit's own evidence-boundary disclaimer: ffmpeg path has `out/ipc_latency.log` (per-command p50/p99/max tracker, ratified 2026-04-24) + `compare-mpv-tanko.ps1` log-parser harness. mpv path has neither — runtime measurement parity matters for Phase 7 cutover decision (we need same-shape evidence on both backends to argue parity).

**Scope:**
- mpv-side equivalent of `ipc_latency.log` — per-command (or per-property-set) latency tracker. mpv's `mpv_set_property*` is synchronous; `mpv_command_async` has reply ordering. Decide: track command-issue → property-change-event round-trip, or just mpv API call wall-clock.
- Extend `compare-mpv-tanko.ps1` to parse mpv-side logs into the same divergence shape it produces today for ffmpeg (mpv_drops vs tanko_drops, stall_pause counts, etc.).
- Audit §2 capability-matrix concrete-numbers section already has prior ffmpeg numbers; Phase 5 produces matching mpv numbers.
- Frame-pacing telemetry: ffmpeg path logs to `sidecar_debug_live.log` + `_player_debug.txt`. mpv path needs equivalent hooks (paint-thread frame counter + present timing).

**Files:** `src/ui/player/MpvBackend.{h,cpp}` (latency tracker), `src/ui/player/MpvVideoWidget.{h,cpp}` (frame-pacing hooks), `scripts/compare-mpv-tanko.ps1` (mpv-side parsing). Audit-derived expansion.

**Acceptance:**
- mpv-side `ipc_latency.log` equivalent present + populating during playback.
- `compare-mpv-tanko.ps1` produces side-by-side divergence report on a fresh smoke pair.
- Phase 7 cutover smoke can run apples-to-apples evidence collection.
- BUILD GREEN.

**Estimate:** 2-3 summons.

---

### Phase 6 — SW fallback (gated on §Q4)

**Why:** Per `MPV_RENDER_API_INTEGRATION_TODO` §Q7 (deferred as Phase 8 of that arc) + this TODO's §Q4: with ffmpeg deleted there's no fallback for hardware where libmpv's GL render API fails. SW (`MPV_RENDER_API_TYPE_SW`) becomes the floor.

**Scope (only if §Q4 ratifies in-scope):**
- `MpvVideoWidget` accepts a runtime "SW mode" flag — if `MPV_RENDER_API_TYPE_OPENGL` init fails, falls back to SW path.
- SW path routes libmpv-rendered BGRA frames through CPU upload to `MpvVideoWidget`'s paint surface (no GL FBO).
- Quality tradeoff: no libmpv `gpu` upscaling/tone-mapping in SW mode. Ship anyway — playback works on weird drivers.
- ~50 LOC per prior scoping.

**Files:** `src/ui/player/MpvVideoWidget.{h,cpp}`, `src/ui/player/MpvBackend.{h,cpp}`.

**Acceptance:**
- On Hemanth's UHD 620 (where direct GL works), SW path NOT exercised by default; existing GL-rendered playback unchanged.
- Synthetic test (force SW path via env var or runtime flag): playback still works, quality visibly lower, no crash.
- BUILD GREEN.

**Estimate:** 1-2 summons. Smallest phase.

**Skipped if §Q4 ratifies STAYS-DEFERRED (Phase 7 keeps ffmpeg, no deletion).**

---

### Phase 7 — Cutover mechanics (CONDITIONAL on downstream Hemanth ratification)

**Why:** Phase 1-6 closed + §New scope items picked + Hemanth ratified the cutover-to-mpv-only direction in a SEPARATE decision (NOT auto-fired by parity-GREEN unless §Q1 is overridden to "auto-cutover").

**This phase only fires if cutover is explicitly ratified.** If Hemanth ratifies "keep both backends" or "pause and revisit later," Phase 7 stays unfired and this TODO archives after Phase 6 + §New scope.

**Scope (RATIFIED CUTOVER path, per §Q7 PERMANENT ARCHIVE — sources move to `agents/_archive/`, never deleted):**
- Run the parity matrix end-to-end: each Phase 1-6 acceptance criterion re-verified on a fresh same-file smoke matrix. Hemanth's smoke is the load-bearing check.
- ON GREEN cutover commit:
  - `git mv native_sidecar/` → `agents/_archive/native_sidecar/` (entire tree preserved with history under new path).
  - `git mv src/ui/player/SidecarProcess.{h,cpp}` → `agents/_archive/sidecar_process/SidecarProcess.{h,cpp}`.
  - `git mv` the IPC dispatcher infrastructure (JSON-over-stdin/stdout handlers, ack tracker, related glue) into `agents/_archive/sidecar_ipc/`.
  - `git mv` the ffmpeg-side `out/ipc_latency.log` tracker code path into `agents/_archive/sidecar_diagnostics/` (mpv-side equivalent from Phase 5 stays in `src/`).
  - REMOVE references to archived sources from `IPlayerBackend` (ffmpeg-only signal/method members retire with `SidecarProcess`; MpvBackend keeps its surface).
  - REMOVE references to archived sources from `BackendFactory` (`chooseFor` becomes a single-implementation factory or retires entirely).
  - UPDATE `CMakeLists.txt` to drop the `add_subdirectory(native_sidecar)` line + drop the SidecarProcess source/header entries from `set(SOURCES ...)` + `set(HEADERS ...)`. Archived sources do NOT compile.
  - UPDATE `build_and_run.bat` to drop sidecar-related deploy + launch steps.
  - UPDATE `setup.bat` to drop sidecar prereq checks (libplacebo / libass / libfribidi / harfbuzz / lcms2 / uchardet / vulkan-1 manual installs at `C:\tools\` are no longer needed for the active build; archived sources still document them for the revert path).
  - UPDATE `CLAUDE.md` "Active agents" — Agent 3 sidecar ownership note retires; add archive pointer.
  - UPDATE `BUILD.md` § Native sidecar prerequisites — section deleted; add a `## Archived backends` section pointing at `agents/_archive/` for revert reference.
  - UPDATE `ARCHITECTURE.md` process-model diagram — sidecar process removed; archive pointer added.
  - UPDATE `.gitattributes` if needed to mark `agents/_archive/native_sidecar/` as `linguist-vendored` (so GitHub repo stats don't double-count the archived code).
  - FLIP `BackendFactory` default to mpv (Q6 stay-default-ffmpeg-until-cutover commitment closes here).
- ON RED gate (cutover ratified but cutover smoke fails):
  - HALT cutover. Do NOT delete anything.
  - Hemanth either: pivots cutover decision (re-ratify "keep both"), or schedules a follow-up wake to fix the regression that surfaced.
  - This is NOT a partial-cutover phase — it's all-or-nothing.

**Files moved (NOT deleted) at cutover:** mirror `MPV_RENDER_API_INTEGRATION` Phase 7's deletion shape but use `git mv` to `agents/_archive/` paths instead of `git rm` per §Q7. Comprehensive list assembled at execution time, not authoring time. Estimate ~30+ files moved across `native_sidecar/` + ~5-10 main-app files. Archived sources stay in repo permanently per §Q7 ratification — cheap revert path always available via reversing the `git mv` + restoring CMake wiring.

**Acceptance:**
- All Phase 1-6 acceptance criteria pass against a fresh same-file smoke matrix (curated set covering subtitle / HDR / stream / audio / format coverage).
- Hemanth's subjective verdict on smoke session.
- Post-deletion BUILD GREEN.
- Post-deletion smoke: representative file plays via mpv backend (because there's no other choice). Library + Stream tabs both functional.

**Estimate:** 1-3 summons IF fired. Bulk of work is the deletion mechanics (well-trodden — see Phase 7 of MPV_RENDER_API_INTEGRATION's IPlayerBackend extraction commits for the deletion-shape pattern).

---

## New scope mpv enables (post-parity opportunities)

This section is the OTHER half of Hemanth's question: what new improvements does mpv unlock that the custom ffmpeg sidecar doesn't easily provide? Each item below is a ratifiable scope-addition — Hemanth picks which ones to bring in (some, all, none), and they become future fix-TODOs OR rolled into a follow-up arc.

These items are NOT in scope of Phases 1-6 (which are pure parity work). They're enumerated here so the strategic upside of the mpv direction is concrete, not handwaved.

### Tier 1 — Clear quality wins (low scope cost, high user-visible upside)

- **`gpu-next` renderer (Vulkan).** Default mpv renderer is `gpu` (OpenGL); `gpu-next` is the modern Vulkan path with measurably better tone-mapping + scaling quality. Trade-off: requires bundle update (current libmpv build is `gpu`-only per `resources/libmpv/windows/VERSION.txt`). Scope: ~1-3 summons + bundle update + Hemanth-hardware verification.
- **Native subtitle extraction** — mpv can demux soft subtitles from any container without round-tripping through ffmpeg's API. UI-side this means subtitle list populates faster + supports more obscure formats out of the box. Scope: ~1 summon, mostly UI wiring.
- **Forced subtitles auto-select** — mpv has `sub-forced-events-only` + related properties for "show forced subs only" (signs/songs but not full dialogue). Tankoban doesn't expose this today. Scope: ~1 summon.
- **mpv's seek-precision modes** — `--hr-seek=yes/no/always`. Frame-accurate vs keyframe seek as a per-file or persistent toggle. Tankoban currently has fixed seek behavior. Scope: ~1 summon.
- **Better network buffering** — mpv has rich cache controls (`cache-secs`, `demuxer-max-bytes`, `network-timeout`) for HTTP sources. Stream mode (Phase 1) already uses these implicitly via mpv's defaults; surfacing them as user-tunables is a separate scope addition. Scope: ~1-2 summons + UI wiring.

### Tier 2 — Opens new feature surfaces (medium scope, medium upside)

- **Custom GLSL shaders** — mpv's `--glsl-shader=` lets users load shader files for postprocessing (anime upscalers like Anime4K, custom CRT filters, custom denoisers). Tankoban can expose a "Load shader…" entry. Surface area is large (shader UI + file management + per-file shader memory) but mpv does the heavy lifting. Scope: ~3-5 summons.
- **mpv scripting hooks (Lua / JavaScript)** — mpv supports user scripts for power-user automation. Can be exposed as a "scripts directory" Tankoban points mpv at, so power users drop their own .lua files. Scope: ~1 summon for the directory hook + docs.
- **ICC profile auto-detect** — mpv has `icc-profile-auto`. Tankoban currently has a custom ICC path on the ffmpeg/libplacebo side. mpv-native ICC removes that custom code. Scope: ~1 summon (parity rolls this in via Phase 3, but additional ICC features like 3D-LUT support open up).
- **Per-file profiles** — mpv supports `auto-profiles` (e.g., "for HDR files use these settings, for SDR use these others"). Tankoban could surface this as per-show or per-format profile picker. Scope: ~2-4 summons.
- **Audio passthrough** — mpv has cleaner passthrough config (`audio-spdif`, `ad-lavc-downmix`). Audit flagged passthrough as unverified on both backends; mpv path probably has a cleaner cut at it. Phase 4 verifies basic passthrough; rich passthrough config is a separate addition. Scope: ~1-2 summons.

### Tier 3 — Architectural upside (high scope, ecosystem benefit)

- **Codec coverage growth automatic.** mpv's bundled FFmpeg gets updated upstream by mpv maintainers; we just bump the bundle. Custom ffmpeg sidecar means we'd be the ones updating + testing FFmpeg every bump. This is "non-feature scope" — it's maintenance cost reduction, but it's real and load-bearing.
- **YouTube / streaming-site URL handling via `youtube-dl` / `yt-dlp` hook.** mpv natively integrates with yt-dlp for direct URL playback. Tankoban could add a "Play URL…" entry that accepts any yt-dlp-supported URL. Scope: ~2-3 summons + bundle yt-dlp + UI wiring.
- **Audio filter chain user-exposed.** mpv supports rich audio filter graphs (`af=`). We have basic DRC; expose more (loudness normalization via `af=loudnorm`, dialogue boost via custom EQ, etc.). Scope: ~3-5 summons for UI + presets.
- **Better hwdec support (Intel UHD 620, future hardware).** mpv has `hwdec=auto-safe` and per-codec hwdec selection. Tankoban's current hwdec story is custom in the sidecar. Switching to mpv-managed hwdec is more robust across the long tail of hardware. Scope: rolls into parity work mostly; new tunables are ~1-2 summons.

### How this list is used

Phase 0 ratification doesn't need to pick items here — that's a separate downstream pass after parity work shows mpv is viable. Once Phase 6 GREEN lands, Hemanth picks which items to bring in (with priorities: Tier 1 likely all-yes, Tier 2 mixed, Tier 3 case-by-case). Each picked item becomes either (a) part of a follow-up consolidated arc TODO, or (b) its own one-off fix-TODO. None of them are in scope of Phases 1-6 directly.

If cutover gets ratified, a subset of these items can land in the same window. If cutover doesn't get ratified, picked items can still ship — they only require mpv being available as a backend, not mpv being the *only* backend.

---

## Risk surface

1. **Indecision drag.** Without a deadline (per §Q2 Hemanth-driven cadence), the parity arc could stretch indefinitely AND the cutover question could stay deferred indefinitely after parity closes. Failure mode: permanent dual-backend by inaction rather than by ratification. Mitigation: each parity phase ships a complete acceptance gate so progress is visible per phase even without a calendar; cutover decision is surfaced as a discrete ratification request after Phase 6 GREEN, not assumed-default.

2. **Stream-mode retrofit scope is the largest unknown.** Audit §2 + §4 acknowledge "no complete Tankoban stream-mode integration today" but don't enumerate the actual gap. Phase 1 may discover sub-phases mid-execution. Mitigation: Stremio reference (`C:\tools\stremio-shell\` — populate if missing, else web-fetch upstream) as parallel exemplar.

3. **libmpv property surface gaps for ffmpeg-path-custom features.** Subtitle Y-offset is the known one (`feedback_subtitle_position_yoffset_not_libass.md`). Filter chain semantics is the suspected one (Phase 3). Audit explicitly notes "subtitle style and filter batch stubs" at `MpvBackend.cpp:734-766`. Mitigation: each phase's acceptance gate enforces parity; failures escalate to §Q1 renegotiation.

4. **GL context threading + paint-thread ownership across backend swaps.** `MpvVideoWidget` is `QOpenGLWidget`; its GL context lives on the GUI thread. Backend swap mid-session (currently apply-on-next-launch, but Phase 7 default-flip changes that) reparents render surfaces. Risk per `feedback_lifecycle_parity_with_mainwindow.md`'s pattern of stream-mode lifecycle bugs from non-MainWindow callers. Mitigation: backend swap behavior stays apply-on-next-launch through Phase 6; Phase 7 default-flip happens once at deletion commit.

5. **Intel UHD 620 driver surface.** Hemanth's dev hardware. MPV_RENDER_API Phase 5 already pivoted once for D3D11/GL interop fail (`user_hardware_intel_uhd_620.md`). Stream-mode work + HDR work likely surfaces more such surprises. Mitigation: smoke gates run on Hemanth's hardware; CI is not the source of truth here.

6. **Stremio reference drift.** Stream-server upstream may change shape during this arc. `project_stream_server_pivot.md` pinned a specific Stremio runtime build. Mitigation: pin the Stremio reference revision at Phase 1 start; don't track upstream during the arc.

7. **Permanent dual-backend trap.** Two backends mean two bug surfaces, two parity matrices, two sets of feature-decisions per future change. Acceptable as a permanent state IF Hemanth wants opt-in mpv (some users prefer it) but expensive otherwise. Mitigation: §Q1 surfaces the cutover question as discrete + ratifiable post-Phase-6; if Hemanth keeps deferring, that's a real choice with known cost rather than silent drift.

8. **Validation cost — same-file smoke per phase.** Audit §followup-evidence requires fresh side-by-side capture. Each phase's acceptance gate is a Hemanth-time investment (~30-60 min per smoke session). 7 phases × multiple smoke iterations = significant time. Mitigation: `compare-mpv-tanko.ps1` automates the log diff; Hemanth's subjective check stays the load-bearing arbiter but the harness reduces per-iteration cost.

9. **mpv `sub-ass-override` interaction (Phase 2-specific).** Audit §5(b).5 warns that forcing style onto authored ASS can break signs / karaoke / multi-event positioning. 2.D wiring needs to default to override-OFF for ASS tracks (preserve authored typography) and override-ON for SRT-injected style (where Tankoban-side tunables are the only style source). Mitigation: 2.D bridge differentiates by track type before applying property values; default behaviors lock in 2.B visual spec ratification.

10. **libass version pinning (Phase 2.E-specific).** `ass_add_font` API stability across libass versions matters for 2.E ffmpeg attachment pass. Different libass builds have differing return-value semantics + memory ownership rules. Mitigation: ship-pin the libass build that lands with the 2.E fix; document the pin in `native_sidecar/CMakeLists.txt` or build script per CLAUDE.md build pattern.

11. **Hemanth-driven mode tempo (Phase 2-wide).** Phase 2 ratification gate IS the tempo control. No calendar pressure per `feedback_hemanth_driving_player_domain.md` + §Q2. Risk shape: gates pile up if Hemanth is busy — 2.B + Q1-Q4 ratification could sit indefinitely. Mitigation: 2.E ships independently when Hemanth fires it (mechanical fix, no Decisions dep) — gives Hemanth visible improvement on anime ASS files without committing to the full ratification cycle; the rest of Phase 2 waits for visual spec lock without forcing.

---

## Open design questions (Agent 3 decides during execution)

These are deliberately NOT in §Decisions — coder-domain choices Agent 3 makes per Rule 14 during implementation.

- Phase 1 mpv stream adapter shape: bridge to existing `StreamServerProcess` REST surface, OR teach MpvBackend to parse stream-server URLs directly? Agent 3 picks based on minimum-LOC + stream-server team's contract preference.
- Phase 2 subtitle Y-offset implementation: native `sub-pos` if it covers the Saiki karaoke case, OR replicate the ffmpeg-path Y-offset hack mpv-side via libmpv's render hook. Smoke-determined.
- Phase 3 mpv renderer choice: bundled `gpu` (default) vs opt-in `gpu-next` (Vulkan, requires bundle update). Smoke-determined by HDR file behavior.
- Phase 5 latency-tracker schema: match `ipc_latency.log` field-for-field (drop-in compatible with `compare-mpv-tanko.ps1` parsing) OR new schema for mpv's different command shape. Whichever lands smaller.
- Phase 7 deletion ordering: single bundled commit OR phase-cursor commits within Phase 7 (sidecar source delete → CMakeLists update → docs update). Single bundle is cleaner for `git revert` (Q7 rollback hedge); split is cleaner for review. Default to single bundle unless review burden makes it unworkable.

---

## Non-goals (explicit deferrals)

- **Not re-litigating the libmpv-via-render-API architecture.** Settled in MPV_RENDER_API P0/P5 redux. This TODO doesn't reconsider `wid` embedding or subprocess shape.
- **Not building a third backend** (e.g., VLC, custom renderer). The §Q6 deletion plan picks one survivor among the existing two.
- **Not adding new player features** that aren't already on the ffmpeg path. Parity means the existing user-facing surface, not "what ffmpeg backend has plus everything else mpv could do."
- **Not changing `IPlayerBackend` interface shape** beyond what each phase requires. Adding methods is OK; removing/renaming requires a cross-backend coordination check until Phase 7.
- **Not touching the comic / book / TTS subsystems.** Player-domain only.
- **Not Linux/macOS support.** Windows-only per existing project scope.
- **Not changing libmpv bundle version mid-arc** unless a phase explicitly requires it (Phase 3 if `gpu-next` is the call; otherwise no).

---

## Rule 6 + Rule 11 application

- **Rule 6** (per-phase smoke before phase exit): each phase's acceptance gate above includes a smoke step. Hemanth's verdict on smoke is the phase-exit gate, not Agent 3's BUILD-OK alone.
- **Rule 11** (READY TO COMMIT discipline): Agent 3 posts an RTC line per phase ship, contracts-v3 `Skills invoked:` field included for non-trivial RTCs. Phase 7's deletion commit is the most consequential RTC of the arc — full sub-step file enumeration + carry-through flag for any pre-existing dirt.
- **Rule 7** (CMakeLists.txt updates on new files): Phase 1, 2, 3, 4, 5, 6 each touch `CMakeLists.txt` if new files appear; Phase 7 deletes the entire `native_sidecar/CMakeLists.txt` + drops the `add_subdirectory(native_sidecar)` reference from main `CMakeLists.txt`.
- **Rule 14** (decision authority): §Decisions are Hemanth's; §Open design questions are Agent 3's; §Non-goals + §Risk surface are Agent 0's authoring calls. Phase ownership Q3 ratification by Hemanth determines whether Agent 4 is in or out.
- **Rule 17** (Rule-17 cleanup post-smoke): each phase's smoke session ends with `scripts/stop-tankoban.ps1`.
- **Rule 19** (MCP lane lock): smoke sessions claim `MCP LOCK — Agent 3 — <phase>` in chat.md; release on completion.

---

## Verification procedure (Phase 7 cutover-gate smoke)

This is the load-bearing smoke. Hemanth runs it; Agent 3 captures evidence; Agent 0 ratifies cutover commit.

1. Fresh build via `build_and_run.bat`. Both backends still present. Confirm `out/Tankoban.exe` + `out/tankoctl.exe` rebuilt clean. Confirm `resources/ffmpeg_sidecar/ffmpeg_sidecar.exe` present (still the deletion target).
2. **Library file smoke (ffmpeg)** — right-click "Use ffmpeg player" on a known good Library tile. Play episode. Capture: pause, seek to mid-file, switch audio track, switch subtitle track, scrub to end-credits, close. Save: `out/ipc_latency.log`, `_player_debug.txt`, `out/sidecar_debug_live.log`.
3. **Library file smoke (mpv)** — same file, right-click "Use mpv player". Same actions. Save: mpv-side latency log, `_player_debug.txt` (mpv branch), libmpv stderr if captured.
4. **Diff** — `pwsh scripts/compare-mpv-tanko.ps1 -MpvLog ... -SidecarLog ...`. Verdict line should be GREEN parity (drop counts within tolerance, stall counts equivalent if the file isn't stream).
5. **Subtitle file smoke** — file with hardcoded subs (PGS) + soft subs (SRT) + ASS karaoke (Saiki Ep 12 per memory). Verify Y-offset slider behaves identically on both backends; PGS renders correctly on both; ASS karaoke effects render on both.
6. **HDR file smoke** — HDR10 file. Verify tone-mapping output subjectively comparable on both backends on Hemanth's display.
7. **Stream file smoke** — known torrent magnet via Stream tab. Play via mpv backend (Phase 1's payoff). Verify: stall/recovery handling, buffered-range overlay, near-end estimate, close + reopen at resume position. Then same magnet via right-click "Use ffmpeg player" — verify ffmpeg-stream still works (regression check pre-deletion).
8. **Format coverage** — h264 + h265 + AV1 + 10-bit + multi-audio-track + chaptered file. Each plays clean on mpv backend.
9. **Audio passthrough** (if test source available) — DTS or Dolby Digital file. Verify passthrough on mpv backend (per Phase 4 acceptance).
10. **Close-from-various-states** — close mid-load, close mid-stall, close at EOF. Each clean on mpv backend (per CLOSE_AUDIO_CONTINUES_FIX equivalent — `mpv_terminate_destroy` is synchronous, no kill-backstop needed).
11. **Cleanup** — `scripts/stop-tankoban.ps1`. Document subjective verdict per file.
12. **GREEN gate** — all 11 steps pass; Hemanth confirms no subjective regression vs ffmpeg playback. Cutover commit lands.
13. **RED gate** — any step fails; Phase 7 halts; Agent 0 files Q1 escalation per §Risk #7 mitigation.

Phases 1-6 each have their own smoke step (subtitle file for Phase 2, HDR for Phase 3, etc.). Phase 7 is the all-up integrated check.

---

## Next steps post-Phase-0-ratification

- Agent 0 posts routing announcement in `agents/chat.md`: "Hemanth ratified §Decisions [wholesale|per-question picks listed]. Phase 1 unblocked under Agent 3 + Agent 4 [consultation per Q3]."
- Agent 0 folds Agent 7 audit §2 capability matrix into Phase 2-4 scope detail (specific feature gaps per phase). Posts updated TODO sub-section as a single edit, not a new commit (TODO authoring is doc-only; substantive edits stay in-place during ratification window).
- Agent 3 executes Phase 1 stream retrofit. Phase exits gated on §Verification per-phase smoke + RTC + Hemanth subjective verdict.
- Agent 0 commits at each phase boundary per `feedback_commit_cadence.md`.
- No calendar; phases fire when Hemanth summons. Pacing entirely Hemanth-driven per §Q2.
- MEMORY.md "Active repo-root fix TODOs" line updated to add this TODO.
- After Phase 6 GREEN + §New scope items picked: Agent 0 surfaces a fresh ratification request asking Hemanth whether to fire Phase 7 cutover (delete ffmpeg) or hold (keep both backends). That's a separate decision shape, not auto-fired by parity-GREEN unless §Q1 was overridden to "auto-cutover."
- On Phase 7 cutover GREEN (if fired): archive this TODO + `MPV_RENDER_API_INTEGRATION_TODO.md` together (paired arc); deletion commit; flip default; update CLAUDE.md + BUILD.md + ARCHITECTURE.md per Phase 7 scope.
- On Phase 7 not-fired (cutover ratified "no" or "later"): this TODO archives after Phase 6 + §New scope; cutover question lives as an open future ratification.
- §New scope picked items spawn follow-up TODOs OR roll into a consolidated post-parity arc, per `feedback_fix_todo_authoring_shape.md`.

---

**End of plan.**
