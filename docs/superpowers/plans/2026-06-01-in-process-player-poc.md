# In-Process Player POC — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **Brotherhood note:** Flat-on-master, NO worktrees (gov-v13). Work directly on the shared tree; every change is flag-gated so the shipping sidecar path is never disturbed. Kill `Tankoban.exe` before each rebuild (Rule 1); verify `out/Tankoban.exe` mtime advanced after build.

**Goal:** Prove (or disprove), on Hemanth's Intel UHD 620, that decoding ffmpeg *inside the main app* and presenting without the cross-process hand-off eliminates the One Piece S01E01 stutter — with the smallest, fully-reversible, flag-gated change.

**Architecture:** Compile the sidecar's existing decode/audio/sync C++ (`native_sidecar/src/{video_decoder,audio_decoder,av_sync_clock,wasapi_output,volume_control,ring_buffer}`) into the main MSVC app. A new `InProcessPlayer` allocates a **plain in-process buffer** (not named shared memory), points the existing `FrameRingWriter` at it for the decoder and an existing `FrameRingReader` at the *same* buffer for `FrameCanvas::attachShm`. The decoder runs its CPU/`path=cpu` branch (zero-copy disabled — the broken-on-UHD-620 D3D11 NT-handle share is simply never used). Audio decodes in-process and drives `AVSyncClock` as today. Gated by `TANKOBAN_INPROCESS_POC=1`.

**Tech Stack:** C++17, Qt6/MSVC2022 (main app), FFmpeg shared libs (`C:\tools\ffmpeg-master-latest-win64-gpl-shared`), WASAPI, existing `FrameCanvas` DXGI waitable swapchain. Build: `build_check.bat` (compile gate) + `build_and_run.bat` (smoke). No GoogleTest — this is a visual/integration POC; gates are **build-verify + Hemanth's eyes + passive telemetry** (project TDD policy: unit tests are opt-in for pure-logic only).

**Reversibility:** Every task is additive + flag-gated. Backout = delete `InProcessPlayer.{h,cpp}`, revert the `VideoPlayer.cpp:185` fork and the CMake additions. The sidecar (`native_sidecar/`, `SidecarProcess`) is never modified.

---

## Why audio is in the POC (not deferred)

A prior brotherhood finding (2026-05-05, memory + obs 1550/1648) attributed earlier ffmpeg stutter to **`set_audio_speed` IPC back-pressure on quirky encodes** — i.e. the A/V-drift-correction *command* flooding the cross-process IPC. That is a *different* sub-cause than slow frame transport, but it is **also a cross-process artifact**. Going in-process removes the IPC command path *and* the SHM frame transport at once. Including audio + sync in the POC is therefore mandatory: it tests both failure classes, and A/V sync is the hardest part of any in-process player. A smooth result confirms "the process boundary was the problem," whichever sub-cause dominated.

---

## File Structure

**Created:**
- `src/ui/player/InProcessPlayer.h` — POC orchestrator interface (owns decoder/audio/clock/ring; feeds `FrameCanvas`).
- `src/ui/player/InProcessPlayer.cpp` — implementation.

**Modified:**
- `src/ui/player/VideoPlayer.cpp:185` — flag fork: construct `InProcessPlayer` instead of `SidecarProcess` when POC flag set.
- `cmake/TankobanSources.cmake` (or root `CMakeLists.txt`) — add the 6 sidecar sources + FFmpeg include/link to the `Tankoban` target.

**Reused unchanged:**
- `native_sidecar/src/{video_decoder,audio_decoder,av_sync_clock,wasapi_output,volume_control,ring_buffer}.{h,cpp}`
- `src/ui/player/FrameCanvas.{h,cpp}` (consumer via `attachShm`)

**Never touched:** entire `native_sidecar/` build, `ffmpeg_sidecar.exe`, `SidecarProcess`.

---

## Phase 0 — Build-seam spike (resolves the spec's open question)

The main app is MSVC; the sidecar is built MinGW. FFmpeg ships as **C-ABI shared libraries** (the `extern "C"` headers in `audio_decoder.h:14-18`), so the runtime DLLs are toolchain-agnostic — the only question is **MSVC import libraries** (`.lib`) for link. This phase proves the link works and one frame decodes in-process, before any orchestration is built.

