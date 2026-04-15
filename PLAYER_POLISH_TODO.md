# Video Player Engine Polish — Kodi + OBS Reference

**Owner:** Agent 3
**Reviewer:** Agent 6 (per-phase cadence)
**Started:** 2026-04-14
**Plan:** `C:/Users/Suprabha/.claude/plans/mossy-sauteeing-crab.md`

---

## Context

Path B native D3D11 refactor (Phases 1–7 in `NATIVE_D3D11_TODO.md`) landed mpv-level render-layer smoothness. The next tier of polish is engine-level — clock, queue, scheduling, color, audio, subtitles, error recovery — and it's what separates our player from Kodi/VLC/mpv.

Two research passes synthesized 2026-04-14:
- **Kodi** (`C:/Users/Suprabha/Downloads/xbmc-master/xbmc-master`) — best reference for master-clock feedback, A/V sync, libass subtitles
- **OBS Studio** (`C:/Users/Suprabha/Downloads/obs-studio-master`) — best reference for frame queue pattern, HDR shader math (already in HLSL), lag-aware pacing, monitor HDR detection, HW decoder fallback
- **MPC-HC / LAV Filters** — evaluated and deferred (DirectShow scaffolding buries algorithms; Kodi+OBS cover the same ground more cleanly)

## Six engine gaps being addressed

1. No master-clock feedback → cascading stutter
2. No lag-aware render pacing → timer backlog on overrun
3. No frame-queue telemetry → can't measure drops
4. No audio resampling with drift correction → creeping lip-sync
5. No HDR pipeline in shader → HDR content crushed/washed
6. Text-only subtitles → ASS/SSA/PGS unsupported

## Locked design decisions (per Hemanth 2026-04-14)

- **Tonemap location:** shader-only. Sidecar emits raw frame + color metadata; shader does all color-space and tonemap math.
- **Review cadence:** per-phase (matches Phase 1–7 pattern).
- **Phase order:** 1 Clock → 2 Queue → 3 HDR → 4 Audio → 5 Subtitles → 6 Error recovery → 7 Cleanup.

## Governance

- One batch, one rebuild, one visual check.
- Each batch ends with Rule 11 `READY TO COMMIT` line; Agent 0 batches commits.
- Each phase ends with `READY FOR REVIEW — [Agent 3, Phase N]` per 2026-04-14 REVIEW protocol.
- No sidecar protocol changes before Phase 4.
- Phase N+1 starts only after REVIEW PASSED for Phase N.

---

## Batch 0 — Project tracking doc [SHIPPED 2026-04-14]

- [x] Create `PLAYER_POLISH_TODO.md` (this file)
- [x] Add supersession note to `NATIVE_D3D11_TODO.md` Phase 8
- [x] Post kick-off announcement to `agents/chat.md`

---

## Phase 1 — Master Clock Feedback & Lag-Aware Pacing

**Objective:** make our clock aware of render lateness and auto-compensate. Main-app only, no sidecar/shader/audio.

