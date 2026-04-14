# Video Player Rendering Roadmap
## Agent 3 — Phased plan for color management, HDR, scaling, and subtitle rendering
## Reference: MPV (C:\Users\Suprabha\Downloads\mpv-master) + VLC (C:\Users\Suprabha\Downloads\vlc-master)

---

## Current State

- **Rendering:** QRhiWidget (D3D11 backend), fullscreen quad, identity fragment shader
- **Frame format:** BGRA8 from sidecar (YUV→RGB conversion happens on CPU in ffmpeg_sidecar)
- **Shaders:** 2 minimal GLSL 440 shaders (vertex passthrough + texture sample), compiled to .qsb
- **Subtitles:** QPainter-based QLabel, plain text only, no formatting
- **Filters:** FFmpeg-side only (yadif, eq brightness/contrast/sat, loudnorm)
- **Color management:** None. No color space metadata, no HDR, no tone mapping, no ICC profiles
- **Scaling:** Bilinear (default RHI sampler). No configurable scaling filters

---

## Phase 1 — GPU Color Pipeline (Foundation)

**Goal:** Move YUV→RGB conversion to GPU, add color space awareness, eliminate CPU conversion bottleneck.

**Why first:** Everything else (tone mapping, HDR, scaling) depends on having a proper GPU color pipeline. Currently the sidecar wastes CPU cycles converting YUV→BGRA before uploading. GPU-native YUV decoding is 2-4x faster for HD/4K.

### 1A. NV12/YUV Texture Upload

Change sidecar to output NV12 (or I420) instead of BGRA8. Upload Y and UV planes as separate textures.

**Reference:**
- MPV: `pass_upload_image()` in `video/out/gpu/video.c` — uploads per-plane textures
- VLC: `d3d11_quad.cpp` — dual render targets for Y/UV planes, `SAMPLE_NV12_TO_YUVA` shader macro

**Implementation:**
- Modify sidecar protocol: add `pixelFormat` field to `firstFrame` event (default "nv12", fallback "bgra")
- Modify `ShmFrameReader` to handle NV12 layout (Y plane full res, UV plane half res)
- Create two `QRhiTexture` objects in FrameCanvas: `m_texY` (R8, full res) and `m_texUV` (RG8, half res)
- Write new fragment shader: sample Y and UV, apply 3x3 color matrix for YUV→RGB

**Files:** `FrameCanvas.h/.cpp`, `ShmFrameReader.h/.cpp`, sidecar code, `resources/shaders/video_nv12.frag`

### 1B. Color Space Metadata

Pass color space info from sidecar to renderer so the correct YUV→RGB matrix is used.

**Reference:**
- MPV: `struct mp_csp_params` in `video/csputils.h` — color primaries, transfer, matrix coefficients
- VLC: `PS_CONSTANT_BUFFER` with `Colorspace[4x3]` matrix in constant buffer

**Implementation:**
- Sidecar extracts from ffmpeg: `color_primaries`, `color_trc`, `color_space`, `color_range` from AVFrame
- Send as fields in `firstFrame` and `mediaInfo` events
- FrameCanvas stores current color space, selects correct YUV→RGB matrix
- Matrices: BT.601 (SD), BT.709 (HD), BT.2020 (UHD) — precomputed as uniform buffer

**Files:** sidecar code, `FrameCanvas.h/.cpp`, new `ColorSpace.h` utility

### 1C. Transfer Function (Gamma) Handling

Apply correct EOTF (electro-optical transfer function) in the fragment shader for accurate display.

**Reference:**
- MPV: `pass_linearize()` / `pass_delinearize()` in `video/out/gpu/video_shaders.c` — handles sRGB, BT.1886, PQ, HLG, S-Log
- VLC: `SRC_TO_LINEAR` / `LINEAR_TO_DST` macros in `d3d_dynamic_shader.c`

**Implementation:**
- Fragment shader gets a `transferFunction` uniform (enum: sRGB, BT709, PQ, HLG)
- Linearization step after YUV→RGB conversion
- Delinearization step before output (apply display gamma)
- For SDR content on SDR display: BT.1886 decode → sRGB encode (standard path)

**Files:** `resources/shaders/video_color.frag`, `FrameCanvas.cpp`

---

## Phase 2 — HDR & Tone Mapping

**Goal:** Play HDR10 and HLG content on SDR displays without washed-out colors. Play SDR content on HDR displays without clipping.

**Depends on:** Phase 1 (color pipeline must exist to insert tone mapping between linearize/delinearize)

