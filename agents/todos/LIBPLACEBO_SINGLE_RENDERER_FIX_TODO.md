# Libplacebo Single-Renderer TODO — Tankoban 2

**Authored 2026-04-25 by Agent 3 (Hemanth-driven mode, per direct request).**
**Owner: Agent 3.** Sole consumer of `native_sidecar/src/{gpu_renderer,video_decoder,demuxer,main}.cpp` per CLAUDE.md sidecar-vendor ownership.

Source audit: `agents/audits/player_gaps_vs_mpv_vlc_2026-04-25_v2.md` §V2 §1 + §V3 §1, RESOLVED via Hemanth's Phase 0 Q1 answer 2026-04-25 ("YES, one renderer for everything"). Full Phase 0 decision context in memory `project_player_phase0_audit_decisions.md`.

---

## Context

Tankoban's `gpu_renderer.cpp` runs a serious libplacebo path with `ewa_lanczossharp` upscaler, hermite downscaler, peak-detect, full tone-mapping function selector (reinhard/bt2390/clip/mobius/linear/hable), HDR metadata input, and Windows ICC auto-load. **All of it is gated on `probe->hdr` being true** at `main.cpp:794`. SDR content — the ~95% of files Hemanth plays — never instantiates GpuRenderer and falls through to `sws_scale(SWS_FAST_BILINEAR)` for resize + `BGRA8` conversion. That's a quality cliff: HDR files get serious upscaling, SDR files get whatever libswscale's fast bilinear produces.

Closing this gate makes the same proven render path run for both. Closes audit V2 §1 (color/HDR pipeline gap), V3 §1 (scaling default gap), and part of V4 (any frame-timing observability that depends on the renderer being instrumented uniformly).

Cost: 4 phases, ~3-5 summons total, ~150-300 LOC of actual surgery. No new dependencies. No new modules.

---

## Objective

A user playing any video file — SDR `.mp4` from a torrent, HDR Blu-ray rip, anime karaoke with styled subtitles, anything — gets the SAME render pipeline:
- `pl_renderer_create` Vulkan-backed
- `ewa_lanczossharp` upscaler
- Hermite downscaler
- Peak-detect on
- Tone-mapping (neutral identity for SDR sources, full curve for HDR)
- ICC profile applied if user has a calibrated display
- Output `BGRA8` sRGB (or scRGB16f on HDR-capable displays) into the existing DXGI waitable swap chain

No user-visible setting, no UI surface change, no per-file picker. Just better SDR upscaling on every file.

---

## Non-Goals (explicit)

- **NOT re-exposing tone-mapping / scaler picker UI.** Phase 0 Q2 = NO (keep all hidden). Engine controls stay engine-only.
- **NOT touching the DXGI waitable swap chain.** Already correct (`FrameCanvas.cpp:150-280`). 3.A consumes it; doesn't redesign it.
- **NOT touching subtitle rendering.** Sub blend pipeline (libass + PGS Y-offset) lands BEFORE final present, unchanged by render-path swap.
- **NOT adding NVDEC / QSV / AMF / VAAPI fallback.** Phase 4.A in player audit; out of scope.
- **NOT user-shader hook (mpv `--glsl-shader=`).** Audit V3 §3, scope-heavy strategic call separate from this TODO.
- **NOT removing the `sws_scale` fallback path.** Stays as the silent fallback when libplacebo init fails. Exact same pattern HDR already uses.
- **NOT changing the audio pipeline.** 3.B / 3.C are separate TODOs.

---

## Reference slate

