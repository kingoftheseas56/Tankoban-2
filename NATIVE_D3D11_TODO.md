# Native D3D11 Video Pipeline — Path B Refactor

## Goal

Replace `FrameCanvas` (currently a `QRhiWidget` subclass — closed box, no vsync visibility, no swap chain access) with a custom `QWidget` that owns its own `ID3D11Device`, `IDXGISwapChain`, and present cycle. This unlocks:
- Real `IDXGISwapChain::GetFrameStatistics` vsync timing → makes display-resample possible
- True D3D11 device sharing with the sidecar (skip the cross-process NT-handle dance)
- Proper exclusive fullscreen
- Real HDR output (DXGI_FORMAT_R10G10B10A2_UNORM swap chain)
- Frame pacing telemetry

## Strategy

**Build the new widget side-by-side with FrameCanvas, swap when ready.** Don't mutate FrameCanvas in place — too risky. Keep the working pipeline intact while we build the replacement, then cut over in a single migration step.

**Each task below is sized to be doable in ~30min–2hr with a verifiable outcome.** Don't move to the next task until the current one demonstrably works.

**Working file**: a new `src/ui/player/D3D11Widget.h/cpp` (eventually renamed to `FrameCanvas.*` after migration).

---

## Phase 1: Foundation — bare D3D11 widget that paints something

### 1.1 — D3D11Widget skeleton class
Create `src/ui/player/D3D11Widget.h` and `.cpp`. Extends `QWidget`. Override `showEvent`, `resizeEvent`, `paintEvent` (no-op for now). Member vars for `ID3D11Device*`, `ID3D11DeviceContext*`, `IDXGISwapChain1*` — all null initially. Constructor sets `WA_PaintOnScreen`, `WA_NativeWindow`, `WA_NoSystemBackground`.

**Done when**: class compiles, can be instantiated, `parentWidget()` returns null without crashing.

