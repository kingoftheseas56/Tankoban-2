# Archive: Player Polish Phase 3 — HDR Color Pipeline

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source:**
- OBS `libobs/data/color.effect` (PQ ST.2084 + HLG inverse transfer functions)
- OBS `libobs/data/format_conversion.effect:37-43` (BT.2020 → BT.709 gamut matrix)
- OBS `libobs-d3d11/d3d11-subsystem.cpp:67-116` (HDR swap chain detection pattern)
- Kodi `system/shaders/GL/1.5/gl_tonemap.glsl` (Reinhard / ACES / Hable coefficients)

**Outcome:** REVIEW PASSED 2026-04-15.
**Shape:** 0 P0, 3 P1 initially, 8 P2. Two P1s closed in-session by Agent 3's P1 fix-batch; third P1 deferred with justification as tracked open debt in `PLAYER_POLISH_TODO.md:164`. Five P2 tracked as forward-port debt, three P2 closed as design choices.

---

## Initial review (Agent 6)

Scope: all five batches (3.1 BT.2020→709 gamut matrix + setHdrColorInfo API; 3.2 PQ ST.2084 inverse EOTF; 3.3 HLG inverse OETF+OOTF; 3.4 Reinhard/ACES/Hable operators wired through FilterPopover; 3.5 adaptive R16G16B16A16_FLOAT/scRGB swap chain + shader hdrOutput branch). All color math in HLSL per locked design.

Files reviewed:
- `resources/shaders/video_d3d11.hlsl`
- `src/ui/player/FrameCanvas.h`
- `src/ui/player/FrameCanvas.cpp` (showEvent + setTonemapMode + setHdrColorInfo)
- `src/ui/player/VideoPlayer.cpp` (mediaInfo handler + FilterPopover::toneMappingChanged binding)

### Parity (Present) — 9 features correct as shipped

- PQ ST.2084 inverse EOTF — verbatim OBS `color.effect:65-74` port.
- HLG inverse OETF — verbatim OBS `color.effect:158-172` port (BT.2020 weights 0.2627 / 0.678 / 0.0593).
- BT.2020→709 gamut matrix — OBS `format_conversion.effect:37-43` coefficients to 8 decimal places.
- ACES (Narkowicz) — coefficients match Kodi `gl_tonemap.glsl:17-25` exactly.
- Hable (Uncharted 2 filmic) — coefficients + white point 11.2 + exposure bias 2.0 match Kodi and the original paper.
- HDR swap chain detection skeleton — `EnumOutputs` → QI IDXGIOutput6 → GetDesc1 → ColorSpace inspect → `R16G16B16A16_FLOAT` + `SetColorSpace1(G10_NONE_P709)`.
- Pipeline ordering — EOTF decode → gamut matrix → BCS controls → (hdrOutput branch OR tonemap+gamma).
- sRGB → linear piecewise — IEC 61966-2-1 formulation.
- `setHdrColorInfo` plumbing — ABI-stable AVCOL_* integer constants, no libavutil leak into main-app.
- FilterPopover wiring — clean hotkey-collision reroute.
- ColorParams cbuffer layout — 32 bytes preserved through `pad2 → hdrOutput` rename.

### Gaps

**P0:** none.

**P1:**

1. **HDR detection queries primary adapter output only; OBS queries per-window monitor.** `FrameCanvas.cpp:149` called `dxgiAdapter->EnumOutputs(0, &output0)`. OBS at `d3d11-subsystem.cpp:71-78` uses `MonitorFromWindow + GetMonitorColorInfo(hMonitor)`. Impact: multi-monitor user with HDR secondary and SDR primary (or vice-versa) gets wrong swap chain branch.

2. **`kPqToSdrScale = 100.0f` applied unconditionally in PQ branch — wrong magnitude for HDR passthrough on scRGB.** scRGB convention is 1.0 = 80 nits, so 10000 nits = 125.0 (not 100.0). Net: HDR10-on-HDR-display outputs at ~64% of intended luminance.

3. **OBS's `get_next_space` three-case ladder collapsed to two — wide-gamut 10-bit SDR displays don't get a 16F linear buffer.** Missing the `GS_CS_SRGB_16F` intermediate case that OBS picks when `info.bits_per_color > 8 && !info.hdr`.

**P2:**

1. Reinhard citation mismatch: READY FOR REVIEW cites OBS `color.effect` for Reinhard, but OBS has no Reinhard in `color.effect`. Shipped form is vanilla `x/(1+x)` — neither OBS's peculiar `format_conversion.effect:45-52` variant nor Kodi's parameterized form. Agent 3 resolved: comment fixed, defensible choice as vanilla textbook Reinhard.
2. HLG OOTF exponent hardcoded at 0.2 (Lw=1000 nits); `MaxLuminance` read but not plumbed.
3. No color-space re-detection on monitor-switch drag.
4. Gamut matrix coefficients truncated from 17 sig-fig to 10 sig-fig (delta < float precision, cosmetic).
5. `aces_narkowicz` + `hable` add `saturate()` wrappers absent from Kodi reference.
6. `sendSetToneMapping` no-op stub + `peakDetect` ignored arg — forward-port debt.
7. Brightness/contrast/saturation applied to unclamped linear values in HDR output (documented limit).
8. Doc nit in PLAYER_POLISH_TODO.md about sRGB EOTF "2.4 approximation" wording.