### 2A. Tone Mapping Algorithms

Implement shader-based tone mapping for HDR→SDR conversion.

**Reference:**
- MPV: `pass_tone_map()` in `video_shaders.c` — 12 algorithms (Hable, Reinhard, Mobius, BT.2390, clip, etc.)
- VLC: Hable filmic operator in `d3d_dynamic_shader.c` — single algorithm, simpler but effective

**Implementation — start with 3 algorithms:**
1. **Hable (filmic)** — VLC's choice, good default, warm look
2. **Reinhard** — simple, predictable, good for UI preview
3. **BT.2390** — ITU standard, most "correct" for broadcast content

All implemented as GLSL functions in the fragment shader, selected via uniform.

**Key constants:**
```
PQ_M1 = 2610/4096 * 1/4
PQ_M2 = 2523/4096 * 128
PQ_C1 = 3424/4096
PQ_C2 = 2413/4096 * 32
PQ_C3 = 2392/4096 * 32
MP_REF_WHITE = 203.0 cd/m2
```

**Files:** `resources/shaders/video_tonemap.frag` (or inline in main frag), `FrameCanvas.cpp`

### 2B. HDR Peak Detection (Compute Shader)

Detect content peak luminance per-frame for adaptive tone mapping.

**Reference:**
- MPV: Compute shader with shared memory atomics, exponential moving average with decay rate, scene change detection

**Implementation:**
- Compute shader dispatched before render pass
- Reads luminance from Y plane (or linearized RGB)
- Outputs: average luminance, peak luminance (percentile-based)
- Stored in SSBO, read by fragment shader next frame (1-frame delay is acceptable)
- Decay rate configurable (default 100ms half-life)

**Files:** `resources/shaders/peak_detect.comp`, `FrameCanvas.cpp`

### 2C. HDR Output (Swapchain Negotiation)

For users with HDR monitors: output PQ/BT.2020 directly instead of tone mapping down.

**Reference:**
- VLC: `dxgi_swapchain.cpp` — `CheckColorSpaceSupport()`, HDR10 metadata via `IDXGISwapChain4`, NVIDIA True HDR GUID