**Scope fence:** `src/ui/player/SyncClock.h`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/VsyncTimingLogger.cpp/.h`.

- [x] **Batch 1.1** — Late-frame signal plumbing *(shipped 2026-04-14, awaiting multi-verify)*
  - `SyncClock::reportFrameLatency(double)` + `lastFrameLatencyMs()` + atomic storage
  - `FrameCanvas` measures present-to-present interval via `QElapsedTimer`; overage vs `m_expectedFrameMs` → `frameLatencyMs`
  - `_vsync_timing.csv` now has 8th column `frame_latency_ms`
  - Files: `SyncClock.h`, `FrameCanvas.h/.cpp`, `VsyncTimingLogger.h/.cpp`

- [x] **Batch 1.2** — Lag-aware skip-ahead + FrameCanvas↔SyncClock wire + monitor refresh query *(shipped 2026-04-14, awaiting multi-verify)*
  - Architectural pivot discovered mid-batch: `SyncClock` + `AudioDecoder` + `readBestForClock` are all dead code; live A/V sync is sidecar-owned. Hemanth ratified Option 2 — keep `SyncClock` as *shadow clock* that Phase 4 will consume via a new sidecar audio-speed command. `SyncClock` now lives on `VideoPlayer` (not `AudioDecoder`) and `FrameCanvas` reports latency to it after every Present.
  - Lag-aware skip: when `frameLatencyMs > 25ms` sustained across 3 ticks, skip next `Present()` (OBS `obs-video.c:814-827` semantic). Old frame stays on screen for one vsync; render pipeline catches up instead of piling up Present calls.
  - Monitor refresh query via `GetDeviceCaps(VREFRESH)` replaces the 16.6667ms hardcode; 60Hz is now fallback only.
  - CSV gained 9th column `frame_skipped` (0/1).
  - Files: `FrameCanvas.h/.cpp`, `VsyncTimingLogger.h/.cpp`, `VideoPlayer.h/.cpp` (one-line ctor wire).

- [x] **Batch 1.3** — Clock velocity feedback *(shipped 2026-04-14, awaiting multi-verify)*
  - `SyncClock` gained an exponential moving average of `frameLatencyMs` (α = 0.05 → ~20-sample timescale, ~0.3s at 60Hz) with a >500ms discontinuity guard (pause/minimize/sleep can't poison the EMA).
  - `getClockVelocity()` derives a one-sided clamp `[0.995, 1.000]` from the EMA (5ms sustained latency → 0.5% floor, matching Kodi's ±% pattern scaled down for our Phase 1 scaffolding). Only pulls velocity DOWN because our latency signal is one-sided (`max(0, overage)` in FrameCanvas).
  - `positionUs()` applies velocity to elapsed-time interpolation — clock is now internally consistent even though no live consumer reads it yet.
  - `seekAnchor` resets EMA + velocity (drift history is meaningless across a seek).
  - CSV gained 10th + 11th columns `latency_ema_ms, clock_velocity`. Agent 6 verification: spin a lag window, CSV should show EMA climbing, velocity dropping from 1.0 toward 0.995, then both returning to nominal when lag subsides.
  - `AudioDecoder` hook explicitly skipped (the plan's original Batch 1.3 fourth bullet) — AudioDecoder is dead code per Batch 1.2 finding. Velocity becomes a live control signal when Phase 4 adds `sendSetAudioSpeed` to the sidecar protocol.
  - Files: `SyncClock.h`, `VsyncTimingLogger.h/.cpp`, `FrameCanvas.cpp`.

**Phase 1 exit:** `READY FOR REVIEW — [Agent 3, Phase 1]: Clock feedback + lag-aware pacing | Objective: Kodi DVDClock + OBS video_sleep pacing` — **posted 2026-04-14, pending Agent 6 review and Hemanth build verification.**

**Batch 1.3 hotfix 2026-04-14** — first CSV pass revealed `clock_velocity` floored at 0.995 during normal playback (5ms noise EMA already saturated the scaling). Tuned to `adj = (ema - 5) / 3000` with `kNoiseFloorMs = 5.0` → velocity now 1.000 during normal playback, gradient through mild/moderate stress, floor only at EMA ≥ 20ms (genuine sustained drift). Single-file edit in `SyncClock.h`, folds into Batch 1.3's existing commit.

---

## Phase 2 — Frame Queue Audit & Telemetry

**Objective:** confirm OBS-style closest-PTS pick is correct; add overflow-drop telemetry.

**Scope fence:** `src/ui/player/ShmFrameReader.cpp`, `src/ui/player/FrameCanvas.cpp`, `VsyncTimingLogger.cpp`.

> **RESUMED 2026-04-14** — Tankostream Batch 5.2 shipped; Agent 4 unblocked for 5.3. Back on Player Polish, closed Phase 2 with Batch 2.2 same session.

- [x] **Batch 2.1** — Audit + switch to `readBestForClock` + per-tick telemetry *(shipped 2026-04-14, awaiting multi-verify)*
  - **Audit verdict:** `readBestForClock` already matches OBS `get_closest_frame` + `ready_async_frame` semantic — walks all SHM slots, filters to `fid > watermark AND pts ≤ clock + 8ms tolerance`, picks newest eligible, includes torn-write re-check on the valid flag. No fix needed. One deliberate deviation: we have an explicit 8ms drift tolerance that OBS lacks (they don't need it because `CFrameTime` absorbs sub-frame precision differently).
  - **Switch:** `FrameCanvas::consumeShmFrame` now calls `readBestForClock(readClockUs(), 8000)` first; falls back to `readLatest()` only when the clock-aware read returns invalid (startup + post-seek race windows where audio clock hasn't advanced past any frame's PTS yet).
  - **Telemetry:** added `chosen_frame_id` (uint64) + `fallback_used` (0/1) as CSV columns 12 + 13. Agent 6 verifies "no dupes/skips during steady-state" by inspecting `chosen_frame_id` monotonicity; fallback column lights up only during startup or after a seek.
  - Files: `FrameCanvas.h/.cpp`, `VsyncTimingLogger.h/.cpp`. **No ShmFrameReader changes** — audit confirmed correctness.

- [x] **Batch 2.2** — Overflow-drop telemetry *(shipped 2026-04-14, awaiting multi-verify)*
  - **Producer drops** (`producer_drops_since_last`): count of frameIds the sidecar wrote to the SHM ring that we never consumed. Inferred from the gap between this tick's consumed id and the previous — `max(0, currentFid - previousFid - 1)`. Reset per-tick; 0 if no consume this tick or if this is the first consume (no baseline yet).
  - **Consumer late** (`consumer_late_ms`, double — name tweaked from plan's `consumer_late_since_last` to reflect that it carries the actual lag value in ms rather than a count): `(sidecarClockUs - framePtsUs) / 1000.0`. Positive = displayed frame's PTS is behind sidecar's audio clock (stale); negative = ahead of clock within `readBestForClock`'s 8ms tolerance; 0 if no consume this tick.
  - Two new CSV columns → **total now 15**.
  - Files: `FrameCanvas.h/.cpp`, `VsyncTimingLogger.h/.cpp`. No `ShmFrameReader` changes (inference is at the consumer using fields already in the Frame struct).

**Phase 2 exit:** `READY FOR REVIEW — [Agent 3, Phase 2]: Frame queue audit + telemetry | Objective: OBS async_frames (obs-source.c:3503-4200)` — **posted 2026-04-14, pending Agent 6 review and Hemanth build verification.**

---

## Phase 3 — HDR Color Pipeline (shader-only)

**Objective:** fill reserved `colorSpace` + `transferFunc` cbuffer fields with HDR math ported from OBS's color.effect. All color math in HLSL.

**Scope fence:** `resources/shaders/video_d3d11.hlsl`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/SidecarProcess.h` (mediaInfo color metadata).