### 1.2 — D3D11 device creation
In `showEvent`, call `D3D11CreateDevice` with `D3D_DRIVER_TYPE_HARDWARE`, `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. Get the immediate context. Log "D3D11Widget: device created".

**Done when**: log line appears when widget is shown; no HRESULT errors.

### 1.3 — Swap chain creation bound to widget HWND
After device, get `IDXGIFactory2` from the device's adapter. Call `CreateSwapChainForHwnd` with `winId()`, `DXGI_FORMAT_B8G8R8A8_UNORM`, 2 buffers, `DXGI_SCALING_STRETCH`, `DXGI_SWAP_EFFECT_FLIP_DISCARD`. Log "D3D11Widget: swap chain created WxH".

**Done when**: log line shows correct widget dimensions; swap chain pointer non-null.

### 1.4 — Clear-screen + present loop
Add a `QTimer` at 16ms. On tick: get back buffer from swap chain, create render target view, `ClearRenderTargetView` to magenta, call `swapChain->Present(1, 0)` for vsync.

**Done when**: widget shows a magenta rectangle. Resizing doesn't crash (handle in resizeEvent: release back buffer, call `ResizeBuffers`).

### 1.5 — Standalone test harness
Create `tests/d3d11widget_test.cpp` (or just temporarily replace MainWindow's central widget). Just needs to instantiate D3D11Widget, show it, see magenta.

**Done when**: visible magenta widget in a Qt window. Phase 1 complete.

---

## Phase 2: Shader pipeline — render a textured quad

### 2.1 — HLSL shaders
Create `resources/shaders/video_d3d11.hlsl` with a vertex shader (transform fullscreen quad) and pixel shader (sample texture). Use `D3DCompile` at runtime (link `d3dcompiler.lib`) so no build-step changes needed.

**Done when**: shader compiles via `D3DCompile`; we have `ID3D11VertexShader*` and `ID3D11PixelShader*`.

### 2.2 — Vertex buffer for fullscreen quad
Define vertex struct `{ float pos[2]; float uv[2]; }`. Create `ID3D11Buffer` with 4 vertices forming a triangle strip. Create `ID3D11InputLayout` matching the vertex shader.

**Done when**: buffer + layout created; no errors.

### 2.3 — Sampler + rasterizer state
Create `ID3D11SamplerState` (linear filter, clamp). Default rasterizer state (no cull). `ID3D11BlendState` with blending disabled.

**Done when**: state objects created.

### 2.4 — Render a checkerboard test texture
Create a 256×256 BGRA texture in code (or load any PNG). Create `ID3D11ShaderResourceView`. In render: bind everything, set viewport, draw 4 vertices.

**Done when**: widget shows the test texture instead of magenta. Resize keeps it correctly aspected.

### 2.5 — Aspect-ratio viewport (parity with FrameCanvas)
Port the aspect math from `FrameCanvas::render()` lines ~210-227 (compute viewport rect that letterboxes content to widget bounds).

**Done when**: test texture stays at correct aspect when widget is resized.

---

## Phase 3: Color processing (parity with current shader)

### 3.1 — ColorParams cbuffer
Define HLSL `cbuffer ColorParams : register(b0)` matching the existing video.frag layout (brightness, contrast, saturation, gamma + reserved fields). Create `ID3D11Buffer` (USAGE_DYNAMIC) for it.

**Done when**: cbuffer is bindable; updated via `Map`/`Unmap` per frame.

### 3.2 — Port video.frag to HLSL
Take the GLSL from `resources/shaders/video.frag` and rewrite the pixel shader body in HLSL. Same math: brightness, contrast around 0.5 pivot, luminance-preserving saturation, gamma.

**Done when**: textured output looks identical to current FrameCanvas output (compare side-by-side).

### 3.3 — `setColorParams` public API
Add the same signature as FrameCanvas: `void setColorParams(float b, float c, float s, float g)`. Stash in a `ColorParams` struct member, upload to cbuffer in render.

**Done when**: changing values via the API visibly affects the rendered texture.

---

## Phase 4: SHM frame consumption (parity with FrameCanvas SHM path)

### 4.1 — `attachShm` / `detachShm` / `startPolling` / `stopPolling`
Same signatures as FrameCanvas. Hold `ShmFrameReader*`. `startPolling` enables the QTimer; `stopPolling` disables it.

**Done when**: API matches FrameCanvas; can be plugged into VideoPlayer the same way.

### 4.2 — SHM read + texture upload in render
At top of render: `m_reader->readLatest()`. If new frame, recreate `ID3D11Texture2D` if dimensions changed, `UpdateSubresource` upload BGRA pixels. Then render.

**Done when**: video plays — same visible quality as old FrameCanvas's SHM path.

### 4.3 — Consumer FID writeback
Call `m_reader->writeConsumerFid(f.frameId)` after upload. Same as old FrameCanvas.

**Done when**: log file shows consumer FID being acknowledged correctly by sidecar (no observable effect, just consistency with old behavior).

---

## Phase 5: D3D11 shared texture import (parity with Holy Grail)

### 5.1 — `attachD3D11Texture` / `detachD3D11Texture`
Same signatures as FrameCanvas. Stash NT handle + dimensions. Set a "pending import" flag.

**Done when**: API matches FrameCanvas.

### 5.2 — Open + wrap in render
On pending import: `device->QueryInterface(ID3D11Device1)`, `OpenSharedResource1(handle)`, get `ID3D11Texture2D*`, create `ID3D11ShaderResourceView`. Use as the source texture (skip SHM upload entirely while imported texture is active).

**Done when**: zero-copy path works, sidecar's `set_zero_copy_active` stays on, video plays smoothly. **Critical milestone — this proves device-side D3D11 sharing works.**

---

## Phase 6: Vsync timing — the actual Phase 0 win we needed

### 6.1 — `IDXGISwapChain::GetFrameStatistics` integration
Add `swapChain->GetFrameStatistics(&fs)` call to the existing `VsyncTimingLogger`. Now we own the swap chain, this should return valid stats every frame.

**Done when**: a fresh F12 → 60s → CSV run shows `DXGI valid: NNNN` matching the sample count (not 0).

### 6.2 — Re-verify display-resample feasibility
Run `python tools/analyze_vsync.py _vsync_timing.csv` again. The analyzer should report DXGI mean matching Qt's render mean within 0.1% and stddev < 1ms.

**Done when**: PASS verdict from the analyzer. Now we genuinely have the vsync signal display-resample needs.

---

## Phase 7: Migration — cut over from FrameCanvas to D3D11Widget

### 7.1 — Add toggle in VideoPlayer
Temporary bool `m_useNewWidget` (or env var or build flag). Construct either `FrameCanvas` or `D3D11Widget` based on it. Both API-compatible at this point.

**Done when**: can switch between old and new widget at compile time without breaking anything else.

### 7.2 — Manual A/B test all features
With new widget: play HEVC 10-bit, play H.264 SDR, switch tracks, toggle subs (PGS + text), seek, pause/resume, fullscreen, color filters, audio EQ, all keyboard shortcuts. Anything broken? Fix.

**Done when**: every existing feature works at least as well with new widget.

### 7.3 — Switch default to D3D11Widget
Flip the default. Old FrameCanvas still in tree, fallback path via toggle.

**Done when**: stock builds use new widget; old still available via toggle for safety.

### 7.4 — Bake-in period
Use only the new widget for at least 2-3 days of normal viewing. Catch edge cases (multi-monitor, sleep/wake, GPU driver hiccups).

**Done when**: 2-3 days of stable use, no surprises.

### 7.5 — Delete old FrameCanvas
Remove old `FrameCanvas.h/cpp` (the QRhiWidget version). Rename `D3D11Widget.h/cpp` → `FrameCanvas.h/cpp` (and class). Update CMakeLists. Update VideoPlayer to remove the toggle.

**Done when**: clean tree, single FrameCanvas class, builds clean. **Phase 7 complete — refactor done.**

---

## Phase 8: Display-resample (the actual feature this enables)

> **Superseded 2026-04-14 by `PLAYER_POLISH_TODO.md`** — Kodi + OBS reference study reorganized and extended this into a 7-phase engine-polish plan (Phases 1–2 directly cover the original B-1/B-2/B-3 material; Phases 3–7 add HDR, audio polish, libass subtitles, error recovery, cleanup).

After phase 7 is verified, the original 4-6 week display-resample plan from earlier becomes feasible. Picks up where we left off:

- **Phase B-1**: Frame queue / future PTS visibility (sidecar exposes next 3-5 frame PTSes)
- **Phase B-2**: Audio tempo infrastructure (`scaletempo2` filter + `set_audio_speed` command)
- **Phase B-3**: Sync state machine (vsync count calc with error accumulation, audio speed adjustment)
- **Phase B-4**: Edge case handling (seek, pause, sleep/wake, fullscreen, multi-monitor)
- **Phase B-5**: Polish + defaults

These phases are still 4-6 weeks combined, but they're now actually achievable because we have real vsync timing.

---

## Estimates

| Phase | Tasks | Estimated time |
|-------|-------|----------------|
| 1. Foundation | 5 | 1 day |
| 2. Shader pipeline | 5 | 1 day |
| 3. Color processing | 3 | half day |
| 4. SHM consumption | 3 | half day |
| 5. Holy Grail import | 2 | half day |
| 6. Vsync timing | 2 | 2 hours |
| 7. Migration | 5 | 2-3 days (incl. bake-in) |
| **Subtotal** | **25 tasks** | **~1 week** |
| 8. Display-resample | (separate plan) | 4-6 weeks |

## Notes for implementation

- **No sidecar changes** in Phases 1-7. Sidecar already speaks D3D11 NT handle + SHM; both paths still supported by the new widget.
- **Use `D3DCompile` at runtime** to avoid adding fxc to the build. `d3dcompiler.lib` is a Windows SDK system lib.
- **Keep `VsyncTimingLogger` as-is** — only its data source improves (DXGI now valid).
- **Don't try to be too clever** with feature levels — `D3D_FEATURE_LEVEL_11_0` is sufficient for everything we need.
- **One task at a time, verify, commit.** Don't batch. The whole point is to keep the working state intact at every checkpoint.