### Task 0.1: Confirm FFmpeg MSVC import libraries exist or generate them

**Files:**
- Inspect: `C:\tools\ffmpeg-master-latest-win64-gpl-shared\lib`

- [ ] **Step 1: Check for `.lib` import files**

Run:
```
powershell -NoProfile -Command "Get-ChildItem 'C:\tools\ffmpeg-master-latest-win64-gpl-shared\lib' -Filter *.lib | Select-Object Name"
```
Expected: `avcodec.lib avformat.lib avutil.lib swscale.lib swresample.lib` (the gpl-shared build ships these).

- [ ] **Step 2: If `.lib` files are ABSENT, generate them from the DLLs**

For each DLL in `...\bin` (`avcodec-62.dll` etc.), generate a `.def` + `.lib` with MSVC `dumpbin`/`lib`:
```
dumpbin /EXPORTS avcodec-62.dll > avcodec.exports.txt
```
Then build a `.def` from the exports and `lib /def:avcodec.def /machine:x64 /out:avcodec.lib`. Document the exact command set that worked in this task's commit message.
Expected: a `.lib` per FFmpeg DLL the POC needs.

- [ ] **Step 3: Commit the finding**

```bash
git add docs/superpowers/plans/2026-06-01-in-process-player-poc.md
git commit -m "[Agent 3, POC P0.1] confirm FFmpeg MSVC import-lib availability for in-process link"
```

### Task 0.2: Minimal in-process FFmpeg link probe

**Files:**
- Create: `src/ui/player/InProcessProbe.cpp` (temporary; deleted at end of Phase 0)
- Modify: `cmake/TankobanSources.cmake` — add FFmpeg include dir + link the import libs to `Tankoban`, guarded by `option(TANKOBAN_INPROCESS_POC "" OFF)`.

- [ ] **Step 1: Add FFmpeg include + link to the main target (POC-gated)**

In the CMake file that defines the `Tankoban` target, add:
```cmake
option(TANKOBAN_INPROCESS_POC "Build the in-process player POC path" OFF)
if(TANKOBAN_INPROCESS_POC)
    target_include_directories(Tankoban PRIVATE "C:/tools/ffmpeg-master-latest-win64-gpl-shared/include")
    target_link_directories(Tankoban PRIVATE "C:/tools/ffmpeg-master-latest-win64-gpl-shared/lib")
    target_link_libraries(Tankoban PRIVATE avcodec avformat avutil swscale swresample)
    target_compile_definitions(Tankoban PRIVATE TANKOBAN_INPROCESS_POC=1)
endif()
```

- [ ] **Step 2: Write a trivial probe that opens the test file in-process**

`src/ui/player/InProcessProbe.cpp`:
```cpp
#ifdef TANKOBAN_INPROCESS_POC
extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>
// Called once from VideoPlayer ctor under the POC flag to prove the link.
void inproc_probe(const char* path) {
    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, path, nullptr, nullptr);
    std::fprintf(stderr, "INPROC_PROBE: open rc=%d nb_streams=%d\n",
                 rc, fmt ? (int)fmt->nb_streams : -1);
    if (fmt) avformat_close_input(&fmt);
}
#endif
```

- [ ] **Step 3: Configure with the flag ON and build**

Run:
```
cmake -S . -B out -DTANKOBAN_INPROCESS_POC=ON
build_check.bat
```
Expected: `BUILD OK`. If link fails with unresolved `av*` symbols, return to Task 0.1 (import libs). If headers not found, fix the include path.

- [ ] **Step 4: Deploy FFmpeg DLLs beside `out/Tankoban.exe`**

The sidecar already copies these to `out/` via `build_and_run.bat`; confirm `out/avcodec-62.dll` etc. exist (they do post-`build_and_run`). No action if present.

- [ ] **Step 5: Commit**

```bash
git add cmake/TankobanSources.cmake src/ui/player/InProcessProbe.cpp
git commit -m "[Agent 3, POC P0.2] FFmpeg MSVC in-process link probe builds + opens test file"
```

**GATE 0:** `build_check.bat` = BUILD OK with the POC flag on. If this cannot be made to pass, STOP and report — the MSVC/FFmpeg link is the make-or-break prerequisite, and that finding alone is worth surfacing before any further work.

---

## Phase 1 — In-process video to screen (video-only, visual gate)

