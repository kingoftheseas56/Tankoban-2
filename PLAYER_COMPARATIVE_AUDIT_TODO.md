# Player Comparative Audit TODO — VLC + PotPlayer vs Tankoban (mpv as precision reference)

**Owner:** Agent 3 (Video Player)
**Coordinator:** Agent 0 — authors follow-on fix-TODOs when Hemanth ratifies DIVERGED/WORSE verdicts at phase boundary.
**Authored:** 2026-04-20 by Agent 0 (Coordinator).
**Provenance:** Hemanth ratified in-session 2026-04-20 after the MCP speed-fix thread + Agent 4 smoke-by-proxy demonstrated MCP's cross-app capability. No audit predecessor — Agent 0 authored directly per Hemanth ask. Plan file at `C:\Users\Suprabha\.claude\plans\enchanted-leaping-bubble.md` (approved).

---

## 1. Context

Tankoban's video player is a native C++ Qt application with a D3D11 rendering path, a classified-stages sidecar (probe → decoder-open → first-frame), libass/PGS subtitle rendering via SHM overlay, an IINA-parity Tracks popover, an HDR tone-map dropdown, an audio EQ with presets, stream-mode lifecycle with telemetry + stall detection, and resume/Continue-Watching persistence gated by `PersistenceMode` domain. Prior comparative audit work against the Stremio reference (Congress 6 Slices A/B/C/D, 2026-04-18) surfaced real divergences caught late, resulting in 4-phase fix work (STREAM_STALL_FIX, closed 2026-04-19).

This TODO applies the **same comparative discipline proactively** to the native player surfaces — against **VLC** (Tier 2 reference per `reference_reader_codebases.md`) and **PotPlayer** (widely-deployed Windows desktop player), with **mpv** as an **optional precision reference** via its `--input-ipc-server` channel (already installed at `C:\tools\mpv\mpv.exe` per `reference_mpv_install.md`). VLC at `C:\Program Files\VideoLAN\VLC\vlc.exe`, PotPlayer at `C:\Program Files\DAUM\PotPlayer\PotPlayerMini64.exe` — all three verified installed 2026-04-20.

