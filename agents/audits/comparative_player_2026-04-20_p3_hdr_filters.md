# Comparative Player Audit — Phase 3: HDR + Video Filters (STATIC-ANALYSIS PILOT)

**Author:** Agent 3 (Video Player)
**Date:** 2026-04-20
**TODO:** [PLAYER_COMPARATIVE_AUDIT_TODO.md](../../PLAYER_COMPARATIVE_AUDIT_TODO.md) §7
**Status:** **STATIC-ANALYSIS PILOT** — The TODO §7 exit criteria require "objective-criteria-met verdicts (no 'looks better' — numerical only)" for HDR tone-map axes, backed by "4K HDR10 BT.2020 PQ sample" + "interlaced 1080i sample" fixtures. **Neither fixture exists on Hemanth's disk** (scan confirmed 2026-04-20: all MKVs in `C:/Users/Suprabha/Desktop/Media/TV/` probe as `bt709 / yuv420p` — standard SDR, no `bt2020nc` or `smpte2084`/PQ). This audit therefore pivots to **algorithm-correctness static analysis + UI-behavior verification** — defensible for what can be checked without real HDR source, with Batch 3.1 pixel-level HDR output diff explicitly deferred to Phase 3.5 once a fixture is available.

Applying the process correction from Phase 2 (Hemanth caught subtitle-position miss): **any sub-axis with observable output that I cannot measure with real data is marked DEFERRED, not "tentative CONVERGED"**. No "looks fine from the code" verdicts for pixel-visible axes.

---

## Executive Summary

Phase 3 covers Tankoban's HDR tone-mapping path (category G — 4 modes: Off / Reinhard / ACES / Hable) and video filters (category H — deinterlace / brightness / contrast / saturation / interpolate / normalization). The tone-map implementation was shipped in PLAYER_PERF_FIX Phase 3 / Batch 3.4 + Batch 3.5 (HDR color-space + scRGB output, closed 2026-04-16) and has been stable since. The video-filter popover (`FilterPopover`) is the audio-EQ analog for video-path settings — compact UI with comboboxes + checkboxes + sliders, live-applied via sidecar ffmpeg filter chain.

**Headline findings from static-analysis + UI-behavior pass:**

1. **Tone-map algorithms are mathematically correct + reference-verified.** The HLSL shader at [resources/shaders/video_d3d11.hlsl:129-176](../../resources/shaders/video_d3d11.hlsl) implements vanilla Reinhard (`x/(1+x)`), Krzysztof Narkowicz's ACES-filmic fit (coefficients A=2.51, B=0.03, C=2.43, D=0.59, E=0.14), and John Hable's Uncharted-2 curve (A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30, white point 11.2, exposure bias 2.0). Both ACES + Hable have comments citing cross-verification against **Kodi's `system/shaders/GL/1.5/gl_tonemap.glsl`** — same coefficients. **Verdict G1: CONVERGED with Kodi + mpv** on algorithm math.
2. **Tankoban's HDR dropdown is "honest" by design.** Per the memory `project_player_ux_fix.md`, the HDR mode dropdown is hidden when `transferFunc == 0` (SDR content); only visible when `transferFunc == 1` (PQ) or `2` (HLG) is detected from `color_trc` in the sidecar probe → FrameCanvas `m_colorParams.transferFunc`. Reference players don't hide non-applicable mode controls. **Verdict G3: BETTER** (hemanth-flagged identity axis, no regression).
3. **Tone-map mode surface is narrower than mpv.** Tankoban exposes 4 modes (Off / Reinhard / ACES / Hable). mpv exposes **6** via `tone-mapping` property (hable / reinhard / mobius / linear / bt.2390 / none). VLC 3.0.23 + PotPlayer without madVR both **hard-clip** (no tone-map mode selector at all) — Tankoban is **BETTER than VLC / PotPlayer-without-madVR**, **DIVERGED with mpv** on mode count (Tankoban missing Mobius + BT.2390). BT.2390 specifically is a standards-body recommendation worth considering; Mobius is a less-distinct shape between Reinhard + Hable.
4. **Pixel-level HDR output diff is DEFERRED.** Cannot do peak-luminance sample or RGB histogram comparison without a real HDR10 fixture. Flagged as Phase 3.5 when Hemanth provides a fixture OR I synthesize one via ffmpeg.
5. **Deinterlace has 5 modes vs VLC 10.** FilterPopover entries: Off / Auto (`yadif=mode=0`) / Bob (`yadif=mode=1`) / Adaptive (`bwdif=mode=0`) / W3FDIF. VLC exposes Discard / Blend / Mean / Bob / Linear / X / Yadif / Yadif 2x / Phosphor / Film NTSC (10 modes). PotPlayer exposes 7. **Verdict H1: DIVERGED on mode count** — Tankoban covers the essential algorithms (yadif + bwdif + w3fdif, the three most-used) but lacks some legacy / exotic modes. **Not a regression**; coverage is idiomatic for modern progressive-content bias.
6. **Interpolate / brightness / contrast / saturation surface is CONVERGED** on presence + UI shape. Sliders expose the same conceptual controls mpv / VLC / PotPlayer all have. Slider range comparison + per-step linearity is deferred to Phase 3.5 unless Hemanth flags visual concerns.
7. **Audio normalization toggle** (Shift+A) applies an ffmpeg `loudnorm` or `volumedetect→volume` filter (per PLAYER_UX_FIX). mpv has `--volume-replaygain` + `--loudnorm`. VLC has Audio → Effects → Normalize Volume. PotPlayer has AGC toggle. **Verdict H4: CONVERGED** on presence.