**Implementation:**
- Query display HDR capability via DXGI
- If HDR available: configure swapchain for `DXGI_FORMAT_R10G10B10A2_UNORM` + `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`
- Send HDR10 metadata (MaxContentLightLevel, MaxFrameAverageLightLevel) from sidecar
- Skip tone mapping when output is HDR-capable
- UI toggle: Auto / Force SDR / Force HDR (like VLC's `hdr_Auto/Never/Always`)

**Files:** `FrameCanvas.cpp`, possibly Qt RHI swapchain configuration

---

## Phase 3 — Scaling Filters

**Goal:** Replace default bilinear sampling with configurable high-quality scaling (Lanczos, Spline, EWA).

**Depends on:** Phase 1 (multi-texture pipeline must exist for intermediate render targets)

### 3A. Separable Kernel Scaling (Lanczos, Spline)

Two-pass scaling: horizontal then vertical, using LUT textures for kernel coefficients.

**Reference:**
- MPV: `pass_sample_separated_gen()` in `video_shaders.c`, `filter_kernels.c` for kernel math
- Filter list: Lanczos (2-4 taps), Spline16/36/64, Mitchell, Catmull-Rom, Bicubic

**Implementation:**
- Generate 1D LUT texture with kernel coefficients (done once per filter change)
- Two render passes: horiz scale to intermediate FBO, then vert scale to output
- Anti-ringing clamp option (MPV's `clamp` parameter)
- Start with: Bilinear (default), Lanczos3, Spline36, Mitchell

**Files:** `resources/shaders/video_scale.frag`, new `ScalingKernel.h/.cpp`, `FrameCanvas.cpp`

### 3B. EWA (Polar) Scaling

Single-pass 2D scaling for highest quality upscaling (EWA Lanczos).

**Reference:**
- MPV: `pass_sample_polar()` — circular sampling, jinc-based kernels, `EWA_LANCZOS` / `EWA_LANCZOSSHARP`

**Implementation:**
- 2D LUT texture for polar kernel weights
- Single pass with dynamic tap count based on scale ratio
- Expensive but produces best quality for anime/animation content
- Only used when user explicitly selects "Quality" preset

**Files:** `resources/shaders/video_ewa.frag`, `ScalingKernel.h/.cpp`

### 3C. Chroma Upscaling

Separate scaler for chroma planes (UV upsampling to luma resolution).

**Reference:**
- MPV: `SCALER_CSCALE` — dedicated chroma scaler, typically Lanczos2 or Mitchell

**Implementation:**
- Apply selected chroma scaler when converting NV12 UV plane to full resolution
- Prevents chroma aliasing artifacts visible on sharp color edges
- Default: Mitchell (good balance). Option: Lanczos2, Spline36

**Files:** Fragment shader uniforms, `FrameCanvas.cpp`

---

## Phase 4 — Subtitle Rendering

**Goal:** Replace plain-text QPainter subtitles with proper formatted rendering (ASS/SSA, bitmap subs).

**Depends on:** Phase 1 (subtitle compositing pass needs the multi-pass infrastructure)

### 4A. ASS/SSA Text Rendering via libass

Integrate libass for styled text subtitle rendering (bold, italic, colors, outline, shadow, animations).

**Reference:**
- MPV: `sub/sd_ass.c` + `sub/osd_libass.c` — libass produces A8 alpha bitmaps with per-surface blend color
- MPV: `video/out/gpu/osd.c` — uploads bitmaps to GPU texture, renders as alpha-blended quads

**Implementation:**
- Link libass (C library, ~200KB, no heavy dependencies beyond FreeType/HarfBuzz/FriBidi)
- Sidecar sends raw ASS events instead of pre-rendered text
- libass renders to A8 bitmap arrays with position and color info
- Upload bitmaps to GPU texture atlas (pack multiple sub bitmaps into one texture)
- Render as alpha-blended quads in final compositing pass
- Keeps: outline, shadow, blur, color, animation, karaoke, drawing commands

**Files:** new `SubtitleRenderer.h/.cpp`, `FrameCanvas.cpp` (compositing pass), CMakeLists.txt (link libass)

### 4B. Bitmap Subtitle Support (PGS, VobSub)

Render image-based subtitles from Blu-ray (PGS) and DVD (VobSub).

**Reference:**
- MPV: `SUBBITMAP_BGRA` format — premultiplied alpha BGRA bitmaps, rendered as textured quads
- VLC: `modules/codec/spudec/` — VobSub decoder

**Implementation:**
- Sidecar decodes PGS/VobSub to BGRA bitmaps, sends via shared memory or base64 in JSON
- Upload bitmap to GPU texture
- Render as single alpha-blended quad at specified position
- No text layout needed — bitmaps are pre-rendered by the subtitle format

**Files:** `SubtitleRenderer.h/.cpp`, sidecar code

### 4C. Subtitle Color Space Awareness

Render subtitles in the correct color space to avoid color mismatch with video.

**Reference:**
- MPV: `sub_bitmaps.video_color_space` flag — determines if subtitle bitmaps match video color space

**Implementation:**
- Subtitles are typically sRGB
- When video is HDR (PQ/BT.2020), subtitle colors need inverse tone mapping or explicit sRGB→linear conversion
- Apply in the compositing shader: linearize subtitle color before blending with HDR video

**Files:** Fragment shader compositing pass

---

## Phase 5 — GPU Filters & Post-Processing

**Goal:** Move filters from CPU (ffmpeg) to GPU (real-time, no re-encode delay).

**Depends on:** Phase 1 (shader infrastructure), Phase 3 (intermediate FBOs)

### 5A. Color Correction (GPU-side)

Replace ffmpeg `eq` filter with real-time GPU adjustments.

**Implementation:**
- Brightness/contrast/saturation/hue as uniform values in fragment shader
- Applied in linear space (after EOTF, before OETF) for perceptually correct results
- Instant response — no 300ms debounce needed since GPU is real-time
- Remove corresponding ffmpeg filter path

### 5B. Dithering

Reduce banding artifacts when quantizing from higher bit depth to 8-bit display.

**Reference:**
- MPV: Ordered dithering with blue noise texture, error diffusion via compute shader

**Implementation:**
- Blue noise texture (64x64) loaded as resource
- Applied in fragment shader as final step before output
- Adds sub-LSB noise to break up banding in gradients
- Especially important for HDR→SDR tone mapped content

### 5C. Sharpening

Adaptive sharpening for upscaled content.

**Implementation:**
- Unsharp mask or CAS (AMD FidelityFX Contrast Adaptive Sharpening)
- Applied after scaling pass
- Configurable strength (0.0-1.0)

---

## Phase 6 — Hardware Decode Integration

**Goal:** Use GPU hardware decoders (DXVA2/D3D11VA) for zero-copy decode → render pipeline.

**Depends on:** Phase 1 (NV12 texture support must exist since HW decoders output NV12)

### 6A. D3D11VA Hardware Decode

Decode video frames directly to D3D11 textures — zero CPU involvement.

**Reference:**
- MPV: `video/out/d3d11/hwdec_d3d11va.c` — maps decoded D3D11 texture directly to render pipeline
- VLC: `modules/video_chroma/d3d11_fmt.h` — DXGI format handling for HW decode surfaces

**Implementation:**
- Use ffmpeg's `hwaccel=d3d11va` in sidecar
- Decoded frames are D3D11 textures (NV12 format)
- Share texture handle via DXGI shared handle (same pattern as Holy Grail)
- FrameCanvas binds the shared texture directly — no copy, no upload
- Eliminates: CPU decode, CPU→GPU upload, shared memory ring buffer overhead

**Impact:** 10-50x decode performance improvement for 4K content. GPU memory only.

### 6B. Zero-Copy Pipeline

Remove shared memory entirely for HW decode path.

**Implementation:**
- Sidecar creates D3D11 device, decodes to texture
- Passes DXGI shared handle to main process
- FrameCanvas opens shared handle, binds as SRV
- Frames stay on GPU from decode to display — true zero-copy

---

## Priority & Dependencies

```
Phase 1 (Color Pipeline) ─── FOUNDATION ── must ship first
  ├── Phase 2 (HDR/Tone Mapping) ── depends on Phase 1
  ├── Phase 3 (Scaling) ── depends on Phase 1
  ├── Phase 4 (Subtitles) ── depends on Phase 1
  └── Phase 5 (GPU Filters) ── depends on Phase 1 + 3

Phase 6 (HW Decode) ── depends on Phase 1A (NV12 support)
```

Phases 2-5 can be parallelized after Phase 1 ships. Phase 6 is the ultimate performance win but requires the most architectural change.

---

## Reference File Index

### MPV (C:\Users\Suprabha\Downloads\mpv-master\mpv-master)
| Area | File | Size | Notes |
|------|------|------|-------|
| Core renderer | video/out/gpu/video.c | 4434 lines | Main render pass orchestration |
| Color/tone shaders | video/out/gpu/video_shaders.c | 1039 lines | All GPU color operations |
| Filter kernels | video/out/filter_kernels.c | 14 KB | Lanczos/Spline/EWA math |
| D3D11 backend | video/out/d3d11/ra_d3d11.c | 85 KB | Full D3D11 abstraction |
| Color space utils | video/csputils.h | - | Enums, matrices, PQ constants |
| ICC/LUT3D | video/out/gpu/lcms.h | 17 KB | LCMS integration |
| Subtitle OSD | video/out/gpu/osd.c | 11 KB | GPU subtitle compositing |
| ASS decoder | sub/sd_ass.c | 42 KB | libass integration |

### VLC (C:\Users\Suprabha\Downloads\vlc-master\vlc-master)
| Area | File | Size | Notes |
|------|------|------|-------|
| D3D11 output | modules/video_output/win32/direct3d11.cpp | 74 KB | Main D3D11 module |
| Shader templates | modules/video_output/win32/d3d_dynamic_shader.c | 39 KB | Runtime HLSL generation |
| Tone mapping | modules/video_output/win32/d3d11_tonemap.cpp | 10 KB | Hable + NVIDIA True HDR |
| Swapchain/HDR | modules/video_output/win32/dxgi_swapchain.cpp | 19 KB | HDR metadata, color space |
| D3D11 scaler | modules/video_output/win32/d3d11_scaler.cpp | 27 KB | HW + SW scaling |
| Text rendering | modules/text_renderer/freetype/freetype.c | 48 KB | FreeType2 integration |
| Deinterlace | modules/video_filter/deinterlace/ | 14 files | yadif, ivtc, phosphor |

### Ours (C:\Users\Suprabha\Desktop\Tankoban 2)
| Area | File | Notes |
|------|------|-------|
| GPU render surface | src/ui/player/FrameCanvas.h/.cpp | QRhiWidget, D3D11 |
| Frame protocol | src/ui/player/ShmFrameReader.h/.cpp | Ring buffer, BGRA8 |
| Sidecar | src/ui/player/SidecarProcess.h/.cpp | JSON over stdin/stdout |
| Subtitles | src/ui/player/SubtitleOverlay.h/.cpp | QPainter plain text |
| Filters | src/ui/player/FilterPopover.h/.cpp | FFmpeg-side only |
| Shaders | resources/shaders/video.vert/.frag | Identity passthrough |