- [x] **Batch 3.1** — Color-space matrices *(shipped 2026-04-14, awaiting multi-verify)*
  - Added `kBT2020toBT709` 3×3 HLSL constant (ported from OBS `format_conversion.effect`, D65 whitepoint both sides).
  - Shader branches on `colorSpace` cbuffer field: `0 = BT.709` (identity), `1 = BT.2020` (apply matrix). SDR path unchanged.
  - `FrameCanvas::setHdrColorInfo(int colorPrimaries, int colorTrc)` translates raw FFmpeg `AVCOL_PRI_*` / `AVCOL_TRC_*` (stable ABI ints; no libavutil include needed in main-app) to our shader enums. Plumbs `transferFunc` too so Batches 3.2 / 3.3 light up without another call-site change.
  - `VideoPlayer` wires it in the existing `mediaInfo` handler — reads `color_primaries` + `color_trc` from sidecar's demuxer probe and forwards.
  - ColorParams default flipped `colorSpace = 1 → 0` to match new semantics (0 = no transform).
  - Documented compromise: matrix multiply runs on gamma-encoded RGB because Batch 3.2 hasn't added linearization yet. ~5–10% off from a proper linear-space pipeline on BT.2020 sources; acceptable as Phase 3 scaffolding. Fixed in 3.2.
  - Files: `resources/shaders/video_d3d11.hlsl`, `src/ui/player/FrameCanvas.h`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/VideoPlayer.cpp`.

- [x] **Batch 3.2** — PQ (ST.2084) EOTF *(shipped 2026-04-14, awaiting multi-verify)*
  - Ported `st2084_to_linear_channel` + `st2084_to_linear` (the per-channel + float3 helpers) from OBS `color.effect:65-74`.
  - Added `transferFunc` branch ahead of the gamut matrix so HDR10 sources decode to linear light BEFORE gamut + color-processing math runs — this retroactively makes Batch 3.1's gamut matrix mathematically correct for HDR content.
  - Placeholder `kPqToSdrScale = 100.0f` maps 100-nit SDR reference into display range; highlights above 100 nits clip at final `saturate()`. Temporary scaffolding — Batch 3.4's tonemap (Reinhard / ACES / Hable / EETF) replaces the scale-and-clip with proper highlight compression.
  - SDR path (`transferFunc == 0`) unchanged; passes through untouched. FXC predicates both branches so the SDR runtime cost is nil.
  - **Single-file edit.** `resources/shaders/video_d3d11.hlsl` only. FrameCanvas's `transferFunc` plumbing was pre-landed in Batch 3.1, so no C++ changes needed.

- [x] **Batch 3.3** — HLG EOTF *(shipped 2026-04-14, awaiting multi-verify)*
  - Ported `hlg_to_linear_channel` + `hlg_to_linear` from OBS `color.effect:158-172`. Decodes ARIB STD-B67 HLG code values through inverse OETF + OOTF; BT.2020 luminance weights for the OOTF match HLG's defined color volume.
  - Hardcoded OOTF exponent `kHlgOotfExponent = 0.2f` (system gamma 1.2 at Lw = 1000 nits — BT.2100's default reference). Will become tunable if we ever add display-peak-aware normalization; today 1000-nit reference covers YouTube HDR and all mainstream HLG sources.
  - `transferFunc == 2` branch in `ps_main` runs parallel to Batch 3.2's PQ branch. Unlike PQ, HLG doesn't need a `kPqToSdrScale`-equivalent because its OOTF output is already SDR-mid-tone-sized; highlights clip at the final `saturate()` until Batch 3.4 tonemap lands.
  - **Single-file shader edit.** `resources/shaders/video_d3d11.hlsl` only.

- [x] **Batch 3.4** — Tonemap algorithms *(shipped 2026-04-14, awaiting multi-verify)*
  - Ported **Reinhard** (vanilla `x/(1+x)`), **ACES** (Narkowicz fit, coefficients verified against Kodi `gl_tonemap.glsl:17-25`), **Hable** (Uncharted 2 filmic with white-point normalization, coefficients verified against Kodi `gl_tonemap.glsl:29-38`) to HLSL. Operate on linear-light post-PQ/HLG decode + gamut matrix; produce display-ready `[0, 1]`.
  - New cbuffer field `tonemapMode`: `0 = Off` (saturate / hard clip — pre-3.4 default, correct for SDR), `1 = Reinhard`, `2 = ACES`, `3 = Hable`. Out-of-range values clamp to Off.
  - Pipeline change: tonemap branch REPLACES the plain `saturate(rgb)` before the final gamma encode. Gamma moves outside saturate so it's not double-applied.
  - **Hotkey collision:** plan called for `T` cycle but Agent 4's subtitle menu already binds it (`KeyBindings.cpp:31`). Rewired through the existing `FilterPopover::toneMappingChanged` signal — HDR tone-mapping dropdown UI already in place from Congress 2 Batch F. Saves a new hotkey + feature shows up in existing UI.
  - Popover string → mode map: `reinhard → 1`, `aces → 2` (future-proofed for popover), `hable → 3`, else Off. Clean degrade for unmapped ffmpeg names (`bt2390`, `mobius`, `clip`, `linear`).
  - `SidecarProcess::sendSetToneMapping` call dropped from handler; method itself kept as no-op stub for protocol stability through Phase 3 exit. `peakDetect` arg ignored shader-side.
  - **EETF deferred** — Kodi/OBS's EETF needs Lw/Lmax params + operates in PQ domain (not linear), which restructures the pipeline. Three shipped operators cover ~95% of the HDR-on-SDR use case; EETF punted to a Phase 3 polish pass or Phase 4.
  - Files: `resources/shaders/video_d3d11.hlsl`, `src/ui/player/FrameCanvas.h`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/VideoPlayer.cpp`.