**Hard scope constraint (Hemanth-locked 2026-04-20):** comparison axes must map to **Tankoban's existing feature set only**. Features VLC or PotPlayer have that Tankoban doesn't (bookmarks, video rotation presets, extended codec options, network stream protocols we don't support, etc.) are **not** comparison axes. This is a **parity audit of our surfaces**, not a feature-expansion proposal. DIVERGED/WORSE verdicts feed fix-work; BETTER verdicts feed reference-pinning (document and protect against regression).

**Why this matters:** the stream-engine side had audit-driven discipline. The native player side hasn't had a structured cross-player behavioral sweep. Silent drift from reference player conventions is a "works-for-me" blind spot — Hemanth has flagged taste-level misalignments before (feedback memories on Qt-vs-Electron aesthetic, IINA identity). This TODO closes that gap with 4 wakes of Agent-3-driven live measurement.

**Phase shape:** 4 phases, each one Agent-3 wake, each producing **one audit deliverable** under `agents/audits/comparative_player_*.md` — **no src/ code changes in this TODO**. Phases closed on audit land. Post-phase: Agent 3 posts a Fix-Candidates ratification-request to Hemanth; Agent 0 authors a follow-on fix-TODO only if ratified.

Required-reading memories listed in §11 Open Design Questions + §15 Verification. Reference-player control channels in §9.

## 2. Objective

After this audit ships (4 phases / 4 wakes / 4 audit deliverables):

1. Hemanth + Agent 0 have a **structured per-surface verdict** (CONVERGED / DIVERGED / WORSE / BETTER) covering ~45 Tankoban player surfaces across 4 behavioral domains.
2. Every DIVERGED/WORSE finding has a **severity tag** (blocker / polish / cosmetic) and a **suggested scope** for follow-on fix work.
3. Every BETTER finding is logged with the comparison data so future regressions are catchable — Tankoban's wins become pinned reference points.
4. Cold-open latency is empirically measured across all 4 players on the same content, giving a production-realistic sidecar-vs-monolith latency baseline.
5. Seek behavior (fast vs exact vs frame-step vs chapter-nav vs click-to-seek) is measured for both precision (time-offset accuracy in ms) and UI-feedback latency against IPC-controllable references (mpv IPC / VLC rc).
6. Subtitle rendering is visually-diffed against reference output at cinemascope content + external SRT + embedded ASS+PGS paths.
7. HDR tone-mapping output is objectively compared (peak-luminance sample + RGB histogram, not eyeballing) across all 4 tone-map modes Tankoban exposes.
8. Audio EQ output is frequency-response-compared against each reference's closest equivalent.
9. Crash recovery + file-switch + resume-position behavior is measured end-to-end via forced-kill loops and file-rotation smokes.
10. Reference-player versions are **pinned in the TODO preamble** and persist through all 4 wakes — re-audit required if any player is upgraded mid-audit.

## 3. Non-goals

Explicit exclusions, Hemanth-locked 2026-04-20:

- **Any VLC/PotPlayer feature Tankoban doesn't have** — e.g., bookmarks, video rotation, DVD/BluRay menu nav, CD/VCD playback, network stream protocols we don't support (SHOUTcast, UPnP), plugin architectures, skins, Lua scripting, wallpaper mode, etc. If Tankoban doesn't have the surface, it's not a comparison axis.
- **Fix-work in this TODO** — this is investigation only. Fix-TODOs authored separately post-phase-ratification.
- **Tankoban-exclusive internals without user-visible peer** — see §12 drop list (stream lifecycle / stall / prefetch / SidecarProcess crash backoff / SHM overlay internals / VsyncTimingLogger).
- **Subjective quality judgments** — HDR "looks better" is not a verdict. Objective criteria (peak-luminance samples, RGB histograms) are required for visual axes.
- **Reference-player feature audits** — we don't audit what PotPlayer does with WASAPI that Tankoban doesn't. We only audit Tankoban's WASAPI status (does it expose exclusive mode? yes/no/partial) against what reference players do with the same surface.
- **Benchmarking as absolute performance claim** — observer-effect of enabled logging + per-player harness overhead is NOT equivalent across players. Findings surface RELATIVE behavior, not absolute FPS claims.

## 4. Agent Ownership

**Primary owner:** Agent 3 (Video Player). All 4 phases executed by Agent 3 via Windows-MCP-driven live measurement.

**Coordinator role (Agent 0):** summon Agent 3 per phase after ratification gate. Author follow-on fix-TODOs when Hemanth ratifies Fix Candidates blocks. Update CLAUDE.md dashboard + MEMORY.md at phase boundaries.

**Cross-agent touches expected:** NONE in this TODO (no src/ code changes). If Phase-specific MCP needs a new windows-mcp capability, Agent 3 flags HELP request; Agent 0 triages.

**Files Agent 3 produces:**
- `agents/audits/comparative_player_2026-04-NN_p1_transport.md`
- `agents/audits/comparative_player_2026-04-NN_p2_subtitles.md`
- `agents/audits/comparative_player_2026-04-NN_p3_hdr_filters.md`
- `agents/audits/comparative_player_2026-04-NN_p4_state_polish.md`

**Reference-player version pin (Agent 3 records at Phase 1 start + freezes through Phase 4):**
- Tankoban commit hash: _to-be-filled_
- VLC version: _to-be-filled via `vlc --version`_
- PotPlayer version: _to-be-filled via file-properties or About dialog_
- mpv version: _to-be-filled via `mpv --version`_

**Windows-MCP version:** the current `.mcp.json` has `WINDOWS_MCP_SCREENSHOT_SCALE=0.6` (shipped this wake at `5788f44`). Phase 1 smoke benefits automatically. If the queued `WINDOWS_MCP_PROFILE_SNAPSHOT=1` A/B measurement (per `project_windows_mcp_live.md` TRIGGER block) hasn't been run by Phase 1 entry, Agent 3 can bundle it with their Phase 1 wake (covers the "next smoke-heavy wake" trigger condition — Agent 3's Phase 1 IS a smoke-heavy wake).

---

## 5. Phase 1 — Transport + Shell (Agent 3 wake 1, ~60 min)

**Why P0:** foundational playback surfaces. If cold-open or seek is divergent, everything downstream inherits the divergence. Highest-signal per unit of measurement effort.

**Covers Tankoban surface categories A, B, C, D, J, K, L** — ~20 surfaces.

### Batch 1.1 — Cold-open + LoadingOverlay (category A)

Tankoban's classified 6-stage overlay (Opening → Probing → OpeningDecoder → DecodingFirstFrame → Buffering → TakingLonger, 30s watchdog) is distinctive. VLC and PotPlayer both show simpler loading indicators; mpv shows only a terminal OSD line. Measure:

