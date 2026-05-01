# Task 4 Phase A — mpv HDR pipeline survey + libplacebo reference
## MAKE_MPV_SOLO Task 4 prep, 2026-05-01 ~13:55pm

This is the Phase A deliverable per `~/.claude/plans/2026-05-01-make-mpv-solo-task-4-hdr-tonemapping.md`. Pure documentation of current state across both backends. No code changes. Phase B begins after this with a baseline mpv-defaults smoke.

---

## Q1 — What HDR-related setOpt calls are in mpv's `initializeMpv` today?

**ZERO.** Lines 145-219 of `src/ui/player/MpvBackend.cpp` only set:
- Hermetic config + audio identity (`config=no`, `audio-client-name=Tankoban`, `hwdec=auto`)
- vo wiring (`vo=null` → switched to `libmpv` after render context creates per MpvVideoWidget.cpp:106-115)
- OSD off (`osd-level=0`)
- EOF behavior (`keep-open=yes`)
- Subtitle margins + visibility + Phase 2.D Q5 visual-spec colors / outline / shadow / `sub-ass-override=no`

No `tone-mapping=`, `target-peak=`, `hdr-compute-peak=`, `target-trc=`, or `target-prim=`. mpv runs all defaults on HDR files.

The mpv defaults that actually apply on Hemanth's machine when an HDR file plays today:
- `tone-mapping=auto` — recent mpv (≥0.36) picks bt.2446a; older picks hable
- `target-peak=auto` — mpv reads system display peak (Windows monitor info → typically ~80-120 nits on a laptop SDR panel)
- `hdr-compute-peak=auto` — equivalent to `yes` on most builds
- `target-trc=auto` — mpv picks bt.1886/sRGB based on display profile
- `gamut-mapping-mode=auto` — perceptual on most modern builds
- `tone-mapping-mode=auto` — picks luma or rgb depending on algo

Per Pattern F from Task 1 baseline: ffmpeg HDR rendering on Boys S03E01 was acceptable to Hemanth. mpv defaults may already match. Phase B's smoke decides.

---

## Q2 — What does `MpvBackend::sendSetToneMapping` do today?

`src/ui/player/MpvBackend.cpp:1055-1061`:

```cpp
int MpvBackend::sendSetToneMapping(const QString& algorithm, bool peakDetect)
{
    if (!m_mpv) return -1;
    if (!algorithm.isEmpty()) setOpt(m_mpv, "tone-mapping", algorithm.toUtf8().constData());
    setFlag(m_mpv, "hdr-peak-decay-rate", peakDetect);
    return nextSeq();
}
```

The `setFlag` line is buggy — `hdr-peak-decay-rate` is mpv's **numeric** decay-rate option (default 100ms), not a boolean flag. Setting it via `setFlag` (which calls `mpv_set_option_string` with `"yes"` or `"no"`) silently no-ops or gets clamped. The boolean for enabling per-scene peak detection is `hdr-compute-peak=yes/no`. Phase E fixes this.

Tankoban-side implication: even if a caller passed `peakDetect=true`, mpv never received the toggle. The function's effective behavior today is "set tone-mapping algo if non-empty, do nothing else." Latent bug — see Q4 below for whether anything actually calls this.

---

## Q3 — What does the ffmpeg-side libplacebo path do for HDR rendering?

Two surfaces in `native_sidecar/src/gpu_renderer.cpp`:

**`set_hdr_metadata(int color_primaries, int color_trc, int color_space, double max_lum, double min_lum, int max_cll, int max_fall)`** (lines 240-279):
- Maps AVCOL_PRI_* → `pl_color_space.primaries` (BT.709=PL_BT_709, BT.2020=PL_BT_2020, DCI_P3 (11/12)=PL_DCI_P3)
- Maps AVCOL_TRC_* → `pl_color_space.transfer` (BT.709=BT_1886, sRGB=SRGB, PQ=PQ, HLG=HLG)
- Stuffs mastering luminance (`max_luma`/`min_luma`) and CLL/FALL into `pl_color_space.hdr` for libplacebo's tone-mapping math
- Stash AVCOL_SPC_* for the SDR libplacebo path (P2 single-renderer arc)
- Invoked from `main.cpp:914` only when `probe->hdr` is true at file open