- [x] **Batch 3.5** — Monitor HDR detection + adaptive swap chain *(shipped 2026-04-14, awaiting multi-verify)*
  - Before swap chain creation: enumerate primary output via `IDXGIAdapter::EnumOutputs(0)` → QI to `IDXGIOutput6` → `GetDesc1`. Inspect `ColorSpace` field; `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` marks the monitor as HDR10-capable.
  - On HDR: swap chain format flips to `DXGI_FORMAT_R16G16B16A16_FLOAT` (16-bit float, scRGB headroom for values > 1.0); after creation, `IDXGISwapChain3::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)` flags the buffer as scRGB. The OS compositor handles final HDR encoding.
  - New cbuffer field `hdrOutput` (replaces the old `pad2` slot — ColorParams stays 32 bytes, existing static_assert passes). Shader branch: when `hdrOutput == 1`, skip the tonemap + gamma chain entirely and output linear light. For SDR source (`transferFunc == 0`) decode sRGB→linear first via a new `srgb_to_linear` piecewise helper; for HDR source we're already linear from the EOTF upstream.
  - **Fallback chain:** any HDR detection failure (EnumOutputs fails, QI to IDXGIOutput6 unavailable, GetDesc1 fails, SetColorSpace1 fails) → silent degrade to SDR swap chain path. Pre-3.5 behavior preserved byte-for-byte on SDR-only systems.
  - **Plan note (files):** plan listed `FrameCanvas.cpp` only but the full delivery required the shader (add `hdrOutput` uniform + output-path branch + `srgb_to_linear`) and the cbuffer layout on `FrameCanvas.h`. Three files total; flagged for transparency.
  - **Known scope limits** (documented in shader comments): 8-bit BGRA input from sidecar limits HDR precision (~8-bit HDR instead of 10+). Sidecar-side upgrade to 10-bit/P010 would raise this ceiling — Phase 4 or later tangent. Brightness/contrast/saturation still apply in HDR mode (unclamped linear space) — may look unusual on extreme settings but gives users their controls. SDR-on-HDR-display path uses approximate 2.4 sRGB curve (piecewise formula is correct at low end, 2.4 approximation on high end — imperceptibly off on mid-tones).
  - Files: `resources/shaders/video_d3d11.hlsl`, `src/ui/player/FrameCanvas.h`, `src/ui/player/FrameCanvas.cpp`.

**Phase 3 exit:** `READY FOR REVIEW — [Agent 3, Phase 3]: HDR color pipeline (shader-only) | Objective: OBS color.effect + format_conversion.effect port, Kodi tonemap algorithm reference.` — **posted 2026-04-14, Agent 6 review 2026-04-15: 0 P0, 3 P1, 8 P2.**

**Phase 3 P1 fix-batch 2026-04-15** — addresses two of the three P1s Agent 6 flagged:
  - **P1 #1 (MonitorFromWindow):** HDR detection switched from `dxgiAdapter->EnumOutputs(0)` to `MonitorFromWindow(hwnd) + GetDesc.Monitor` match iteration. Multi-monitor: picks the output the window is actually on. Fallback to output 0 if no monitor match. Mirrors OBS `d3d11-subsystem.cpp:71-78`.
  - **P1 #2 (kPqToSdrScale in HDR branch):** shader scale branches on `hdrOutput` — `kPqToScRgbScale = 125.0f` for scRGB HDR output (correct per "1.0 = 80 nits, 10000 nits = 125"), `kPqToSdrScale = 100.0f` for SDR output (unchanged, keeps Batch 3.4 tonemap calibration). Was at ~64% of intended HDR brightness pre-fix.
  - Files: `src/ui/player/FrameCanvas.cpp`, `resources/shaders/video_d3d11.hlsl`.

**P1 #3 (3-case `get_next_space` collapsed to 2) — deferred** as open Phase 3 polish debt. Wide-gamut 10-bit SDR case needs deeper DXGI probing; too much for a fix-batch. Bundles with cross-monitor re-detection (P2) and other Phase 3 polish follow-ons.

---

## Phase 4 — Audio Polish

**Objective:** drift correction + volume amp + DRC + passthrough. Consumes Phase 1 velocity signal.