### Questions asked

1. Is test display primary or secondary? (Agent 3: single-monitor SDR, P1 #1 latent but fixed anyway.)
2. Was `*100` in HDR path intentional? (Agent 3: slipped through — Batch 3.2 added the constant pre-HDR-branch and Batch 3.5 didn't re-scope.)
3. Wire `MaxLuminance` → HLG exponent now or defer? (Agent 3: defer to Phase 3 polish follow-on.)
4. Keep `saturate()` on ACES + Hable or drop to match Kodi? (Agent 3: keep; defense-in-depth for pathological inputs; documented.)
5. `sendSetToneMapping` stub timing — close before Phase 4 opens? (Agent 3: bundled with Phase 3 polish follow-on.)

---

## Agent 3 response (fix-batch)

Triage: P1 #1 and P1 #2 addressed in-session; P1 #3 deferred to Phase 3 polish follow-on bundle with the P2 debts.

**P1 #1 fix** — `FrameCanvas::showEvent` HDR detection rewritten:
- `MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY)` → HMONITOR for window's actual monitor.
- Iterate `EnumOutputs(i)`, call `GetDesc`, match `odesc.Monitor == targetMonitor`, take ownership on match.
- Fallback to `EnumOutputs(0)` when no match (rare — window off all known monitors during drag).
- Matched output flows into existing IDXGIOutput6 QI + GetDesc1 path. Pattern mirrors OBS `d3d11-subsystem.cpp:71-78`.

**P1 #2 fix** — shader PQ branch:
- Added `kPqToScRgbScale = 125.0f` next to existing `kPqToSdrScale = 100.0f`.
- PQ decode: `st2084_to_linear(rgb) * ((hdrOutput == 1) ? kPqToScRgbScale : kPqToSdrScale);`
- SDR path unchanged — Batch 3.4 Reinhard/ACES/Hable tonemap calibration preserved byte-for-byte.
- HDR path: 10000-nit peak → scRGB 125.0 (correct display luminance).

**P1 #3 defer** — scope exceeds fix-batch budget:
- Detecting wide-gamut 10-bit SDR requires deeper DXGI probing (enum variants beyond binary HDR check + bit-depth).
- 16-bit float without scRGB passthrough is a third `hdrOutput` value, not a boolean.
- Tracked at `PLAYER_POLISH_TODO.md:164` as open debt; bundles with P2 cross-monitor re-detection + P2 HLG exponent parameterization in a Phase 3 polish follow-on batch.

Files touched by fix-batch: `src/ui/player/FrameCanvas.cpp`, `resources/shaders/video_d3d11.hlsl`. No CMakeLists changes, no Phase 4 files touched.

---

## Agent 6 re-verdict

**Both P1 fixes code-verified:**

- `FrameCanvas.cpp:153-193` — MonitorFromWindow + EnumOutputs walk + HMONITOR match + fallback + IDXGIOutput6 path. Mirrors OBS pattern. ✓
- `video_d3d11.hlsl:89-90 + 207` — dual scale constants + `hdrOutput`-branched selector in the PQ decode. Math correct: 10000 nits / 80 nits-per-unit = 125.0. ✓

**Minor observation (not blocking):** Agent 3's fix uses `MONITOR_DEFAULTTOPRIMARY`; OBS uses `MONITOR_DEFAULTTONEAREST`. Practical divergence only when the window is off all known monitors mid-drag — `TONEAREST` snaps to geometrically closest, `TOPRIMARY` snaps to primary. Both defensible. Call it a forward-port nit alongside the other Phase 3 polish debt.

**P1 #3 defer accepted.** Tracked open debt at `PLAYER_POLISH_TODO.md:164` + bundling with the two related P2s into a coherent future polish batch is the right scope call. The 10-bit SDR case requires adding a third `hdrOutput` cbuffer value and a different `SetColorSpace1` branch (`G22_NONE_P709`) — that's a standalone feature, not a fix-batch squeeze.

**P2 dispositions at archive:**
- P2 #1 Reinhard citation → closed by Agent 3 (comment corrected).
- P2 #2 HLG exponent, P2 #3 cross-monitor re-detect → bundled with P1 #3 into Phase 3 polish follow-on.
- P2 #4 matrix coefficient precision → cosmetic; accepted as shipped.
- P2 #5 `saturate()` on ACES + Hable → design choice; Agent 3 keeps + documents.
- P2 #6 `sendSetToneMapping` stub → bundled with Phase 3 polish follow-on.
- P2 #7 BCS in HDR → known limit, acceptable.
- P2 #8 doc nit → non-blocking wording fix.

### Verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (P1 #1 fixed; P1 #2 fixed; P1 #3 deferred-as-documented-debt)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, Player Polish Phase 3]** 2026-04-15.

Phase 3 closes. Phase 3 polish follow-on batch carries P1 #3 + P2 #2 + P2 #3 + P2 #6 + TOPRIMARY/TONEAREST nit. Phase 4 (already in-flight via Batch 4.1 + 4.2) is clear for its own review cadence when shipped.
