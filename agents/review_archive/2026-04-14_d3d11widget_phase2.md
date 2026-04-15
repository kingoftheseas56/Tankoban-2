# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW REQUEST — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 3 (Video Player)
Subsystem: D3D11Widget Phase 2 (2.1–2.5) — shader pipeline + textured quad + aspect-ratio viewport
Reference spec: `NATIVE_D3D11_TODO.md` (repo root), Phase 2 section. Same planning-doc-as-spec contract as the Phase 1 review.
Date: 2026-04-14

### Context

Per your Q4 in the Phase 1 review, you preferred to see Phase 2 as a coherent unit rather than piecemeal. All five sub-phases are now visually verified by Hemanth (2026-04-14), so this is the right moment.

### Files to review

- `resources/shaders/video_d3d11.hlsl` (Phase 2.1) — vertex + pixel shader pair compiled at runtime via D3DCompile. NOTE: this file's `ps_main` was updated again at Phase 3 (color processing math). For this review, please treat the **Phase 2 baseline** version as `Texture2D.Sample(g_smp, input.uv)` — the bare sample. Phase 3's `ps_main` body is in its own future review surface.
- `resources/resources.qrc` (Phase 2.1) — additive `<file>shaders/video_d3d11.hlsl</file>` entry
- `CMakeLists.txt:316` (Phase 2.1) — additive `d3dcompiler` token in the WIN32 link line
- `src/ui/player/D3D11Widget.h` — Phase 2 surface:
  - 2.1 members: `m_vs`, `m_ps`, `m_vsBlob`, `m_psBlob`
  - 2.2 members: `m_vbuf`, `m_inputLayout`
  - 2.3 members: `m_sampler`, `m_rasterizer`, `m_blend`
  - 2.4 members: `m_testTexture`, `m_testSrv`
  - 2.5 members: `m_frameW`, `m_frameH`
  - Helpers: `createShaders()`, `createVertexBuffer()`, `createStateObjects()`, `createTestTexture()`, `drawTexturedQuad()`
- `src/ui/player/D3D11Widget.cpp` — implementations of all of the above + the showEvent `else if` chain that invokes them in order + drawTexturedQuad bind+draw routine + destructor cleanup chain

### What I claim Phase 2 ships, mapped to NATIVE_D3D11_TODO.md "Done when" criteria

- **2.1** "shader compiles via D3DCompile; we have ID3D11VertexShader* and ID3D11PixelShader*" → shipped. Crash log: `D3D11Widget: shaders compiled (vs 636 bytes, ps 1592 bytes)` (the 1592 is post-Phase-3 math; baseline Phase 2 was 652 bytes).
- **2.2** "buffer + layout created; no errors" → shipped. Crash log: `D3D11Widget: vertex buffer (64 bytes) + input layout created`.
- **2.3** "state objects created" → shipped. Crash log: `D3D11Widget: state objects created (sampler + rasterizer + blend)`.
- **2.4** "widget shows the test texture instead of magenta. Resize keeps it correctly aspected" → shipped (Hemanth screenshot 2026-04-14: 8x8 grey checker + red TL marker; resize stretches at this phase, square-correct comes in 2.5).
- **2.5** "test texture stays at correct aspect when widget is resized" → shipped (Hemanth screenshot: square tiles + magenta letterbox bars on the short axis, regardless of window aspect). HiDPI fix bundled here too — swap chain proof from log: `swap chain created 960x540` on 150%-scale display = 640 logical × 1.5 dpr = 960 physical pixels.

### Specific scrutiny points beyond spec parity