**Scope fence:** `src/ui/player/AudioDecoder.cpp`, `src/ui/player/SidecarProcess.h/.cpp` (new passthrough command).

- [x] **Batch 4.1** — Dynamic SWR ratio (drift correction) *(shipped 2026-04-15, awaiting sidecar rebuild + multi-verify)*
  - **Plan pivot:** plan said "modify AudioDecoder.cpp" but per Batch 1.2 architectural finding, our `AudioDecoder.cpp` is dead code — sidecar owns audio end-to-end. Real fix lives in **sidecar's** `native_sidecar/src/audio_decoder.cpp` at `C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/`.
  - **Main-app side (compiles immediately):** `SidecarProcess::sendSetAudioSpeed(double)` new method with ±5% clamp; new `QTimer m_audioSpeedTicker` in `VideoPlayer` at 500ms intervals reads `m_syncClock.getClockVelocity()` and forwards via `sendSetAudioSpeed` on change beyond a 0.0005 deadband. Includes `<cmath>` for `std::abs`. Ticker starts alongside the existing UI timers in `buildUI`.
  - **Sidecar side (requires rebuild):** new `handle_set_audio_speed` handler in `main.cpp` dispatcher + `AudioDecoder::set_speed(double)` public setter (atomic member) + audio-thread poll reads `speed_` before each `swr_convert`, calls `swr_set_compensation(swr, delta, 1024)` on change with `delta = 1024 * (1/speed - 1)` to pad/drop samples over ~20ms. `last_applied_speed` local latches the last-armed value so we only re-arm on change.
  - Pre-Phase-4 sidecar binaries ignore `set_audio_speed` cleanly (unknown-command warning log + NOT_IMPLEMENTED error, no break); main-app ships safely without coordinated sidecar rebuild.
  - Range `[0.95, 1.05]` clamped on both ends (main-app + sidecar). Matches Kodi ActiveAE `m_maxspeedadjust`.
  - **Files main-app:** `src/ui/player/SidecarProcess.h/.cpp`, `src/ui/player/VideoPlayer.h/.cpp`. **Files sidecar:** `native_sidecar/src/audio_decoder.h/.cpp`, `native_sidecar/src/main.cpp`.

- [x] **Batch 4.2** — Volume amp beyond 100% *(shipped 2026-04-15, awaiting sidecar rebuild + multi-verify)*
  - Main-app volume range extended `[0, 100]` → `[0, 200]`. `adjustVolume` clamp updated, `sendSetVolume(m_volume / 100.0)` unchanged — just sends higher values now (150 → 1.5, etc.). `m_volume` default stays at 100 (unity); amp is opt-in via `+` keyboard/context/wheel.
  - `VolumeHud::showVolume` clamp extended to 200. Fill bar `fillW` visually capped at `barW` when percent > 100 so it doesn't overflow the rectangle. Existing right-side percentage text ("150%" / "200%") already serves as the amp-zone indicator — zero new layout work.
  - Sidecar `VolumeControl::set_volume` upper clamp lifted `1.0 → 2.0` so the command's value actually lands.
  - **Sidecar `audio_decoder.cpp` soft-clip via `std::tanh`** when `vol > 1.0` — amp-zone samples pass through `tanh(sample * vol)` instead of straight multiply. `vol = 1.5` peaks near 0.90, `vol = 2.0` peaks near 0.96 on full-scale input; output stays in `[-1, +1]` so the hardware never sees a clipped waveform. Unity and attenuation paths (`vol <= 1.0 && vol != 1.0`) keep the original multiply — hot path unchanged, no regression.
  - Pre-Batch-4.2 sidecar binaries still clamp at 1.0 in `VolumeControl::set_volume` — main-app can ship 150% safely; old sidecar just plays at 100% until rebuilt. No regression, smooth rollout.
  - **Files main-app:** `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/VolumeHud.cpp`. **Files sidecar:** `native_sidecar/src/volume_control.h`, `native_sidecar/src/audio_decoder.cpp`.

- [⊘] **Batch 4.4** — Passthrough detection + bitstream — **DEFERRED 2026-04-15** (Hemanth ratified)
  - Plan-mode investigation 2026-04-15 confirmed: sidecar uses **PortAudio** for audio output (no IEC 61937 bitstream support); Kodi's reference WASAPI sink is ~2000 LOC across direct WASAPI + IEC encoder + device-loss recovery; realistic scope is 3 sub-batches over 2–3 weeks. Hemanth's framing: *"this feels like an endgame project, we haven't even fixed the subtitles and basic features."*
  - **Reopen criteria:** subtitles + basic features caught up + an AVR/HDMI bitstream verification setup consistently in use + a user with surround content driving the ask. At reopen: scope as 4.4a (detection + media_info + UI toggle), 4.4b (WASAPI device enum + format probe replacing PortAudio for passthrough streams), 4.4c (IEC 61937 encoder + exclusive-mode bitstream output). Reference: Kodi `xbmc/cores/AudioEngine/Sinks/AESinkWASAPI.cpp` + `WAVEFORMATEXTENSIBLE_IEC61937` + `KSDATAFORMAT_SUBTYPE_IEC61937_*` GUIDs.

