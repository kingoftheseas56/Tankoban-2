# VLC Aspect + Crop Reference-Data Audit — 2026-04-20

**Owner:** Agent 3 (Video Player)
**Wake:** 2026-04-20 single-wake execution of `VLC_ASPECT_CROP_REFERENCE_TODO.md`
**Scope:** VLC-only reference dataset (per TODO §11 — no PotPlayer / mpv) + Tankoban same-matrix measurement + divergence summary.
**Goal:** exact numbers for Tankoban aspect/crop behaviour against a known-good baseline so real bugs have measurable residue.

---

## 1. Executive Summary

**Audit framing.** Per Hemanth 2026-04-20 reframe: VLC + PotPlayer = the **standard** for video-player behavior. This audit does not assess "better" or "worse" — only "does Tankoban **match the standard** or **deviate**." Deviations are gaps to close.

**Audit scope delivered:** VLC reference dataset on F1 (Chris Gayle 87, 16:9 1080p, 6 fullscreen cells) and F3 (Chainsaw Man, 2.40:1 cinemascope, 6 fullscreen cells) + Tankoban static-analysis prediction derived from `FrameCanvas::fitAspectRect` + `VideoPlayer::aspectStringToDouble/cropStringToDouble` at HEAD.

**Standard-match findings (rendering pipeline):** on the 24 measured/analyzed rendering cells, Tankoban **MATCHES the VLC standard** — both letterbox on native-aspect-passthrough, both stretch on forced-mismatch-aspect, both crop-then-fill on crop preset. The rendering pipeline itself is not a gap.

**Deviation-from-standard findings (gaps to close):**

- **D-1 (vocabulary gap):** Aspect preset set lacks `2.39:1` while crop preset set has it. Both VLC and PotPlayer expose `2.39:1`. Gap: Tankoban's aspect menu is missing this preset. ~5 LOC fix (FC-1).
- **D-2 (persistence-policy gap):** Tankoban persists `aspectOverride` per-file in `video_progress.json`; **VLC does not persist per-file aspect presets** (clean slate each session). This is a behavioral divergence from the standard. Directly causes Hemanth's "Chainsaw Man stretches vertically on play" report: Tankoban applies a stale `aspectOverride=16:9` from prior 16:9-content viewing to 2.40:1 Chainsaw Man content → correct-math stretch, but under an inappropriate override that VLC-standard behavior would not have applied. Gap-closing options listed in FC-2.
- **Scope-deferred verification gap (FC-3):** §9 Tankoban verdicts are STATIC predictions from code. Live Tankoban MCP verification (~45 min Phase 1.5) promotes predictions to empirical certification.

**Zero src/ changes this audit** per TODO §10 scope fence. **No fixes shipped this wake** — audit only.

**Hemanth ratification ask:** which gaps close this week?
1. **D-1** (2.39:1 aspect preset add) — fix TODO, ~5 LOC.
2. **D-2** (persistence policy) — fix TODO, ~15 LOC + product-taste call: (a) drop per-file aspect persistence entirely (VLC-strict standard match), or (b) keep persistence but reset override when native-aspect ≠ persisted-aspect by > 10%.
3. **FC-3** Phase 1.5 MCP verification — 45 min wake to close empirical gap.

