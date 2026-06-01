# In-Process Player — Proof-of-Concept Design Spec

**Date:** 2026-06-01
**Author:** Agent 3 (Video Player)
**Status:** Approved for spec → plan (Hemanth, 2026-06-01)
**Arc:** opening move toward CROSS_PLATFORM_BACKEND (player half)

---

## Plain-language summary

Today the video is decoded ("cooked") in a **separate program** — the `ffmpeg_sidecar.exe` process — and the finished frames are carried across the process boundary into the main app to be drawn on screen. On Hemanth's Intel UHD 620 that hand-off falls back to a slow per-frame copy, and frames arrive late and uneven. **That is the stutter** Hemanth's eyes confirmed on One Piece S01E01 (2026-05-31 smoke).

The fix is to stop carrying frames between programs: **decode and present inside the main app, in one process, with no cross-process hand-off** — the model Kodi / VLC / mpv use.

Before gutting the mature sidecar, we run a **small proof**: a hidden, minimal in-process play path that decodes one local file (picture + sound) and feeds the **existing on-screen surface** (`FrameCanvas`) directly. Hemanth plays the known stutter repro through it. **His eyes are the verdict.** If smooth, we design the full in-process player; if not, we learned cheaply and nothing was torn down.

---

## Problem statement & evidence

- **Symptom:** visible playback stutter on One Piece S01E01 (local 1080p h264 mkv, subtitles on), Hemanth-confirmed 2026-05-31.
- **Engine is healthy:** sidecar `[PERF]` telemetry over a clean 30s window showed `drops=0/s`, 24–25 fps, 3–7 ms per-frame work against a ~41 ms budget (`path=cpu`). Decode is not the problem.
- **The stall is in frame delivery, not decode:** periodically the video fell **2.6 s – 4.1 s behind the audio clock** then drop-burst ~30 frames to resync (`VideoDecoder: dropped late frame ... behind=4131ms`). With healthy per-frame work, something stalls the *delivery/present* pipeline for seconds.
- **Hardware root:** `FrameCanvas: OpenSharedResource1 failed, hr=0x80070057 — falling back to SHM` (events.jsonl). The cross-process **D3D11 shared-texture (NT-handle) zero-copy import is broken on Intel UHD 620** (known: `user_hardware_intel_uhd_620` — D3D11 NTHANDLE interop broken, native GL works). So every frame takes the cross-process **shared-memory copy** path.
- **Observer effect (secondary, real):** dev-control bridge polling on the UI thread worsened the stall (one bridge call timed out at 60 s). Removed during diagnosis. Not the root cause — Hemanth's eyes confirmed stutter persisted with zero polling.
- **Engine-swap history:** the decode engine was swapped (ffmpeg ↔ mpv) but the **cross-process frame hand-off never changed**. Per Hemanth, mpv was never proven on his hardware. The hand-off is the untested variable and the prime suspect.

## Goal

**Prove (or disprove), on Hemanth's Intel UHD 620, that decoding ffmpeg inside the main app and presenting without the cross-process hand-off eliminates the stutter** — with the smallest, most reversible change possible, before committing to a full in-process rewrite.

This is a **decision gate**, not a product feature. Its only output is a confident yes/no plus the telemetry that justifies it.

## Non-goals (explicitly out of POC scope)

- Subtitles, HDR / tone-mapping, seeking, track switching, speed/aspect/crop, screenshots, OSD/HUD.
- Streaming / network sources (Theatre). **Local file only.**
- Cross-platform (Mac/Linux) rendering. POC is Windows-only, reusing the existing D3D11 `FrameCanvas`.
- Replacing or removing the sidecar. The shipping sidecar path stays the default and untouched.
- Production error handling, lifecycle parity, multi-file robustness.

These are the *full build*, designed only after the POC earns a "yes."

## Approach (chosen: Option A)

**In-process decode feeding the existing `FrameCanvas`, behind a flag.**

Considered and deferred:
- **Option B — new OpenGL present surface (mpv/Kodi style):** the likely home for the *full* cross-platform build (native GL works on UHD 620 and ports to Mac/Linux), but it adds a whole new render surface — too much infrastructure to *start* a proof with.
- **Option C — full in-process backend now:** that is the rewrite, not a proof. Violates prove-first.

Option A reuses the most battle-tested display code (`FrameCanvas` already presents frames in the main app today via a DXGI waitable swapchain) and isolates the one variable under test: **removing the cross-process boundary.** The UHD 620 failure is specifically the *cross-process* GPU-texture share; in-process drawing to the same surface sidesteps it.

## Architecture & components

### Key reuse insight
The sidecar's decode/audio/sync logic is plain, mature C++ already in-tree under `native_sidecar/src/`:
- `video_decoder.{h,cpp}` — libavcodec decode loop, A/V wait, late-frame drop, BGRA output.
- `audio_decoder.{h,cpp}` + `wasapi_output.{h,cpp}` — audio decode + WASAPI output.
- `av_sync_clock.{h,cpp}` — audio-master A/V clock.
- `ring_buffer.h` — frame slot model.

