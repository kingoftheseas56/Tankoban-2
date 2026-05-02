# MAKE_MPV_BEAT_FFMPEG — Task 1 Architecture Summary

**Date:** 2026-05-02 ~10:35am IST
**Author:** Agent 3 (Video Player)
**Status:** Pending Hemanth ratification
**Output of:** Task 1 of `MAKE_MPV_BEAT_FFMPEG.md`

---

## TL;DR (plain English)

Going into Task 1 I assumed the "ffmpeg quality bar" came from libplacebo+Vulkan and that mpv needed to adopt that pipeline to match. **That assumption was wrong.** Reading the sidecar code reveals:

1. **ffmpeg sidecar's SDR path is plain CPU bilinear scaling via swscale, then D3D11 blit through FrameCanvas.** No libplacebo involved by default. (Sopranos S06E04 — your "pristine smooth like butter" verdict — flows through THIS path.)
2. **libplacebo+Vulkan only fires for HDR sources** (or the rare opt-in via `TANKOBAN_LIBPLACEBO_SDR=1`).
3. **mpv has a built-in software render mode (`MPV_RENDER_API_TYPE_SW`)** that hands us BGRA bytes — same shape the ffmpeg sidecar produces over SHM today.

The cleanest way to make mpv match ffmpeg is **NOT a new Vulkan window with libplacebo** — it's switching mpv's render context from OpenGL to software, routing the BGRA output through FrameCanvas (the existing D3D11 path), and letting mpv handle decode + color + scale + subtitle compositing internally.

**This cuts the original plan's scope roughly in half** — ~2–3 days instead of ~7–10 days, ~400–600 LOC instead of ~1500.

---

## What the ffmpeg sidecar actually does (read of native_sidecar/src/)

### Two render paths, gated by HDR

`native_sidecar/src/main.cpp:904-924` shows the gate:

```cpp
GpuRenderer* gpu_ren = nullptr;
const bool sdr_libplacebo = !probe->hdr && libplacebo_sdr_enabled();
if (probe->hdr || sdr_libplacebo) {
    gpu_ren = new GpuRenderer();   // libplacebo+Vulkan
    ...
}
// else: gpu_ren stays nullptr → "software path"
```

So:
- **HDR file** → `GpuRenderer` (libplacebo+Vulkan with `ewa_lanczossharp` + `hermite` + ICC profile)
- **SDR file** (default) → `gpu_ren = nullptr` → falls through to swscale CPU path
- **SDR file with `TANKOBAN_LIBPLACEBO_SDR=1`** → libplacebo path (rare opt-in; not Hemanth's daily)

### SDR path: where the actual bytes flow

Hemanth's Sopranos S06E04 = SDR + 1080p HEVC 10-bit. That hits the `gpu_ren = nullptr` branch and goes into `VideoDecoder::decode_one_frame` at `native_sidecar/src/video_decoder.cpp`. The relevant code at line 1086-1108:

```cpp
// Software fallback: sws_scale
sws_ctx = sws_getContext(
    convert_src->width, convert_src->height, src_fmt,
    fw, fh, AV_PIX_FMT_BGRA,
    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
sws_scale(sws_ctx, ...)
```

`SWS_FAST_BILINEAR` is the cheapest swscale kernel. CPU does the YUV→BGRA conversion + any resize at canvas dimensions. The output BGRA bytes then go over SHM to the main app, where `FrameCanvas` (D3D11 backed) presents them to the swapchain.

**The pipeline:**

```
ffmpeg decode (CPU or hwaccel)
  → sws_scale (CPU bilinear YUV→BGRA at canvas size)
  → SHM transfer
  → FrameCanvas D3D11 texture upload
  → D3D11 swapchain present (default linear sampler at viewport size)
```

**Why it's smooth on Sopranos:** all the work is either CPU-bounded (decode + sws_scale, both well within 24fps budget on modern x86) or GPU-cheap (D3D11 BGRA blit is essentially free). No expensive shader work on the GPU.

### HDR path: libplacebo+Vulkan with quality scalers

When the file is HDR (Boys S03E01, etc.), `GpuRenderer::init` spins up Vulkan + libplacebo and configures `ewa_lanczossharp` upscaler + `hermite` downscaler + ICC profile + tone-mapping. The output is still BGRA bytes (host-readable target texture), still goes over SHM, still arrives at FrameCanvas. **Same downstream pipeline shape as SDR — only the upstream rendering differs.**

So even for HDR, the integration point with the main app is BGRA over SHM, not a Vulkan-shared texture.

---

## What mpv has available

### Three render API types (per `resources/libmpv/windows/include/mpv/render.h`)

| API | Type constant | What it does | Suitable for our case? |
|---|---|---|---|
| OpenGL | `MPV_RENDER_API_TYPE_OPENGL` | mpv renders into a host-provided GL FBO; what `MpvVideoWidget` uses today | Already proven slow on UHD 620 OpenGL — NO |
| Software | `MPV_RENDER_API_TYPE_SW` | mpv renders into a host-provided BGRA buffer | **YES — matches ffmpeg sidecar shape** |
| Vulkan | (not exposed in this libmpv build) | mpv renders into a Vulkan texture | Not available |

The SW API requires four params per render call: `MPV_RENDER_PARAM_SW_SIZE` (target W×H), `MPV_RENDER_PARAM_SW_FORMAT` (e.g., `"0bgr"` or `"rgb0"`), `MPV_RENDER_PARAM_SW_STRIDE`, `MPV_RENDER_PARAM_SW_POINTER` (output buffer). mpv handles decode, YUV→RGB, scaling, subtitle compositing, HDR tone-mapping all internally — we just receive ready-to-display BGRA.

### Cost concern (and resolution)

mpv's `render.h` documents the SW renderer as "extremely simple (but slow)." That sounds bad — but "slow" here means relative to GPU rendering on a fast discrete GPU. For our case, the CPU work mpv does is the same kind of work the ffmpeg sidecar does via swscale (which we already know is smooth on Sopranos). **Sw render's CPU footprint should be roughly equivalent to the sidecar's CPU footprint.** Plus mpv's threading is well-tested.

### What about HDR through SW path?

mpv handles HDR tone-mapping internally before producing the output BGRA buffer. Set `tone-mapping=auto` (or whatever Task 4 of MAKE_MPV_SOLO settled on — `bt.2446a` per the close), and mpv produces SDR-correct BGRA from an HDR source. **No external libplacebo+Vulkan needed for HDR.** The Task 4 close already verified mpv's own HDR pipeline produces results equivalent to libplacebo's tone-mapping ("yeah it looks good as can be so green" on Boys S03E01).

This means we can drop the libplacebo+Vulkan branch from the new plan entirely — mpv's SW path covers both SDR and HDR. (libplacebo+Vulkan stays alive on the ffmpeg sidecar side for as long as ffmpeg is decommissioned-but-not-deleted; it's separate code that never needs to talk to mpv.)