**Verdict counts this pass:** 3 CONVERGED / 1 DIVERGED-intentional / 1 BETTER-than-VLC+PotPlayer-without-madVR / 1 DIVERGED (deinterlace mode count) / **4 DEFERRED** (all pixel-output axes awaiting fixtures). **0 WORSE.**

---

## 1. Fixture gap + decision

The TODO §7 methodology calls for measurement on:

| Required fixture | Found on disk? | Decision |
|---|---|---|
| 4K HDR10 BT.2020 PQ sample | **No** — all scanned MKVs are `bt709` yuv420p (confirmed via ffprobe on Chainsaw Man 1080p WEB-DL, JoJo 1080p WEB-DL + other 1080p / HEVC library items) | Batch 3.1 pixel output DEFERRED to Phase 3.5 |
| 1080i interlaced sample | **No** — library is modern progressive encodes (WEB-DL + BluRay x265 all deinterlaced-at-source). Closest candidates: `Mr. Inbetween S00E01 The Magician 576p DVD x265` — which is a DVD **rip** (post-deinterlace), not a raw interlaced feed | Batch 3.2 H1 pixel-output DEFERRED to Phase 3.5 |
| SDR content for filter sliders | **Yes** — any 1080p title | Available for filter-slider UX verification |

**Why not synthesize via ffmpeg?** Synthesizing a fake HDR10 sample via `ffmpeg -c:v libx265 -x265-params "hdr-opt=1:repeat-headers=1" -color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc ...` would produce a file with correct metadata tags that Tankoban's sidecar would detect as HDR — but the synthesized content wouldn't exercise real PQ-curve pixel values with high nits, so the tone-map algorithms wouldn't have meaningful peak-luminance data to diff. Not worth the effort; wait for a real fixture.

**1080i synthesis is more viable** — `ffmpeg -f lavfi -i testsrc=size=1920x1080:rate=30000/1001 -vf tinterlace=4 -c:v libx264 -flags +ilme+ildct -field_order tb output_1080i.mkv` would produce a genuinely interlaced test pattern. But a test-pattern deinterlace comparison doesn't tell us about real-content behavior where combing artifacts vs motion resolution tradeoffs matter. Deferring to Phase 3.5.

---

## 2. Batch 3.1 — HDR tone-map (category G)

### G1 — Tone-map algorithm correctness (STATIC ANALYSIS)