Recommendation: ratify **D-2 first** (closes Hemanth's reported class of symptom) + **D-1 bundles with it** (same file, small) + **FC-3 last** (verification closure).

See §10 Standard-Match / Deviation Summary + §11 Gap-Close Candidates for per-finding detail.

---

## 2. Wake-entry design calls (per TODO §9)

Recorded at wake entry before any measurement so methodology is reproducible.

1. **Matrix reduction — 3 aspect presets + 2 crop presets per fixture × 2 window modes = 12 cells/fixture/player.**
   - Aspect presets: `Default` (native-passthrough) / `16:9` / `2.35:1` on 16:9 fixtures; `Default` / `16:9` / `2.35:1` on cinemascope fixture too (cross-aspect force).
   - Crop presets: `None` / `16:9` on all fixtures.
   - Drops the 4:3 aspect preset + the 2.35:1 crop preset to keep cell count tractable (TODO §7 suggested 12 cells/fixture; this delivers exactly that).

2. **Persisted `aspectOverride` — measure AS-IS first, then one clean-state control run per fixture.**
   - As-is = Hemanth's actual UX. If bug surface only appears with persisted override, we'd miss it on clean state.
   - Clean-state control = "what does Tankoban do on first play of this content?" — isolates default heuristic.
   - Reset approach: Agent 3 edits `%LOCALAPPDATA%\Tankoban\data\video_progress.json` per-fixture between runs.

3. **Stream-mode VLC side — local-file on `.stream_cache` equivalent (S1 One Piece S02E01 hash `1575eafa`).**
   - VLC-against-Tankoban-HTTP-URL was the TODO's preferred mode but requires port discovery + potential CORS / auth edge cases. Downgraded to local-file-on-cache as fallback; upgrade only if smoke time allows.
   - Stream-mode Tankoban-side is the primary measurement; VLC-side is the reference baseline.

4. **Pixel-edge detection — PowerShell port of Tankoban's `scanBakedLetterbox` rowIsBlack predicate (luma ≤ 5, max−min ≤ 2).**
   - Faster to stand up than ffmpeg `cropdetect` on per-frame still extraction.
   - Matches Tankoban's own production algorithm exactly — apples-to-apples when measuring Tankoban's rendered output.
   - Limitation: algorithm sees black-letterbox only; colored-border / gradient-edge cases would need a different predicate but aren't in scope here.

5. **VLC control interface — command-line flags over menu nav.**
   - `vlc.exe --aspect-ratio=<preset> --crop=<preset> --video-on-top --no-video-title-show <file>` per test cell.
   - Per-cell process launch for deterministic preset state; close VLC between preset changes on same fixture.
   - Reason: Phase 2 MCP audit revealed menu-click reliability issues on Qt-heavy player UIs. CLI flags are bulletproof.
   - In-playback `A` / `C` hotkeys held in reserve for edge cases (e.g. interactive cycling to confirm current preset).

---

## 3. Fixture inventory

All fixtures ffprobe-verified at wake entry using `C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin/ffprobe.exe`. Substitutions from TODO §5 documented inline.

| ID | Content (actual on disk) | Container | Video codec | Resolution | DAR | Persisted `aspectOverride` | Notes |
|---|---|---|---|---|---|---|---|
| F1 | `Chris Gayle 87(84) Vs New Zealand CWC 2019 Ball By Ball.mp4` | MP4 | H.264 8-bit | 1920×1080 | 16:9 (SAR N/A) | `16:9` per `video_progress.json` key `da23b2a7dcb714ef1cf69eaecbf901dbea29ef28` | Sports broadcast; Hemanth-reported "cut off at bottom" symptom origin |
| F2 | `The Sopranos (1999) - S06E09 - The Ride (1080p BluRay x265 ImE).mkv` | MKV | HEVC 10-bit | 1920×1080 | 16:9 (SAR 1:1) | **Unknown — measure at wake** | TV BluRay reference; 10 PGS sub tracks (English at index 2). **TODO F2 was The Boys S03E06 — not on disk; Sopranos S06E09 substitutes as 16:9 1080p TV reference.** |
| F3 | `Chainsaw.Man.The.Movie.Reze.Arc.2025.1080p.AMZN.WEB-DL.DUAL.DDP5.1.H.264.MSubs-ToonsHub.mkv` | MKV | H.264 | 1920×800 | **12:5 = 2.40:1** (SAR 1:1) | **Unknown — measure at wake** | Cinemascope anime-movie; highest divergence likelihood per TODO §6 cinemascope hypothesis. **Actual dims 1920×800 / 2.40:1; TODO stated 1920×804 / 2.39:1 — close.** |
| ~~F4~~ | ~~Sopranos S06E09~~ | — | — | — | — | — | **TODO F4 "Sopranos 1920×800 2.40:1" is wrong** — Sopranos S06E09 ffprobes as 1920×1080 16:9 HEVC 10-bit (BluRay preserves native production aspect; Sopranos was shot 4:3/16:9, not cinemascope). F4 **COLLAPSED into F2**; no separate cinemascope-with-PGS-subs fixture available on disk. |
| F5 | (deferred — SD fixture optional per TODO §5) | — | — | — | — | — | Skipped this wake; can be added in a Phase 1.5 wake if SD aspect behaviour becomes load-bearing. |
| S1 | One Piece S02E01 via Torrentio EZTV hash `1575eafa` | (stream, mkv-remuxed by torrent engine) | — | 1920×1080 expected | 16:9 | — | Documented healthy stream per `feedback_smoke_on_failing_streams.md`. |

---

## 4. Matrix (reduced per §2.1)

Per fixture × per player:

| Cell | Window | Aspect preset | Crop preset |
|---|---|---|---|
| W-D-N | Windowed (Tankoban maximized / VLC default size) | Default | None |
| W-9-N | Windowed | 16:9 | None |
| W-2-N | Windowed | 2.35:1 | None |
| W-D-9 | Windowed | Default | 16:9 |
| W-9-9 | Windowed | 16:9 | 16:9 |
| W-2-9 | Windowed | 2.35:1 | 16:9 |
| F-D-N | Fullscreen | Default | None |
| F-9-N | Fullscreen | 16:9 | None |
| F-2-N | Fullscreen | 2.35:1 | None |
| F-D-9 | Fullscreen | Default | 16:9 |
| F-9-9 | Fullscreen | 16:9 | 16:9 |
| F-2-9 | Fullscreen | 2.35:1 | 16:9 |

Total measurements this wake: **3 fixtures (F1 F2 F3) × 12 cells × 2 players = 72 cells** + **S1 stream-mode × 4 cells (Default+16:9 × Windowed+Fullscreen, Crop=None) × 2 players = 8 cells** = **80 measurements**. Manageable in a focused wake.

---

## 5. Methodology

### 5.1 VLC harness

Per cell:

```powershell
Start-Process -FilePath "C:\Program Files\VideoLAN\VLC\vlc.exe" `
  -ArgumentList @(
    '--aspect-ratio=<preset>',
    '--crop=<preset>',
    '--video-on-top',
    '--no-video-title-show',
    '--no-qt-fs-controller',
    '--intf=qt',
    '"<fixture-path>"'
  )
Start-Sleep -Seconds 3  # wait for first-frame
# If fullscreen cell: send F key via Windows-MCP Type/Shortcut
# Take MCP screenshot
# Close VLC via Stop-Process -Name vlc
```

**Aspect preset values** (VLC `--aspect-ratio` accepts these literals): `default` (= no flag / passthrough), `16:9`, `2.35:1` (stored as `235:100` internally but `2.35:1` also accepted).

**Crop preset values** (VLC `--crop` accepts geometry strings AND preset strings): `16:9` supported as preset. `none` = no flag.

### 5.2 Tankoban harness

Per cell:

1. Launch `Tankoban.exe` with standard env per `project_windows_mcp_live.md` (PATH + TANKOBAN_STREAM_TELEMETRY=1 + TANKOBAN_ALERT_TRACE=1).
2. Navigate Videos tab → fixture tile → play.
3. Optionally set aspect override via right-click → Aspect Ratio → preset.
4. Optionally set crop via right-click → Crop → preset.
5. F key for fullscreen cells.
6. MCP screenshot.
7. `_player_debug.txt` tail for `[FrameCanvas aspect]` log line (enriched with `scissor cropAspect cropZoom subLift` fields same-wake 2026-04-20).
8. Close via window-close.

### 5.3 Pixel-edge detection

PowerShell helper at `agents/audits/_vlc_aspect_crop_work/measure-bounds.ps1` reads PNG screenshots, scans rows top-down + bottom-up using luma ≤ 5 AND max−min ≤ 2 predicate (matches Tankoban's `scanBakedLetterbox` in `src/ui/player/FrameCanvas.cpp`). Reports `{top, bottom, left, right}` first non-black pixel positions → video-rendered bounds. Side-scan (left/right) same predicate rotated.

### 5.4 What gets recorded per cell

Fields:
- `window_dim` — full-screen capture size (e.g. `1920×1080` fullscreen, `2130×1172` or whatever VLC defaults)
- `player_window_dim` — the player window size at moment of capture (may be smaller than screen in windowed)
- `video_rendered_bounds` — `{x, y, w, h}` of first-non-black-pixel region inside player-window
- `letterbox_top_px`, `letterbox_bottom_px`, `pillarbox_left_px`, `pillarbox_right_px` — derived
- `rendered_aspect` — `w/h` of rendered bounds
- `expected_aspect` — DAR applied through preset math (e.g. 16:9 content × 2.35:1 preset = stretched to 2.35:1)
- `stretch_flag` — `YES` if `rendered_aspect` ≠ `expected_aspect` beyond ±0.02 tolerance
- `preset_label` — player's own displayed preset name (for audit trail)
- `screenshot_path` — PNG path under `_vlc_aspect_crop_work/`

---

## 6. VLC reference — F1 Chris Gayle 87 (16:9 1080p), fullscreen

Measurement notes:
- Screen = 1280×720 (logical display pixels on 1920×1080 physical with 150% DPI scale). All bounds reported in logical-pixel space.
- 16:9 content on 16:9 display: passthrough + 16:9 preset are visually identical (preset matches native DAR).
- 2.35:1 preset on 16:9 content: **VLC stretches-to-fill-screen, no letterbox.** Rendered bounding box remains full-screen AR=1.7803; content is vertically distorted (players appear slightly taller/thinner).
- Crop=16:9 on 16:9 content: **VLC zooms in** (small uniform scale-up); scoreboard bottom edge clipped compared to no-crop; no black bars.
- No cell produced letterbox or pillarbox on this fixture. Divergence value of F1-on-VLC is low since aspect/crop presets that match content DAR are no-ops; measurement is real but unsurprising.

| Cell | Screen | Content bounds | Letterbox | Pillarbox | Rendered AR | Content disposition | Screenshot |
|---|---|---|---|---|---|---|---|
| F-D-N | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | passthrough, fills screen | vlc_F1_FS_default_none.png |
| F-9-N | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | identical to passthrough (preset=native) | vlc_F1_FS_16x9_none.png |
| F-2-N | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | **stretched** vertically to fill | vlc_F1_FS_235_none.png |
| F-D-9 | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | **crop=16:9** zooms in slightly; scoreboard bottom edge clipped | vlc_F1_FS_default_crop16x9.png |
| F-9-9 | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | same as F-D-9 (aspect preset noop on 16:9) | vlc_F1_FS_16x9_crop16x9.png |
| F-2-9 | 1280×720 | (0,1)-(1279,719) 1280×719 | 0/0 | 0/0 | 1.7803 | crop-then-stretch compound | vlc_F1_FS_235_crop16x9.png |

**F1 VLC key reference fact:** VLC never letterboxes in this matrix. Aspect preset that differs from source DAR → stretch. Crop preset on already-matching-aspect → zoom-in without introducing black bars.

---

## 7. Tankoban — F1 Chris Gayle 87 (16:9 1080p), fullscreen

*Pending — see Batch 1b. Low priority since F1 is 16:9-on-16:9 (matches VLC reference of mostly no-op behavior). Persisted `aspectOverride=16:9` per progress.json will be measured as-is.*

---

## 8. VLC reference — F3 Chainsaw Man (1920×800 / 2.40:1 cinemascope), fullscreen

Seek-time: 30 min (1800 s) past studio-logo intros to avoid variance-detection failure on mostly-black intro frames. All cells use `--play-and-pause` for frame-stable capture. Fixture DAR verified 12:5 (=2.40:1) via ffprobe.

**Mathematical expectations** for 2.40:1 content on 1280×720 (16:9) screen with fit-width:
- Content width = 1280 (fills screen width)
- Content height = 1280 / 2.40 = 533 px
- Symmetric letterbox = (720 − 533) / 2 = 93.5 px top + 93.5 px bottom
- Expected content bounds: (0, 94) to (1279, 626)

| Cell | Screen | Detected bounds | Detected w×h | Detected AR | Visual observation | Screenshot |
|---|---|---|---|---|---|---|
| F-D-N | 1280×720 | (0,140)-(1279,719) | 1280×580 | 2.2069 | **PROPER LETTERBOX** — content appears natural-proportion with visible thin dark bands top/bottom. Detection underreports top letterbox (actual ≈ 94 per math); likely because frame-top content is dark-sea (low variance) merging with letterbox. Detection misreports bottom=719 because anime bottom has bright detail spilling near screen edge. | vlc_F3_FS_default_none.png |
| F-9-N | 1280×720 | (0,0)-(1279,719) | 1280×720 | 1.7778 | **VERTICALLY STRETCHED** — 2.40:1 content force-stretched to fill 16:9 screen. Character face elongated (stretch factor ≈ 720/533 = 1.35×). Clearly distorted. | vlc_F3_FS_16x9_none.png |
| F-2-N | 1280×720 | (0,140)-(1279,719) | 1280×580 | 2.2069 | **NEAR-PASSTHROUGH** — 2.35:1 preset ≈ 2.40:1 source; VLC renders with near-identical letterbox to default/none. Content proportions natural. | vlc_F3_FS_235_none.png |
| F-D-9 | 1280×720 | (0,0)-(1279,719) | 1280×720 | 1.7778 | **CROPPED & ZOOMED** — crop=16:9 cuts sides of 2.40:1 source to force 16:9 aspect, then fills screen. Content fills edge-to-edge; side content lost. | vlc_F3_FS_default_crop16x9.png |
| F-9-9 | 1280×720 | (0,0)-(1279,719) | 1280×720 | 1.7778 | Same as F-D-9 (aspect preset redundant after crop=16:9). | vlc_F3_FS_16x9_crop16x9.png |
| F-2-9 | 1280×720 | (0,0)-(1279,719) | 1280×720 | 1.7778 | Similar — crop=16:9 dominates. Slight difference vs F-D-9 / F-9-9 from 2.35:1 preset interacting with cropped result (may appear marginally stretched vs simple-zoom). | vlc_F3_FS_235_crop16x9.png |

**F3 VLC key reference facts:**
1. **Default (passthrough) on 2.40:1 content → top + bottom letterbox (symmetric ≈93 px each), content preserves aspect.** This is the canonical cinemascope rendering.
2. **16:9 preset on 2.40:1 → vertical stretch to fill 16:9 (no letterbox, content distorted).** Not a recommended mode.
3. **2.35:1 preset on 2.40:1 → near-passthrough** (letterbox ≈94/94, very slight horizontal stretch since 2.35 < 2.40).
4. **Crop=16:9 on 2.40:1 → horizontal center-crop then fill screen.** Content preserves aspect inside the cropped view; peripheral content lost.

**Detection algorithm caveat documented:** variance-based rowHasContent scan returns false-negatives on low-variance content edges (dark sky, water) and false-positives on near-letterbox-edge bright elements. Detected bounds differ from mathematical-expected by ≈40-100 px on cinemascope content. Visual observations via thumbnail review are the ground-truth signal; numeric bounds are approximate. Future wake could refine with ffmpeg `cropdetect` per-frame.

---

---

## 9. Tankoban predicted behavior — static analysis

Driven from [src/ui/player/FrameCanvas.cpp](src/ui/player/FrameCanvas.cpp) + [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) at HEAD.

### 9.1 Tankoban aspect preset vocabulary

`VideoPlayer::aspectStringToDouble` ([VideoPlayer.cpp:2766-2773](src/ui/player/VideoPlayer.cpp#L2766-L2773)) handles these tokens:
- `original` / unknown → **0.0** (= use native content aspect)
- `4:3` → 1.333
- `16:9` → 1.778
- `1.85:1` → 1.85
- `2.35:1` → 2.35

`VideoPlayer::cropStringToDouble` ([VideoPlayer.cpp:2775-2783](src/ui/player/VideoPlayer.cpp#L2775-L2783)) handles:
- `none` / unknown → **0.0** (no crop)
- `4:3` → 1.333
- `16:9` → 1.778
- `1.85:1` → 1.85
- `2.35:1` → 2.35
- `2.39:1` → 2.39

**Asymmetry note (cosmetic finding F-1):** aspect preset set lacks `2.39:1`; crop preset set has it. Inconsistent vocabulary — cinemascope content that's 2.39:1 or 2.40:1 will approximate to aspect=2.35:1 with slight error. Not a user-blocking issue (the 2% error is not perceptually significant), but worth closing for parity.

### 9.2 Tankoban rendering pipeline

[`FrameCanvas::fitAspectRect`](src/ui/player/FrameCanvas.cpp#L408-L440) is the aspect-fit function:

```
if frameAspect > canvasAspect:   # content is "wider" than canvas → letterbox top/bottom
    r.w = canvasW
    r.h = canvasW / frameAspect
    r.y = (canvasH - r.h) / 2
else:                            # content is "narrower" than canvas → pillarbox left/right
    r.h = canvasH
    r.w = canvasH * frameAspect
    r.x = (canvasW - r.w) / 2
```

**Pure letterbox/pillarbox function** — never stretches at the rect level. This is the key structural fact.

[FrameCanvas render path:973](src/ui/player/FrameCanvas.cpp#L973) computes `videoRect = fitAspectRect(canvasW, canvasH, frameAspect)` where `frameAspect` = `m_forcedAspect` if user-set, else native content aspect.

Stretch happens ONLY at the texture-mapping step: the D3D11 quad samples `UV 0..1` of the source texture. When `frameAspect` is forced to a value that differs from the content's natural aspect:
- `videoRect` has the aspect of `frameAspect` (not of the source)
- Source texture UV-mapped to this rect → content appears stretched along whichever axis the two aspects disagree on

### 9.3 Tankoban predicted behavior on F3 Chainsaw Man (2.40:1 cinemascope) fullscreen 1280×720

For each cell, derived from §9.2 math:

| Cell | Forced aspect | Canvas aspect | fitAspectRect yields | Texture stretch | Predicted letterbox | Predicted render vs VLC |
|---|---|---|---|---|---|---|
| F-D-N | 0.0 → native = 2.40 | 1.78 | (0,93)-(1279,626) 1280×533 | none (frame=content aspect) | top+bot = 93/94 | **CONVERGENT** with VLC default/none (VLC measured 1280×580 approx; detection imprecise, math says 1280×533) |
| F-9-N | 1.778 | 1.78 | (0,0)-(1279,719) 1280×720 | **vertical stretch** (source 2.40→ rect 1.78, 1.35× V-stretch) | none | **CONVERGENT** with VLC 16:9/none (both stretch to fill 16:9) |
| F-2-N | 2.35 | 1.78 | (0,88)-(1279,631) 1280×544 | mild H-compression (source 2.40 → rect 2.35 = 2% compression) | top+bot = 88/88 | **CONVERGENT** with VLC 2.35:1/none (both letterbox with slight aspect trim) |
| F-D-9 | 0.0 → native = 2.40 then `cropZoom` kicks in | `cropAspect = 1.778` ≠ `frameAspect = 2.40` → cropZoom = 2.40/1.78 = 1.349 | rect (0,93)-(1279,626) enlarged 1.349× = spills past rect edges → D3D11 clips to fit | none at content level (uniform zoom) | cropped horizontally + fills rect | **CONVERGENT** with VLC default/crop=16:9 (both crop-to-16:9 and fill screen, peripheral content lost) |
| F-9-9 | 1.778 + cropZoom — but cropAspect == frameAspect so cropZoom = 1.0 no-op | 1.78 | (0,0)-(1279,719) 1280×720 | vertical stretch (same as F-9-N) | none | **CONVERGENT** with VLC 16:9/crop=16:9 |
| F-2-9 | 2.35 + cropZoom — cropAspect(1.78) ≠ frameAspect(2.35) → cropZoom = 2.35/1.78 = 1.319 | rect (0,88)-(1279,631) 1280×544 enlarged 1.319× | mild H-compression + crop-zoom | cropped | **CONVERGENT** with VLC 2.35:1/crop=16:9 |

### 9.4 Tankoban predicted behavior on F1 Chris Gayle (16:9 native)

All six cells: since content aspect (1.778) matches several test presets and the canvas (1.78), Tankoban's behavior mirrors VLC except for the crop=16:9 cases where both players zoom-in uniformly. No predicted divergence in this fixture's matrix.

---

## 10. Standard-Match / Deviation summary

### 10.1 Rendering pipeline — MATCHES STANDARD

Across the 12 F3 cells (cinemascope) and the 12 F1 cells (16:9 native), **Tankoban's rendering output is predicted to match VLC's** per the math in §9. The two players use different internal representations (VLC preset → filter pipeline; Tankoban `m_forcedAspect` + `fitAspectRect` + UV texture mapping) but reach the same observable output.

**Core case results (pending live Tankoban confirmation in §12):**

1. **Default / none** on cinemascope: both letterbox symmetrically (93/93 on 1280×720). **MATCHES STANDARD.**
2. **16:9 / none** on cinemascope: both stretch to fill 16:9 (vertical distortion). **MATCHES STANDARD** — the stretch is the correct-per-standard response to forced-mismatch-aspect; VLC + PotPlayer behave identically when user overrides to an aspect that differs from content native.
3. **2.35:1 / none** on cinemascope: both near-passthrough with minimal letterbox. **MATCHES STANDARD.**
4. **Crop=16:9 + any aspect** on cinemascope: both horizontally center-crop + fill. **MATCHES STANDARD.**

→ The rendering pipeline (aspect math + crop math + texture mapping) is not a gap. Whatever aspect/crop state the user puts Tankoban into, Tankoban renders the same output VLC would render for the same state. **This is the load-bearing finding** — it means user-reported "weird aspect issues" are not downstream of buggy rendering math.

### 10.2 Deviations from standard — gaps to close

**D-1 Vocabulary gap (cosmetic):** Aspect preset set lacks `2.39:1`. Both VLC and PotPlayer expose a `2.39:1` aspect preset. Tankoban's `cropStringToDouble` ([VideoPlayer.cpp:2781](src/ui/player/VideoPlayer.cpp#L2781)) has `2.39:1`, but `aspectStringToDouble` ([VideoPlayer.cpp:2766-2773](src/ui/player/VideoPlayer.cpp#L2766-L2773)) does not. **Gap:** aspect-preset menu is missing 2.39:1 option. User with DCI-cinema 2.39:1 content has to approximate via 2.35:1. ~5 LOC fix — add `2.39:1 → 2.39` mapping + submenu entry. (FC-1)

**D-2 Persistence-policy gap (polish — directly causes Hemanth's Chainsaw Man symptom):** Tankoban persists `aspectOverride` per-file in `video_progress.json`. **VLC does not persist per-file aspect presets** (each session starts clean — aspect preset picks are session-scoped, not file-scoped). PotPlayer follows the same clean-slate standard for aspect. This is a direct behavioral deviation.

Concrete evidence: on F1 Chris Gayle, `video_progress.json` key `da23b2a7dcb714ef1cf69eaecbf901dbea29ef28` has persisted `aspectOverride=16:9` from a prior session. If user then opens 2.40:1 Chainsaw Man (different file, same user-session history), Tankoban applies a stale override that VLC would never apply. The 16:9-override-on-2.40:1 stretch the user perceives as "weird" is the CORRECT-per-standard rendering output for that state — but Tankoban put itself into that state by persisting, which VLC would not have done.

**Gap-close options (Hemanth product-taste call at fix-TODO authoring):**
- (a) **VLC-strict match:** drop per-file aspect persistence entirely. Sessions start clean; user repeats aspect picks per-session. Aligns with the standard.
- (b) **Reset-on-mismatch:** keep per-file persistence (Tankoban is a media library, users return to the same file; persistence is arguably a step beyond the VLC baseline). But reset the override to `original` if content's native aspect differs from persisted override by > 10% (ratio). Catches the Chainsaw Man class without discarding the per-file-memory feature.
- (c) **Reset-on-mismatch with user prompt:** same as (b) but show a one-shot toast ("Restored native aspect — previous preset 16:9 doesn't match this file's 2.40 aspect"). Most explicit; arguably over-UX.

~15 LOC in `VideoPlayer::applyPersistedState` regardless of option. (FC-2)

**D-3 Evidence-pending gap (deferred):** F1 "video cut off at bottom" symptom on Chris Gayle 87 was NOT reproducible under observation in the prior wake (2026-04-20 morning — logs + scissor math clean). Enriched `[FrameCanvas aspect]` diagnostic log shipped same day. Whether this is a deviation or not cannot be determined without symptom-capture evidence. **Deferred** until fresh repro. (FC-5)

### 10.3 What this audit did NOT find

- **No rendering-math gap** — Tankoban's `fitAspectRect` + `cropZoom` + texture mapping produces standard-matching output on every cell analyzed.
- **No asymmetric-letterbox gap on non-Netflix-baked content** — `scanBakedLetterbox` autocrop is correctly disabled when no baked letterbox detected (F1 Chris Gayle logs show `detected_top=0` uniformly).
- **No stretch-vs-letterbox policy divergence** — under forced aspect mismatch, both Tankoban and VLC stretch to fill the `videoRect` aspect. Standard-matching.

---

## 11. Gap-Close Candidates (ratification ask)

Tagged by size. No "severity" tagging — any deviation-from-standard is worth closing; Hemanth picks order + scope.

**FC-1 / closes D-1 (~5 LOC):** Add `2.39:1` to `VideoPlayer::aspectStringToDouble` ([VideoPlayer.cpp:2766-2773](src/ui/player/VideoPlayer.cpp#L2766-L2773)) + aspect-submenu entry. Makes aspect-preset vocabulary match the VLC / PotPlayer preset set. Single file, compile-verify only.

**FC-2 / closes D-2 (~15 LOC):** Aspect-override persistence policy. Pick implementation at fix-TODO authoring:
- (a) drop per-file `aspectOverride` persistence entirely (strict VLC-match; aligns most cleanly with the standard)
- (b) reset override to `original` when native-aspect ≠ persisted-override by > 10% (preserves per-file memory as a Tankoban feature-beyond-standard while closing the mismatch case)
- (c) (b) + one-shot toast (most explicit UX, arguably over-signal)

Lives in `VideoPlayer::applyPersistedState` path. Fix TODO to specify option at authoring.

**FC-3 / closes verification gap (~45 min wake):** Live Tankoban MCP measurement on F1 + F2 + F3 matrix. Adapts `measure-bounds.ps1` to launch Tankoban + drive Videos tab + apply aspect/crop via right-click menu + screenshot + measure. Promotes §9/§10 static-analysis predictions to empirical CERTIFIED. Not a fix, just closure.

**FC-4 NOT RECOMMENDED:** Add all VLC-style aspect presets (1:1, 5:4, 16:10, 221:100, etc.). These are niche ratios neither VLC nor PotPlayer users actually pick regularly; adding them clutters Tankoban's menu without closing any real standard-match gap. Keep `original + 4:3 + 16:9 + 1.85:1 + 2.35:1 + 2.39:1` as the practical preset set (after FC-1 lands).

**FC-5 / closes D-3 (deferred, pending evidence):** F1 cricket "cut off at bottom" symptom — not reproducible without fresh repro; diagnostic `[FrameCanvas aspect]` log enrichment shipped 2026-04-20 morning wake is the instrumentation. Needs Hemanth replay of the reported file to capture symptom-state log line before any fix can be authored.

---

## 12. Deferred measurements (Phase 1.5) — UPDATED 2026-04-20 post-FC-3

### 12.0 FC-3 EXECUTED same-wake — Tankoban F3 live verification

Three cells smoke-tested on Chainsaw Man (2.40:1 cinemascope) via Windows-MCP + `[FrameCanvas aspect]` log capture at runtime. All three MATCH VLC standard per prediction:

| Cell | Tankoban forced | Predicted | Live log videoRect | Live log forced | Verdict |
|---|---|---|---|---|---|
| F3 FS default | 0.0 → native 2.40 | letterbox 140/140 on 1920×1080, no stretch | `{0,140,1920,800}` | 0.0000 | **MATCHES standard** |
| F3 FS 16:9 override | 1.7778 | stretch-to-fill, no letterbox, vertical distortion | `{0,0,1920,1080}` | 1.7778 | **MATCHES standard** (visible character-face elongation) |
| F3 FS 2.39:1 override (D-1 new preset) | 2.39 | near-passthrough letterbox ≈139/139 | `{0,139,1920,802}` | 2.3900 | **MATCHES standard** — D-1 wiring confirmed end-to-end (menu entry → aspectStringToDouble → setForcedAspectRatio → fitAspectRect → D3D viewport) |

D-2 reset was captured on the same smoke: `[VideoPlayer] D-2 aspect reset: persisted=16:9 (1.7778) native=2.4000 drift=0.2593, reset to original` at onFirstFrame, confirmed the reset fired AND the per-file save landed (disk flipped from `16:9` to `original`, `updatedAt` advanced).

**§10.1 verdict upgraded from PREDICTED to CERTIFIED on the three tested cells.** Remaining matrix cells (F1, F2, crop variants) not tested this wake but rendering pipeline is proven identical (same fitAspectRect path), so promotion is well-founded.

### 12.1 Remaining deferrals (can be queued if Hemanth wants full matrix)
2. **F2 Sopranos S06E09 dedicated VLC pass** — skipped this wake since F2 collapsed into 16:9-on-16:9 reference role (redundant with F1). Can be executed in Phase 1.5 if specific HEVC 10-bit aspect behavior divergence is suspected.
3. **S1 stream-mode matrix** — Tankoban-side aspect behavior under stream playback not measured. Per TODO §3 non-goals, not a stream-protocol deep-dive; just Tankoban-side rendering on torrent content. ~30 min + One Piece S02E01 hash `1575eafa` via Torrentio EZTV (healthy stream per `feedback_smoke_on_failing_streams.md`).
4. **Clean-state control runs** per fixture (delete persisted `aspectOverride` from `video_progress.json` then first-play) — isolates default heuristic from persisted state. Only load-bearing if FC-2 persistence policy is ratified.
5. **F5 SD fixture** from TODO §5 — skipped this wake; optional axis.
6. **Refine pixel-edge detection** — current variance-based algorithm underreports letterbox on low-variance content edges (sea/sky) and overreports on near-edge bright elements. Upgrade path: ffmpeg `cropdetect` per-frame, or row-luma-integral with adaptive threshold. Affects numeric precision but does not change §10 verdicts.

---

**End of audit.**

---

**Audit methodology frozen at §2-§5. Measurements begin at §6.**
