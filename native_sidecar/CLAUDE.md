# Native Sidecar Domain — Agent 3

This file auto-loads when any agent reads a file under `native_sidecar/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Sibling file at `src/ui/player/CLAUDE.md` carries the Qt-side player that drives this binary.

## Domain owner

**Agent 3** (Video Player + native sidecar). The sidecar and the Qt-side player are one domain — same agent, same arc, same discipline. Everything in `src/ui/player/CLAUDE.md` applies here. This file covers the binary-specific shape: ffmpeg pipeline, GPU renderer, IPC protocol, build toolchain.

## What this binary is

The native sidecar is a standalone MinGW-built executable that lives at `resources/ffmpeg_sidecar/`. The main Qt app launches it as a child process (`SidecarProcess`) and communicates via JSON-over-stdout. There is **no shared memory**, no D3D11 texture handed over the wire at this layer — the sidecar decodes, renders, and routes frames; the Qt side reads state and controls playback via IPC commands sent to the sidecar's stdin.

This is the **only** player backend. mpv was retired 2026-05-05 (MPV_CUTOVER arc). There is no fallback, no env-var override, no second backend to swap to. Single-backend is the permanent architecture. See `project_mpv_cutover_2026-05-05.md` and `src/ui/player/CLAUDE.md` for the cutover rationale and recovery path.

## Key files in `native_sidecar/src/`

- `main.cpp` — entry point + JSON command dispatcher. Receives commands on stdin, dispatches to worker functions, emits JSON events on stdout. Session-id strict mode: emits `version` event with `{schema:"v2", session_strict:true}` at startup; session-scoped events with empty sessionId are dropped post-strict-handshake.
- `gpu_renderer.{h,cpp}` — **libplacebo + Vulkan + ewa_lanczossharp scaler stack.** This is the picture-quality lift from the MAKE_MPV_BEAT_FFMPEG arc, carried through the cutover. Do NOT simplify or remove this without a new Hemanth-ratified arc. See `project_make_mpv_beat_ffmpeg_arc.md`.
- `video_decoder.{h,cpp}` — ffmpeg-based video decode. Feeds gpu_renderer.
- `audio_decoder.{h,cpp}` — ffmpeg-based audio decode + device routing.
- `subtitle_renderer.{h,cpp}` — ASS/SRT subtitle rendering. Sub-position slider = Y-offset on ASS_Image, NOT ass_set_line_position. See `feedback_subtitle_position_yoffset_not_libass.md`.

## IPC protocol (JSON-over-stdio)

The sidecar speaks JSON. Commands arrive on stdin; events emit on stdout. Every event has a `sessionId` field. After the `version` event fires (session_strict=true), any session-scoped event with an empty sessionId is **silently dropped** — this is intentional (P4.6 lifecycle fix from REPO_HYGIENE Phase 4).

Key protocol rules:
- Probe data (metadata, track list, duration) must be emitted **post-probe in open_worker**, NOT at first_frame. Violating this makes `firstFrameSeen` unreliable in the Qt-side bridge. See `feedback_sidecar_metadata_decoupling.md`.
- `safe_stoi` helper (P4.7) wraps all integer parsing — returns `std::optional<int>` + emits PARSE_FAILED error event. The prior pattern of bare `std::stoi` was process-fatal on malformed input. Do not reintroduce unguarded stoi.
- `handle_set_tracks` dispatcher blocks on HTTP subtitle preload (`preload_subtitle_packets`). This is the root cause of the stream pause/close regression. Agent 4's SIDECAR_DISPATCHER_NON_BLOCKING arc is the pending fix — see `project_sidecar_dispatcher_non_blocking_decision.md`. Do not add more synchronous HTTP I/O in dispatcher handlers.

## GPU path — libplacebo + Vulkan

The GPU renderer (`gpu_renderer.cpp`) uses libplacebo with the Vulkan backend and ewa_lanczossharp as the default scaler. This is a high-quality path — perceptibly better than the plain ffmpeg software path, and the reason Hemanth approved the MAKE_MPV_BEAT_FFMPEG arc before the cutover. The stack survives post-cutover as the sidecar's sole render path.

Hardware note: Hemanth's machine is Intel UHD 620. WGL_NV_DX_interop + D3D11 NTHANDLE is broken on this GPU — native GL works. The sidecar's GPU path is designed around this constraint. See `user_hardware_intel_uhd_620.md`. Any new GPU-path work must validate on UHD 620 first.

Unification TODO: `project_libplacebo_unification_todo.md` — LIBPLACEBO_SINGLE_RENDERER_FIX_TODO at repo root. 4 phases env-gated. P1 ready to execute on next Agent 3 wake that has sidecar bandwidth.

## Reference apps

- **mpv** at `C:\tools\mpv\` per `reference_sidecar_premigration.md` and `reference_mpv_install.md`. Mine it for: display-resample scheduling, libass internal sub render pipeline, audio-clock drift compensation. Source is on disk — read it directly.
- **TankobanQTGroundWork/native_sidecar/src/** via `reference_sidecar_premigration.md` — the pre-migration sidecar state. Regression baseline only. Do NOT pull code from it without checking that MPV_CUTOVER + REPO_HYGIENE Phase 4 changes haven't superseded it.

## Load-bearing memories (sidecar-specific)

- `project_mpv_cutover_2026-05-05.md` — MPV_CUTOVER arc closed. Single-backend architecture. Recovery path for mpv reversal. Read first on any Agent 3 wake.
- `project_make_mpv_beat_ffmpeg_arc.md` — libplacebo + Vulkan + ewa_lanczossharp arc. GPU renderer is this arc's primary artifact. Read before touching gpu_renderer.
- `project_make_mpv_solo_arc.md` — MAKE_MPV_SOLO 15-task arc. Tasks 1-10 (sidecar-side: subtitle residuals, session-id, std::stoi hardening, async sidecar start) all closed.
- `project_sidecar_migration.md` — Option 1 ratified 2026-04-15: vendor native_sidecar. Agent 3 owns.
- `project_sidecar_dispatcher_non_blocking_decision.md` — dispatcher-blocks-on-HTTP root cause. Agent 4's pending fix arc. Read before adding any synchronous I/O in dispatcher handlers.
- `feedback_sidecar_metadata_decoupling.md` — probe data must emit in open_worker, not first_frame.
- `feedback_subtitle_position_yoffset_not_libass.md` — Y-offset on ASS_Image is the right lever. Not ass_set_line_position.
- `reference_sidecar_premigration.md` — pre-migration regression baseline.
- `reference_mpv_install.md` — mpv at C:\tools\mpv\ for pattern reference.
- `user_hardware_intel_uhd_620.md` — WGL_NV_DX_interop broken on UHD 620. Native GL only.
- `feedback_subjective_over_trace.md` — subjective smoothness is a valid reason to keep "dead" code. Player feel matters.
- `feedback_tankoban_build_link_dominates.md` — main app build ~915s link-dominated. Sidecar builds separately via `native_sidecar/build.ps1` (MinGW) — faster, independent of main app link. Prefer sidecar-only build cycles when only sidecar code changed.
- `feedback_one_fix_per_rebuild.md` — one change, one rebuild, one visual check. No batching.

## Build / MCP lane discipline (gov-v7)

**Sidecar build command:** `powershell -File native_sidecar/build.ps1` (MinGW toolchain; installs built binary to `resources/ffmpeg_sidecar/`). This is agent-runnable per contracts-v2 (sidecar build unlocked for agents 2026-04-16). No Hemanth involvement needed for build.

Sidecar smokes typically claim **both lanes**: `mcp` for visual UI smoke of the playing video (canvas paint, subtitle render, HUD state via tankoctl + pywinauto screenshot) and `build` for the sidecar rebuild cycle. Claim both in chat.md before starting; release both when done. See gov-v7 lease registry (Rules 19 + 22).

The v1.7 dev-bridge commands (`sidecar-*`) talk to the Qt-side SidecarProcess, not the binary directly. `sidecar-get-process-state` / `sidecar-get-stream-info` / `sidecar-restart` / `sidecar-get-ipc-latency` cover the sidecar's observable state from the Qt side. `sidecar-get-decoder-queue` and `sidecar-get-render-queue` return `NOT_YET_IMPLEMENTED` pending a native_sidecar IPC extension — that's the v1.8 commission.

`log-mark <label>` (v1.9 tankoctl) writes a correlation marker across all 4 log streams including `sidecar_debug_live.log` (env-gated via `TANKOBAN_SIDECAR_DEBUG=1`). Use it: `log-mark before-test → do thing → log-mark after-test` then grep `sidecar_debug_live.log` between the two markers instead of guessing timestamps.

## When this file activates

Auto-loads when any agent reads a file under `native_sidecar/`. Treat the content as ambient context: who owns this binary, what the IPC protocol constraints are, which memories are load-bearing, and what the build toolchain is.

If you're **Agent 3**: home turf.

If you're **Agent 4** (Stream): the sidecar's HTTP subtitle preload blocking is directly in your SIDECAR_DISPATCHER_NON_BLOCKING arc's scope. IPC protocol changes that affect the Qt-side SidecarProcess need Agent 3 co-sign (Rule 14). The session-id strict mode is load-bearing for stream-mode session hygiene — don't weaken it.

If you're **Agent 0**: sidecar build verification cross-domain is fine per contracts-v2. Architectural changes need Agent 3.

If you're **any other brother**: read and ask Agent 3 before touching. The GPU renderer in particular (`gpu_renderer.cpp`) has hardware-specific constraints, a non-trivial libplacebo + Vulkan setup, and picture-quality lift that Hemanth has approved — it's not a candidate for simplification without a new ratified arc.