**Phase 4 exit (post-deferral):** `READY FOR REVIEW — [Agent 3, Phase 4]: Audio polish | Objective: Kodi ActiveAEResampleFFMPEG (drift correction) + AEEncoder soft-clip + DRC compressor patterns.` Three batches shipped — 4.1 (drift correction, closing the Phase 1 SyncClock loop end-to-end), 4.2 (volume amp 0–200% with sidecar tanh soft-clip), 4.3 (sidecar feed-forward DRC compressor). Batch 4.4 deferred per investigation above.

- [x] **Batch 4.3** — Dynamic Range Compression *(shipped 2026-04-15, awaiting sidecar rebuild + multi-verify)*
  - Sidecar-side feed-forward compressor: threshold -12 dB, ratio 3:1, attack 10 ms, release 100 ms. One-pole envelope follower on stereo peak (shared gain across channels so the image doesn't wobble). Post-volume in the audio thread — runs after the Batch 4.2 tanh soft-clip on the amp path, so user's choice of high volume + DRC still stays in `[-1, +1]`.
  - Sidecar attack/release coefficients computed once per file open from output sample rate (`exp(-1 / (sample_rate * time_sec))`). Envelope + coefs live as thread-locals in `audio_thread_func` so no atomics on the hot path — only the enable bool is atomic.
  - New sidecar command `set_drc_enabled` + handler. Emits `drc_enabled_changed` event on acknowledge for UI state sync (unused today; future ACK consumer).
  - Main-app: `SidecarProcess::sendSetDrcEnabled(bool)`. `EqualizerPopover` gained a grayscale-styled `QCheckBox m_drcCheck` below the Reset button labeled "Dynamic Range Compression"; toggle emits `drcToggled(bool)`. VideoPlayer wires `drcToggled` → `m_sidecar->sendSetDrcEnabled`. Default unchecked.
  - Pre-Phase-4 sidecars ignore `set_drc_enabled` cleanly — main-app ships independently; old sidecar just continues rendering flat DR.
  - **Files main-app:** `src/ui/player/SidecarProcess.h/.cpp`, `src/ui/player/EqualizerPopover.h/.cpp`, `src/ui/player/VideoPlayer.cpp`. **Files sidecar:** `native_sidecar/src/audio_decoder.h/.cpp`, `native_sidecar/src/main.cpp`.

- [ ] **Batch 4.4** — Passthrough detection + bitstream
  - Query WASAPI exclusive-mode + AC3/DTS format support
  - New sidecar command `sendSetPassthrough(bool, codecsAllowed)`
  - Sidecar routes bitstream when codec matches; main-app writes raw packets to WASAPI
  - Files: `AudioDecoder.cpp`, `SidecarProcess.h/.cpp`
  - Verify: 5.1 AC3 + AV receiver shows "Dolby Digital" not "PCM"

**Phase 4 exit:** `READY FOR REVIEW — [Agent 3, Phase 4]: Audio polish | Objective: Kodi ActiveAEResampleFFMPEG + AEEncoder`

---

## Phase 5 — Subtitle Rendering (libass)

**Objective:** replace text-only QLabel with libass-rendered ASS/SSA + PGS bitmap passthrough.

**Scope fence:** sidecar (external binary), `src/ui/player/SubtitleOverlay.h/.cpp`, `src/ui/player/SidecarProcess.h`.

> **REVISED PHASE 5 SCOPE 2026-04-15** — sidecar's `subtitle_renderer.cpp` already implements full libass + PGS rendering end-to-end (discovered during Tankostream 5.2 side-quest). Original Batches 5.1–5.5 are mostly no-ops post-discovery. New scope: UX audit driven by Hemanth's specific complaints + targeted bug fixes. Batches below renumbered per the audit.

- [x] **Batch 5.1 (revised)** — Subtitle visibility consistency *(shipped 2026-04-15, awaiting sidecar rebuild + multi-verify)*
  - **Bug:** Hemanth reported "subtitles appear for some videos, not others." Investigation traced three root causes:
    1. Sidecar never told Qt its subtitle-renderer visibility state in `tracks_changed` payload — Qt assumed `m_subsVisible = true`, sidecar's renderer state was undefined → divergent.
    2. `VideoPlayer::restoreTrackPreferences` only sent `sendSetSubVisibility` when a per-file preference existed; fresh files never got the visibility command.
    3. Sidecar didn't preload subtitle packets on first open (libass-rendered ASS/SSA needs full event list pre-built into ASS_Track or render_blend silently emits nothing for the first event window).
  - **Fixes:**
    - Sidecar `main.cpp:394` (tracks_changed emit): added `tracks_payload["sub_visibility"]` field.
    - Sidecar `main.cpp:579-585`: now calls `preload_subtitle_packets(g_sub_renderer, g_current_path, sub_idx)` after `set_active_sub_stream` on first open. Falls back to no-preload path when renderer unavailable (defense-in-depth).
    - Main-app `VideoPlayer.cpp:restoreTrackPreferences` end: unconditionally calls `m_sidecar->sendSetSubVisibility(m_subsVisible)` so fresh files always get the explicit command.
  - Files: main-app `src/ui/player/VideoPlayer.cpp`; sidecar `native_sidecar/src/main.cpp`.

- [x] **Batch 5.2 (revised)** — Context-menu subtitle changes apply visibility *(shipped 2026-04-15, awaiting multi-verify)*
  - **Bug:** Hemanth reported "I can't change subtitles from the context menu." Root cause: `VideoContextMenu::SetSubtitleTrack` handler at `VideoPlayer.cpp:1746-1754` called `sendSetTracks` but never adjusted `m_subsVisible` / `sendSetSubVisibility` — if user had previously toggled subs off (T key, prior context menu, etc.), context-menu track selections landed but stayed hidden.
  - **Fix:** Mirrored `cycleSubtitleTrack`'s visibility-on logic (line 1162-1165). Picking a non-zero track auto-enables visibility; picking "off" auto-disables it. Both paths keep `m_subsVisible` coherent with sidecar state.
  - Files: `src/ui/player/VideoPlayer.cpp` only.

- [ ] **Batch 5.3 (deferred, on-flag)** — SubtitleMenu visibility sync
  - Same class of bug as 5.2 may exist in Agent 4's Tankostream 5.3 SubtitleMenu (T-key popover). `SubtitleMenu::applyChoice` calls `sendSetSubtitleTrack(index)` but doesn't sync visibility. Cleanest fix when needed: put visibility-on logic inside `SidecarProcess::sendSetSubtitleTrack` itself + emit `subVisibilityChanged` for VideoPlayer's `m_subsVisible` to track.
  - **Reopen on Hemanth's confirmation** that the bug persists in the T-menu path post-5.2.

**Original Batch 5.1–5.5 retained below for historical context — replaced by audit-driven fixes above.**

- [⊘] **Batch 5.1 (original)** — libass dep in sidecar — **N/A: already shipped in `subtitle_renderer.cpp`**
  - Add libass; init `ASS_Library` + `ASS_Renderer` per file
  - Sidecar-internal, no protocol change yet
  - Reference: Kodi `DVDSubtitlesLibass.h:20-169`
  - Verify: sidecar log confirms init on ASS-subbed file

- [ ] **Batch 5.2** — `subtitleBitmap` event
  - New signal alongside `subtitleText`: BGRA bitmap + (x, y, w, h) + duration
  - Transport: base64 to start, SHM ring if bandwidth tight
  - Files: `SidecarProcess.h/.cpp`, sidecar side
  - Verify: event fires during ASS playback

- [ ] **Batch 5.3** — SubtitleOverlay bitmap blit
  - `setBitmap(QImage, QRect)` alongside `setText`
  - Custom `paintEvent`; fallback to QLabel for text
  - Files: `SubtitleOverlay.h/.cpp`
  - Verify: anime ASS subs render correctly; SRT still renders

- [ ] **Batch 5.4** — Style override hooks
  - Wire `sendSetSubStyle` to `ass_set_font_scale` / `ass_set_line_position`
  - Files: sidecar side, `SubtitleOverlay.cpp`
  - Verify: font-size changes reflected

- [ ] **Batch 5.5** — PGS bitmap passthrough
  - libavcodec PGS decode → already bitmap → route through `subtitleBitmap` without libass
  - Files: sidecar side only
  - Verify: Blu-ray rip with PGS subs displays

**Phase 5 exit:** `READY FOR REVIEW — [Agent 3, Phase 5]: Libass + PGS subtitles | Objective: Kodi DVDSubtitlesLibass.h`

---

## Phase 6 — Error Recovery

**Objective:** robustness. Crash recovery, device-lost handling, codec-error skip.

**Scope fence:** `src/ui/player/FrameCanvas.cpp`, `src/ui/player/SidecarProcess.cpp`, `src/ui/player/VideoPlayer.cpp`.

- [x] **Batch 6.1** — Sidecar crash auto-restart *(shipped 2026-04-15, awaiting verify)*
  - SidecarProcess distinguishes intentional shutdown from crash via `m_intentionalShutdown` flag (set in `sendShutdown`, cleared in `start`); `onProcessFinished` emits new `processCrashed(exitCode, ExitStatus)` signal only on unintentional termination.
  - VideoPlayer consumes `processCrashed` via `onSidecarCrashed` — detaches canvas/SHM (producer is dead), schedules respawn via single-shot `m_sidecarRestartTimer` with backoff 250/500/1000 ms for retries 0/1/2. After 3 failed retries: "Player stopped — reconnection failed" toast + reset. Uses `m_lastKnownPosSec` (mirrored from each clean `timeUpdate`) as resume point. On successful recovery (`onFirstFrame`): retry counter clears. On user-driven stop/new openFile: timer cancelled, counter reset.
  - Files: `SidecarProcess.h/.cpp`, `VideoPlayer.h/.cpp`.
  - Verify: `taskkill //F //IM Tankoban_sidecar.exe` during playback → "Reconnecting player…" toast → playback resumes near crash point within ~2s.

- [x] **Batch 6.2** — D3D device-lost recovery *(shipped 2026-04-15, awaiting verify)*
  - Extracted device init from `showEvent` into `FrameCanvas::initializeD3D()` (returns bool) + destructor Release logic into `FrameCanvas::tearDownD3D()` (idempotent, reverse creation order). `showEvent` now dispatches to `initializeD3D` on first show.
  - `isDeviceLost(HRESULT)` helper (anon namespace) matches DXGI_ERROR_DEVICE_REMOVED + DXGI_ERROR_DEVICE_RESET per OBS d3d11-subsystem pattern. `renderFrame`'s Present failure path consults it; on match calls `recoverFromDeviceLost()` which: logs `GetDeviceRemovedReason`, emits `deviceReconnecting` signal, stops render timer, signals zero-copy OFF (since import slot dies with old device), clears pending D3D handle, tears down, reinits. Re-entry guarded via `m_recovering` flag. On failed reinit the timer stays stopped — user can close+reopen player to retry (showEvent re-dispatches).
  - VideoPlayer wires `deviceReconnecting` → `ToastHud::showToast("Reconnecting display…")`. SHM video texture recreates lazily via `consumeShmFrame` (videoTexW=0 triggers); zero-copy stays OFF until sidecar re-publishes d3d11_texture.
  - Files: `FrameCanvas.h/.cpp`, `VideoPlayer.cpp`.
  - Verify: enable TDR, GPU stress (e.g., `dxcap -Delay` or `NV_dev_reset`), observe "Reconnecting display…" toast → playback resumes within the TDR timeout window (~2s). Alt: `Win+Ctrl+Shift+B` triggers a soft display driver reset on most systems.

- [x] **Batch 6.3** — Codec error skip signal *(shipped 2026-04-15, awaiting sidecar rebuild + verify)*
  - Sidecar `video_decoder.cpp` decode loop: the existing silent-skip paths on `avcodec_send_packet` and `avcodec_receive_frame` now emit `on_event_("decode_error", "CODE:msg")` in addition to continuing/breaking. Two codes: `DECODE_SKIP_PACKET` (bad packet), `DECODE_SKIP_FRAME` (receive_frame failure). `av_strerror` output carried in the message.
  - Sidecar `main.cpp` on_video_event: new branch serializes to JSON event `decode_error` with payload `{code, message, recoverable: true}`. State is NOT mutated — sidecar stays in PLAYING.
  - Main-app `SidecarProcess.h/.cpp`: new signal `decodeError(code, message, recoverable)`; `processLine` parses `decode_error` event and emits.
  - Main-app `VideoPlayer.cpp`: lambda connect → throttled toast "Skipping corrupt frame…" (3 s minimum interval to avoid spam on sustained corruption).
  - Files: main-app `src/ui/player/SidecarProcess.h/.cpp`, `src/ui/player/VideoPlayer.cpp`; sidecar `native_sidecar/src/video_decoder.cpp`, `native_sidecar/src/main.cpp`.
  - Verify: play a file with a known-bad packet region (e.g., `dd if=/dev/urandom` splice inside an MKV, or a file saved from an unstable network drop) → expect "Skipping corrupt frame…" toast + playback advances past the bad region without aborting. Sidecar rebuild (`build_qrhi.bat`) required before the main-app event path has anything to consume.

**Phase 6 exit:** `READY FOR REVIEW — [Agent 3, Phase 6]: Error recovery | Objective: Kodi ProcessVideo.cpp error loop + OBS device-lost pattern`

---

## Phase 7 — Deferred Cleanup

Small independent items. Each reviewable alone if desired.

- [ ] **Batch 7.1** — Keyframe-aligned seek confirmation (`seekCompleted` event)
- [ ] **Batch 7.2** — HW decoder priority fallback (port `decode.c:23-66`)
- [ ] **Batch 7.3** — Frame-advance while paused (`.` / `,` hotkeys → existing `sendFrameStep`)
- [ ] **Batch 7.4** — Delete dead code `FfmpegDecoder.cpp/.h` (zero callers confirmed)

---

## Testing corpus (curate before Phase 3)

- [ ] SDR H.264 1080p (baseline regression guard, every phase)
- [ ] HDR10 HEVC 4K (Phase 3)
- [ ] HLG stream e.g. YouTube HDR (Phase 3)
- [ ] 5.1 AC3 movie (Phase 4.4)
- [ ] Anime with colorful ASS subtitles (Phase 5)
- [ ] Blu-ray rip with PGS subs (Phase 5.5)
- [ ] Corrupted MKV (Phase 6.3)

## Per-batch verification protocol

1. Compile clean (Rule 6)
2. `build_and_run.bat`; play baseline SDR file — no regression
3. Play batch-specific test file; capture `_vsync_timing.csv` delta where relevant
4. Post Rule 11 `READY TO COMMIT` line
5. At phase end, post `READY FOR REVIEW`

## Timeline (rough)

| Phase | Effort | Cumulative |
|-------|--------|------------|
| 1 Clock feedback | 1 day | 1d |
| 2 Queue audit | 0.5 day | 1.5d |
| 3 HDR pipeline | 2 days | 3.5d |
| 4 Audio polish | 3 days | 6.5d |
| 5 Subtitles | 4 days | 10.5d |
| 6 Error recovery | 1 day | 11.5d |
| 7 Cleanup | 0.5 day | 12d |

**Total: ~2 weeks of Agent 3 time.** Each phase independently commitable.
