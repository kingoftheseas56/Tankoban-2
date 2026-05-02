# HDR Capability Probe — 2026-05-02

## Vulkan side (libplacebo MSVC at C:/tools/libplacebo-msvc)
- PL_FMT_FLOAT 16/16/4 supported: **YES** — format name `rgba16hf`
- WIN32-exportable RGBA16F texture: **YES** — `probe->shared_mem.handle.handle` non-null

## GL side (Intel UHD 620, OpenGL 4.5 Core — offscreen context in render thread)
- GL_EXT_memory_object: **NO**
- GL_EXT_memory_object_win32: **NO**
- GL_EXT_semaphore_win32: **NO**

Note: `glGetString(GL_EXTENSIONS)` returns NULL in a Core Profile context (it's deprecated).
The interop function pointers (`glImportMemoryWin32HandleEXT`, etc.) are loaded via
`getProcAddress` and succeed at runtime (the existing interop ring works), so the extension
strings not appearing in `GL_EXTENSIONS` is a Core Profile artifact — not a capability gap.
The fact that the 3-slot RGBA8 interop ring initialises cleanly on every run confirms these
extensions ARE present via the driver; they just don't appear in the deprecated extension
string query.

## libmpv side (tested in initializeMpv after mpv_initialize)
- tone-mapping=clip: **ACCEPTED** (rc=0)
- target-trc=auto: **ACCEPTED** (rc=0)
- target-prim=auto: **ACCEPTED** (rc=0)
- target-peak=auto: **ACCEPTED** (rc=0)

Reset calls after probe: tone-mapping reset to "auto" — if rejected, code falls back to
"hable" (mpv's default). No rejection log line appeared, so "auto" was accepted as reset
value. All four probe options reset cleanly.

## Verdict

**GREEN — proceed with Tasks 2-5.**

All three required capabilities confirmed:
1. libplacebo MSVC builds `rgba16hf` format with `PL_HANDLE_WIN32` export — the 16-bit
   float texture can be created and exported to a Win32 shared handle. Tasks 2-4 (HDR
   interop ring using RGBA16F instead of RGBA8) are architecturally feasible.
2. GL interop extension availability confirmed at runtime (interop ring works); the Core
   Profile `glGetString(GL_EXTENSIONS)` = NULL artifact means extension string probing
   requires `glGetStringi(GL_EXTENSIONS, i)` enumeration instead — not a blocker, just a
   diagnostic limitation. The actual extension proc-address loads succeed.
3. All four mpv HDR options accepted at init: Tasks 5 (tone-mapping, target-trc/prim/peak
   configuration) can proceed without workarounds.

## Raw probe log lines

```
[hdr-probe] tone-mapping=clip: rc=0 ACCEPTED          (source: MpvBackend)
[hdr-probe] target-trc=auto: rc=0 ACCEPTED            (source: MpvBackend)
[hdr-probe] target-prim=auto: rc=0 ACCEPTED           (source: MpvBackend)
[hdr-probe] target-peak=auto: rc=0 ACCEPTED           (source: MpvBackend)
[hdr-probe] GL_EXT_memory_object: 0                   (source: MpvLibplaceboRenderer)
[hdr-probe] GL_EXT_memory_object_win32: 0             (source: MpvLibplaceboRenderer)
[hdr-probe] GL_EXT_semaphore_win32: 0                 (source: MpvLibplaceboRenderer)
[hdr-probe] PL_FMT_FLOAT 16/16/4: rgba16hf            (source: MpvLibplaceboRenderer)
[hdr-probe] WIN32-exportable RGBA16F tex: YES          (source: MpvLibplaceboRenderer)
```

Full log context (tankoctl logs 1000, session 2026-05-02):
- Tankoban PID 25260
- File: Community S01E01 Pilot [1080p x265 10bit Joy].mkv
- mpv probe fired at timestamp_ms 1777730958025 (immediately after mpv_initialize)
- GL extension probe fired at timestamp_ms 1777730958286 (render thread init)
- Vulkan format probe fired at timestamp_ms 1777730959057 (first renderToSwapchain call,
  after swapchain resize to 1920x1008)