- Wall time from file-open click to first video frame on-screen across all 4 players, same 1080p MKV, 3 cold-open runs, median reported.
- Does each player show "taking longer than expected" on slow opens? Tankoban has explicit 30s watchdog. VLC/PotPlayer behavior unknown — document.
- Loading stage granularity — does user know WHY loading takes long (probe vs decoder vs buffer)?
- Harness: Tankoban via `stream_telemetry.log` mdReadyMs/firstPieceMs/first_frame pattern. mpv via IPC `playback-time` poll loop. VLC via rc `get_time` poll + `--file-logging`. PotPlayer via MCP screenshot loop comparing frame-hash.

**Deliverable contribution:** Observed/Reference/Divergence block per sub-axis in Phase 1 audit.

### Batch 1.2 — Core playback (category B)

- Play/pause toggle latency (key-press → actual pause, measured in frames elapsed between keyup and frozen frame)
- Volume control: Tankoban uses 0-200 range with 100=nominal + 101-200=+6dB amp zone; VLC is 0-125; PotPlayer is 0-100 default with amp toggle. Does Tankoban's amp-zone behave equivalently to VLC+200% or PotPlayer's amp toggle? Sample at 50/100/150/200 equivalent positions.
- Mute key: Tankoban = M, VLC = M, PotPlayer = M. Behavior convergence.
- Speed preset chip: Tankoban's dedicated chip popover with 0.25/0.5/0.75/1.0/1.25/1.5/2.0 presets — does VLC's `[` `]` keys or PotPlayer's speed menu hit same presets? Audio pitch preservation behavior at non-1.0x?

### Batch 1.3 — Seek behavior (category C)

**Highest-value batch in Phase 1.** Tankoban has:
- Fast seek (arrow keys ±10s, Shift+arrows ±60s) — keyframe-aligned
- Exact seek (chapter-nav PageUp/PageDown forces exact)
- Frame step (comma/period)
- Click-to-seek (SeekSlider one-shot OR drag)
- Chapter tick markers on slider
- Stream-mode buffered ranges gray-bar (skip for VLC/PotPlayer — no stream analog, mark Tankoban-exclusive axis)

Measure:
- Key-press → frame-on-screen latency for fast seek
- Seek-to-exact-time-offset accuracy in ms across 10 seeks to random offsets
- Frame step latency + step-size accuracy
- Click-to-seek offset accuracy (SeekSlider vs mpv `seek <time> absolute` vs VLC rc `seek <time>`)
- Chapter tick visibility + click-to-chapter behavior
- Reference via mpv IPC `time-pos` property (microsecond precision ground truth)

### Batch 1.4 — HUD / overlay (category D)

- Filename title: ellipsis-elision shape + update-on-resize behavior
- Time counter format: HH:MM:SS vs MM:SS preference across players
- Progress bar visual style comparison (chapter ticks, drag handle, hover tooltip shape)
- Auto-hide timeout: Tankoban 3s, VLC 1-2s default, PotPlayer configurable. Document divergences.
- Cursor auto-hide behavior
- Mouse-move reveal (already validated in Agent 0 smoke-by-proxy 2026-04-20 — HUD needs mouse MOVE not just hover)
- SeekSlider hover tooltip (Tankoban shows chapter + time) vs reference

### Batch 1.5 — Fullscreen + aspect (category J)

- F / F11 toggle latency
- Cinemascope aspect handling (letterbox vs crop) — critical since STREAM_AUTOCROP Bug A just shipped 2026-04-20. Compare against VLC/PotPlayer auto-crop or manual-aspect selection
- Aspect-ratio menu entries: Tankoban has (none / 16:9 / 2.35:1 / 2.39:1 / 1.85:1 / 4:3) — does VLC/PotPlayer cover the same set?

### Batch 1.6 — Keyboard shortcuts (category K)

~30 Tankoban shortcuts. Map each to its VLC + PotPlayer equivalent. Document:
- Which shortcuts CONVERGE (Space, M, F) across all 4 — standard
- Which DIVERGE (Backspace behavior, Ctrl+T always-on-top, `?` keybind editor)
- Which are Tankoban-exclusive (Shift+N stream-next, keybind editor itself — skip reference comparison)
- PotPlayer keybinding factory-default profile required (PotPlayer rebinds heavily per user-customization)

### Batch 1.7 — Close / exit (category L)

- Escape behavior: Tankoban = back_to_library, VLC = stop-to-pause-at-end-of-file default, PotPlayer configurable. Document.
- Backspace = back_fullscreen in Tankoban. Reference comparison.
- Intentional-stop flag behavior (clears m_currentFile + m_openPending) — reference players' close-state analog

### Phase 1 exit criteria