---

## Picked architecture (Rule 14 agent call)

**Single render path:** `mpv → MPV_RENDER_API_TYPE_SW → BGRA buffer → FrameCanvas D3D11 → swapchain present`

**Mirrors the ffmpeg sidecar's SDR pipeline shape**, with mpv as the producer of BGRA bytes instead of swscale.

**No new Vulkan window. No new libplacebo integration on mpv side.** Reuse FrameCanvas.

### Why this picks above alternatives

| Path | Verdict | Reasoning |
|---|---|---|
| **A — mpv SW + FrameCanvas D3D11 (this pick)** | ✅ Picked | Matches sidecar shape; reuses FrameCanvas; smallest diff; same picture quality as ffmpeg SDR by construction |
| B — vo=gpu-next + wid=HWND (mpv owns window) | ✗ Skip | mpv-owns-window conflicts with Tankoban's player UI (HUD, popovers, fullscreen toggle) |
| C — Custom libplacebo+Vulkan render hook on mpv | ✗ Skip | Over-engineered for SDR (which is what stutters today); doesn't match what ffmpeg actually does |
| D — Keep mpv OpenGL, optimize | ✗ Skip | OpenGL on UHD 620 is the budget problem; we measured this |

### Frame format pick

mpv SW supports several output formats. We want **`0bgr`** (BGRA) because that's what FrameCanvas already consumes from the sidecar SHM. Single format, single texture upload path, zero conversion.

### Subtitle compositing

**Free with this architecture.** mpv composites libass/PGS subtitles internally before producing the SW output buffer. The BGRA bytes we receive already have subtitles burned in. No separate overlay surface, no SubtitleOverlay bridging. Existing position/size sliders that drive mpv properties (`sub-pos`, `sub-scale`) work as-is.

(Caveat: if Hemanth wants a separate subtitle layer for picture-in-picture or screenshot-without-subs, that requires later extension. Not in scope for parity.)

---

## Plan revision (vs current MAKE_MPV_BEAT_FFMPEG.md)

The current plan has 9 tasks and assumes Vulkan + libplacebo as the destination. With this discovery, **8 tasks suffice** and most shrink in scope:

| Original Task | Revised Task | Change |
|---|---|---|
| 1. Architecture survey | 1. Architecture survey | ✅ This file. Done. |
| 2. Empty Vulkan window | **2. Switch mpv to SW render API; route BGRA into FrameCanvas; first frame on screen** | No new Vulkan window. Reuse FrameCanvas. ~150 LOC |
| 3. One frame through libplacebo | (merged into Task 2) | Removed |
| 4. Continuous playback | **3. Continuous playback at floor (Community SDR)** | Same content, same goal |
| 5. Match ffmpeg quality on Sopranos | **4. Match ffmpeg quality on Sopranos** | mpv SW + FrameCanvas should hit it for free; if not, tune internal mpv scaler choice |
| 6. HDR via libplacebo | **5. HDR works through SW path** | Use mpv's own tone-mapping (Task 4 of MAKE_MPV_SOLO already shipped this); no libplacebo |
| 7. Subtitles | (free with SW path; verify only) | Verify-only smoke; no compositing code needed |
| 8. Edge cases | **6. Edge-case sweep** | Same |
| 9. Remove OpenGL | **7. Remove OpenGL widget** | Same |