### Task 1.1: Add the 6 sidecar sources to the main target (POC-gated)

**Files:**
- Modify: `cmake/TankobanSources.cmake`

- [ ] **Step 1: Add the sources inside the existing `if(TANKOBAN_INPROCESS_POC)` block**

```cmake
target_sources(Tankoban PRIVATE
    native_sidecar/src/ring_buffer.cpp
    native_sidecar/src/av_sync_clock.cpp
    native_sidecar/src/video_decoder.cpp
    native_sidecar/src/audio_decoder.cpp
    native_sidecar/src/wasapi_output.cpp
    native_sidecar/src/volume_control.cpp)
target_include_directories(Tankoban PRIVATE native_sidecar/src)
```

- [ ] **Step 2: Build; resolve compile fallout**

Run: `build_check.bat`
Expected likely fallout: `video_decoder.cpp` references `subtitle_renderer.h`, `filter_graph.h`, `gpu_renderer.h`, `d3d11_presenter.h`, `overlay_shm.h`. The decoder accepts these as **nullable constructor params** (see `native_sidecar/src/main.cpp:928` — `sub_ren`, `vfilt`, `gpu_ren` may be null). For the POC, either (a) add the headers' source files too if they are light, or (b) compile `video_decoder.cpp` with the optional features `#ifdef`-guarded off. Prefer (a) only for headers that have no heavy deps; document which sources were added in the commit.
Iterate until `BUILD OK`.

- [ ] **Step 3: Commit**

```bash
git add cmake/TankobanSources.cmake
git commit -m "[Agent 3, POC P1.1] compile sidecar decode/audio/sync sources into main app (POC-gated)"
```

### Task 1.2: `InProcessPlayer` — video-only path feeding FrameCanvas

**Files:**
- Create: `src/ui/player/InProcessPlayer.h`
- Create: `src/ui/player/InProcessPlayer.cpp`

- [ ] **Step 1: Write the header**

`src/ui/player/InProcessPlayer.h`:
```cpp
#pragma once
#ifdef TANKOBAN_INPROCESS_POC
#include <QObject>
#include <memory>
#include <vector>
#include <cstdint>

class VideoDecoder;
class AudioDecoder;
class AVSyncClock;
class WasapiOutput;
class VolumeControl;
class FrameRingWriter;
class FrameCanvas;

// Minimal in-process player POC. Owns an in-process frame ring (plain heap
// buffer, NOT named shared memory), an in-process VideoDecoder writing into it,
// and feeds FrameCanvas by attaching a reader over the SAME buffer. Audio +
// clock added in Phase 2. Local file only; no subs/HDR/seek/tracks.
class InProcessPlayer : public QObject {
    Q_OBJECT
public:
    explicit InProcessPlayer(FrameCanvas* canvas, QObject* parent = nullptr);
    ~InProcessPlayer() override;

    void openFile(const QString& path);   // probe + start video (+audio in P2)
    void stop();

private:
    FrameCanvas* m_canvas;                 // not owned
    std::vector<uint8_t> m_ringBuffer;     // in-process backing for the frame ring
    std::unique_ptr<FrameRingWriter> m_ringWriter;
    std::unique_ptr<VideoDecoder> m_videoDecoder;
    std::unique_ptr<AVSyncClock> m_clock;
    // Phase 2 adds: m_audioDecoder, m_wasapi, m_volume
    int m_slotBytes = 0;
};
#endif
```

- [ ] **Step 2: Write the implementation (video-only)**