**`set_tone_mapping(const std::string& algorithm, bool peak_detect)`** (lines 218-238):
- Maps algorithm string to `pl_tone_map_function*`:

| Tankoban string | libplacebo function |
|---|---|
| `"reinhard"` | `&pl_tone_map_reinhard` |
| `"bt2390"` | `&pl_tone_map_bt2390` |
| `"clip"` | `&pl_tone_map_clip` |
| `"mobius"` | `&pl_tone_map_mobius` |
| `"linear"` | `&pl_tone_map_linear` |
| `"hable"` (also default-fallback) | `&pl_tone_map_hable` |
| anything else | `&pl_tone_map_hable` (silent fallback) |

- Toggles `peak_detect_params` via `peak_detect` boolean
- Note: NOT in the Tankoban string set: `"bt2446a"`, `"auto"`. Those won't reach gpu_renderer today through this code path, even if some caller passed them.

**Initialization defaults** (gpu_renderer.cpp:58):
```cpp
color_map = pl_color_map_default_params;
```
libplacebo's own defaults: `tone_mapping_function = &pl_tone_map_auto` (which dynamically picks bt.2446a on PQ content, hable on HLG, etc.) + `peak_detect_params = nullptr` (no peak detection by default).

**So:** when nothing calls `set_tone_mapping` (which is the live state today per Q4), HDR files render through libplacebo's `pl_tone_map_auto` → typically bt.2446a → with NO peak detection. That's the Pattern F baseline that Hemanth subjectively passed on Boys S03E01.

---

## Q4 — Is the FilterPopover tone-mapping picker live today?

**Searched src/ for `toneMappingChanged`, `sendSetToneMapping` callers:**
- `IPlayerBackend.h:157` — abstract API declaration
- `MpvBackend.cpp:1055` — mpv implementation
- `MpvBackend.h:108` — header declaration
- `SidecarProcess.h:83` + `SidecarProcess.cpp:433` — ffmpeg implementation
- **No emitters. No connect statements. No `setToneMapping(...)` call sites in the entire src/ tree.**

The abstract API exists but nothing wires it to a UI surface. `FrameCanvas.h:63` has a stale comment referring to "existing FilterPopover::toneMappingChanged handler" — but a grep for `FilterPopover::toneMappingChanged` and `toneMappingChanged` returns no live emit. Historical leftover from a removed FilterPopover feature.

**Implication for Phase D iteration table:** the "match libplacebo's currently-set algo" row in the plan is a no-op suggestion since libplacebo today is on `pl_tone_map_auto` (no Tankoban override). The mpv equivalent of `pl_tone_map_auto` is `tone-mapping=auto`. Keep that as the reference if iteration calls for it.

**Implication for Phase E:** the `setFlag(hdr-peak-decay-rate, peakDetect)` bug fix is precautionary — no code path today actually passes a `peakDetect` boolean to `sendSetToneMapping`. Fix anyway because (a) it's a 1-line fix and (b) Task 9 may wire the FilterPopover, at which point the fix becomes load-bearing.

---

## Q5 — What does mpv expose at the property level?

The full set of HDR-relevant mpv properties (queryable in libmpv 0.36+, all tunable via `mpv_set_option_string`):