- `agents/audits/comparative_player_2026-04-NN_p1_transport.md` lands, ~150-250 lines, Executive Summary + per-sub-axis Observed/Reference/Divergence/Verdict blocks, closing Fix Candidates block.
- Reference-player version pin recorded in TODO preamble.
- Tankoban + all reference players cleanly killed via `scripts/stop-tankoban.ps1` + equivalent (VLC: `Stop-Process -Name vlc`, etc.) per Rule 17.
- Windows-MCP PROFILE_SNAPSHOT A/B (queued in `project_windows_mcp_live.md`) run if desired during this wake (bundles well with smoke).
- Agent 3 posts `READY TO COMMIT` for the audit file + chat.md ratification-request for Fix Candidates.

---

## 6. Phase 2 — Tracks + Subtitle Decode (Agent 3 wake 2, ~75 min)

**Why P0-P1:** subtitle rendering is Tankoban's highest-visibility differentiation axis (libass via SHM overlay, cinemascope-aware geometry). Tracks popover IINA-parity is a recent ship (PLAYER_UX_FIX Phase 6 closed 2026-04-16). Divergences here are user-noticed.

**Covers Tankoban surface categories E, F** — ~10 surfaces.

### Batch 2.1 — Tracks popover IINA-parity (category E)

- Ctrl+Right (or chip click) open — does reference player have equivalent popover vs fullscreen menu?
- Audio track selection + live-switch latency
- Subtitle track selection (internal tracks from demuxer)
- Subtitle delay ±100ms stepper + reset — VLC has `h/j` keys (50ms steps); PotPlayer has Alt+arrow. Stepper UX comparison.
- Subtitle style UI: font 8-72pt slider, margin V 0-100px, outline toggle, font color combo, bg opacity slider. VLC/PotPlayer equivalent settings depth
- Per-show persistence (Tankoban saves to `shows` CoreBridge domain) — reference players' sidecar/library-level persistence

### Batch 2.2 — Subtitle rendering paths (category F)

- Embedded ASS/SSA (libass) rendering — visual diff of animated karaoke + positioning directives + stylized text vs VLC's libass vs PotPlayer's native ASS renderer vs mpv libass (precision reference)
- Embedded PGS (bitmap) rendering — position + alpha compositing diff
- SRT→ASS conversion path (Tankoban-internal) vs reference handling
- External SRT auto-load (Tankoban scans parent folder for `.ass/.srt/.ssa/.vtt/.sub`) — VLC + PotPlayer auto-load behavior comparison
- Addon subtitles (Tankostream aggregator) — NO reference peer (skip), Tankoban-exclusive axis
- Cinemascope crop + subtitle lift behavior — critical post-STREAM_AUTOCROP Bug A. Visual screenshot at same frame position across all players, diff subtitle placement in pixels.
- SHM overlay internals — SKIP (no user-visible peer, drop per §12)

**Objective criteria for Batch 2.2:** pixel-level screenshot comparison at same frame timestamp, crop to subtitle region, RGB diff summary.

### Phase 2 exit criteria

- `agents/audits/comparative_player_2026-04-NN_p2_subtitles.md` lands.
- Test fixtures for this phase: MKV with ASS/SSA + PGS + multi-audio + external SRT sibling + cinemascope 2.39:1 source. **Flag to Hemanth at Phase 2 entry if any fixture is missing** — see §9.
- Same Rule 17 cleanup.

---

## 7. Phase 3 — HDR + Video Filters (Agent 3 wake 3, ~75 min)

**Why P1:** HDR tone-mapping is both identity-shaping (honest dropdown per `project_player_ux_fix.md`) and visually-sensitive. Video filters are the audio-EQ-analog for video path.

**Covers Tankoban surface categories G, H** — ~6 surfaces.

### Batch 3.1 — HDR tone-map (category G)

Tankoban exposes 4 tone-map modes: Off (hard clip) / Reinhard / ACES / Hable. mpv exposes `tone-mapping` with `hable`/`reinhard`/`mobius`/`linear`/`bt.2390`/`none`. VLC has limited HDR handling (defaults to hard clip in most builds). PotPlayer has `madVR` integration for HDR (optional but user-accessible).

Measure on **4K HDR10 BT.2020 PQ sample**:
- Peak luminance sample from rendered output (screenshot → max-pixel-luma histogram)
- RGB histogram of a color-rich frame across modes — per-player, per-mode
- Color gamut indicator: does BT.2020 content render in-gamut on BT.709 display? Clipped/mapped/preserved?
- Mode labeling honesty: Tankoban's "honest dropdown" semantic (only shows HDR modes on HDR content) — VLC+PotPlayer don't hide UI for non-applicable modes

**Objective criterion required** per §14 risks: no eyeballing. Peak-luminance numerical + histogram diff.

### Batch 3.2 — Video filters (category H)