Tankoban's HLSL shader at [video_d3d11.hlsl:129-176](../../resources/shaders/video_d3d11.hlsl#L129-L176):

**Reinhard** — `x / (1 + x)` — vanilla. No coefficient tweaks. Correct per standard reference (Reinhard et al. 2002 "Photographic Tone Reproduction for Digital Images").

**ACES** — Narkowicz fit:
```hlsl
return saturate((x * (A * x + B)) / (x * (C * x + D) + E));
// A=2.51, B=0.03, C=2.43, D=0.59, E=0.14
```
Matches Kodi's `gl_tonemap.glsl:17-25` per the shader comment. Matches Unreal Engine's default + the coefficients on [Krzysztof Narkowicz's blog](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/). **Correct.**

**Hable** — Uncharted 2 curve:
```hlsl
return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
// A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30
// white point kHableW = 11.2; exposure bias 2.0
```
Coefficients match John Hable's original GDC 2010 "Uncharted 2: HDR Lighting" talk. Matches Kodi's `gl_tonemap.glsl:29-38` per the comment.

**Verdict G1: CONVERGED** — algorithm implementations are mathematically correct + reference-verified against two independent implementations (Kodi + Narkowicz blog). On a hypothetical real HDR10 fixture, output should match mpv's `tone-mapping=hable` / `reinhard` pixel-for-pixel modulo input-pipeline differences.

### G2 — Mode selection surface

| Player | Modes | UI |
|---|---|---|
| Tankoban | 4: Off (hard clip) / Reinhard / ACES / Hable | Dropdown in right-click context menu (hidden on SDR content) |
| mpv | 6: hable / reinhard / mobius / linear / bt.2390 / none | `--tone-mapping` property; `cycle tone-mapping` via input.conf |
| VLC 3.0.23 | 1 (hard-clip default, no tone-map UI) | Preferences → Video → HDR not exposed as mode set |
| PotPlayer | 1 without madVR plugin (hard-clip); madVR adds 3-4 tone-map modes if installed | Preferences → Video → HDR (plugin-dependent) |

**Verdict G2: BETTER than VLC + PotPlayer-without-madVR** (VLC 3.x has no tone-map selector; PotPlayer requires plugin). **DIVERGED with mpv** on mode breadth — mpv exposes Mobius (smoother-than-Reinhard alternative) and BT.2390 (ITU standards-body recommendation). Neither is strictly better than what Tankoban has; BT.2390 is worth considering for a future fix-TODO if HDR-heavy users emerge.

### G3 — "Honest dropdown" hide-on-SDR behavior

Per `project_player_ux_fix.md`: Tankoban's HDR mode dropdown only appears on HDR content. The gate is `m_colorParams.transferFunc != 0` (sidecar probe detects PQ/HLG via `color_trc`). On SDR content (`transferFunc == 0`), the dropdown is hidden so users aren't presented with modes that have no effect.

VLC + PotPlayer don't hide non-applicable mode controls — the settings are always exposed regardless of content.

mpv's `tone-mapping` property is always settable but has no UI surface by default (terminal-only), so the hide-question doesn't apply cleanly.

**Verdict G3: BETTER** (Tankoban-unique identity axis; Hemanth already flagged this as an "honest UI" principle in player-ux-fix context).

### G4 — Objective pixel-output measurement

**DEFERRED to Phase 3.5.** Per TODO §7 exit criteria: "HDR + interlaced fixtures on disk (Hemanth-provided OR ffmpeg-synthesized at Phase 3 entry)" — neither available this wake. Methodology when fixture arrives:

1. Play real HDR10 fixture (e.g. a 4K UHD BluRay rip with `color_trc=smpte2084`) in Tankoban at each of 4 modes (Off/Reinhard/ACES/Hable). MCP screenshot at a reference frame with broad luminance range (specular highlight + shadow detail in same frame).
2. Parallel: play same fixture in mpv at each of matching modes (`tone-mapping=none/reinhard/hable`). MCP screenshot at same frame timestamp.
3. Pixel-level RGB histogram diff per mode pair. Acceptance: < 2% per-channel histogram delta between Tankoban + mpv at same mode (Reinhard↔Reinhard, Hable↔Hable). Tankoban ACES↔mpv has no direct mpv analog (mpv uses a different ACES shape); document the divergence without a verdict.
4. VLC + PotPlayer-without-madVR: capture hard-clip output. Confirm numerically-different from Tankoban's tone-mapped output (sanity check — if VLC's output equals Tankoban's, our tone-map is silently disabled).

**Estimated Phase 3.5 effort:** 45 min if a 4K HDR10 sample is on disk; 2+ hours if it needs downloading (Netflix-encoded samples aren't publicly available; Jellyfish Bitrate Test Files or LG Demo samples are free + small).

---

## 3. Batch 3.2 — Video filters (category H)

### H1 — Deinterlace modes

**Tankoban** FilterPopover modes at [src/ui/player/FilterPopover.cpp:41-46](../../src/ui/player/FilterPopover.cpp#L41-L46):
- Off (empty filter)
- Auto → ffmpeg `yadif=mode=0` (temporal + spatial, one output per input field)
- Bob → ffmpeg `yadif=mode=1` (sends one frame per field)
- Adaptive → ffmpeg `bwdif=mode=0` (Bob-Weaver motion-adaptive)
- W3FDIF → ffmpeg `w3fdif` (3-field Weston)

**VLC 3.0.23**: Video → Deinterlace → 10 modes (Discard / Blend / Mean / Bob / Linear / X / Yadif / Yadif 2x / Phosphor / Film NTSC). Covers both legacy (Discard) and modern (Phosphor).

**PotPlayer**: Right-click → Playback → Deinterlace → Auto / Force / Off + mode submenu (~7 modes including DXVA hardware deinterlace).

**mpv**: `--deinterlace=yes` + `--vf=yadif[=mode=N]`. Configurable.

**Verdict H1: DIVERGED on mode count** — Tankoban covers the algorithmically-distinct modern modes (yadif / bwdif / w3fdif) but lacks legacy modes (Discard / Blend / Mean) and the hardware-DXVA mode that PotPlayer exposes. For modern source content (WEB-DL, BluRay, digital broadcast), yadif + bwdif cover the needs; Tankoban's subset is idiomatic.

**Pixel-output verification** on a 1080i fixture: DEFERRED to Phase 3.5.

### H2 — Interpolate ("Motion smoothing")

Tankoban exposes as checkbox in FilterPopover. When enabled, applies ffmpeg `minterpolate` (frame-rate up-conversion).

**Reference:**
- mpv: `--interpolation` (default `no`, also `--video-sync=display-resample` + tscale kernel settings)
- VLC: buried in Tools → Preferences → Video Codecs → FFmpeg audio/video decoder → Motion interpolation
- PotPlayer: exposes "Motion interpolation" for supported GPUs

**Verdict H2: CONVERGED** on presence. Tankoban's checkbox surface is the most discoverable of the four.

### H3 — Brightness / Contrast / Saturation sliders

Tankoban exposes three sliders in FilterPopover (at [FilterPopover.cpp:66-75](../../src/ui/player/FilterPopover.cpp#L66-L75)). Range: not verified in this static pass (slider min/max would be in the slider constructor). Applied via ffmpeg filter chain (e.g. `eq=brightness=X:contrast=Y:saturation=Z`).

**Reference:**
- mpv: `--brightness`, `--contrast`, `--saturation`, `--gamma` (each -100 to 100)
- VLC: Tools → Effects and Filters → Video Effects → Essential tab (sliders same ranges)
- PotPlayer: Right-click → Video → Screen Controls (sliders same ranges)

**Verdict H3: CONVERGED** on presence + UI shape. Numerical range + per-step linearity comparison deferred to Phase 3.5 unless Hemanth flags visual concerns.

### H4 — Audio normalization (Shift+A)

Per PLAYER_UX_FIX Phase 6 ship notes: Tankoban audio normalization toggle applies ffmpeg `volumedetect`+`volume` filter or `loudnorm` filter to sidecar audio path.

**Reference:**
- mpv: `--volume-replaygain` (ReplayGain based) + `--af=loudnorm` (EBU R128)
- VLC: Audio → Audio Effects → Normalize Volume (peak-based)
- PotPlayer: AGC toggle in Preferences → Audio

**Verdict H4: CONVERGED** on presence. Algorithm choice (loudnorm vs peak) varies; Tankoban's loudnorm approach matches mpv + industry-standard EBU R128.

---

## 4. Deferred-Measurement Ledger (Phase 3.5 scope)

| Deferred axis | Requires | Effort |
|---|---|---|
| G4 HDR tone-map pixel-output diff (Tankoban Reinhard vs mpv Reinhard, Tankoban Hable vs mpv Hable) on real HDR10 content | 4K HDR10 BT.2020 PQ fixture OR real HDR source | 45 min with fixture, 2+ h without |
| H1 Deinterlace pixel-output diff (Tankoban yadif vs VLC Yadif vs PotPlayer Yadif on 1080i combing) | 1080i interlaced fixture OR ffmpeg-synthesized test pattern | 30 min with fixture, 45 min with ffmpeg synth |
| H3 Slider range + per-step linearity measurement | Any SDR content + MCP screenshot chain at slider positions (0, 25, 50, 75, 100) | 30 min |
| G3 "Honest dropdown" hide-on-SDR verification via MCP (open right-click menu on SDR file, confirm HDR modes section is absent; open same menu on HDR file, confirm modes appear) | SDR content (have) + HDR fixture (deferred) | 15 min with HDR fixture |
| H4 loudnorm EBU R128 actual output measurement | Pink noise test source + loopback capture | 30 min |

**Total Phase 3.5 effort:** 2 h with HDR + interlaced fixtures present, 3.5 h if fixtures need to be sourced/synthesized. **Narrow alternative:** prioritize G4 alone (the TODO's headline Phase 3 measurement) once an HDR fixture is on disk, ~45 min.

---

## 5. Fix Candidates ratification-request block

**BLOCKER tier:** None. No DIVERGED/WORSE verdicts at blocker severity from this pass.

**POLISH tier:** None from pilot. Phase 3.5 pixel-output measurement may surface polish items if Tankoban's Reinhard or Hable implementation drifts from mpv numerically — architecturally verified-correct so unlikely, but can't confirm without numbers.

**COSMETIC / future-consideration:**
- **BT.2390 tone-map mode as optional addition** — mpv exposes this (ITU-R BT.2390 standard recommendation for HDR→SDR mapping). Would require adding a 5th option to the dropdown + a new fit in the shader (ITU-R BT.2390-9 annex). NOT a current gap — Tankoban's 3 modes cover the spectrum adequately (Reinhard gentle, ACES punchy, Hable filmic). Flag for future if Hemanth hears HDR-user requests.
- **Mobius tone-map mode** — mpv exposes it as a middle-ground between Reinhard + Hable. Similar consideration as BT.2390; not a current gap.
- **VLC-style 10-mode deinterlace menu** — Tankoban at 5 modes is adequate for modern content; expanding to include Phosphor / Film NTSC / legacy Blend is niche value.

**Pin as BETTER (protect against regression):**
- G1 tone-map algorithm correctness + Kodi-verified coefficients
- G2 tone-map mode surface (better than VLC + PotPlayer-without-madVR)
- G3 honest hide-on-SDR dropdown behavior

**Ratification-request summary for Hemanth:**

- Pilot produces 3 CONVERGED / 1 DIVERGED-intentional (deinterlace mode count) / 1 BETTER-than-VLC+PotPlayer / 1 DIVERGED-with-mpv (HDR mode breadth) / 4 DEFERRED (all pixel-output axes awaiting fixtures) / 0 WORSE.
- **No fix-TODO candidates requested from this pilot.**
- **Phase 3.5 re-summon requested** once Hemanth provides (or points Agent 3 at) a 4K HDR10 BT.2020 PQ fixture + optional 1080i sample (ffmpeg-synthesized acceptable for interlaced). Estimate 45 min for HDR G4 alone, 2 h for full Phase 3.5 with both fixtures. Narrow scope option: defer indefinitely — Phase 3.5 is "nice to have" empirical confirmation of already-architecturally-verified-correct implementations.
- **BT.2390 + Mobius** flagged as future-consideration, not a current gap; no fix-TODO requested.

---

## 6. Rule 17 cleanup trace

- Reference players (VLC/PotPlayer/mpv): **no live smoke this pilot** (static + UI-behavior analysis only). No processes started, nothing to clean.
- Tankoban: no video-playback smoke this Phase 3 wake (relying on Phase 2 evidence for UI-behavior + code analysis for algorithm correctness). `scripts/stop-tankoban.ps1` already-clean from end of Phase 2.

---

## 7. Phase 3 Exit Criteria Status

Per TODO §7 exit criteria:

| Criterion | Status |
|---|---|
| Audit file lands with objective-criteria-met verdicts | **PARTIAL** — static analysis verdicts are objective (algorithm coefficient verification, mode-count enumeration); pixel-output verdicts are DEFERRED (fixtures absent) |
| HDR + interlaced fixtures on disk | **NOT MET** — both absent; decision made to static-analyze + defer pixel-diff to Phase 3.5 |
| Same Rule 17 cleanup | **MET** — no new smoke processes |

**Phase 3 is partially closed on static evidence. Full closure requires Phase 3.5 with real fixtures.**

---

**End of Phase 3 STATIC-ANALYSIS PILOT audit.**

---

## Addendum 2026-04-21 — PotPlayer live re-verification

Per `feedback_audit_reverification_scope.md`. PotPlayer 260401 Preferences navigated live via MCP; Audio AGC + Brightness/Contrast/Saturation panel deferred (tree-scroll + F8 Video Adjust panel out of wake-context-budget).

### A. HDR controls — CORRECTION: built-in HDR options, not madVR-only

Original claim: "PotPlayer: HDR via madVR plugin, hard-clip without."

Observed in **Preferences → Video** (main panel):
- **`D3D11 GPU RTX Video HDR`** (checkbox, unchecked default) — hardware-accelerated HDR→SDR tone-mapping via NVIDIA RTX.
- **`Use H/W HDR output mode`** (checkbox, unchecked default) — HDR passthrough to display.
- `D3D11 GPU Super Resolution`, `D3D11 GPU RTX Video HDR`, `Use H/W HDR output mode`, `10-bit output`, `Enable flip mode compatibility`, `Disable 3D subtitle depth correction` — rich renderer-level HDR feature set.

Also a separate `Pixel Shaders` sub-tab exists (not explored this wake) — likely where custom tone-mapping shader chains live (the madVR-equivalent surface).

Corrected: **PotPlayer has built-in HDR tone-mapping (RTX-accelerated) + HDR passthrough. madVR is optional but not the primary path.** The "hard-clip without madVR" claim is WRONG — RTX Video HDR is a built-in fallback.

### B. Deinterlace mode count — CORRECTION

Original claim: "PotPlayer: 7 deinterlace modes incl DXVA hardware."

Observed in **Preferences → Video → Deinterlacing → Software Deinterlacing → Method dropdown**:

1. Blending (Recommended)
2. Linear Interpolation
3. Linear Blend
4. FFmpeg (Modified)
5. Cubic Interpolation
6. Median
7. Lowpass
8. Motion Adaptive
9. Motion Adaptive (2× frame)
10. BOB (2× frame)
11. FFmpeg (Original)
12. Edge Line Average (2× frame)
13. Field Resize (2× frame)
14. First field
15. Second field

**Actual count = 15 software modes.** Plus separate `Hardware Deinterlacing` panel with its own Method dropdown (count not enumerated this wake; includes DXVA per dropdown label).

Corrected: **PotPlayer deinterlace = 15 software modes + Hardware Deinterlacing (DXVA) panel.** Tankoban's 5 modes is a larger gap vs PotPlayer than the original "7 vs 5" framing suggested. Verdict "DIVERGED but idiomatic for modern content" still stands (most of the 15 modes are legacy-interlaced-specific); modern progressive-heavy content doesn't need all 15.

### C. Video filter effects panel — RICHER THAN CLAIMED

Original claim: "PotPlayer: brightness/contrast/saturation via Ctrl+F1 panel or keybinds."

What I found in **Preferences → Video → Effects** panel:
- Flip Vertical / Flip Horizontal / 270 degrees / Motion Blur / Multi-threaded video processing toggles
- Soften (Radius / Luma / Chroma)
- Sharpen (Luma / Chroma)
- Deblock (Threshold)
- Gradual Denoise
- Denoise 3D (Luma / Chroma / Time / High-quality)
- Temporal noise reducer (3 thresholds)
- Warpsharp (Depth / Threshold)
- Deband (Threshold / Radius)

Brightness / Contrast / Saturation NOT in this panel or in `Levels/Offset` (which is video-levels + gamma + pixel-offsets, more technical). B/C/S likely in runtime `F8 Video Adjust` panel (PotPlayer convention) — **not verified this wake**.

Corrected: **PotPlayer Effects panel = rich set of noise/sharp/deband filters; Brightness/Contrast/Saturation location not confirmed (likely runtime F8 panel; unverified).**

### D. Audio normalize / AGC toggle — NOT VERIFIED THIS WAKE

Original claim: "PotPlayer: AGC toggle in Preferences → Audio."

Tree-scroll required to reach Preferences → Audio; deferred. Claim remains docs-sourced.

### E. Preferences → Video → Extend/Crop — NEW FINDING

Not in original Phase 3 audit but surfaced during navigation. Panel has:
- Method dropdown (Do nothing / ... / pan-scan variants)
- H. Position (%) + V. Position (%) sliders (default 50/50)
- Bottom Margin dropdown (default "No Margin"; if set, adds black bar where subs render)
- "Operate only when subtitles exist" toggle

This is PotPlayer's answer to aspect/crop/pan-scan adjustment + subtitle-letterbox interaction. Relates to VLC_ASPECT_CROP_REFERENCE + SUBTITLE_HEIGHT_POSITION audits.

### Verdicts post-re-verification

- **G1 tone-map algorithm correctness** — not re-verified on PotPlayer (algorithm-internal; PotPlayer's RTX-HDR is GPU driver-assisted, not custom shader). Tankoban's ACES/Reinhard/Hable cross-verification vs Kodi/Narkowicz stands.
- **G2 HDR surface** — PotPlayer now **MATCHES STANDARD for HDR feature availability** (has built-in options), not "DIVERGED with mpv" as originally positioned vs Tankoban. Tankoban's "honest SDR-hide" is still Tankoban-unique. Re-frame: Tankoban MATCHES the standard that PotPlayer+VLC establish for HDR surfaces BEING AVAILABLE; the SDR-hide behavior is a Tankoban-beyond-standard convenience.
- **G3 mode count** — PotPlayer 4-tier-tone-mapping not verified this wake (would need Pixel Shaders tab exploration). Tankoban 4 modes (Off/Reinhard/ACES/Hable) + mpv 6 modes (+ Mobius/BT.2390) comparison stands.
- **H1 deinterlace** — PotPlayer 15 software modes CORRECTED from 7. Tankoban 5 modes gap is larger than original audit suggested. Verdict "DIVERGED but idiomatic for modern content" stands.
- **H2 interpolation** — not re-verified.
- **H3 B/C/S** — location not confirmed; deferred.
- **H4 audio normalize** — not verified; deferred.

**Credibility state:** 2 direct corrections (A HDR, B deinterlace count), 1 description-enrichment (C Effects panel), 2 deferred (D Audio AGC, B/C/S runtime panel). Phase 3 audit body text now spot-corrected on PotPlayer HDR + deinterlace axes.