| Property | Type | Default | Range | Effect |
|---|---|---|---|---|
| `tone-mapping` | string | `auto` | `auto`, `clip`, `mobius`, `reinhard`, `hable`, `gamma`, `linear`, `spline`, `bt.2390`, `bt.2446a`, `st2094-40`, `st2094-10` | Algorithm for HDR→SDR luminance mapping |
| `tone-mapping-param` | float | `default` | algo-specific (e.g., mobius peak-handling parameter) | Per-algorithm tuning knob |
| `tone-mapping-mode` | string | `auto` | `auto`, `rgb`, `max`, `hybrid`, `luma` | Mapping space (RGB vs luma vs hybrid) |
| `target-peak` | string/int | `auto` | `auto` or `1`–`10000` (nits) | Target display peak luminance |
| `hdr-compute-peak` | yes/no/auto | `auto` | `yes`, `no`, `auto` | Per-scene peak detection (matches libplacebo `peak_detect_params`) |
| `hdr-peak-decay-rate` | float | `100.0` | `0.0`–`1000.0` (ms) | Smoothing rate for peak detection (numeric — NOT a flag) |
| `hdr-scene-threshold-low` | float | `5.5` | dB | Lower threshold for scene-change peak reset |
| `hdr-scene-threshold-high` | float | `10.0` | dB | Upper threshold |
| `target-trc` | string | `auto` | `auto`, `bt.1886`, `srgb`, `linear`, `gamma1.8`, `gamma2.2`, `pq`, `hlg` | Output transfer curve |
| `target-prim` | string | `auto` | `auto`, `bt.709`, `bt.470bg`, `bt.2020`, `dci-p3`, `display-p3`, … | Output primaries |
| `gamut-mapping-mode` | string | `auto` | `auto`, `clip`, `perceptual`, `relative`, `saturation`, `absolute`, `desaturate` | How to handle out-of-gamut colors |
| `icc-profile-auto` | yes/no | `no` | — | Auto-detect Windows ICC profile from monitor |

For Tankoban Phase B baseline, the relevant ones to watch are: `tone-mapping`, `target-peak`, `hdr-compute-peak`, `target-trc`. Phase D iteration knob mapping (per the plan) covers all of these.

---

## Tankoban-filter-string ↔ mpv-property ↔ libplacebo-function table

Phase D's "what knob to twist" reference. Reading off this table on a Hemanth complaint should produce one mpv `setOpt` change.

| Tankoban filter UI | mpv `tone-mapping=` | libplacebo `tone_mapping_function` | Notes |
|---|---|---|---|
| (no UI today; default) | `auto` | `&pl_tone_map_auto` | What mpv + libplacebo both run today |
| "Hable" | `hable` | `&pl_tone_map_hable` | Older default; gentle highlight rolloff; can wash out colors |
| "Mobius" | `mobius` | `&pl_tone_map_mobius` | Mid-rolloff; less saturated than hable |
| "Reinhard" | `reinhard` | `&pl_tone_map_reinhard` | Aggressive compression; rarely used |
| "BT.2390" | `bt.2390` | `&pl_tone_map_bt2390` | ITU standard; older spec |
| "BT.2446A" | `bt.2446a` | (libplacebo `bt2446a`; not in current Tankoban string switch) | Modern ITU-R Method A; mpv current default via `auto` |
| "Clip" | `clip` | `&pl_tone_map_clip` | No mapping; truncates to display range |
| "Linear" | `linear` | `&pl_tone_map_linear` | Flat scaling; no perceptual adjustment |

---

## Render-context-level HDR signaling — out of Phase B-E scope

`MpvVideoWidget::createRenderContextIfReady` (MpvVideoWidget.cpp:82-103) creates the mpv render context with PURE OpenGL params:
- `MPV_RENDER_PARAM_API_TYPE = MPV_RENDER_API_TYPE_OPENGL`
- `MPV_RENDER_PARAM_OPENGL_INIT_PARAMS = {get_proc_address, get_proc_address_ctx}`

NO HDR-related render params (e.g., `MPV_RENDER_PARAM_ICC_PROFILE`, `MPV_RENDER_PARAM_AMBIENT_LIGHT`). All tone-mapping happens inside libmpv's render pipeline before the FBO write. For Hemanth's SDR display, this is fine — the property-level knobs in `initializeMpv` are sufficient. If Phase D's escalation gate fires (4 iterations stuck on YELLOW), this is the next layer to investigate.

---

## Pre-flight checklist for Phase B

Before Phase B fires:
- [x] mpv defaults documented above (`auto` everywhere)
- [x] libplacebo defaults documented (also `pl_tone_map_auto`, no peak detect)
- [x] Pattern F bar known (Boys S03E01 on ffmpeg = acceptable to Hemanth)
- [x] Test files on disk (Boys S03E01 confirmed at `C:/Users/Suprabha/Desktop/Hemanth's Folder/The Boys (2019) Season 3 S03 ...`)
- [x] Bug at sendSetToneMapping documented for Phase E (precautionary fix; not load-bearing today)
- [x] No FilterPopover wiring expected to break — there is no FilterPopover live today

Phase B can proceed when Hemanth gives the go-ahead for an MCP smoke session.
