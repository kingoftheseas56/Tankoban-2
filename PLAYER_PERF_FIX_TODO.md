# Player Perf Fix TODO — DXGI waitable cadence + cinemascope D3D11 box + GPU subtitle overlays + P1 cleanup

**Owner:** Agent 3 (Video Player). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/video_player_perf_2026-04-16.md` as co-objective.

**Created:** 2026-04-16 by Agent 0 after Agent 7's per-frame smoothness audit + Agent 3's observation-only validation pass (chat.md:19039-19133).

## Context

Hemanth reported a "20-hour-stable → judder regression" class in the video player. Agent 7's audit at `agents/audits/video_player_perf_2026-04-16.md` identified 3 P0s + 4 P1s + 3 P2s comparing Tankoban's decode/subtitle/D3D11/SHM/clock pacing paths against mpv, IINA, QMPlay2, and Microsoft DXGI docs.

**Agent 3's validation did something none of the prior validations did:** shipped a 1 Hz sidecar `[PERF]` log DURING the validation session and proved the sidecar is delivering frames with ~90% headroom on the 41ms budget for 24fps content — `blend p99 < 1.5 ms, present p99 <= 3.5 ms, total p99 <= 4.5 ms, drops = 0/sec`. This empirical data inverts the audit's prioritization: the sidecar is fine; the stutter is on the main-app display side.

**Revised P0 ranking per Agent 3 validation:**

- **P0-2 (QTimer + Present cadence) = PRIMARY stutter suspect.** Fixed `QTimer(16)` + `Present(1, 0)` + `m_skipNextPresent` single-skip guard. No DXGI waitable. Qt docs explicitly warn `QTimer` wakes late under load. mpv uses DXGI waitable end-to-end. Display-side cadence jitter is the only explanation for user-observed stutter on smooth-decoded frames.
- **P0-3 (cinemascope D3D11 source-box UB) = real bug, confirmed hit.** `present_slice` passes `nullptr` source box per Microsoft UB docs; Agent 3's prior `[cinemascope-diag]` log documented `HW texture=1920x896 vs shared=1920x804` on The Boys S03E06. Padded-pool condition hits; may manifest as intermittent driver-side stalls invisible from the sidecar.
- **P0-1 (subtitle-active HEVC 10-bit off zero-copy path) = real architectural gap, NOT acute regression.** Confirmed broken at file:line but cumulative cost is inside budget per `[PERF]` data. Scope for long-term architectural parity with mpv, not for acute stutter fix.

**Identity direction locked by Hemanth 2026-04-16** via AskUserQuestion: **mpv-clone (full arch).** Phase 3 GPU subtitle overlays IN scope — closes the last big mpv architectural gap even though current CPU blend cost is within budget. Commitment to architectural parity, not just "good enough."

**Scope:** 4 phases, ~13 batches. Phase 1 carries the empirical stutter fix (cadence); Phase 2 is a surgical cinemascope correctness fix; Phase 3 is the big architectural re-arch (GPU subtitle overlays); Phase 4 is P1 polish (CV hop, mutex granularity, A/V gate) deferred per Agent 3 capacity.

## Objective

After this plan ships, the video player meets mpv-class per-frame smoothness standards:

1. **Display cadence is vsync-aligned via DXGI waitable swap chain** (Microsoft's documented `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` + `GetFrameLatencyWaitableObject` pattern). No fixed `QTimer(16)` wall-clock timer driving the render loop. User-observed 20-hour-stable judder resolves.
2. **FrameCanvas `[PERF]` log** empirically proves cadence fix landed — `timer_interval_ms` p99 converges to vsync interval (~16.67ms at 60Hz), `skipped_presents` drops to zero on content that previously triggered the overrun guard.
3. **Cinemascope / padded-pool playback** uses explicit `D3D11_BOX` source rects sized to destination width×height per mpv's `hwdec_d3d11va.c:220-226` pattern. Microsoft UB path eliminated.
4. **Subtitle-active HEVC 10-bit playback stays on the D3D11VA zero-copy fast path.** Subtitles render as GPU overlay textures attached to the shared D3D11 texture (mpv `vo_gpu_next.c:313-408` model). `render_blend` CPU path retires. Zero full-frame CPU/GPU/BGRA conversion + CPU blend per frame.
5. **(Stretch) P1 cleanup:** subtitle render-thread CV hop flattened, `subtitle_renderer::mutex_` split into ingest mutex + render mutex, A/V sync gate de-coupled from decode throughput.

## Non-Goals (explicitly out of scope for this plan)

- **P1-4 (SHM `readBestForClock` O(slot_count) scan)** — Agent 3 validation confirmed SAFE in practice (4-8 slots, essentially free). Deprioritized.
- **P2-1 (compiler flags / LTO / PGO / -march=native)** — Agent 3 confirmed deprioritize. Architectural gaps dominate; compiler tuning is polish after measurement, not the primary gap.
- **P2-2 (pre-migration sidecar delta)** — Agent 3's Option C-A swap-in this session confirmed the remaining migration delta (`audio_decoder.cpp` HTTP case-check, `demuxer.{cpp,h}` HTTP probe/fps field) is file-open cosmetics, not per-frame work. Not the regression source.
- **Queued video output architecture** (mpv's vo push/pull frame queue with `VD_WAIT`). Out of scope — this TODO fixes cadence at the present layer, not the decode-to-VO layer. If Phase 1's DXGI waitable doesn't fully recover smoothness, queued VO becomes a later follow-up.
- **Non-subtitle OSD** — Tankoban's OSD (TrackPopover, SubtitleMenu, FilterPopover, EqualizerPopover, CenterFlash, VolumeHud, StatsBadge) all render on the Qt side over the D3D11 canvas. Not affected by Phase 3's GPU subtitle overlay work; no changes needed.
- **Cross-agent work** — entirely Agent 3's domain. No cross-agent touches. No HELP requests expected.
- **Any work outside `src/ui/player/FrameCanvas.*`, `src/ui/player/VideoPlayer.*`, `native_sidecar/src/video_decoder.*`, `native_sidecar/src/subtitle_renderer.*`, `native_sidecar/src/d3d11_presenter.*`, `native_sidecar/src/av_sync_clock.*`** plus minor additions/new files for Phase 3 (GPU overlay texture resource).

## Agent Ownership

All batches are **Agent 3's domain** (Video Player). Primary files:

- **Main-app:** `src/ui/player/FrameCanvas.{h,cpp}`, `src/ui/player/VideoPlayer.{h,cpp}` (Phase 1).
- **Sidecar:** `native_sidecar/src/d3d11_presenter.{h,cpp}` (Phase 2), `native_sidecar/src/video_decoder.{h,cpp}` + `native_sidecar/src/subtitle_renderer.{h,cpp}` (Phases 3 + 4), `native_sidecar/src/av_sync_clock.{h,cpp}` (Phase 4).

**Sidecar rebuild triggers:** Phases 2 + 3 + 4 all require `build_qrhi.bat` sidecar rebuild. Hemanth runs that per the existing workflow. Phase 1 is main-app-only — no sidecar rebuild.

Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — DXGI waitable swap chain + vsync-aligned render loop (P0-2)

**Why:** The empirical primary stutter suspect per Agent 3 validation. `FrameCanvas::m_renderTimer.setInterval(16)` under `Qt::PreciseTimer` + `DXGI_SWAP_CHAIN_DESC1::Flags = 0` (no waitable) + `Present(1, 0)` is the current render pipeline. Sidecar delivers on time; main-app's wall-clock timer jitters under system load. Qt docs explicitly warn `QTimer` wakes late. Microsoft's DXGI 1.3 waitable-swap-chain pattern (docs link in audit) is the standard fix. mpv uses exactly this pattern on Windows.

Main-app-only. No sidecar rebuild required. Cheapest architectural fix in the TODO + highest user-visible payoff.

### Batch 1.1 — FrameCanvas [PERF] diagnostic log (evidence before fix)

**Isolate-commit:** yes. Pure instrumentation; no behavior change. Landing it alone gives empirical proof of the diagnosis BEFORE the fix, matching `feedback_evidence_before_analysis` discipline. Hemanth captures a pre-fix trace, we land Batch 1.2, compare.

- Add 1 Hz summary log inside `FrameCanvas::renderFrame()`:
  ```
  [PERF] frames=N timer_interval_ms p50=X p99=Y draw_ms p50=X p99=Y
         present_ms p50=X p99=Y skipped_presents=N
         [DXGI] presents_queued=N vsync_interval_us=X sync_qpc_time=X
  ```
- Timer interval = time between consecutive `renderFrame()` invocations (actual wake cadence vs requested 16ms).
- Draw ms = time inside `clear + drawTexturedQuad` pre-Present.
- Present ms = time inside `m_swapChain->Present(1, 0)`.
- Skipped presents = cumulative count of `m_skipNextPresent` firings.
- DXGI stats via `IDXGISwapChain1::GetFrameStatistics(DXGI_FRAME_STATISTICS*)` if available. If not (some driver configs don't populate), leave blank. Microsoft doc: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics.
- Log format: match existing sidecar `[PERF]` log shape so tooling is unified.
- No gating; always-on at 1Hz during playback only (not idle).

**Files:** [src/ui/player/FrameCanvas.h](src/ui/player/FrameCanvas.h), [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp).

**Success:** log emits cleanly during playback. Hemanth captures a 60-second cinemascope trace. Expected finding per Agent 3's hypothesis: `timer_interval_ms p99` materially exceeds 16 (likely 17-30ms intermittently), `skipped_presents` non-zero, DXGI `presents_queued` sits at 1-2 (no frame-latency waitable control).

### Batch 1.2 — Add DXGI waitable swap chain flag

- [FrameCanvas.cpp:215](src/ui/player/FrameCanvas.cpp#L215) current: `desc.Flags = 0`
- Change to: `desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`
- Microsoft doc: https://learn.microsoft.com/cs-cz/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains
- No other changes this batch. Flag takes effect at swap chain creation; waitable handle not yet consumed (that's Batch 1.3).
- Regression check: existing `QTimer(16)` + `Present(1, 0)` still drives cadence this batch. The flag is inert until `GetFrameLatencyWaitableObject` is called. So Batch 1.2 should be a no-op behavior-wise.

**Files:** [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp).

**Success:** swap chain creates without error. Playback continues unchanged. If `CreateSwapChainForHwnd` rejects the flag on this Qt/D3D11 combo, surface the error early + investigate alternate creation path before proceeding to Batch 1.3.

### Batch 1.3 — Replace QTimer tick with WaitForSingleObject on waitable handle

**Isolate-commit:** yes. Core cadence change. Largest per-batch behavior delta in the TODO. Isolate so pre/post `[PERF]` traces are directly comparable.

- After swap chain creation, grab the waitable handle:
  ```cpp
  Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
  m_swapChain.As(&swapChain2);
  HANDLE waitable = swapChain2->GetFrameLatencyWaitableObject();
  swapChain2->SetMaximumFrameLatency(1);  // Standard 1-frame latency for video playback
  ```
- Refactor render loop: replace `m_renderTimer` timeout-driven `renderFrame()` with a `WaitForSingleObject(waitable, timeoutMs)` loop in a dedicated render thread (or on the main thread via `QObject::installEventFilter` non-blocking wait — Agent 3's call on thread topology).
- On `WAIT_OBJECT_0`, run `renderFrame()` immediately (drain + draw + Present).
- On `WAIT_TIMEOUT` (100ms cap), skip this iteration — acts as keepalive so the loop doesn't hang forever if DXGI stops signaling.
- `m_renderTimer` retires entirely OR repurposes to a 1Hz keepalive (Agent 3's call).
- Per [Microsoft docs on `SetMaximumFrameLatency`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency): latency of 1 is the standard video-playback value. Default is 3.

**Files:** [src/ui/player/FrameCanvas.h](src/ui/player/FrameCanvas.h), [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp).

**Success:** post-fix `[PERF]` trace shows `timer_interval_ms p99` converges to vsync interval (~16.67ms at 60Hz, ~8.33ms at 120Hz). DXGI `presents_queued` stays at 1-2. User-subjective judder resolves on the cinemascope test content that reproducibly stuttered pre-fix.

**Rollback path:** if DXGI waitable + SetMaximumFrameLatency breaks on some driver/Windows combination (edge-case incompatibility), revert to `QTimer(16)` is one-commit revert of Batches 1.2+1.3. Agent 3 flags if observed.

### Batch 1.4 — Audit m_skipNextPresent guard for redundancy

- Current `m_skipNextPresent` arms after sustained timer-overrun. With vsync-aligned waitable presents, the overrun class should no longer occur — timer latency is gated by DXGI rather than QTimer.
- Review `FrameCanvas.cpp:480-488, :540, :565-567` (skipNextPresent arm/fire sites). Options:
  - **(A)** Remove entirely. With waitable cadence, the guard has no triggering scenario.
  - **(B)** Recalibrate threshold — if a waitable still occasionally produces a late wake (system hitch, driver reset), the skip guard could serve as a last-resort drop. Threshold shifts from "QTimer overrun" to "WaitForSingleObject latency spike."
  - **(C)** Keep as-is. Guard becomes dead code but no cost to leaving it.
- Agent 3's call during implementation based on Batch 1.3 post-fix `[PERF]` trace. Recommend (A) if traces are clean; (B) if residual spikes appear.

**Files:** [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp).

**Success:** skipNextPresent decision documented + applied. Post-fix `[PERF]` trace shows `skipped_presents` stable (zero or near-zero) under normal playback.

### Phase 1 exit criteria
- FrameCanvas `[PERF]` log live.
- DXGI waitable swap chain + SetMaximumFrameLatency(1) active.
- Render loop vsync-aligned via WaitForSingleObject.
- skipNextPresent audited.
- Pre/post `[PERF]` comparison trace captured, shows cadence convergence to vsync interval.
- Subjective stutter resolves on Hemanth's reproducible cinemascope content.
- Agent 6 review against audit P0-2 citation chain.
- `READY FOR REVIEW — [Agent 3, PLAYER_PERF_FIX Phase 1]: DXGI waitable swap chain + vsync-aligned render loop | Objective: Phase 1 per PLAYER_PERF_FIX_TODO.md + agents/audits/video_player_perf_2026-04-16.md. Files: src/ui/player/FrameCanvas.h, src/ui/player/FrameCanvas.cpp.`

---

## Phase 2 — Explicit D3D11_BOX source rect in present_slice (P0-3)

**Why:** Agent 3 validation (chat.md:19071): `native_sidecar/src/d3d11_presenter.cpp:85-90` `present_slice` uses `CopySubresourceRegion(dst, 0, 0, 0, 0, src, slice_idx, nullptr)` — null source box. Microsoft docs are explicit: when source and destination dimensions differ, UB. Agent 3's prior `[cinemascope-diag]` trace (chat.md earlier) documented exactly this condition hits for cinemascope content: `HW texture = 1920x896` (16-pixel-aligned decoder output) vs `shared texture = 1920x804` (actual content size). The copy currently works by luck/driver tolerance; could manifest as intermittent driver-side pipeline stalls invisible from sidecar timing.

Surgical targeted fix. ~10 LOC. No architectural implications. Ships quickly.

### Batch 2.1 — D3D11_BOX in present_slice

- [d3d11_presenter.cpp:85-90](native_sidecar/src/d3d11_presenter.cpp#L85-L90) current:
  ```cpp
  device_context_->CopySubresourceRegion(
      shared_texture_.Get(), 0,
      0, 0, 0,
      src, src_slice_idx,
      nullptr  // ← source box
  );
  ```
- Change to explicit box sized to destination (shared texture) dimensions:
  ```cpp
  D3D11_BOX src_box = {};
  src_box.left = 0;
  src_box.top = 0;
  src_box.front = 0;
  src_box.right = static_cast<UINT>(width_);    // shared texture width
  src_box.bottom = static_cast<UINT>(height_);  // shared texture height
  src_box.back = 1;

  device_context_->CopySubresourceRegion(
      shared_texture_.Get(), 0,
      0, 0, 0,
      src, src_slice_idx,
      &src_box
  );
  ```
- Reference: mpv `hwdec_d3d11va.c:220-226` uses `src_box.right = mapper->dst_params.w`, `src_box.bottom = mapper->dst_params.h` — exactly this pattern.
- Microsoft doc: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion

**Files:** [native_sidecar/src/d3d11_presenter.cpp](native_sidecar/src/d3d11_presenter.cpp). Sidecar rebuild required — Hemanth runs `build_qrhi.bat`.

**Success:** cinemascope content (1920x804 or similar non-aligned resolutions) plays without intermittent driver stalls. No visual regression on standard 16:9 content (1920x1080). `[PERF]` `present_slice_ms` should stay flat (<= 3.5ms p99) — if it was bleeding from UB stalls pre-fix, improvement may surface.

### Phase 2 exit criteria
- Explicit D3D11_BOX source rect live.
- Cinemascope smoke clean.
- Standard 16:9 regression clean.
- Agent 6 review against audit P0-3 citation chain.

---

## Phase 3 — GPU subtitle overlays (P0-1 — architectural)

**Why:** Hemanth locked **mpv-clone identity** via AskUserQuestion. Phase 3 is the big architectural re-arch that closes the last remaining per-frame cost gap vs mpv. Even though Agent 3's `[PERF]` data shows current CPU blend cost is within budget (blend p99 < 1.5ms), the architectural pattern is incorrect — subtitle rendering should NEVER knock the D3D11VA fast path offline. mpv `vo_gpu_next.c:313-408` treats subtitles as overlay textures attached to the video frame, rendered as a separate GPU draw call after the video pass.

**Biggest batch in the TODO.** 200-400 LOC across sidecar + potentially a new overlay-texture resource concept. Phase 3 is the "beyond just cadence, beyond just correctness, full mpv parity" phase.

### Batch 3.1 — libass-to-overlay-bitmap pipeline (no GPU yet)

- Refactor `subtitle_renderer::render_blend` into a two-step API:
  - `render_to_bitmaps(pts_ms) → std::vector<SubOverlayBitmap>` — returns libass output as independent RGBA bitmaps with {x, y, width, height, data} per `ASS_Image` entry. For PGS, similar shape — each rect is an overlay bitmap.
  - `blend_into_frame(bitmaps, dst_frame)` — current CPU blend logic, now operating on pre-computed bitmaps rather than calling libass inside the blend loop.
- Current `render_blend` becomes `render_to_bitmaps` + `blend_into_frame` called sequentially. Zero behavior change this batch — just splits the pipeline into two composable stages.
- Store bitmaps in a flat `std::vector` per frame; avoid per-`ASS_Image` allocation (pool + reuse per audit advisory).

**Files:** [native_sidecar/src/subtitle_renderer.h](native_sidecar/src/subtitle_renderer.h), [native_sidecar/src/subtitle_renderer.cpp](native_sidecar/src/subtitle_renderer.cpp). Sidecar rebuild required.

**Success:** pipeline split; existing CPU blend path still works; `[PERF]` blend p99 unchanged.

### Batch 3.2 — D3D11 overlay texture resource

- New class `D3D11OverlayTexture` in `native_sidecar/src/d3d11_presenter.{h,cpp}` OR a new file `native_sidecar/src/overlay_renderer.{h,cpp}` (Agent 3's call on file organization).
- Represents a GPU texture holding packed subtitle bitmaps. Updated per-frame from `render_to_bitmaps` output.
- Upload path: `ID3D11Device::CreateTexture2D(DXGI_FORMAT_B8G8R8A8_UNORM)` with CPU-write + GPU-read access. Per-frame `Map/memcpy/Unmap` to upload bitmap atlas.
- Overlay draw pass: after the main video quad present, draw a textured quad with alpha blending enabled, using the overlay texture. Standard `DrawIndexed` with a compiled `video_d3d11.hlsl` overlay entry point (add overlay shader if not present).
- Atlas packing strategy: simple horizontal pack if bitmap count is small (<20 typically for ASS subtitles). Can optimize later if needed.

**Files:** new `native_sidecar/src/overlay_renderer.{h,cpp}` (or extension to `d3d11_presenter.{h,cpp}`), `resources/shaders/video_d3d11.hlsl` (add overlay entry), `native_sidecar/CMakeLists.txt` (new source file).

**Success:** overlay texture creates, uploads test bitmap, draws as a textured quad over the video. Alpha-blended correctly. Isolated test — no subtitle flow wired yet.

**Isolate-commit:** yes. First new GPU resource in the sidecar presenter. Isolate so any D3D11 device/context interaction issues surface in isolation before Phase 3.3 wires it.

### Batch 3.3 — Wire sidecar video_decoder to overlay path

- [video_decoder.cpp:429 sub_blend_needed guard](native_sidecar/src/video_decoder.cpp#L429) retires. Fast path becomes:
  ```cpp
  fast_path = zero_copy_active_ && d3d_gpu_copied;
  // sub_blend_needed no longer blocks fast path
  ```
- After `present_slice` (zero-copy GPU path), call `subtitle_renderer::render_to_bitmaps(pts_ms)`, upload result to overlay texture, enqueue overlay draw.
- CPU fallback path (non-D3D11VA sources — software-decoded AVIs etc.) still uses `blend_into_frame` with `render_to_bitmaps` output. Graceful degradation for content that can't use the fast path anyway.
- Cleanup: retire `render_blend`, `sub_blend_needed` state variable, related synchronous wait logic.

**Files:** [native_sidecar/src/video_decoder.cpp](native_sidecar/src/video_decoder.cpp), [native_sidecar/src/subtitle_renderer.cpp](native_sidecar/src/subtitle_renderer.cpp). Sidecar rebuild.

**Success:** HEVC 10-bit subtitle-active playback stays on D3D11VA zero-copy. `[PERF]` shows the `hwframe_transfer_ms + sws10_ms + sws_bgra_ms + render_blend_ms + present_cpu_ms + shm_write_ms` pipeline collapses to `present_slice_ms + overlay_upload_ms + overlay_draw_ms`. Per-frame cost drops materially.

### Batch 3.4 — Main-app FrameCanvas overlay integration

- `FrameCanvas::renderFrame` picks up the overlay texture from SHM (if applicable) OR from the sidecar's shared D3D11 texture (preferred for zero-copy path).
- Draw order per frame: clear → draw video quad (existing) → draw overlay quad (new). Alpha-blended.
- If overlay texture handle is invalid (no subs), skip overlay pass cleanly.
- Regression check: CPU fallback content (non-D3D11VA) still renders subtitles correctly via the `blend_into_frame` → SHM path.

**Files:** [src/ui/player/FrameCanvas.h](src/ui/player/FrameCanvas.h), [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp), [src/ui/player/ShmFrameReader.h](src/ui/player/ShmFrameReader.h) + `.cpp` (extend SHM contract with overlay-texture handle payload).

**Success:** subtitle-active HEVC 10-bit playback renders subs correctly AND stays on zero-copy fast path. Subtitle positioning, styling, timing unchanged from user perspective.

### Batch 3.5 — Smoke + retirement

- Full-matrix smoke: ASS anime subtitles, PGS Bluray subtitles, SRT text subs, addon-fetched OpenSubtitles SRT, no-sub playback. All render correctly, all on zero-copy when source is D3D11VA.
- Retire dead code paths: `render_blend`, `sub_blend_needed` state, associated mutex/CV overhead (becomes Phase 4 territory for the render-thread hop cleanup).

**Files:** cleanup pass across subtitle_renderer + video_decoder.

**Success:** all test content renders cleanly. Subtitle regression checklist complete.

### Phase 3 exit criteria
- GPU subtitle overlay pipeline live.
- HEVC 10-bit + subtitles stays on D3D11VA zero-copy.
- All subtitle formats (ASS / PGS / SRT) render correctly.
- `[PERF]` confirms per-frame cost drop for subtitle-active content.
- Agent 6 review against audit P0-1 citation chain.

---

## Phase 4 — P1 cleanup (subtitle CV hop + mutex granularity + A/V gate)

**Why:** Agent 3 validation confirmed P1-1 + P1-2 + P1-3 all BROKEN as documented but low current impact (blend p99 < 1.5ms etc.). Given Hemanth's mpv-clone identity, architectural parity is in scope — these cleanups close the last remaining structural gaps. Deferred as polish; ship when Phase 3 lands + Agent 3 has capacity.

**This is the only phase where "defer to capacity" is explicit.** Phase 1-3 must ship. Phase 4 ships when bandwidth opens.

### Batch 4.1 — Flatten subtitle render-thread CV hop (P1-1)

- [subtitle_renderer.cpp:426-439](native_sidecar/src/subtitle_renderer.cpp) — decode thread posts request, waits on `render_done_cv_`, render thread signals. Pure overhead given decode waits anyway.
- Refactor: `render_to_bitmaps` becomes a direct inline call from the decode thread. Retire the render-thread-plus-CV model entirely.
- If Agent 3 finds the render thread's MMCSS priority was materially improving p99 latency (unlikely per `[PERF]`), keep as a dedicated thread but make the call synchronous direct rather than via CV.

**Files:** [native_sidecar/src/subtitle_renderer.h](native_sidecar/src/subtitle_renderer.h), [native_sidecar/src/subtitle_renderer.cpp](native_sidecar/src/subtitle_renderer.cpp).

**Success:** CV hop removed OR documented as preserved for MMCSS-priority reasons. Mutex count reduces. `[PERF]` `sub_submit_wait_ms` drops to zero.

### Batch 4.2 — Split subtitle_renderer::mutex_ (P1-2)

- Current: one mutex covers both `ass_render_frame`/`blend_into_frame` AND `process_packet` packet ingest.
- Split into:
  - `render_mutex_` — held only during `render_to_bitmaps` (libass call).
  - `packet_mutex_` — held only during `process_packet` (libass ingest).
  - State shared between the two (internal libass state) requires careful handling — `ass_process_chunk` + `ass_render_frame` on the same `ASS_Track` need synchronization. Worst case: keep as one mutex but reduce hold time via shorter critical sections.

**Files:** [native_sidecar/src/subtitle_renderer.h](native_sidecar/src/subtitle_renderer.h), [native_sidecar/src/subtitle_renderer.cpp](native_sidecar/src/subtitle_renderer.cpp).

**Success:** mutex contention p99 drops. `[PERF]` `subtitle_mutex_wait_ms` near-zero.

### Batch 4.3 — De-couple A/V sync gate from decode throughput (P1-3)

- [video_decoder.cpp:697-703](native_sidecar/src/video_decoder.cpp#L697-L703) — video thread blocks in sleep loop on audio clock. Coarse.
- Refactor: switch to mpv-style `time_frame`-based pacing. Audio updates its clock non-blocking; video computes `time_frame = frame_pts - audio_clock` and queues to the present stage rather than busy-waiting in decode.
- Requires restructuring the decode loop: introduce a frame queue between decode and present, pop the next frame when `time_frame` is due.
- Larger refactor than 4.1/4.2 — may grow to 2-3 batches if queue introduction proves nontrivial. Agent 3's call on splitting.

**Files:** [native_sidecar/src/video_decoder.h](native_sidecar/src/video_decoder.h), [native_sidecar/src/video_decoder.cpp](native_sidecar/src/video_decoder.cpp), [native_sidecar/src/av_sync_clock.h](native_sidecar/src/av_sync_clock.h), [native_sidecar/src/av_sync_clock.cpp](native_sidecar/src/av_sync_clock.cpp).

**Success:** decode throughput decoupled from audio clock jitter. `[PERF]` `avsync_wait_ms` drops materially. No audio-video desync regression.

### Phase 4 exit criteria
- CV hop + mutex split + A/V gate cleanup landed (or explicit deferral documented per batch).
- Architectural parity with mpv at the sidecar level.
- Agent 6 review against audit P1-1 + P1-2 + P1-3 citation chain.

---

## Scope decisions locked in

- **Identity = mpv-clone (full arch).** Phase 3 GPU subtitle overlays IN scope despite current CPU blend cost being within budget. Architectural parity commitment.
- **Phase 1 is main-app-only.** No sidecar rebuild. Cheapest and highest-payoff fix.
- **Phase 2 is surgical.** ~10 LOC cinemascope correctness fix. Microsoft-documented UB eliminated.
- **Phase 3 is architectural re-arch.** Retires `render_blend` CPU path. Subtitles become GPU overlay textures attached to shared D3D11 texture.
- **Phase 4 is capacity-gated.** Ships per Agent 3 bandwidth after Phase 3 lands.
- **Evidence-before-fix discipline** (per `feedback_evidence_before_analysis`): Batch 1.1 `[PERF]` log ships BEFORE Batch 1.2's structural change. Hemanth captures pre-fix trace; post-fix trace validates diagnosis.
- **Rollback path for Phase 1** is one-commit revert. Phase 1 is the highest-impact-but-also-rollbackable phase.
- **Queued VO architecture (mpv's `VD_WAIT` push/pull model)** is explicitly out of scope. If Phase 1 doesn't fully recover smoothness, queued VO becomes a follow-up TODO.

## Isolate-commit candidates

Per the TODO's Rule 11 section:
- **Batch 1.1** (FrameCanvas `[PERF]` diagnostic) — pure instrumentation; isolate so pre-fix trace is captured in a clean commit before behavior changes.
- **Batch 1.3** (DXGI waitable render loop) — core cadence change. Isolate for clean pre/post `[PERF]` comparison and one-commit rollback if needed.
- **Batch 3.2** (D3D11 overlay texture resource infrastructure) — first new GPU resource; isolate before Phase 3.3 wires it.

Other batches commit at phase boundaries.

## Existing functions/utilities to reuse (not rebuild)

- **Sidecar `[PERF]` log pattern** — Agent 3 shipped this during the validation session. Format + cadence reuse for Batch 1.1's FrameCanvas version.
- **`IDXGISwapChain2` accessor** — `m_swapChain.As(&swapChain2)` is standard Microsoft WRL pattern.
- **mpv `vo_gpu_next.c:313-408` overlay texture pattern** — Phase 3 implementation reference.
- **mpv `hwdec_d3d11va.c:220-226` D3D11_BOX pattern** — Phase 2 implementation reference.
- **Current video_d3d11.hlsl shader** — Phase 3 adds overlay entry point; shader infrastructure already landed via Commit B.

## Review gates

Each phase exits with:
```
READY FOR REVIEW — [Agent 3, PLAYER_PERF_FIX Phase X]: <title> | Objective: Phase X per PLAYER_PERF_FIX_TODO.md + agents/audits/video_player_perf_2026-04-16.md. Files: ...
```
Agent 6 reviews against audit + TODO as co-objective.

## Open design questions Agent 3 decides as domain master

- **Render loop thread topology (Batch 1.3).** Dedicated render thread with `WaitForSingleObject` + cross-thread signal to Qt UI thread, OR main-thread wait with `QEventLoop` + non-blocking wait. Agent 3's architectural call.
- **m_skipNextPresent disposition (Batch 1.4).** Remove / recalibrate / keep. Data-driven decision from Batch 1.3 post-fix trace.
- **Overlay texture packing (Phase 3.2).** Simple linear pack vs atlas with rect packing. Start simple.
- **Overlay file organization (Phase 3.2).** Extend `d3d11_presenter` vs new `overlay_renderer` file. Agent 3's preference.
- **Phase 4.3 queue granularity.** How deep a frame queue is worth introducing for A/V gate decoupling. mpv's approach is 2-3 frames typically. Agent 3 tunes.

## What NOT to include (explicit deferrals)

- **Queued video output push/pull architecture** (mpv `VD_WAIT` model). Out of scope unless Phase 1 doesn't recover smoothness. Follow-up TODO if needed.
- **Runtime-configurable vsync interval / refresh rate detection.** DXGI `GetFrameLatencyWaitableObject` handles this implicitly; explicit runtime refresh-rate query is polish.
- **Sidecar compiler tuning (LTO/PGO/-march=native).** P2-1 per audit, deprioritized.
- **Full IINA-style libmpv adoption.** Out of scope structurally — we use our own sidecar, not libmpv.
- **Audio path improvements.** Separate concern; Phase 4 P1-3 only touches the A/V sync gate coupling, not audio decode/present.
- **SHM frame reader optimization (P1-4).** Confirmed SAFE in practice per Agent 3 validation. Not touched.
- **Tankoban-Max UX parity for player controls.** Separate `VIDEO_PLAYER_FIX_TODO.md` handles UX surface (chapter markers, thumbnails, etc.). Not overlapping.

## Rule 6 + Rule 11 application

- Rule 6: every batch compiles + smokes on Hemanth's box before `READY TO COMMIT`. Agent 3 does not declare done without build verification. Sidecar-rebuild batches require `build_qrhi.bat` smoke before READY TO COMMIT.
- Rule 11: per-batch READY TO COMMIT lines; Agent 0 commits at phase boundaries (isolate-commit candidates above ship individually). Per tightened `feedback_commit_cadence` memory (2026-04-16), commits land within the same session the phase exits.
- Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.
- **Evidence-before-analysis** per `feedback_evidence_before_analysis`: Batch 1.1 is the literal embodiment of this — diagnostic log before fix. Phase 3's sidecar-side `[PERF]` buckets (`hwframe_transfer_ms`, `overlay_upload_ms`, `overlay_draw_ms`) confirm the architectural improvement post-fix.

## Verification procedure (end-to-end once all 4 phases ship)

1. **Cadence baseline:** 60-second `[PERF]` trace on reproducible cinemascope content. `timer_interval_ms p99` converges to vsync interval. `skipped_presents` stable near zero. DXGI `presents_queued` at 1-2. (Phase 1.)
2. **Subjective stutter:** Hemanth's reproducible judder content plays smoothly. No periodic hitch pattern. (Phase 1.)
3. **Cinemascope correctness:** The Boys S03E06 (1920x804) plays without intermittent driver stalls. Standard 16:9 content (1920x1080) regression-clean. (Phase 2.)
4. **Subtitle-active zero-copy:** HEVC 10-bit + ASS / PGS / SRT subs all stay on D3D11VA fast path. `[PERF]` shows the per-frame pipeline collapses to overlay-path stages. (Phase 3.)
5. **Multi-format subtitle render:** ASS anime (complex positioning/styling), PGS Bluray (bitmap subs), SRT text subs, addon-fetched OpenSubtitles SRT, no-sub playback — all render correctly. (Phase 3.)
6. **A/V sync stability:** long-duration playback (1hr+) — no audio drift, no desync regression. (Phase 4.3 if shipped.)
7. **Rapid file-switch stress:** open file A → open file B within 500ms → no cadence regression. (Phase 1 + interaction with PLAYER_LIFECYCLE_FIX_TODO.md if that lands.)
8. **Regression:** existing player features (fullscreen, PiP, always-on-top, snapshot export, Open URL, recent files, drag-drop, playlist, keybind editor, stats badge) all function correctly. None affected by render-loop refactor or subtitle pipeline change.

## Next steps post-approval

1. Agent 0 posts routing announcement in chat.md directing Agent 3 to Phase 1 Batch 1.1.
2. Agent 3 executes phased per Rule 6 + Rule 11.
3. Agent 6 gates each phase exit.
4. Agent 0 commits at phase boundaries (isolate-commit exceptions per Rule 11 section). Per tightened `feedback_commit_cadence` — commits within the same session the phase exits.
5. MEMORY.md `Active repo-root fix TODOs` line updated to include this TODO.

---

**End of plan.**