That's **7 tasks** (we collapsed two pairs into one). Estimated **~2–3 days agent time, ~400–600 LOC**, vs original 7–10 days / 1500 LOC.

---

## Risks I'm carrying forward

1. **mpv SW render performance unverified on this hardware.** mpv's docs call it "slow" without quantifying. If 1080p@24fps SW rendering chokes on UHD 620 CPU, the whole pipeline falls over. Mitigation: Task 2 (revised) is exactly the test for this — if it can't sustain Community S01E01 at floor drop rate, we know early and pivot to libplacebo+Vulkan as the real Task 2.

2. **HDR via mpv SW vs HDR via libplacebo+Vulkan picture-quality delta.** Task 4 of MAKE_MPV_SOLO already verified mpv's HDR matches ffmpeg's HDR subjectively. But that was on the OPENGL render path. Re-verifying on SW path is cheap (one Hemanth eyeball pass on Boys S03E01).

3. **FrameCanvas was built around sidecar SHM transfer.** Plumbing mpv directly into FrameCanvas (no SHM, in-process buffer pointer) requires adapter code. ~50 LOC.

4. **Subtitle position-slider via mpv property** assumes mpv's `sub-pos` / `sub-scale` properties are wired in MpvBackend. They are (per Task 6 of MAKE_MPV_SOLO). But they apply to mpv's internal compositing, not a separate overlay — verify-only smoke.

---

## Ratification questions for Hemanth

(Per Rule 14, technical decisions are agent calls. These are user-facing only.)

**1. "Picture quality on SDR (Sopranos) once SW path lands — should it match ffmpeg or surpass?"** The honest answer: SW path with bilinear matches ffmpeg's SDR. To surpass, we'd want the libplacebo+Vulkan branch you originally requested (which would give us `ewa_lanczossharp` quality). But on UHD 620, the Vulkan path is a real arc on its own. Recommend: ship parity with this plan first; revisit "surpass on SDR" as a future arc once parity is locked.

**2. "Picture quality on HDR (Boys S03E01) once SW path lands — should I use mpv's internal tone-mapping (free) or wire libplacebo+Vulkan (more work but matches ffmpeg sidecar exactly)?"** Same trade-off as Q1. Recommend: try mpv's internal first (free with SW path); if your eyes say "ffmpeg HDR looks better," then wire libplacebo+Vulkan as Task 8 add-on.

**3. "Is the simpler/smaller plan (~3 days) acceptable, or do you want me to do the full libplacebo+Vulkan integration anyway for the long-term picture-quality ceiling?"** Smaller plan gets you parity sooner + simpler code. Larger plan gets you headroom for "surpass ffmpeg" later. Both reach today's goal (mpv = ffmpeg).

---

## Recommended next move

1. Hemanth ratifies the architecture pick (mpv SW + FrameCanvas) and answers Q1+Q2+Q3 above.
2. Agent 3 revises `MAKE_MPV_BEAT_FFMPEG.md` to the 7-task shape.
3. Agent 3 begins revised Task 2 (mpv SW integration + FrameCanvas plumbing).

**No code touched in Task 1** — this is paper-only by design.

---

## RATIFICATION DECISION (2026-05-02 ~10:42am, Hemanth verbatim "we go with the original plan")

Hemanth overrides agent's recommend-the-simpler-path call. Sticks with the **original 9-task plan**: full libplacebo+Vulkan integration as mpv's renderer.

**Implications of this call:**
- Q1 (SDR picture quality) answered: SURPASS ffmpeg, not just match. mpv on libplacebo+Vulkan gets `ewa_lanczossharp` quality where ffmpeg-SDR has plain `SWS_FAST_BILINEAR`. Long-term picture-quality ceiling.
- Q2 (HDR picture quality) answered: full libplacebo+Vulkan, not mpv-internal tone-mapping. Matches ffmpeg-HDR exactly.
- Q3 (scope) answered: full integration. Accept ~7-10 days agent time, ~1500 LOC.

**Agent (me) still picks HOW to connect mpv to libplacebo+Vulkan.** Sub-options:
- **Path A (recommend) — mpv SW render → CPU BGRA → Vulkan texture upload → libplacebo composites/scales/HDR → present to swapchain.** Most aligned with `gpu_renderer.cpp`'s current shape (which takes AVFrame → uploads → runs libplacebo). Avoids GL/Vulkan interop complexity. CPU↔GPU memcpy cost identical to what ffmpeg sidecar pays today. Picked.
- Path B — mpv OpenGL render → GL FBO → GL/Vulkan interop → libplacebo. GL/Vulkan interop is fragile on Intel iGPU.
- Path C — mpv decodes to YUV planes via property API → upload YUV to Vulkan textures → libplacebo. More libmpv integration, less integration risk on the GPU side.

**Plan stays at 9 tasks.** No revision to MAKE_MPV_BEAT_FFMPEG.md needed.

**Task 2 of the original plan (Stand up an empty Vulkan window) begins now.** The Vulkan window stand-up is identical regardless of which sub-path (A/B/C) we pick for connecting mpv to libplacebo — so beginning Task 2 doesn't lock the connection-path choice yet.
