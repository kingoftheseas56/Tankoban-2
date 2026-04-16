# Cinemascope aspect audit — 2026-04-16

**Author:** Agent 7 (Trigger C — comparative audit; no fixes proposed, observations only)
**Scope:** Two user-reported symptoms on 1920×804 cinemascope content:
1. Top letterbox bar visibly larger than bottom in 16:9 fullscreen (asymmetric)
2. With user-selected aspect override "16:9", bottom bar disappears but top bar remains

**Prior fix under audit:** `ade3241` (`src/ui/player/FrameCanvas.cpp`), which introduced `fitAspectRect` and moved the viewport to integer-fit centering with `spare / 2`.

---

## Part A — Empirical verification: is the fix actually running?

**YES.** Log evidence from `_player_debug.txt` (221 `[FrameCanvas aspect]` lines written by the diagnostic at [FrameCanvas.cpp:961-979](../../src/ui/player/FrameCanvas.cpp#L961-L979)) confirms `fitAspectRect` executes on every frame-dim or widget-dim change and produces symmetric viewport rects for cinemascope sources. Representative lines:

```
16:26:23.060 [FrameCanvas aspect] source=1920x804 widget=1920x1080 dpr=1.50
   frameAspect=2.3881 widgetAspect=1.7778 vp={0,138,1920,804} forced=0.0000

16:26:16.638 [FrameCanvas aspect] source=1920x804 widget=1920x974 dpr=1.50
   frameAspect=2.3881 widgetAspect=1.9713 vp={0,85,1920,804} forced=0.0000
```

Arithmetic check:
- Fullscreen (widget=1920×1080): top bar = `vp.y = 138`; bottom bar = `1080 − 138 − 804 = 138`. **Symmetric.**
- Windowed (widget=1920×974): top = 85; bottom = `974 − 85 − 804 = 85`. **Symmetric.**

**Observation A1:** The viewport-centering math produces geometrically symmetric bars in every logged configuration. Any asymmetry the user perceives is not coming from `fitAspectRect`'s output.

**Observation A2:** No `[FrameCanvas aspect]` line in the log has a non-zero `forced=` field, across 221 entries. This means either (a) the user never exercised the aspect-override menu during the instrumentation window, or (b) the diagnostic's fire-predicate ([FrameCanvas.cpp:961-962](../../src/ui/player/FrameCanvas.cpp#L961-L962)) — which triggers only on frame-dim or widget-dim change — never fires on override changes, since `setForcedAspectRatio` ([FrameCanvas.cpp:1771-1774](../../src/ui/player/FrameCanvas.cpp#L1771-L1774)) mutates `m_forcedAspect` but not the dims gating the log. The "override" symptom path is therefore **unlogged**, and no empirical record exists of what viewport the override produces on Hemanth's machine.

---

## Part B — What the `fitAspectRect` math predicts for the "override 16:9" case

The user's second symptom ("override 16:9 → bottom disappears, top remains") cannot come from [fitAspectRect](../../src/ui/player/FrameCanvas.cpp#L408-L440)'s output. The math:

- **Fullscreen (canvas 1920×1080)** + override 16:9 → `frameAspect = 16/9 = 1.7778`; `canvasAspect = 1.7778`. Condition `frameAspect > canvasAspect` is FALSE → else-branch → `r.h = 1080, r.w = 1920, spare = 0`. Zero bars. Content stretches 804 → 1080 vertically.
- **Windowed (canvas 1920×974)** + override 16:9 → `frameAspect = 1.7778`; `widgetAspect = 1.9713`. Condition FALSE → else-branch → `r.h = 974, r.w = 1731, spare.x = 189`. **Pillarbox (side bars), not letterbox.** No top/bottom bars at all.

**Observation B1:** In both canvases, override=16:9 on cinemascope content produces zero top/bottom bars from the viewport math. A persisting "top bar" in this configuration is therefore either (i) chrome/HUD rendered outside the render-target area, (ii) a non-letterbox artefact misread as one, or (iii) evidence that `m_forcedAspect` is never actually receiving `16/9`.

**Observation B2:** `aspectStringToDouble` ([VideoPlayer.cpp:2416-2421](../../src/ui/player/VideoPlayer.cpp#L2416-L2421)) maps the menu token `"16:9"` to `16.0 / 9.0`. Context-menu plumbing at [VideoPlayer.cpp:3008-3017](../../src/ui/player/VideoPlayer.cpp#L3008-L3017) calls `setForcedAspectRatio(aspectStringToDouble(val))`. The token → double mapping is straightforward; no layer transforms or clamps it. If override were broken, it would be at the signal-routing layer, not the math.

---

## Part C — Chrome / non-letterbox candidates above the render path

`FrameCanvas` is full-canvas: [VideoPlayer.cpp:2484](../../src/ui/player/VideoPlayer.cpp#L2484) sets `m_canvas->setGeometry(0, 0, width(), height())`. The only HUD widget is `m_controlBar`, positioned bottom-only at [VideoPlayer.cpp:2487](../../src/ui/player/VideoPlayer.cpp#L2487) (`0, height() - barH, width(), barH`). **No top HUD/chrome widget exists in the player surface.**

**Observation C1:** Windowed mode logs show `widget=1920×974` at `dpr=1.50` while the user's display is 1920×1080 physical. The 106-pixel delta above the client area (1080 − 974 = 106 physical, ≈71 logical at DPR 1.5) is **Qt window chrome**: title bar + window frame, plus the OS taskbar if not docked to the bottom. This chrome occupies screen real estate immediately above the render target's top edge. A screenshot that captures the entire window (chrome included) would show chrome-plus-letterbox at top, letterbox-only at bottom — asymmetric **visually** while the letterbox bars themselves (85 / 85) are symmetric. This is a plausible source of the user-perceived asymmetry in the first symptom, but requires the reported screenshot to confirm.

**Observation C2:** Fullscreen mode (`widget=1920×1080`) has no window chrome. Letterbox bars are 138 / 138 per the log. If the user's first symptom is reproducible specifically in fullscreen (not windowed), then C1 does not apply and the cause is elsewhere — DPI mismatch, swap-chain sizing, or a path outside the logged viewport call — and would need new instrumentation to localize.

---

## Part D — Overlay viewport timeline (relevant because of PGS / libass subs)

The subtitle overlay plane is a second viewport drawn after the video quad.

- **Pre-ade3241 baseline**: overlay viewport was `(0, 0, canvasW, canvasH)` — full canvas. Subtitles stretched vertically past the video into letterbox bars on cinemascope content. This is the bug ade3241's Batch 4 was addressing.
- **Post-ade3241 (committed)**: overlay viewport narrowed to `{videoRect.x, videoRect.y, videoRect.w, videoRect.h}` — same as the video quad. See [FrameCanvas.cpp:1025-1032](../../src/ui/player/FrameCanvas.cpp#L1025-L1032) of `HEAD` and the diff in commit `ade3241`.
- **Uncommitted (working tree)**: overlay `TopLeftY` is shifted upward by `m_subtitleLiftPx`:
  ```
  overlayVp.TopLeftY = static_cast<float>(videoRect.y - m_subtitleLiftPx);
  ```
  ([FrameCanvas.cpp uncommitted diff](../../src/ui/player/FrameCanvas.cpp#L1027)). `m_subtitleLiftPx` is set from `m_controlBar->sizeHint().height() * dpr` when controls are shown ([VideoPlayer.cpp:2186-2197](../../src/ui/player/VideoPlayer.cpp#L2186-L2197)) and cleared when hidden.

**Observation D1:** The uncommitted overlay-lift code does not currently ship — no user screenshot can be attributed to it unless it was taken on a local dev build. The overlay shift is additive to `videoRect.y`; when `m_subtitleLiftPx > videoRect.y` (e.g., 80-px control bar at dpr=1.5 = 120 physical px lift, vs. cinemascope `y = 85` windowed or `y = 138` fullscreen), `TopLeftY` goes negative. D3D11's documented behavior for negative `TopLeftY` is valid (viewport can extend outside the render target); pixels above Y=0 are clipped. No observed rendering pathology from negative viewports in the logged sessions.

**Observation D2:** No top-chrome-like bar is painted by the overlay path; the overlay texture is the subtitle composition (PGS rects or libass render), which is transparent outside the subtitle regions. Overlay-sourced "top bar" effects can be excluded.

---

## Part E — Uncommitted aspect-persistence chain (new attack surface)

189 uncommitted lines in `VideoPlayer.cpp` introduce a multi-layer aspect-restore chain on every `openFile` call ([VideoPlayer.cpp:321-346 diff](../../src/ui/player/VideoPlayer.cpp#L321-L346)):

```
carry-forward → per-file record (videos domain / aspectOverride)
    → per-show record (shows domain / aspectOverride) → "original"
```

Saved in:
- `data["aspectOverride"] = m_currentAspect;` at [VideoPlayer.cpp:2238](../../src/ui/player/VideoPlayer.cpp#L2238) on progress-save, **and** at line 2443 on show-prefs save.
- Carry-forward set in `onEndOfFile`, `prevEpisode`, `nextEpisode`, and the playlist-drawer jump handler.

**Observation E1:** Once shipped, every file open will auto-apply an `aspectOverride` token persisted from any prior user action in the same show folder. `aspectStringToDouble("16:9")` → `1.7778` feeds into `setForcedAspectRatio`, which drives the viewport math. A user who once picked "16:9" on an episode in a show folder will, thereafter, see cinemascope episodes in that same folder auto-stretched to 16:9 (zero bars, or pillarbox if windowed). That is a **behavioral change, not a bug**, but it will be indistinguishable from a regression to the user if they didn't intend the persistence.

**Observation E2:** The restore chain uses `QJsonObject::contains("aspectOverride")` rather than empty-string check ([VideoPlayer.cpp:336, 340](../../src/ui/player/VideoPlayer.cpp#L336-L340)), so `"original"` is a valid persisted token, not a "never set" sentinel. Any accidental write of `"original"` to per-file early (before the intended first user action) locks the per-show layer out forever. Not observed currently, but a candidate for state-corruption scenarios.

---

## Part F — Reference-app comparison

### mpv — `mp_get_src_dst_rects` ([`video/out/aspect.c`](C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/video/out/aspect.c))

Core centering (line 88-95):
```c
align = (align + 1) / 2;
*dst_start = (dst_size - scaled_src_size) * align + pan * scaled_src_size;
*dst_end   = *dst_start + scaled_src_size;
*osd_margin_a = *dst_start;
*osd_margin_b = dst_size - *dst_end;
```

- Integer math only. No parity tweak; odd-spare asymmetry of 1 pixel is accepted.
- `align` parameter (`(align+1)/2`) collapses user options `-1/0/+1` to `[0, 0.5, 1]`; default 0 → 0.5 → centered.
- Source-aspect override: not an override per se — mpv derives display size via `mp_image_params_get_dsize()` baking PAR in. User "stretch" / "force DAR" options mutate `d_w, d_h` upstream before this call.
- `osd_margin_a/b` is reported back so OSD widgets can position themselves relative to the letterbox. Tankoban 2 has no equivalent return channel — HUD layout is window-space, not video-space.

### VLC — `vout_display_PlaceRotatedPicture` ([`src/video_output/display.c`](C:/Users/Suprabha/Downloads/Video player reference/secondary reference/vlc-master/vlc-master/src/video_output/display.c))

Aspect + centering (lines 103-165):
```c
const int64_t scaled_height =
    (int64_t)source->i_visible_height * source->i_sar_den * dp->sar.num * display_width /
            (source->i_visible_width  * source->i_sar_num * dp->sar.den);
...
place->x = ((int)dp->width  - (int)place->width)  / 2;
place->y = ((int)dp->height - (int)place->height) / 2;
```

- int64 fixed-point SAR-baked math (avoids float precision loss across wide aspect/resolution spread).
- Centering: plain integer `/2`; odd spare → 1-pixel top/left bias. No parity correction.
- Source-aspect override: `vout_SetDisplayAspect` → `UpdateSourceSAR` (lines 674-699) has **three modes**: `VLC_DAR_IS_FROM_SOURCE` (use source SAR), `VLC_DAR_IS_FILL_DISPLAY` (compute SAR to fill), custom DAR (decompose to SAR as `(dar.num * vid_h) / (dar.den * vid_w)`). Override mutates source SAR, not a separate "forced" scalar; downstream viewport math is unchanged.
- Fitting orthogonal to centering: `VLC_VIDEO_FIT_{NONE,SMALLER,LARGER,WIDTH,HEIGHT}` decides which dimension drives scale; centering applies after.

### QMPlay2 — `Functions::getImageSize` ([`src/qmplay2/Functions.cpp:269`](C:/Users/Suprabha/Downloads/Video player reference/QMPlay2-master/src/qmplay2/Functions.cpp))

```c
if (aspect_ratio > 0.0) {
    if (W / aspect_ratio > H) W = H * aspect_ratio;
    else                      H = W / aspect_ratio;
}
if (zoom != 1.0 && zoom > 0.0) { W *= zoom; H *= zoom; }
if (X) *X = (winW - W) / 2;
if (Y) *Y = (winH - H) / 2;
```

- Float aspect / int centering. No parity tweak.
- Zoom is linear multiplicative (not mpv's exponential `2^zoom`).
- Override flows through `PlayClass` → `vThr->setARatio(double)`. `aspect_ratio` passed in is the final scalar; any PAR correction is upstream.

### IINA

- `Aspect.swift` (55 lines) parses aspect strings to NSSize. No viewport math.
- Viewport fully delegated to embedded mpv (`MPVController`). Inherits mpv's integer-truncation centering and 1-pixel-odd-spare behavior without modification.

---

## Part G — Tankoban 2 divergences from reference patterns

**Observation G1 — Parity tweak.** `fitAspectRect` ([FrameCanvas.cpp:421-425, 431-435](../../src/ui/player/FrameCanvas.cpp#L421-L435)) decrements `r.h` or `r.w` by 1 if `spare` is odd, so `spare / 2` cleanly splits:
```cpp
int spare = canvasH - r.h;
if ((spare & 1) != 0 && r.h > 1) {
    --r.h;
    spare = canvasH - r.h;
}
r.y = spare / 2;
```
None of mpv, VLC, or QMPlay2 do this. They accept a 1-pixel top/bottom (or left/right) asymmetry on odd spare, biased toward origin. Tankoban 2's parity tweak trades a strictly-symmetric letterbox for a 1-pixel scale loss on the video. **Cosmetically the stronger guarantee, but architecturally idiosyncratic.** Not a cause of the user-reported multi-pixel asymmetry, which is far larger than 1 pixel.

**Observation G2 — Override model.** Tankoban 2 stores `m_forcedAspect` as a single `double` (display aspect) ([FrameCanvas.cpp:1771-1774](../../src/ui/player/FrameCanvas.cpp#L1771-L1774)). Source SAR (pixel-aspect-ratio) is not represented anywhere in the main-app render path — the sidecar reports `frameW × frameH` as rendered buffer dims, but non-square-pixel sources (rare, but exist — DV, anamorphic DVD, some Blu-ray streams) are not correctly handled. VLC's SAR-through-display model (Observation: VLC `display.c:674-699`) and mpv's PAR-baked `dsize` model both separate display aspect from source pixel aspect; Tankoban 2 conflates them. **Not the current symptom**, but a latent divergence.

**Observation G3 — Diagnostic coverage gap.** Aspect-override changes do not trigger the `[FrameCanvas aspect]` diagnostic because the fire-predicate keys on frame/widget dim changes only ([FrameCanvas.cpp:961-962](../../src/ui/player/FrameCanvas.cpp#L961-L962)). Neither reference app's viewport code has diagnostic instrumentation comparable to Tankoban 2's, so this is not a reference-apps divergence — but it is the reason Part A contains no empirical record of the override path's output. A second dim-invariant dirty flag would close the gap.

**Observation G4 — Persistence-driven override (uncommitted).** None of mpv / VLC / QMPlay2 auto-restore a per-folder or per-show aspect override on file open by default. mpv has `--saved-aspect` as an opt-in runtime flag; VLC's aspect is session-scoped and must be explicitly saved in preferences for persistence. Tankoban 2's uncommitted per-show chain ([VideoPlayer.cpp:321-346 diff](../../src/ui/player/VideoPlayer.cpp#L321-L346)) is an additive behavior with no direct reference parallel. **Not currently shipping**, but will diverge from established video-player conventions once it does.

**Observation G5 — DPR handling.** Tankoban 2 multiplies widget size by DPR at [FrameCanvas.cpp:935-937](../../src/ui/player/FrameCanvas.cpp#L935-L937) to get physical-pixel canvas dims. Both reference apps (mpv via `vo.c` and VLC via display) operate in buffer-space from the vout driver, so DPR is either already factored upstream or not a consideration. Tankoban 2's swap chain is physical-pixel sized; viewport in physical pixels matches the swap chain. No observed mismatch in the logs, but the `dpr=1.50` multiplier means any subtle widget↔swap-chain scale skew would land here.

---

## Part H — Summary: where to point hypothesis-testing (no fixes proposed)

The audit's load-bearing finding (Observation A1) is that `fitAspectRect` **is running and producing symmetric viewports** for both documented symptoms. The working theory that the `ade3241` math is subtly wrong is not supported by the log. Any re-investigation of the asymmetric-top-bar symptom should:

1. **Attach the symptom to a known canvas mode.** Hemanth's screenshot should be labeled windowed vs. fullscreen — the chrome-source hypothesis (Observation C1) applies to windowed only and is falsifiable in fullscreen.
2. **Close the diagnostic coverage gap (Observation G3)** before testing override behavior. The 221-line log has no data on override scenarios; adding an `m_forcedAspect`-change edge to the fire-predicate would capture the second symptom's viewport rect in the log.
3. **Cross-check whether the persistence chain (Part E, currently uncommitted) is already running on the machine that produced the screenshot.** If the uncommitted tree is loaded, auto-restored aspect override is a latent confounder.
4. **If fullscreen asymmetry is reproducible** and Observations A1/B1 still hold in the fresh log after (2), the cause is outside the main draw path this audit covered — candidates would include the swap-chain back-buffer dims vs. widget dims (outside `drawTexturedQuad`'s view), the sidecar's reported `frameW/frameH` (if it carries PAR-cropped dims instead of decoded buffer dims, `fitAspectRect` would get a wrong source aspect), or the Present/flip-model scaling behavior under DXGI waitable swap chain.

No fix is proposed. Above are audit observations; next-step hypothesis selection is Agent 0's / Agent 3's call.

---

## References

- mpv, `video/out/aspect.c`, `mp_get_src_dst_rects` at file:line 88-110 (reference path: `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/video/out/aspect.c`)
- VLC, `src/video_output/display.c`, `vout_display_PlaceRotatedPicture` at line 83, `UpdateSourceSAR` at line 674 (`C:/Users/Suprabha/Downloads/Video player reference/secondary reference/vlc-master/vlc-master/src/video_output/display.c`)
- QMPlay2, `src/qmplay2/Functions.cpp`, `Functions::getImageSize` at line 269 (`C:/Users/Suprabha/Downloads/Video player reference/QMPlay2-master/src/qmplay2/Functions.cpp`)
- IINA, `iina/Aspect.swift` (wrapper; delegates to embedded mpv)
- Tankoban 2 committed state: [FrameCanvas.cpp fitAspectRect:408-440](../../src/ui/player/FrameCanvas.cpp#L408-L440), [drawTexturedQuad viewport:929-953](../../src/ui/player/FrameCanvas.cpp#L929-L953), [overlay pass:1014-1037](../../src/ui/player/FrameCanvas.cpp#L1014-L1037), [setForcedAspectRatio:1771-1774](../../src/ui/player/FrameCanvas.cpp#L1771-L1774), [aspectStringToDouble:2416-2421](../../src/ui/player/VideoPlayer.cpp#L2416-L2421), [context menu route:3008-3017](../../src/ui/player/VideoPlayer.cpp#L3008-L3017), [HUD layout:2484-2487](../../src/ui/player/VideoPlayer.cpp#L2484-L2487)
- Tankoban 2 uncommitted tree: per-show persistence at [VideoPlayer.cpp:321-346 diff](../../src/ui/player/VideoPlayer.cpp#L321-L346), subtitle lift at [FrameCanvas.cpp:1025-1032 diff](../../src/ui/player/FrameCanvas.cpp#L1025-L1032), `aspectOverride` save at [VideoPlayer.cpp:2238](../../src/ui/player/VideoPlayer.cpp#L2238)
- Commit `ade3241` — cinemascope viewport + canvas-sized overlay (once-only exception by Agent 7)
- Commit `4a69e8d` — PLAYER_PERF_FIX Phase 3.B (Option B, SHM-routed overlay — predecessor of the overlay viewport path)
- Log: `C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt` — 221 `[FrameCanvas aspect]` diagnostic lines, first 15:54:19 2026-04-16