`src/ui/player/InProcessPlayer.cpp`:
```cpp
#ifdef TANKOBAN_INPROCESS_POC
#include "ui/player/InProcessPlayer.h"
#include "ui/player/FrameCanvas.h"
#include "ring_buffer.h"
#include "av_sync_clock.h"
#include "video_decoder.h"
#include <cstdio>

// Slot sizing mirrors the sidecar: BGRA at the source dimensions. The POC
// allocates for 1920x1080 (the One Piece fixture); a production path would size
// from the probe. DECODE_RING_SLOT_COUNT mirrors native_sidecar/src/main.cpp.
static constexpr int kRingSlots = 4;

InProcessPlayer::InProcessPlayer(FrameCanvas* canvas, QObject* parent)
    : QObject(parent), m_canvas(canvas) {}

InProcessPlayer::~InProcessPlayer() { stop(); }

void InProcessPlayer::openFile(const QString& path) {
    const int w = 1920, h = 1080;          // POC fixed; see note above
    m_slotBytes = w * h * 4;               // BGRA
    m_ringBuffer.assign(ring_buffer_size(kRingSlots, m_slotBytes), 0);

    m_ringWriter = std::make_unique<FrameRingWriter>(
        m_ringBuffer.data(), kRingSlots, m_slotBytes);

    m_clock = std::make_unique<AVSyncClock>();

    // Decoder writes BGRA frames into the in-process ring. sub/filter/gpu = null
    // (POC: no subs, no filters, no zero-copy -> forces the path=cpu branch).
    auto on_event = [](const std::string& ev, const std::string& detail) {
        std::fprintf(stderr, "INPROC_VID_EVENT %s %s\n", ev.c_str(), detail.c_str());
    };
    m_videoDecoder = std::make_unique<VideoDecoder>(
        m_ringWriter.get(), on_event, m_clock.get(), m_slotBytes,
        /*sub_ren=*/nullptr, /*vfilt=*/nullptr, /*gpu_ren=*/nullptr);

    // Attach FrameCanvas to a reader over the SAME in-process buffer. Mirror how
    // VideoPlayer builds m_reader before attachShm (see VideoPlayer.cpp ~1104),
    // but construct the reader over m_ringBuffer.data() instead of a named SHM
    // segment. (Reader ctor type confirmed against ring_buffer.h during P1.2.)
    m_canvas->attachInProcessRing(
        m_ringBuffer.data(), kRingSlots, m_slotBytes);   // added in Task 1.3

    // best video stream = -1; start from 0.0
    m_videoDecoder->start(path.toStdString(), 0.0, /*video_stream_index=*/-1);
}

void InProcessPlayer::stop() {
    if (m_videoDecoder) m_videoDecoder.reset();   // dtor joins decode thread
    m_ringWriter.reset();
    m_clock.reset();
}
#endif
```

- [ ] **Step 3: Build**

Run: `build_check.bat`
Expected: `BUILD OK` once Task 1.3's `attachInProcessRing` exists. Build after 1.3.

### Task 1.3: `FrameCanvas::attachInProcessRing` — in-process feed entry

**Files:**
- Modify: `src/ui/player/FrameCanvas.h` (declare), `src/ui/player/FrameCanvas.cpp` (define)

- [ ] **Step 1: Read the existing `attachShm` path first**

Read `FrameCanvas::attachShm` and the `m_reader` member it stores (the type consumed from `VideoPlayer.cpp:1104`). The new method must reuse the SAME reader + waitable-swapchain present loop — the ONLY difference is the backing buffer is a raw pointer, not a mapped named segment.

- [ ] **Step 2: Add the method**