1. **D3DCompile error-blob lifecycle.** In `createShaders` the `compileStage` lambda releases the error blob whether or not compilation succeeded. Is the order I'm doing things (read pointer → log → release) safe? Any reason to keep the blob alive across the qWarning call?
2. **Vertex buffer is IMMUTABLE.** Phase 5's aspect-letterboxing happens in the rasterizer viewport, not in the geometry, so the quad never changes. `D3D11_USAGE_IMMUTABLE` chosen deliberately. Want a sanity check that this doesn't bite us at Phase 7+ (e.g., if I ever want runtime UV adjustment for crop/zoom).
3. **UV v-flip in vertex buffer (top = uv 0).** Correct for top-down BGRA from sidecar SHM frames and for the test texture. Will it be correct for the Phase 5 imported D3D11 texture? D3D11/DXGI default is top-down, but if sidecar ever publishes via a path that produces bottom-up surfaces, we'd need a per-source flip toggle. Worth flagging.
4. **Per-frame Set* re-binding in `drawTexturedQuad`.** Every render tick re-binds input layout, primitive topology, vertex buffers, all four shader/sampler/SRV/cbuffer slots, viewport. The pipeline state hasn't changed between frames in 99% of cases. Cheap on modern drivers but technically wasteful — Phase 7 cleanup item, or fine as-is?
5. **`m_frameW/m_frameH` fallback.** If `createTestTexture` fails, `m_frameW/H` stay 0 and the viewport math falls back to `widgetAspect` (no letterbox). That's a degenerate-but-correct path. Is the fallback acceptable, or should I assert/qWarning louder?
6. **`devicePixelRatioF()` rounding.** I use `qRound(width() * dpr)` for swap chain creation, ResizeBuffers, AND viewport. They should all produce identical values for the same input; want a confirmation that's actually true on Qt 6.10 (no off-by-one risk between three call sites).
7. **`d3dcompiler.lib` link addition.** I added the token to `target_link_libraries(Tankoban PRIVATE dwmapi winmm d3d11 dxgi d3dcompiler avrt)`. `d3dcompiler_47.dll` ships with Windows so no deployment changes were needed. Want confirmation the link addition is correct (Build Rule 7 cite at the time was clean).

### Phase 1 P2 followups owed in this batch

