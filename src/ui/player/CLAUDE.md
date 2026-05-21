# Player Domain — Agent 3

This file auto-loads when any agent reads a file under `src/ui/player/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Sibling file at `native_sidecar/CLAUDE.md` carries the ffmpeg binary side of the same domain.

## Domain owner

**Agent 3** (Video Player). Full Tier-1 + Tier-2 skill autonomy restored 2026-05-16 after a 3-week HEMANTH-DRIVEN MODE directive was reverted. Agent 3 may now initiate audits, author fix-TODOs, and fire Codex Trigger A/B/C/D for player work without Hemanth pre-approval. The 8 player TODOs archived 2026-04-25 stay archived — three weeks of MPV_CUTOVER + ffmpeg-sidecar evolution makes most of them partly moot; Agent 3 may mine `agents/_archive/todos/` for ideas but authors fresh TODOs for new work. See `feedback_hemanth_driving_player_domain.md`.

## Active arc — post-MPV_CUTOVER steady state

**MPV_CUTOVER CLOSED 2026-05-05.** The short version: mpv backend is RETIRED. Tankoban runs a single backend — the native ffmpeg sidecar. That's the permanent architecture going forward.

What that means for this directory: `VideoPlayer`, `FrameCanvas`, `IPlayerBackend`, `SidecarProcess` are the only player-side classes that matter. The dual-backend scaffolding (BackendFactory, `switchBackendTo`, `m_currentBackendType`, `m_mpvWidget`) has been deleted. There is no "option B." See `project_mpv_cutover_2026-05-05.md` for the full 13-task arc ledger and recovery instructions.

**Picture-quality lift survives:** the libplacebo + Vulkan + ewa_lanczossharp scaler stack from the prior MAKE_MPV_BEAT_FFMPEG arc was carried through the cutover into the sidecar's GPU renderer (`native_sidecar/src/gpu_renderer.{h,cpp}`). The quality gain is real; the implementation now lives entirely on the ffmpeg path. See `project_make_mpv_beat_ffmpeg_arc.md`.

**Recovery path** (if mpv ever needs to come back): `git revert` the cutover commit + `git mv agents/_archive/src_player_mpv/ src/ui/player/mpv/` + `git mv agents/_archive/resources_libmpv/ resources/libmpv/`. Everything is intact there.

## Key files in this directory

- `VideoPlayer.{h,cpp}` — top-level player widget: open/close, HUD, keyboard/mouse, screensaver inhibit, `devSnapshot()` for the dev bridge
- `FrameCanvas.{h,cpp}` — D3D11-backed render surface (replaced the old D3D11Widget). Cross-process shared texture routing.
- `IPlayerBackend.{h,cpp}` — interface that VideoPlayer talks to. Single concrete impl is SidecarProcess.
- `SidecarProcess.{h,cpp}` — Qt-side IPC client. Manages the ffmpeg sidecar child process, JSON-over-stdout event parsing, IPC round-trip latency tracker (writes to `out/ipc_latency.log` on shutdown). Agent 4 (Stream) has a cross-domain stake here — it drives SidecarProcess for stream-mode playback.

## Reference apps

- **mpv** at `C:\tools\mpv\` — source on disk per `reference_mpv_install.md`. Mine for: display-resample patterns, libass internal sub rendering, audio-clock architecture. Do NOT re-introduce mpv as a runtime dep without a new Codex arc + Hemanth ratification.
- **Stremio mpv.cpp** — QOpenGLWidget + libmpv reference in the same family as the prior MpvVideoWidget. See `reference_stremio_mpv_cpp.md`.
- **Nuvio** at `Downloads\NuvioMobile-cmp-rewrite\` — HTTP-only ExoPlayer reference per `project_nuvio_reference.md`. Cross-references Agent 4's stream-server pivot on the network-source side.

## Load-bearing memories (read when touching this domain)

**Arc history (read first on any Agent 3 wake):**
- `project_mpv_cutover_2026-05-05.md` — MPV_CUTOVER 13-task arc closed. Single-backend restored. Recovery path. Read first.
- `project_make_mpv_solo_arc.md` — MAKE_MPV_SOLO 15-task arc (tasks 1-10 closed 2026-05-01; 11-15 = cutover tasks now done)
- `project_make_mpv_beat_ffmpeg_arc.md` — libplacebo + Vulkan scaler arc. Picture-quality lift carried through cutover.
- `project_mpv_backend_integration_complete.md` — MPV_RENDER_API_INTEGRATION history. Historical only; superseded by cutover.
- `project_mpv_ffmpeg_parity_p1_complete.md` + `project_mpv_ffmpeg_parity_p2_complete.md` + `project_mpv_ffmpeg_parity_p2_decisions.md` — Phase 1 stream-mode retrofit + Phase 2 subtitle BEST-POSSIBLE arc. Decisions still load-bearing for subtitle UX.

**Hardware + rendering constraints:**
- `user_hardware_intel_uhd_620.md` — Hemanth's GPU. WGL_NV_DX_interop + D3D11 NTHANDLE is BROKEN on this hardware; native GL WORKS. Design any GPU-path change around this constraint first.
- `project_native_d3d11.md` + `project_player_polish.md` — D3D11Widget → FrameCanvas migration, COMPLETE 2026-04-14.
- `feedback_dxgi_resizebuffers_flags_must_match.md` — ResizeBuffers flags MUST equal creation flags. Check swapBB vs hwnd EARLY when touching FrameCanvas resize paths.
- `feedback_cross_process_d3d11_sync.md` — Cross-process D3D11 shared textures need keyed mutex OR SHM routing.

**Sidecar IPC discipline:**
- `project_sidecar_migration.md` — Option 1 ratified 2026-04-15: vendor native_sidecar. Agent 3 owns.
- `feedback_sidecar_metadata_decoupling.md` — Sidecar probe data must emit post-probe in open_worker, NOT at first_frame. Violating this causes play-file's firstFrameSeen to be unreliable.
- `feedback_dev_bridge_visual_blindspot.md` — `tankoctl player-*` / `firstFrameSeen=true` proves the signal fired, NOT that the canvas painted. Eyes-on-screen (pywinauto screenshot) is still required to gate visual smokes.
- `project_sidecar_dispatcher_non_blocking_decision.md` — Stream pause/close regression = dispatcher blocks on handle_set_tracks → preload_subtitle_packets (HTTP). Root cause for Agent 4's SIDECAR_DISPATCHER_NON_BLOCKING arc; touches SidecarProcess.

**Player UX history:**
- `project_player_perf.md` — PLAYER_PERF_FIX CLOSED 2026-04-16. P1+2+3B (D3D11_BOX path) shipped.
- `project_player_ux_fix.md` — PLAYER_UX_FIX CLOSED 2026-04-16. 6 phases shipped.
- `project_video_player_state_2026_04_24.md` — 4 orthogonal player fixes 2026-04-24. Pre-read before touching VideoPlayer.
- `project_libplacebo_unification_todo.md` — LIBPLACEBO_SINGLE_RENDERER_FIX_TODO at repo root. 4 phases env-gated. P1 ready to execute.
- `reference_sidecar_premigration.md` — TankobanQTGroundWork pre-migration sidecar. Regression reference only.

**Coding taste + discipline:**
- `feedback_subjective_over_trace.md` — "Dead" code removal that regresses subjective smoothness with unchanged metrics → keep it. Player feel is a valid justification.
- `feedback_subtitle_position_yoffset_not_libass.md` — Sub-position slider = Y-offset on ASS_Image, NOT ass_set_line_position.
- `feedback_lifecycle_parity_with_mainwindow.md` — Any non-MainWindow VideoPlayer open/close path: diff against MainWindow.cpp:545-570 for parity before shipping.
- `feedback_qt_sizehintforrow_unreliable_pre_show.md` — Don't query sizeHintForRow pre-show. Hardcode ROW_HEIGHT.
- `feedback_backend_swap_review_grep.md` — Plan-mode review for any swap-shaped work: grep BOTH explicit pointer-storers AND qobject_cast<X> cast-gated setup blocks.
- `feedback_mpv_property_vs_render_semantic_gap.md` — Property edges lead render. Defer UI dismiss to MPV_EVENT_PLAYBACK_RESTART; pattern reusable for phased playback transitions.
- `feedback_mcp_keyboard_differential_test.md` — When key X "doesn't work" but key Y does: run MCP differential test (send X + Y, check tankoctl logs for keyPress lines) BEFORE shipping a focus-policy guess.
- `feedback_hemanth_picks_longer_path.md` — Hemanth picks higher-ceiling architectural paths over shortcut equivalents. Don't soft-pedal the longer option.
- `feedback_tankoban_build_link_dominates.md` — Build time ~915s regardless of flags or files touched; link step is dominant. Don't chase --parallel / header tweaks.
- `feedback_one_fix_per_rebuild.md` — Never batch fixes. One change, one rebuild, one visual check.
- `feedback_pick_simpler_equivalent_before_fallback.md` — First-pick HW-blocked + peer in same family → switch peer, not fallback.

## Dev-bridge surface (Agent 3's commands)

Agent 3 owns the `player-*` / `sidecar-*` / `subs-*` / `osd-*` tankoctl prefixes (v1.7 — 40 commands):
- **player-***: tracks / chapters / volume / speed / HUD state / decoder-stats / canvas-size / screenshot / seek / pause / resume / aspect / crop / keybindings
- **sidecar-***: process-state / stream-info / restart / ipc-latency (`sidecar-get-decoder-queue` + `sidecar-get-render-queue` return `NOT_YET_IMPLEMENTED` — deferred to v1.8 native_sidecar IPC extension)
- **subs-***: positioning / fonts / active-track
- **osd-***: OSD state reads

**IMPORTANT:** v1.7 shipped code-green at `a3c0633` but smoke was deferred. First Agent 3 wake on a stable build should run the full smoke matrix: `ping → tankoctl player-* enumerated (40 cmds) → play-file pick-from-get-videos → player-pause/resume/get-audio-tracks/seek 30 → sidecar-get-process-state → player-screenshot`.

Full catalog enumerable via `out\tankoctl.exe ping`. Cross-cutting `log-mark <label>` (v1.9) is the headline diagnostic tool — mark before + after a test, then pivot all 4 log streams on that marker.

## Build / MCP lane discipline (gov-v7)

Player smokes typically claim **two lanes simultaneously:** `mcp` (visual UI smoke via pywinauto-mcp for canvas paint + HUD text verification) and `build` (when sidecar rebuild is involved — `native_sidecar/build.ps1`). Claim both in chat.md before starting; release both when done.

Use the lease registry per Rules 19 + 22. `mcp` lane lock claimed in chat.md as `MCP LOCK CLAIMED` / released as `MCP LOCK RELEASED`. Per-lane build dirs (`TANKOBAN_BUILD_LANE=<lane>`) bypass the shared lock — use them when isolating from concurrent brothers' builds.

`tankoctl player-*` replaces most structural-state smokes (pause state, track list, seek position) — reach for it before pywinauto. pywinauto is the fallback for visual-only verification (canvas paint, subtitle render position, HUD text) that tankoctl can't read directly. See `feedback_dev_bridge_visual_blindspot.md`.

## When this file activates

Auto-loads when any agent reads a file under `src/ui/player/`. Treat the content as ambient context: who owns this code, what arc is in flight, which memories are load-bearing, which dev-bridge commands belong to this domain.

If you're **Agent 3**: this is your home turf, redundant reminder.

If you're **Agent 4** (Stream): SidecarProcess is shared territory — stream-mode playback drives it directly. Cross-domain edits to SidecarProcess that change the IPC protocol require Agent 3 sign-off (Rule 14). For stream-side dispatching concerns, see `project_sidecar_dispatcher_non_blocking_decision.md`.

If you're **Agent 0**: build verification cross-domain is fine. Full player direction changes are not.

If you're **any other brother**: read, audit, and ask Agent 3 before touching. This is a domain with significant hardware-specific constraints (Intel UHD 620, D3D11 texture routing, sidecar IPC protocol) — surface-level edits in the wrong place break things that aren't obvious from the code alone.