- **Source audit:** `agents/audits/player_gaps_vs_mpv_vlc_2026-04-25_v2.md` §V2 §1, §V3 §1, §V4, §3 don't-break list, §4 Q1 answered, §6 checklist V2/V3 entries.
- **Phase 0 decision memory:** `project_player_phase0_audit_decisions.md` (Hemanth ratified 2026-04-25, Q1 = YES).
- **Renderer reference (mpv):** `C:\Users\Suprabha\Downloads\Video player reference\mpv-master\video\out\gpu_next\` — mpv's libplacebo-for-everything integration. Read for pattern, NOT copied verbatim.
- **libplacebo source:** `C:\tools\libplacebo-source\` (per audit reference, may need shallow clone if absent).
- **Memory baselines:**
  - `project_native_d3d11.md` — D3D11Widget→FrameCanvas migration complete 2026-04-14; 3.A consumes this surface, doesn't change it.
  - `project_player_perf.md` — PLAYER_PERF_FIX closed 2026-04-16; the DXGI cadence + HiDPI sizing + HDR detection in FrameCanvas are load-bearing.
  - `feedback_dxgi_resizebuffers_flags_must_match.md` — relevant because libplacebo init may interact with swap-chain creation flags.
  - `feedback_sidecar_metadata_decoupling.md` — colorspace data must emit post-probe, not inside first_frame handler. Applies to P1.
  - `feedback_subtitle_position_yoffset_not_libass.md` — sub blend stays as-is; render-path swap doesn't affect it.
- **Existing Tankoban surfaces (cite into):**
  - `native_sidecar/src/gpu_renderer.{h,cpp}` (init + render + tone-map + ICC + HDR metadata)
  - `native_sidecar/src/video_decoder.cpp:1006-1007, 1078-1109` (10-bit gate + sws_scale fallback chokepoint)
  - `native_sidecar/src/demuxer.cpp:426-453` (probe colorspace extraction — currently HDR-only)
  - `native_sidecar/src/main.cpp:792-807` (the HDR gate; the env flag will live nearby)
- **TODO authoring template:** `feedback_fix_todo_authoring_shape.md` (14-section ratified shape, mirrored here).

---

## Phases

### P1 — SDR colorspace extraction in probe (1 summon)

**Scope:**
- `demuxer.cpp:426-453` currently captures `color_primaries`, `color_trc`, `colorspace` only when the HDR detection branch fires. Extend to capture them for SDR sources too. Default values for missing fields: `BT.709` primaries, `sRGB` transfer (or BT.709 transfer for HD), `BT.709` matrix.
- `ProbeResult` struct gains the colorspace fields cleanly (or surfaces existing ones) — Rule 14 at impl time; pick the smallest diff.
- `main.cpp:792-807`: when constructing GpuRenderer (still HDR-gated this phase), pass the extracted colorspace fields explicitly even for HDR. P2 will use them for SDR.

**Files:** `native_sidecar/src/demuxer.cpp`, `native_sidecar/src/main.cpp`, possibly `demuxer.h` if struct extends.

**Acceptance:**
- Probe a known SDR file (any anime episode, e.g. Saiki Ep 12); sidecar log confirms `color_primaries`/`color_trc` populated with non-zero values reflecting source (typically BT.709 for anime).
- Probe a known HDR file (any HDR Blu-ray rip available); fields still populated correctly (regression check).
- `build_check.bat` BUILD OK; `native_sidecar/build.ps1` GREEN.
- No behavior change visible to user — pure data plumbing.

---

### P2 — Env-gated unconditional libplacebo (1-2 summons)

**Scope:**
- `main.cpp:792-807` — wrap the GpuRenderer instantiation. New shape:
  - If `probe->hdr` → instantiate as today (no behavior change for HDR).
  - Else if `getenv("TANKOBAN_LIBPLACEBO_SDR")` is "1" → instantiate GpuRenderer with SDR neutral metadata (BT.709 / sRGB / standard luminance ~80-100 nits).
  - Else (default) → leave `gpu_ren = nullptr`, sws_scale path as today.
- `gpu_renderer.{h,cpp}` — extend `set_hdr_metadata()` to gracefully accept SDR neutral values, OR add a sister `set_sdr_metadata()` that sets the same internal fields with SDR defaults (Rule 14 — diff size at impl time decides).
- `video_decoder.cpp:1006-1007` — adjust the 10-bit intermediate gate. Current: strips 10-bit→YUV420P when GPU renderer absent. New: if GpuRenderer present (HDR or env-on SDR), let it handle 10-bit natively (preserves quality on 10-bit SDR sources like newer anime BDMVs).
- `video_decoder.cpp:1078-1109` — the `gpu_renderer_->active()` branch is the silent-fallback chokepoint. Stays unchanged structurally; just exercised by SDR files now too when env is on.
- ICC auto-load (`gpu_renderer.cpp:272-321`) runs for both — no change. Identity-maps for sRGB-default displays.

**Files:** `native_sidecar/src/main.cpp`, `native_sidecar/src/gpu_renderer.{h,cpp}`, `native_sidecar/src/video_decoder.cpp`.

**Acceptance:**
- `TANKOBAN_LIBPLACEBO_SDR=1 build_and_run.bat` launches; sidecar log confirms `GpuRenderer instantiated for SDR file: <path>` on first SDR play.
- Without the env flag: SDR files behave EXACTLY as before (no regression on baseline). HDR files unchanged regardless.
- A/B smoke matrix: same SDR file (e.g. Saiki Ep 12), env off → screenshot, env on → screenshot. Visual diff: subjectively sharper edges, cleaner text overlay regions, no color shift.
- Init cost measured via existing `out/ipc_latency.log` IPC tracker + sidecar PERF logs — Vulkan init typically 100-300ms one-shot per file. Acceptable if first-frame doesn't visibly delay vs baseline.
- Init failure path: deliberately corrupt env or trigger libplacebo init failure → falls back silently to sws_scale, file still plays. Exact same fail-fallback pattern HDR has today.
- `build_check.bat` BUILD OK; `native_sidecar/build.ps1` GREEN.

**Note:** P2 may take 2 summons if the 10-bit SDR edge case (4:2:0 10-bit BT.709 sources) needs its own colorspace probe code path.

---

### P3 — Ungate, ship as default (1 summon)

**Scope:**
- Drop `getenv("TANKOBAN_LIBPLACEBO_SDR")` check in `main.cpp:792-807`. GpuRenderer instantiated unconditionally (HDR or SDR doesn't matter).
- Remove env-gate doc references in CLAUDE.md "Build Quick Reference" if any were added in P2.
- Confirm Hemanth's smoke session(s) post-P2 showed no regression over a multi-file playback range.

**Files:** `native_sidecar/src/main.cpp` (gate drop, ~5 LOC).

**Acceptance:**
- Default `build_and_run.bat` (no env vars) launches SDR file → libplacebo path active immediately. Verified via sidecar log.
- A regression-check pass: at least 3 different SDR sources (anime, live-action, screen-recording), all play without visual or perf issue.
- `build_check.bat` BUILD OK.

---

### P4 — Cleanup, memory, audit close (1 summon)

**Scope:**
- Memory writes:
  - NEW `feedback_libplacebo_unified_renderer.md` — captures the why-this-was-fine-actually learning + Vulkan init cost real-world numbers + any edge cases hit.
  - Possible update to `project_player_perf.md` if the closure changes the perf-fix landscape (likely just a one-line addendum).
- Audit checklist update: edit `agents/audits/player_gaps_vs_mpv_vlc_2026-04-25_v2.md` §6 to mark V2 §1 and V3 §1 as `[x] closed by 3.A` with commit cite.
- CLAUDE.md "Active Fix TODOs" table row update; STATUS.md Agent 3 section refresh.
- Decide (Rule 14): does the now-rarely-used `sws_scale` fallback path warrant deletion, or keep as defensive net? Recommend KEEP — exotic init-failure scenarios still benefit from the fallback. ~10 LOC of dead-looking code is fine.

**Files:** memory files at `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\`, `agents/audits/player_gaps_vs_mpv_vlc_2026-04-25_v2.md`, `CLAUDE.md`, `agents/STATUS.md`.

**Acceptance:**
- All memory files indexed in MEMORY.md.
- Audit checklist V2 §1 + V3 §1 marked `[x]`.
- CLAUDE.md table reflects 3.A done; STATUS.md Agent 3 section updated.
- `feedback_libplacebo_unified_renderer.md` exists and is reachable.

---

## Decisions

1. **Rollout shape:** env-gated via `TANKOBAN_LIBPLACEBO_SDR=1`, default off (Hemanth pick 2026-04-25). Mirrors STREAM_SERVER_PIVOT pattern.
2. **ICC for SDR:** keep auto-load on (Rule 14, mirrors HDR — identity for sRGB displays, real benefit on calibrated displays).
3. **Init failure fallback:** keep the existing `sws_scale(SWS_FAST_BILINEAR)` chokepoint at `video_decoder.cpp:1090-1093` as the silent fallback (Rule 14, mirrors today's HDR-init-failure behavior).
4. **10-bit SDR handling:** let libplacebo handle natively where possible; preserves quality on 10-bit SDR sources (Rule 14).
5. **`set_hdr_metadata` extension vs sister `set_sdr_metadata`:** decide at P2 implementation time based on diff size. If the SDR-neutral case is just default arg values, extend; if branches diverge meaningfully, sister method (Rule 14, deferred).
6. **Tone-mapping for SDR:** neutral / identity / pass-through (no curve). Source already in sRGB transfer; libplacebo passes through cleanly. The same `set_tone_mapping` setter accepts a "linear" / "clip" / null choice — pick at impl time.
7. **Per-file Vulkan init cost target:** "no visibly worse first-frame latency vs baseline" (qualitative). If P2 measures show >500ms median init, regroup with Hemanth before P3.
8. **`sws_scale` fallback retention:** KEEP through P3 + P4. Defensive net for libplacebo init failures (driver issues, GPU context loss). Tiny code surface; high failure-mode value.

---

## Risk surface

1. **Vulkan init cost on every SDR file open.** P2's smoke + IPC latency tracker measures real numbers. If pessimistic case >500ms first-frame delay, defer and revisit (could ship as opt-in setting instead of default).
2. **10-bit SDR handling regression.** Current path strips 10-bit to YUV420P. Letting libplacebo handle natively SHOULD be cleaner, but untested. P2 acceptance includes 10-bit SDR smoke.
3. **Color-space extraction edge cases.** Files with missing/malformed `color_primaries` (raw camera output, screen recordings, some old encodings) need sane defaults. P1 sets BT.709/sRGB defaults; if a file hits a worse-default failure mode, surface in smoke.
4. **ICC profile interaction on SDR.** Display profiles tuned for HDR or wide-gamut workflows could subtly tint SDR content differently than the today's untouched-by-ICC sws_scale path. Smoke on Hemanth's actual setup is the only real check.
5. **Init-failure fallback path stays exercised.** sws_scale is now the failure fallback for SDR too. If we drop it accidentally in a future cleanup, init failures would crash the playback. Keep documented (P4 decision).
6. **DXGI swap-chain interaction.** Per `feedback_dxgi_resizebuffers_flags_must_match.md`, ResizeBuffers flags must equal creation flags. libplacebo's Vulkan-D3D interop on the SDR path should not change swap-chain flags vs today, but verify in P2 smoke.
7. **Subtitle blend ordering preserved.** Sub Y-offset machinery (`feedback_subtitle_position_yoffset_not_libass.md`) blends ASS_Image into the BGRA frame BEFORE final present. Libplacebo render lands the BGRA frame; sub blend overlays. Should be unchanged. Verify in P2 smoke.

---

## Smoke criteria per phase

- **P1 self-smoke:** sidecar log shows colorspace fields populated for both SDR and HDR probes. No behavior change.
- **P2 smoke (Hemanth-driven A/B):** same SDR file env-off vs env-on → 2 screenshots. Subjective: sharper, cleaner. Empirical: no >500ms first-frame regression. 10-bit SDR file (if available) plays cleanly with env on.
- **P3 self-smoke:** default `build_and_run.bat` launches; SDR + HDR files both use libplacebo path; no regressions over 3+ source variety pass.
- **P4 self-smoke:** memory files written + indexed; audit checklist + CLAUDE.md + STATUS.md reflect closure.

---

## Exit criteria

- All 4 phases shipped + smoked GREEN.
- `TANKOBAN_LIBPLACEBO_SDR` env var fully removed from source post-P3.
- v2 audit §6 V2 §1 + V3 §1 marked `[x]` with commit cite.
- New memory `feedback_libplacebo_unified_renderer.md` captures real-world Vulkan init cost numbers + any P2 edge-case learnings.
- CLAUDE.md "Active Fix TODOs" row updated; Agent 3 STATUS.md section reflects 3.A done; dashboard refresh.
- `build_check.bat` + `native_sidecar/build.ps1` + main app launch + SDR + HDR + 10-bit SDR all green.
- Hemanth's smoke session post-P3 ratifies no perceptible regression over a multi-file playback range.

---

## Sign-off

Authored 2026-04-25 by Agent 3 (Hemanth-driven mode, per direct request). Rollout shape locked by Hemanth same-day (env-gated). P1 ready on first summon.