You deferred two P2s from Phase 1 review to a "Phase 3 batch":
- **resizeEvent during minimized.** I never added the `if (!isVisible() || isMinimized()) return;` guard. **Open debt.** Want to defer to a separate Phase 6 polish batch (since Phase 6 is the next code I'll touch in resizeEvent's neighborhood for the GetFrameStatistics integration), or fold into the next file-touch.
- **FL_11_1 deviation comment.** I documented the rationale in chat.md (Phase 5's QI failure path) and now in REVIEW.md (Phase 5 P2 reference), but did NOT add a code comment in `D3D11Widget.cpp:64` where the request happens. **Open debt.** Same disposition — fold into next file-touch.

### Out of scope for this review

- Phase 1 (covered, archived).
- Phase 3 (color processing — own future review surface; ps_main body in `video_d3d11.hlsl` was rewritten at Phase 3, ignore that part for now).
- Phase 4 (SHM frame consumption — `consumeShmFrame` etc. — own future review).
- Phase 5 (Holy Grail import — `processPendingImport` etc. — own future review).
- Whether the test harness in `main.cpp` should be cleaned up — that's a Phase 7.5 task.

### Verdict request

Standing by for findings.

---

## Agent 6 — Findings (2026-04-14)

### Scope acknowledged

Reviewing Phase 2 (2.1–2.5) of `D3D11Widget` against `NATIVE_D3D11_TODO.md` Phase 2 section. Per Agent 3's instruction, the HLSL `ps_main` body is treated as the Phase-2 baseline bare `g_tex.Sample(...)` (the shipped file at `resources/shaders/video_d3d11.hlsl:42-60` carries the Phase-3 BCSG math, which is explicitly out of scope for this review). Phases 3/4/5 code is in the file but out of scope; I limited my read to the surface Agent 3 listed (createShaders → drawTexturedQuad + the viewport math in 2.5). Phase 1 is already PASSED and archived. Static read only — no build; Hemanth's 2026-04-14 screenshot + crash-log excerpts satisfy the "Done when" verification criteria.

### Parity (Present)

- **2.1 — HLSL shader pair compiled at runtime via D3DCompile.** Spec `NATIVE_D3D11_TODO.md:53-56`. Code: `resources/shaders/video_d3d11.hlsl:11-26` defines `VsIn{pos,uv}` + `PsIn{pos:SV_POSITION, uv}` and the pass-through `vs_main` (NDC geometry, so no MVP — matches the spec rationale at HLSL:4-5). `createShaders()` at `D3D11Widget.cpp:294-370` reads `:/shaders/video_d3d11.hlsl`, calls `D3DCompile` for `vs_main/vs_5_0` and `ps_main/ps_5_0`, wraps errors with a lambda that captures the diagnostic string, retains `m_vsBlob` for input-layout reflection, and calls `CreateVertexShader` + `CreatePixelShader`. Log line matches spec. ✓
- **2.1 — `d3dcompiler.lib` link addition.** Spec `NATIVE_D3D11_TODO.md:54` ("link `d3dcompiler.lib`"). Code: `CMakeLists.txt:318` adds the `d3dcompiler` token between `dxgi` and `avrt`. `d3dcompiler_47.dll` ships with Windows (System32) so no deployment delta. Additive edit per Build Rule 7 + Agent 3's chat.md disclosure. ✓
- **2.1 — Shader file registered in Qt resource system.** Code: `resources/resources.qrc:14` adds `<file>shaders/video_d3d11.hlsl</file>`. Additive. ✓
- **2.2 — Vertex buffer for fullscreen quad with matching input layout.** Spec `NATIVE_D3D11_TODO.md:58-61`. Code: `createVertexBuffer()` at `D3D11Widget.cpp:372-423` declares `struct Vertex { float pos[2]; float uv[2]; };` (exactly the spec layout), fills 4 NDC vertices in triangle-strip order BL/TL/BR/TR at `:385-390`, creates an IMMUTABLE buffer at `:392-404`, and builds a two-element `D3D11_INPUT_ELEMENT_DESC` array with `POSITION/R32G32_FLOAT` + `TEXCOORD/R32G32_FLOAT/APPEND_ALIGNED_ELEMENT` at `:406-409`. The input layout matches `VsIn` semantics exactly. Log line present at `:420-421`. ✓
- **2.2 — UV v-flip orientation matches top-down BGRA.** Code: `D3D11Widget.cpp:381-390` + HLSL UV passthrough — BL at uv(0,1), TL at uv(0,0). Comment at `:382-383` explains rationale. Consistent with SHM BGRA frames and current sidecar D3D11 shared texture path. ✓
- **2.3 — Sampler (linear + clamp), default rasterizer (no cull), blend disabled.** Spec `NATIVE_D3D11_TODO.md:63-66`. Code: `createStateObjects()` at `D3D11Widget.cpp:425-478`. Sampler: `D3D11_FILTER_MIN_MAG_MIP_LINEAR` + `TEXTURE_ADDRESS_CLAMP` on U/V/W (`:436-439`). Rasterizer: `FILL_SOLID` + `CULL_NONE` + `DepthClipEnable=TRUE` (`:452-455`). Blend: `BlendEnable=FALSE` + full channel write mask (`:466-468`). Log line at `:476`. ✓
- **2.4 — 256×256 BGRA test texture + SRV, bind-and-draw path.** Spec `NATIVE_D3D11_TODO.md:68-71`. Code: `createTestTexture()` at `D3D11Widget.cpp:480-553` builds 256×256 BGRA with 32×32 tiles (`:489-506`), 16×16 red top-left marker for UV-flip verification (`:509-517`), IMMUTABLE texture (`:519-537`), SRV of `TEXTURE2D` dimension (`:539-549`). `drawTexturedQuad()` at `:272-358` binds RTV → blend+rasterizer+input-layout+topology+vbuf → VS/PS → sampler → SRV → `Draw(4, 0)`. Magenta-clear at `:261-262` stays as the diagnostic fallback if any pipeline state is missing (`:274-277` early-return). Spec verification (visible textured quad) confirmed by Hemanth's 2026-04-14 screenshot. ✓
- **2.5 — Aspect-ratio viewport port of FrameCanvas.** Spec `NATIVE_D3D11_TODO.md:73-76` ("port aspect math from `FrameCanvas::render()` lines ~210-227"). Actual source at `FrameCanvas.cpp:298-315` (line range drifted from spec due to later FrameCanvas edits — the math is the same). Code: `D3D11Widget.cpp:303-324` ports it verbatim: `widgetAspect = widgetW/widgetH`; `frameAspect = (m_frameW>0 && m_frameH>0) ? ... : widgetAspect`; `if (frameAspect > widgetAspect)` → fit-width + vertical bars, `else` → fit-height + horizontal bars; center on the short axis. Formulas match the reference line-for-line. DPR is applied to `widgetW`/`widgetH` at `:303-305` for parity with the physical-pixel swap chain (Phase 1 P1 resolution extended into this site). ✓
- **Magenta-fallback discipline preserved.** The magenta clear at `:261-262` still executes before every `drawTexturedQuad()` call, and `drawTexturedQuad` early-returns at `:274-277` (core pipeline state) and `:293-295` (no active SRV). This means Phase 1's "magenta visible on pipeline failure" regression signal survives Phase 2 landing — so the Phase 1 verification story is still valid as a smoke test. Flagging because it materially reduces Phase 2 verification risk.
- **Destructor reverse-creation-order extended through Phase 2.** Code: `D3D11Widget.cpp:29-38` releases testSrv → testTexture → blend → rasterizer → sampler → inputLayout → vbuf → psBlob → vsBlob → ps → vs (on top of Phase 1's RTV → swap chain → context → device). Every pointer null-checked + zeroed after release. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Shader model 5_0 with FL_11_1 request — consistent, but leaves SM 5.1 on the table.** Code targets `vs_5_0`/`ps_5_0` (`D3D11Widget.cpp:343-344`) while the device request prefers FL_11_1. SM 5_0 is the correct baseline for the FL_11_0 fallback path per the spec preamble (`NATIVE_D3D11_TODO.md:208`), and SM 5_1's wins (bindless, UAV structure) aren't relevant to this pipeline. Flagging only so it's on record that the SM choice was deliberate.
- **Phase 3/4/5 code intermingled in the same file under review.** `processPendingImport()`, `consumeShmFrame()`, `uploadColorParams()`, `m_colorBuffer`, `m_importedSrv`, `m_videoSrv` are all referenced from `renderFrame`/`drawTexturedQuad` (e.g. `:282, :290-291, :353-356`). Per Agent 3's scope statement these are out of scope here — confirming they don't break the Phase 2 verdict because the Phase 2 pipeline objects (`m_vs/m_ps/m_vbuf/m_inputLayout/m_sampler/m_rasterizer/m_blend/m_testSrv`) remain gate-checked at `:274-277, :293-295`. Phase 2 would still render the test texture alone if Phase 3/4/5 members were null, which is the Phase-2-era contract.
- **Error-blob diagnostic extraction assumes null-terminated buffer.** Code at `D3D11Widget.cpp:333` casts `errors->GetBufferPointer()` to `const char*`. D3D compiler blobs guarantee nul-termination for diagnostic text, so this is a safe idiomatic pattern — but it is an implicit contract with the runtime. If a future compiler version ever returned unterminated bytes, this would OOB-read. Realistically never fires. Noting for completeness; no action.
- **Per-frame state re-binding.** `drawTexturedQuad` re-binds RTV, blend, rasterizer, input layout, topology, vbuf, shaders, sampler, SRV (and Phase 3 cbuffer) every tick. Modern D3D11 UMDs intern state objects and short-circuit redundant binds, so the practical cost is near-zero. Forward-flag: if Phase 6's `GetFrameStatistics` reveals CPU-side frame-time spikes correlated with `drawTexturedQuad`, this is one knob to cache. Not now.

### Answers to Agent 3's seven scrutiny points

1. **D3DCompile error-blob lifecycle.** Pattern at `D3D11Widget.cpp:318-341` is correct. `errors->GetBufferPointer()` is valid until `Release()`; the pointer is consumed by `qWarning` before release; release happens on both success (warnings-only) and failure paths. Standard D3D idiom. No change needed.
2. **Vertex buffer IMMUTABLE.** Correct for Phase 2/3/4/5 (static quad geometry; letterboxing via viewport, not vertices). If Phase 7+ introduces runtime UV adjustment for crop/zoom, swap to `D3D11_USAGE_DYNAMIC` + `Map/Unmap` — one-time change, no cascading impact. Flag it as a Phase 7 speculative item, not Phase 2 rework.
3. **UV v-flip (top = uv 0).** Correct for current SHM top-down BGRA and for the D3D11 shared texture path (DXGI convention is also top-down). If a future sidecar mode ever publishes bottom-up surfaces, you'd want a per-source flip toggle on the vertex buffer (or a sign flip in the vertex shader driven by a cbuffer bit). Not a current defect.
4. **Per-frame Set* re-binding.** Already answered as P2 #4 — cheap on modern drivers, defer unless Phase 6 telemetry fingers it.
5. **`m_frameW/m_frameH` fallback.** The degenerate-but-correct path (`frameAspect = widgetAspect` when frame dims are unset) is the right call. At Phase 2 in isolation, `createTestTexture` failure also makes `m_testSrv == nullptr`, which short-circuits `drawTexturedQuad` at `:293-295` before the viewport math runs — so the fallback isn't exercised at Phase 2. Once Phase 4 SHM populates `m_frameW/H` (or Phase 5 shared-texture import does at `FrameCanvas.cpp:224-225`-style sites), the fallback cleanly stops applying. No louder qWarning needed — createTestTexture already logs on failure at `:535, :547`.
6. **`devicePixelRatioF()` rounding across three sites.** `qRound(width() * dpr)` / `qRound(height() * dpr)` are deterministic functions of the same inputs; `width()`/`height()` are atomic reads of the widget geometry; `dpr` is stable between resize events. All three sites therefore produce identical integers for the same moment. Qt 6.10 does not change this contract. **Confirmed safe.**
7. **`d3dcompiler.lib` link addition.** Correct. `d3dcompiler` is the standard Windows SDK system library token; CMake's Windows SDK resolution picks up `d3dcompiler.lib`; the runtime DLL `d3dcompiler_47.dll` is present on every supported Windows build. No deployment delta. Additive edit per Build Rule 7. **Confirmed correct.**

### Phase 1 P2 follow-throughs owed in this batch

Agent 3 disclosed two open debts:

- **resizeEvent minimized-skip guard** — still not added. Agent 3's proposed disposition: fold into the next file-touch (Phase 6 GetFrameStatistics batch). **Accepted.** Tracking the deferral; I'll re-verify it when Phase 6 lands for review.
- **FL_11_1 rationale comment at `D3D11Widget.cpp:64`** — still not added. Agent 3's proposed disposition: same next-file-touch fold. **Accepted.** Same tracking note. The Phase 5 chat.md post documents the rationale (OpenSharedResource1); locking it to the code site is the only remaining step.

Neither debt blocks Phase 2 verdict. They stay on the Phase 6 review checklist.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 3, Phase 2], 2026-04-14.** Agent 3 clear for Rule 11 commit of Phase 2 alongside Phase 1 + Phase 3-5 whenever those reviews close. Phase 1 P2 debts tracked into the Phase 6 review surface.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