The POC's core move is to **compile the needed decode/audio/sync sources into the main app** and **redirect the video frame output from the SHM ring → directly into `FrameCanvas`**, while audio drives WASAPI in-process. The cross-process IPC (JSON-over-stdin) and the SHM / D3D11-shared-texture transport are simply **not used**.

### Components (POC)
1. **`InProcessPlayer` (new, main app)** — owns in-process instances of the decode + audio + clock logic. Exposes a minimal surface: `openFile(path)`, `play()`, `stop()`. Lives behind the flag.
2. **In-process video decode** — reuse of the sidecar's `VideoDecoder` logic (CPU/SHM/`path=cpu` branch), with its frame-output callback changed to hand the decoded BGRA frame **directly to `FrameCanvas`** instead of writing the SHM ring. No D3D11 shared-texture / NT-handle path.
3. **In-process audio + clock** — reuse `AudioDecoder` + `WasapiOutput` + `AVSyncClock` in-process; audio remains the master clock (same model as today).
4. **`FrameCanvas` feed point** — a new in-process entry that accepts a CPU BGRA frame + PTS and routes it through the existing waitable-swapchain present path. (Mirrors the shape `FrameCanvas` already consumes from SHM, minus the cross-process attach.)
5. **Flag + wiring** — `VideoPlayer` constructs `InProcessPlayer` instead of `SidecarProcess` when the POC flag is set (env `TANKOBAN_INPROCESS_POC=1` and/or a CLI switch baked into a launch helper). Default off → shipping path unchanged.

### Data flow
```
Local file → [in-process libavformat/libavcodec decode] → BGRA frame + PTS
           → FrameCanvas in-process feed → DXGI waitable swapchain present → screen
Audio packets → [in-process audio decode] → WASAPI out → drives AVSyncClock (master)
Video present paced against AVSyncClock (same A/V wait as today, in-process)
```
The producer→consumer hand-off (decode thread → present) still exists, but it is **in-process** (shared memory / a queue in one address space) instead of cross-process SHM + NT-handle share — that is the whole point.

### What stays untouched
- The entire sidecar (`native_sidecar/`, `ffmpeg_sidecar.exe`) and `SidecarProcess` — still the default backend.
- `FrameCanvas`'s existing SHM consume path (we *add* an in-process feed, not replace).
- All other player features.

## Success criteria (the gate)

**Primary (Hemanth's eyes, on his UHD 620):** play One Piece S01E01 from the start through the POC path for ~30–60 s — **is it smooth?** Smoothness is the gate, not a metric (per `feedback_dev_bridge_visual_blindspot`: telemetry proves emit, eyes prove paint).

**Supporting telemetry (Agent 3, alongside the eye test):**
- In-process per-frame timing + present cadence (reuse/port the `[PERF]` line).
- A/V drift stays bounded (no multi-second "behind" drop-bursts like the sidecar showed).
- Read passively from logs — **no UI-thread bridge polling during the smoke** (it perturbs the very thing we measure).

**Outcomes:**
- **Smooth →** in-process is the answer. Green-light the full in-process player design (subtitles, HDR, seek; OpenGL present for cross-platform). Separate spec + plan.
- **Still stutters →** the cross-process hand-off was not the (sole) cause. Document the finding, keep the sidecar, pivot the investigation — at the cost of a flag-gated POC, not a rewrite.

## Risks & open questions (for the plan)

1. **A/V sync in-process** is the trickiest part of any player. The POC *includes audio* precisely to test sync; shaky sync is itself a finding.
2. **Build integration:** compiling `native_sidecar/src/*` into the main app (MSVC/Qt) vs its current MinGW sidecar build — toolchain/ABI for the ffmpeg libs must be sorted. Open question for the plan: link the same shared ffmpeg DLLs the sidecar uses, against MSVC. (The main app is MSVC; the sidecar is MinGW — this seam needs a decision.)
3. **`FrameCanvas` in-process feed API:** confirm the cleanest entry point that reuses the waitable-swapchain pacing without disturbing the SHM path.
4. **Threading:** decode thread → present hand-off in-process (lock-free queue or small ring) — cheap vs cross-process, but must be correct.
5. **Flag hygiene:** POC must be fully inert when off; zero risk to the shipping sidecar path.

## What a "yes" unlocks (out of scope here)

A full in-process player design — subtitles (libass), HDR/libplacebo, seeking, tracks — presenting via **OpenGL** (works natively on UHD 620; ports to Mac/Linux), as the player half of the CROSS_PLATFORM_BACKEND arc. Its own brainstorm → spec → plan.

---

*Per brotherhood governance: this spec is the decision-gate artifact. Implementation proceeds via the writing-plans skill after Hemanth reviews this document.*