- Deinterlace toggle (Tankoban D key) — comparison on 1080i sample
- Brightness / Contrast / Saturation sliders — step size comparison, visual at center + ±50% settings
- Normalization / volume norm (Shift+A) — Tankoban applies via ffmpeg audio filter. VLC+PotPlayer equivalent (ReplayGain / loudness norm)
- Interpolate pass — Tankoban exposes a toggle; reference behavior

### Phase 3 exit criteria

- `agents/audits/comparative_player_2026-04-NN_p3_hdr_filters.md` lands with **objective-criteria-met** verdicts (no "looks better" — numerical only).
- HDR + interlaced fixtures on disk (Hemanth-provided OR ffmpeg-synthesized at Phase 3 entry).
- Same Rule 17.

---

## 8. Phase 4 — State + Polish + Misc (Agent 3 wake 4, ~45 min)

**Why P1-P2:** cleanup of all remaining Tankoban surfaces with reference peers. Shorter phase (fewer visual diffs, more binary "does it do X? yes/no").

**Covers Tankoban surface categories I, O (user-visible subset only), P, Q, R + 2 added surfaces** — ~15 surfaces.

### Batch 4.1 — Audio EQ (category I)

- 10-band 31-16kHz frequency response measurement — Tankoban presets (Flat/Rock/Pop/Jazz/Classical/BassBoost/TrebleBoost/VocalBoost) vs VLC's built-in preset list vs PotPlayer equivalent
- Per-band gain range + slider step
- User-profile save/load UX (Tankoban saves to QSettings `eq/profiles`) vs reference
- DRC toggle (Tankoban) — Dynamic Range Compression presence + behavior in reference

Measure frequency response by playing a **pink noise sample + capturing audio** via each player's output → FFT compare peak amplitudes at band centers. Harness: each player → loopback capture → audacity or PowerShell `Measure-Object` on .wav.

### Batch 4.2 — User-visible performance (category O subset)

**SKIP these internals** (drop per §12): SHM overlay internals, D3D11 zero-copy import mechanism, VsyncTimingLogger, SyncClock A/V drift internals.

**COMPARE externally-measurable:**
- Frame-pacing jitter: each player's stat overlay / VLC `Tools > Media Information > Stats` / PotPlayer Ctrl+F1 stats / Tankoban `I` badge. Dropped-frames-per-minute on a sustained 5-minute play.
- CPU/GPU utilization via PowerShell `Get-Counter` sampled once per second during playback

### Batch 4.3 — Persistence (category P)

- Resume position: close player mid-playback, reopen same file — does resume-position match across players?
- Track preference persistence (audio + subtitle language selection) — per-show (Tankoban) vs per-file vs global
- EQ profile persistence across app-restart
- Always-on-top persistence (Tankoban Ctrl+T)
- Stats badge persistence (Tankoban I)

### Batch 4.4 — Error handling (category Q)

- `streamFailed` text path + 3s auto-nav — stream-mode only, mark Tankoban-exclusive if no VLC/PotPlayer stream-analog
- File-switch recovery (sendStopWithCallback) — comparison: open file A, while playing switch to file B. Tankoban fences stop before re-open.
- Crash recovery user-visible outcome ONLY — force-kill sidecar via `taskkill /F /IM ffmpeg_sidecar.exe` mid-playback. Does Tankoban auto-resume at last position? Reference players have no sidecar — compare against VLC force-process-kill → main-process resume. **Skip SidecarProcess exponential backoff timing** per §12.

### Batch 4.5 — Additional surfaces (category R)

- Always-on-top (Ctrl+T) — reference equivalents
- Snapshot (Ctrl+S, saves to Pictures/Tankoban Snapshots/, burns subtitles in) — VLC `Shift+S` to snapshots dir, PotPlayer Ctrl+E. Format + path + burn-in comparison.
- Picture-in-Picture (Ctrl+P, 320×180, pinned bottom-right, always-on-top) — VLC has no native PiP (Qt limitation), PotPlayer has. Comparison.
- Open URL (Ctrl+U) — HTTP / magnet support comparison (Tankoban has magnet via stream-mode; VLC has HTTP + some streaming protocols; PotPlayer has HTTP)
- Playlist queue (L toggle PlaylistDrawer, .m3u load/save) — VLC playlist panel, PotPlayer playlist. Behavior + UX comparison.
- Keybinding live editor (?) — Tankoban-exclusive (PotPlayer has keybind editor buried in Settings; VLC in Preferences). Document UX path depth comparison.
- Stats badge (I, 1 Hz polled) — VLC stats window, PotPlayer Ctrl+F1. Information density + refresh rate comparison.
- Right-click context menu content — scope-match axes only (aspect / crop / speed / track — NOT VLC's plugin menu or PotPlayer's extended options).