In `FrameCanvas.h`, beside `attachShm`:
```cpp
#ifdef TANKOBAN_INPROCESS_POC
    // POC: attach to a frame ring backed by a plain in-process buffer instead of
    // a named shared-memory segment. Reuses the exact reader + present path.
    void attachInProcessRing(void* buf, int slotCount, int slotBytes);
#endif
```
In `FrameCanvas.cpp`, implement by constructing the same reader type `attachShm` uses, over `buf` (mirroring `attachShm`'s body minus the SHM `open`), then store it in `m_reader` and kick the existing present loop. (Exact reader ctor confirmed in Step 1.)

- [ ] **Step 3: Build**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/player/InProcessPlayer.h src/ui/player/InProcessPlayer.cpp src/ui/player/FrameCanvas.h src/ui/player/FrameCanvas.cpp
git commit -m "[Agent 3, POC P1.2-1.3] InProcessPlayer video path + FrameCanvas in-process ring feed"
```

### Task 1.4: Flag fork in VideoPlayer

**Files:**
- Modify: `src/ui/player/VideoPlayer.cpp:185`

- [ ] **Step 1: Fork the backend construction behind the flag**

Replace `m_backend = new SidecarProcess(this);` (VideoPlayer.cpp:185) with:
```cpp
#ifdef TANKOBAN_INPROCESS_POC
    if (qEnvironmentVariableIsSet("TANKOBAN_INPROCESS_POC")) {
        // POC path: in-process player feeding m_canvas directly. m_backend stays
        // null; the POC owns its own decode/present. Created after m_canvas
        // exists (see openFile wiring in Step 2).
        m_inProcessPoc = true;
    } else
#endif
    {
        m_backend = new SidecarProcess(this);
    }
```
Add `bool m_inProcessPoc = false;` and `#ifdef TANKOBAN_INPROCESS_POC InProcessPlayer* m_inproc = nullptr; #endif` to the header.

- [ ] **Step 2: Route openFile + canvas creation through the POC when active**

Where `VideoPlayer` opens a file and where `m_canvas = new FrameCanvas(this)` (≈1369): when `m_inProcessPoc`, after the canvas exists, construct `m_inproc = new InProcessPlayer(m_canvas, this);` and call `m_inproc->openFile(path)` instead of the sidecar open. Guard all sidecar-only calls (`m_backend->...`) with `if (m_backend)`.

- [ ] **Step 3: Build**

Run: `build_check.bat`
Expected: `BUILD OK`. Resolve any `m_backend` null-deref guards revealed by the compiler/linker.

- [ ] **Step 4: Commit**

```bash
git add src/ui/player/VideoPlayer.cpp src/ui/player/VideoPlayer.h
git commit -m "[Agent 3, POC P1.4] VideoPlayer flag fork — in-process POC path when TANKOBAN_INPROCESS_POC set"
```

**GATE 1 (visual, Agent 3 drives):** Build with the flag, launch with `TANKOBAN_INPROCESS_POC=1`, play One Piece S01E01. Expected: **video renders on screen** (no audio yet, may run fast — no clock pacing until Phase 2). If the picture appears and advances, the in-process decode→FrameCanvas path works. Capture a screenshot via pywinauto for evidence.

---

## Phase 2 — Audio + A/V sync

### Task 2.1: Add audio + clock to InProcessPlayer

**Files:**
- Modify: `src/ui/player/InProcessPlayer.{h,cpp}`

- [ ] **Step 1: Add members + construct audio stack**

In `openFile`, before starting the video decoder, add:
```cpp
#include "audio_decoder.h"
#include "wasapi_output.h"
#include "volume_control.h"
// members: std::unique_ptr<WasapiOutput> m_wasapi;
//          std::unique_ptr<VolumeControl> m_volume;
//          std::unique_ptr<AudioDecoder> m_audioDecoder;

m_volume = std::make_unique<VolumeControl>();
m_wasapi = std::make_unique<WasapiOutput>();
// open() WASAPI per the sidecar's usage in native_sidecar/src/main.cpp (mirror
// the open args used there for the audio device).
auto on_audio = [](const std::string& ev, const std::string& detail) {
    std::fprintf(stderr, "INPROC_AUD_EVENT %s %s\n", ev.c_str(), detail.c_str());
};
m_audioDecoder = std::make_unique<AudioDecoder>(
    m_clock.get(), m_volume.get(), on_audio,
    /*audio_filter=*/nullptr, m_wasapi.get());
m_audioDecoder->start(path.toStdString(), 0.0, /*audio_stream_index=*/-1);
```
Audio is the master clock (drives `AVSyncClock`); the video decoder already waits on the clock in its `path=cpu` branch — so adding audio automatically paces video correctly.

- [ ] **Step 2: Stop audio in `stop()`** — reset `m_audioDecoder`, then `m_wasapi`, then `m_volume`, then video, then clock (audio first so the clock stops advancing before video teardown).

- [ ] **Step 3: Build**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/player/InProcessPlayer.h src/ui/player/InProcessPlayer.cpp
git commit -m "[Agent 3, POC P2.1] InProcessPlayer audio + AVSyncClock — paced A/V in-process"
```

**GATE 2 (visual + audible, Agent 3 drives):** Launch with the flag, play One Piece S01E01. Expected: **picture and sound, in sync, at correct speed.** Passive-read the in-process video decoder's `[PERF]`/drop lines from the live log (repo-root `sidecar_debug_live.log` is the *sidecar's*; the in-process decoder's stderr goes to the app's stderr — capture it). Confirm no multi-second `behind=` drop-bursts.

---

## Phase 3 — Smoke gate (the decision)

### Task 3.1: Telemetry capture harness (passive, no bridge polling)

**Files:**
- Use existing logs; no code unless the in-process `[PERF]` isn't reaching a readable sink.

- [ ] **Step 1: Ensure the in-process decoder's `[PERF]` line is captured**

The reused `video_decoder.cpp` emits `[PERF] ... path=cpu`. In-process it writes to the app's stderr. Confirm `build_and_run.bat`'s launched `Tankoban.exe` stderr is captured to a file (if not, add a `freopen` of stderr under the POC flag to `out/inproc_poc.log`). Passive `Select-String '\[PERF\]'` on that file — **no `tankoctl` calls during playback** (UI-thread contention perturbs the result, per 2026-05-31 finding).

### Task 3.2: Hemanth smoke — the verdict

- [ ] **Step 1: Agent 3 builds + launches**

```
build_and_run.bat   # (configured with -DTANKOBAN_INPROCESS_POC=ON), with TANKOBAN_INPROCESS_POC=1 in env
```
Agent 3 confirms the app is up and the POC path is active (look for `INPROC_PROBE` / `INPROC_VID_EVENT` lines).

- [ ] **Step 2: Hemanth plays One Piece S01E01 for ~30–60s and reports — smooth or stutter?**

This is THE gate. His eyes decide. Agent 3 cross-reads the passive `[PERF]`/drift telemetry alongside.

- [ ] **Step 3: Record the verdict + telemetry in a findings note**

Create `agents/audits/inprocess_player_poc_result_2026-06-01.md` with: Hemanth's verdict (verbatim), the passive `[PERF]` summary, A/V drift behavior, and the decision.

### Task 3.3: Decision

- [ ] **If SMOOTH:** in-process is confirmed on UHD 620. Write the follow-on brainstorm/spec for the **full** in-process player (subtitles via libass, HDR via libplacebo, seek/tracks, OpenGL present for Mac/Linux) as the player half of CROSS_PLATFORM_BACKEND. The POC code stays flag-gated until the full path supersedes it.
- [ ] **If STILL STUTTERS:** the process boundary was not the (sole) cause. Document the in-process `[PERF]`/drift evidence, keep the sidecar as default, and pivot the investigation (e.g. FrameCanvas present cadence on Intel integrated GPUs, or a file-specific decode issue per the 2026-05-05 "stutter is file-specific" finding). Cost incurred: a flag-gated POC, no rewrite, fully revertible.

- [ ] **Commit the findings note**

```bash
git add agents/audits/inprocess_player_poc_result_2026-06-01.md
git commit -m "[Agent 3, POC P3] in-process player POC result + decision"
```

---

## Self-review (against the spec)

- **Spec coverage:** problem/evidence → "Why audio is in the POC" + Phase 0 rationale; goal/decision-gate → Phase 3; Option A (in-process decode → FrameCanvas) → Phases 1-2; non-goals (no subs/HDR/seek/stream/cross-platform) → POC scope held throughout; success = Hemanth's eyes + passive telemetry → Gates 1/2 + Task 3.2; open question (MSVC/MinGW ffmpeg seam) → Phase 0 (the make-or-break prerequisite, gated). ✅ All covered.
- **Placeholders:** none — every code step shows real types (`FrameRingWriter(buf,slots,bytes)`, `VideoDecoder(ringWriter,on_event,clock,slotBytes,nullptr,nullptr,nullptr)`, `AudioDecoder(clock,volume,on_event,nullptr,wasapi)`, `FrameCanvas::attachInProcessRing`). Two ctor specifics (the `FrameRingReader` ctor and the `WasapiOutput::open` args) are explicitly instructed to be confirmed against the named existing call sites during their tasks — these are *read-the-adjacent-code* steps, not invent-it placeholders.
- **Type consistency:** `m_ringBuffer`/`m_ringWriter`/`m_clock`/`m_videoDecoder`/`m_audioDecoder` names consistent across H and CPP; `TANKOBAN_INPROCESS_POC` flag name consistent across CMake, sources, and env gate; `attachInProcessRing` named identically in 1.3 and used in 1.2.

## Execution note

Phase 0 GATE is the true risk. If the FFmpeg-against-MSVC link cannot be made to pass, that finding alone — surfaced cheaply, before any orchestration — is a valuable result and changes the full-build approach (e.g. the full in-process build may need to vendor FFmpeg differently). Do not proceed past GATE 0 on a red build.