### Batch 4.6 — Added surfaces from Plan agent review

- **WASAPI exclusive mode** — VLC + PotPlayer both expose via output device settings. Does Tankoban expose WASAPI exclusive? Check sidecar audio path + UI. If not exposed, flag as Tankoban-exclusive-gap (we don't, they do — but per hard-scope constraint §3, this is only a comparison axis if TANKOBAN has some surface for it).
- **Audio delay control** (AV sync, distinct from subtitle delay) — Tankoban: Ctrl+Minus/Equal per shortcut list. VLC: `k/l` keys. PotPlayer: Alt+Shift+arrow. Step size + reset behavior + UI visibility comparison.

### Phase 4 exit criteria

- `agents/audits/comparative_player_2026-04-NN_p4_state_polish.md` lands.
- All persistence tests cleaned up — delete test QSettings keys before wake close.
- Same Rule 17.
- **Overall audit-closing block:** Agent 3 posts a single cross-phase summary to chat.md: "Comparative audit COMPLETE. 4 phases, 4 audit files. Aggregate: X CONVERGED, Y DIVERGED, Z WORSE, W BETTER. Fix Candidates ratification-request bundles (see phase audits)." Agent 0 triages post-summary.

---

## 9. Scope decisions locked in

Documented calls so re-litigation is avoided:

1. **4 phases, 1 TODO** (not 3 separate TODOs; not 5-8 finer phases). Governance fragmentation vs review-gate inflation tradeoff — 4 phases wins.
2. **1 Agent-3 wake per phase**, ~45-75 min each. Wake pacing matches our phase-per-wake convention.
3. **Audit deliverable, not fix deliverable.** DIVERGED/WORSE findings feed separate fix-TODO authoring. BETTER findings are pinned, never fed to fix.
4. **Objective criteria required** for any visual axis (HDR, subtitle placement, color). No eyeballing allowed for verdict.
5. **Reference players pinned at Phase 1 start** — version drift mid-audit triggers re-audit for affected phases.
6. **mpv as precision reference**, not identity target. Tankoban's identity is IINA (per `feedback_reader_rendering.md` + `project_player_perf.md`). mpv IPC is a ground-truth tool, not a "match mpv behavior" mandate.
7. **Hybrid control channels per player**, not MCP-only. mpv IPC / VLC rc / PotPlayer CLI+MCP / Tankoban telemetry+sidecar PERF. Document precision ceiling per channel.
8. **Scope fence: Tankoban feature set only.** No "VLC has X, should we add it?" analysis. If Tankoban doesn't have the surface, it's not a comparison axis.

## 10. Isolate-commit candidates

Each phase's audit .md is **one commit**. Agent 3's RTC line per phase names the single audit file + chat.md. Rule 11 batching via Agent 0 sweep (or Agent 3 direct commit if Agent 0 absent).

Suggested commit shape: `[Agent 3, PLAYER_COMPARATIVE_AUDIT Phase N SHIPPED]: ...` followed by aggregate verdict counts + Fix-Candidates-block link.

**No src/ commits expected** in this TODO. If Agent 3 discovers a critical DIVERGED finding mid-phase that warrants immediate hotfix (e.g., "HDR dropdown is broken, not just divergent"), escalate via chat.md — don't inline src/ changes in this TODO's scope.

## 11. Existing functions / utilities / memories to reuse

- **MCP patterns** — `feedback_mcp_smoke_discipline.md` 5 rules (Screenshot over Snapshot, batch clicks, Shortcut over Click, compound PowerShell, cached coords). Every Phase applies these.
- **Plugin skills adopted** — `feedback_plugin_skills_adopted.md`. Phase-entry invocations:
  - `superpowers:systematic-debugging` at bug-hunt moments (DIVERGED investigation)
  - `superpowers:verification-before-completion` pre-RTC on each phase
  - `superpowers:brainstorming` for Phase-audit-design adjustments if scope needs tweaking
  - `claude-mem:mem-search` before re-deriving anything that might have been investigated in prior sessions
  - `claude-mem:smart-explore` for Tankoban code structural queries during audit (e.g., "what does VideoPlayer::sendSeek do?")
- **Telemetry parsers** — `scripts/runtime-health.ps1` (Agent 7 MCP audit recommendation #1, shipped 2026-04-19). Tankoban-side measurement collapses to one command.
- **Rule 17 cleanup** — `scripts/stop-tankoban.ps1`. Run after each phase smoke.
- **MCP launch pattern** — `project_windows_mcp_live.md` direct-launch recipe (Qt PATH + TANKOBAN_STREAM_TELEMETRY=1 + TANKOBAN_ALERT_TRACE=1 + Start-Process).
- **Stop-process pattern for reference players**: `Stop-Process -Name vlc`, `Stop-Process -Name PotPlayerMini64`, `Stop-Process -Name mpv` after each phase smoke. Kill pipes if using mpv IPC.
- **ffmpeg on PATH** — Tankoban deploys ffmpeg next to Tankoban.exe. Use for fixture synthesis (external SRT, interlaced sample, corrupted MP4) without separate install.
- **Existing reference audits for shape**: `agents/audits/mcp_smoke_harness_2026-04-19.md` (Agent 7 audit) + `agents/audits/_superseded/player_stremio_mpv_parity_2026-04-17.md` (prior player parity audit). Phase audit files should mirror the shape (Exec Summary + Q1/Q2/Q3 axes + Observed/Reference/Divergence/Verdict + Fix Candidates block).

## 12. Review gates / Open design questions

**Review gate** (Agent 6 decommissioned): Agent 3 self-reviews each phase audit via `superpowers:verification-before-completion` skill before posting RTC.

**Open design questions — Agent 3 decides as domain master:**

1. **PROFILE_SNAPSHOT A/B bundling with Phase 1**: the Bucket 2 measurement is queued for the next smoke-heavy wake per `project_windows_mcp_live.md` TRIGGER. Phase 1 qualifies. Agent 3 decides whether to bundle — if yes, edit `.mcp.json` + ask Hemanth for 2 restarts around Phase 1; if no, defer to a later smoke.

2. **Fixture sourcing for Phase 2/3**: MKV with ASS + PGS + multi-audio; 4K HDR10 BT.2020 PQ sample; interlaced 1080i sample. Agent 3 flags to Hemanth at Phase 2 entry AND Phase 3 entry with specific fixture requirements. Hemanth either points at an existing file OR Agent 3 synthesizes via ffmpeg (SRT + interlaced are synthesizable; HDR sample needs real source).

3. **PotPlayer keybinding profile**: factory-default or user-customized? Agent 3 chooses factory-default per Plan agent recommendation; document choice in TODO preamble. Agent 3 applies `Reset All Shortcuts` in PotPlayer Preferences before Phase 1 measurement.

4. **Stream-mode surface exclusion**: streamFailed/streamStopped/bufferedRangesChanged/stall/prefetch are Tankoban-exclusive. Phase 1 Batch 1.3 only includes the buffered-ranges axis as a Tankoban-exclusive-mark (log it, don't compare). Agent 3 decides whether to run a **secondary mini-audit** comparing Tankoban stream-mode to Stremio reference per prior Slice A/B work — defer to post-audit if interesting, not in this TODO's scope.

5. **Objective HDR criteria choice**: peak-luminance sample vs RGB histogram vs CIE1931 color-gamut plot. Agent 3 picks criterion at Phase 3 design entry (peak-luminance sample is simplest + sufficient per Plan agent suggestion). Document choice in Phase 3 audit preamble.

6. **Phase 4 Batch 4.6 WASAPI surface**: if Tankoban doesn't expose WASAPI at all, is this a comparison axis? Per §3 hard scope, NO — but flag it as an empty-surface observation ("Tankoban doesn't surface this; VLC+PotPlayer do") in the audit without a verdict. Agent 3 decides how to frame.

7. **Re-audit trigger on version drift**: if mid-audit a reference player is upgraded (e.g., VLC auto-updates), which phases re-audit? Agent 3 decides severity — Phase 1 (transport) likely unaffected by point-releases; Phase 3 (HDR) most sensitive. Document re-audit scope if triggered.

## 13. What NOT to include (explicit deferrals)

Beyond the §3 non-goals:

- **Stream-mode vs Stremio reference mini-audit** (open question #4) — not in this TODO. Separate TODO if Agent 3 flags interest post-main-audit.
- **Codec support matrix** — which codecs do all 4 players decode? Out of scope; Tankoban relies on ffmpeg-in-sidecar which covers everything. Not a user-visible surface divergence.
- **Network streaming protocols** (SHOUTcast / UPnP / DLNA / etc.) — Tankoban doesn't support; not a comparison axis.
- **Plugin / extension architectures** — Tankoban is monolithic sidecar; no plugin surface; not a comparison axis.
- **Library management** — Tankoban's Videos tab library is Agent 5 domain, not player domain. Excluded.
- **Stream mode source selection** — Torrentio addons are stream-mode / Stream page domain (Agent 4), not player. Excluded.
- **Sidecar stats panel** — not user-visible; internal telemetry. Excluded per §12 drop list.
- **CMakeLists.txt touches** — NO src/ changes in this TODO, so no CMakeLists updates.

## 14. Rule 6 + Rule 11 application

**Rule 6 (phase exit criteria):** each phase exits on audit-file land + Agent 3's RTC line in chat.md. No code smoke required — the "smoke" IS the audit's live measurement. Verification procedure in §15.

**Rule 11 (commit batching):** each phase's audit file commits via Agent 0 sweep (or Agent 3 direct if Agent 0 absent). Expect 4 commits from this TODO over 4 wakes, plus any follow-on fix-TODO authoring commits if ratified.

**Rule 7 (sidecar rebuild):** NO sidecar touches expected in this TODO. If a DIVERGED finding mid-phase surfaces a sidecar bug, Agent 3 flags + defers to follow-on fix-TODO; does not ship fix here.

**Rule 17 (smoke cleanup):** every phase MUST kill Tankoban + ffmpeg_sidecar + VLC + PotPlayer + mpv before wake close. `scripts/stop-tankoban.ps1` handles Tankoban side; reference players via `Stop-Process -Name`.

**Rule 14 / 15 (decision authority + self-service):** Agent 3 makes all technical calls (fixture choice, criterion selection, phase pacing). Hemanth only ratifies Fix Candidates scope lines at phase boundary. Agent 3 drives all smokes via Windows-MCP — no Hemanth clicks.

## 15. Verification procedure

End-to-end smoke shape for this TODO itself (not per-phase):

1. **Phase 1 entry**: Agent 3 records reference-player versions in a new `## Reference-player versions (PINNED)` section of this TODO. Commits that section update with Phase 1 audit.

2. **Per-phase wake** (pattern repeats 4x):
   - Agent 3 invokes `claude-mem:mem-search` to recall any prior related investigation.
   - Agent 3 invokes `superpowers:systematic-debugging` for structured approach.
   - Agent 3 launches each reference player via documented control channel (mpv IPC / VLC rc / PotPlayer CLI / Tankoban direct-launch per memory recipe).
   - Agent 3 drives the phase's comparison axes via Windows-MCP (applying Bucket 1 discipline rules).
   - Agent 3 captures findings inline in the audit .md draft.
   - Pre-RTC: Agent 3 invokes `superpowers:verification-before-completion` for evidence checklist.
   - Agent 3 posts `READY TO COMMIT` for the audit file + Fix Candidates ratification-request to Hemanth.
   - Rule 17: all 4 players killed before wake close.

3. **Fix Candidates ratification loop** (per phase):
   - Agent 3's phase-closing chat.md post: "Phase N audit complete. X CONVERGED, Y DIVERGED, Z WORSE, W BETTER. Proposing fix-TODO covering {axes A, B, C} at {blocker|polish} tier. Defer {cosmetic axes}. Ratify scope?"
   - Hemanth reads audit + ratifies / adjusts / defers.
   - On RATIFY: Agent 0 authors follow-on fix-TODO (separate TODO file; this comparative TODO is audit-only).
   - On DEFER: Agent 0 notes the deferred axes in a memory update + advances phase cursor.
   - On ADJUST: Agent 3 may rerun specific sub-axes if scope changes.

4. **Post-Phase-4 overall close**:
   - Agent 3 posts cross-phase summary aggregate verdict counts.
   - CLAUDE.md dashboard row removes the TODO from Active (moves to CLOSED / SUPERSEDED section).
   - MEMORY.md index updates with closure note.
   - Aggregate fix-TODOs (if Hemanth ratified any) become Active repo-root TODOs for future Agent 3 wakes.

## Next steps post-approval

1. Agent 0 updates `CLAUDE.md` dashboard with the new Active TODO row (this action, same wake).
2. Agent 0 updates `MEMORY.md` index entry for Active repo-root fix TODOs (this action, same wake).
3. Agent 0 updates `agents/STATUS.md` Agent 3 section to reflect standby-for-Phase-1 state (this action, same wake).
4. Agent 0 posts authoring-done + Agent 3 summon-brief + RTC to chat.md (this action, same wake).
5. Agent 3 reads required-memory chain per §11, picks up Phase 1 on next wake.
6. Agent 0 triages per-phase Fix Candidates at each ratification gate; authors follow-on fix-TODOs as Hemanth ratifies.
7. Post-Phase-4: Agent 0 archives this TODO to `agents/_archive/todos/` per repo-hygiene pattern; closing notes to MEMORY.md.

---

**End of plan.**
