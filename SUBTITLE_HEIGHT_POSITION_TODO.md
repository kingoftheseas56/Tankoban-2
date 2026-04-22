# Subtitle Height + Position Audit TODO — measure Tankoban's subtitle geometry against VLC + PotPlayer standard

**Owner:** Agent 3 (Video Player)
**Coordinator:** Agent 0
**Authored:** 2026-04-20 by Agent 3
**Provenance:** Hemanth directive 2026-04-20: *"Add subtitle height and positioning (use potplayer and vlc for reference)"*. Follow-on to the 2026-04-20 PLAYER_COMPARATIVE_AUDIT Phase 2 same-wake subtitle fix (VideoPlayer.cpp subtitleBaselineLiftPx 6% → 2%, shipped at commit on wire). That fix closed the gross-position symptom; this TODO closes the REMAINING subtitle-geometry gaps against the VLC + PotPlayer standard, and is the audit that should have been done to catch the position issue in the first place.

---

## 1. Context

The VLC_ASPECT_CROP_REFERENCE audit (2026-04-20) closed aspect/crop gaps but did NOT systematically measure subtitle geometry. Sub-axes that currently lack measured-vs-reference data:

- **Vertical margin from frame bottom (text subs)** — Tankoban's `subtitleBaselineLiftPx()` defaults to 2% of canvas height (~22 px on 1080). Reference players: mpv `sub-margin-y=22`, VLC default ~30 px, PotPlayer measured 63 px on a 1080 render. **This is the symptom axis** — Phase 2 fix got the direction right but magnitude needs rigorous cross-check.
- **Horizontal centering** — Tankoban centers subs horizontally (libass default); reference players do the same. Untested but expected-match.
- **HUD-visible lift** — Tankoban has a Tankoban-specific behavior: when HUD is visible, subs lift to `qMax(hudLiftPx, baseline)` so subs don't hide behind the control bar. VLC / PotPlayer lift their subs when HUD shows. **Does Tankoban's lift magnitude match?** Untested.
- **Cinemascope letterbox interaction** — when 2.40:1 content plays in 16:9 fullscreen with top/bottom letterboxing, should subs render **inside** the content area or **in the letterbox bar**? VLC + PotPlayer both render subs in the letterbox bar by default (below content). Tankoban's current behavior: `subtitleLift` is relative to the full canvas, so subs land in the bottom letterbox area by coincidence of math. **Coincident behavior or intentional — and does the margin match reference?** Not verified.
- **Aspect override interaction** — when user forces aspect=16:9 on 2.40:1 content (stretch-to-fill, no letterbox), where do subs land? Tankoban renders them at canvas-relative position (on top of stretched content). VLC + PotPlayer do similar. Untested but expected-match.
- **PGS / bitmap subtitle positioning** — Tankoban renders PGS subs via libass conversion. Positioning math may differ from text subs. Not measured against reference.
- **ASS tag respect** — ASS subtitles carry their own `MarginV` / `\pos` / `\an` tags. Does Tankoban respect these, or does subtitleLift override? VLC + PotPlayer respect ASS tags (content-authored placement wins over default margin). **Critical axis** — if Tankoban overrides ASS tags, anime with stylized subs would render wrong.
- **Font size auto-scaling** — subtitle font size relative to canvas height. libass default is 36pt at 288-canvas-height baseline. Tankoban uses libass default; reference players have per-player defaults that may differ. Size-drift between players.

---

## 2. Objective

After this audit ships (~2-3 h single Agent-3 wake):

1. A reference-card table under `agents/audits/subtitle_geometry_reference_2026-04-NN.md` with measured VLC + PotPlayer subtitle position / margin / size across a defined fixture × subtitle-format × window-state matrix.
2. Same matrix measured on Tankoban. Rows side-by-side with reference.
3. Per-cell divergence verdict using standard-match framing (MATCHES STANDARD / DEVIATES FROM STANDARD) per `feedback_audit_framing_standard_not_better_worse.md`.
4. Gap-close candidates for any deviations, sized by effort.
5. No src/ code changes in this TODO — investigation only. Fix-TODOs authored separately if Hemanth ratifies specific deviations.

---

## 3. Non-goals

- **No subtitle styling** (font face / color / outline / background) — out of scope. This TODO is geometry only.
- **No subtitle track management** (Alt+L track switching, external sub auto-load) — covered by PLAYER_COMPARATIVE_AUDIT Phase 2 Batch 2.1.
- **No subtitle timing / sync** (delay adjustments, sync offsets) — separate axis, not geometry.
- **No mpv as reference** — per `feedback_audit_reverification_scope.md`, the Phase 2 re-verification is PotPlayer-only. This TODO adds **VLC** per Hemanth's 2026-04-20 directive. mpv stays docs-sourced.
- **No OCR of rendered subtitle pixels** — overkill. Visual screenshot comparison + pixel-margin measurement via edge detection is sufficient.
- **No 4K / HDR subtitle behavior** — no fixtures available per Phase 3 audit finding; SDR 1080p coverage only.

---

## 4. Agent Ownership

**Primary owner:** Agent 3. Single-wake execution.

**Coordinator (Agent 0):** summon Agent 3; post RTC sweep after wake closes.

**Cross-agent touches expected:** none. If deviations suggest a shared-code bug (e.g. FrameCanvas overlay viewport math), Agent 3 flags to Agent 0 for follow-on fix-TODO.

---

## 5. Fixture set

Frozen at wake start using `ffprobe` at `C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin/ffprobe.exe`.

| ID | Content | Subtitle type | Purpose |
|---|---|---|---|
| S1 | `The Boys (2019) - S05E03 - *.mkv` OR substitute | text/SRT (subrip) | 16:9 1080p TV + text subs — baseline margin measurement |
| S2 | `Chainsaw.Man.The.Movie.Reze.Arc.2025.*.mkv` | SRT sidecar | 2.40:1 cinemascope + text subs — letterbox-interaction axis |
| S3 | `The Sopranos S06E09 - The Ride.mkv` | PGS (bitmap) | 16:9 1080p HEVC + bitmap subs — PGS positioning axis |
| S4 | Any One Pace anime episode with ASS + custom TTF | ASS (styled) | anime with stylized subs — ASS-tag respect axis |
| S5 (optional) | JoJo multi-variant fixture | ASS with MarginV tags | fallback ASS test if S4 lacks MarginV variants |

Fixtures confirmed present on disk as of PLAYER_COMPARATIVE_AUDIT Phase 2 (`agents/audits/comparative_player_2026-04-20_p2_subtitles.md` §3). If any moved or deleted, substitute + document.

---

## 6. Methodology

### 6.1 VLC measurement harness

Adapt `agents/audits/_vlc_aspect_crop_work/measure-bounds.ps1` for subtitle-position measurement:

1. Launch VLC with fixture + `--start-time=<content-rich-timestamp>` + `--sub-file=<external-srt-if-needed>` + `--play-and-pause` + `--fullscreen`.
2. Seek to a timestamp where a known subtitle is on-screen (pick from SRT/ASS file timestamps).
3. MCP screenshot.
4. Pixel-scan the screenshot for the subtitle text's **bottom-most rendered row** (luma > threshold on white/outlined text inside a dark region; specialized variance method).
5. Record: subtitle bottom y-coord, frame bottom y-coord, **margin = frame_bottom - subtitle_bottom** in px.
6. Also record: subtitle font-height in px (top row of subtitle to bottom row).
7. Repeat for HUD-visible state (Alt+L or hover for HUD).

### 6.2 PotPlayer measurement harness

PotPlayer supports command-line `"C:/Program Files/DAUM/PotPlayer/PotPlayerMini64.exe" <file> /seek=<sec> /title="<title>"`. Launch fullscreen via `/fullscreen` flag. Use same screenshot + pixel-scan approach as VLC.

### 6.3 Tankoban measurement harness

Existing `[FrameCanvas aspect]` log carries `subLift=<px>` field from 2026-04-20 enrichment. That gives the **baseline lift value Tankoban computed**, but NOT the actual rendered-subtitle-position (which libass adds to the lift). For end-to-end measurement:

1. Launch Tankoban (existing direct-exe recipe per `project_windows_mcp_live.md`).
2. Open fixture, wait for first frame + subtitle to appear.
3. MCP screenshot.
4. Pixel-scan for subtitle bottom y.
5. Cross-reference with `_player_debug.txt` `[FrameCanvas aspect]` `subLift` field to separate Tankoban's contribution from libass's contribution.

### 6.4 Pixel-edge detection for subtitle text

Subtitle text is typically white with black outline / background strip. Detection:
- Scan for rows with **high variance + bright-pixel cluster** in the lower 30% of the frame.
- Top of cluster = subtitle top; bottom of cluster = subtitle bottom.
- For bitmap (PGS) subs: may span more rows (whole sub-image bounds).
- Script at `agents/audits/_subtitle_geometry_work/measure-sub-bottom.ps1` to be authored this wake.

---

## 7. Matrix to measure

Per fixture × per player:

| Cell | Window mode | HUD state | Aspect state | Subtitle format |
|---|---|---|---|---|
| A | Fullscreen | hidden | native | native sub (as-loaded) |
| B | Fullscreen | visible | native | native sub |
| C | Windowed-maximized | hidden | native | native sub |
| D | Fullscreen | hidden | forced 16:9 (on cinemascope S2) | native sub |

Per cell, measure:
- `sub_bottom_y` (px from top of capture)
- `sub_top_y`
- `sub_height_px`
- `frame_bottom_y` (from letterbox top-edge detection)
- `margin_from_frame_bottom = frame_bottom_y - sub_bottom_y`
- `relative_margin_pct = margin_from_frame_bottom / frame_height`

Total cells: 4 fixtures × 4 cells × 2 players = **32 measurements**. Reducible: skip cell C (windowed) if cell A already proves margin math scales; Cell D only meaningful on S2. Practical cell count ~20.

Batched per-fixture: one fixture end-to-end on both players before moving to next.

---

## 8. Deliverable shape

`agents/audits/subtitle_geometry_reference_2026-04-NN.md` with sections:

1. **Executive Summary** — standard-match summary + gap list
2. **Wake-entry design calls** (per §9)
3. **Fixture inventory** — ffprobe-verified, with sub-track index + sample-cue timestamps
4. **Matrix (reduced per §9 decisions)**
5. **Methodology** (scripts used, harness notes)
6. **VLC reference tables** — per-fixture
7. **PotPlayer reference tables** — per-fixture
8. **Tankoban tables** — per-fixture
9. **Tankoban predicted-behavior static analysis** (from `subtitleBaselineLiftPx()` + `setSubtitleLift` + libass-overlay-vp code path)
10. **Standard-match / deviation summary** — per sub-axis
11. **Gap-close candidates** — sized by effort (no severity tagging per framing memory)
12. **Deferred measurements** — anything skipped
13. **Related audits pointer** — PLAYER_COMPARATIVE_AUDIT Phase 2, VLC_ASPECT_CROP_REFERENCE

Target length: 250-350 lines.

---

## 9. Rule-14 design calls at wake entry

1. **Fixture prioritization.** S1 (16:9 text) + S2 (cinemascope text) are highest-value (cover baseline + letterbox axes). S3 PGS and S4 ASS are secondary. Decide at wake entry whether to full-matrix-all-4 or prioritize S1+S2 for depth.
2. **Subtitle-text pixel-detection algorithm.** Variance-based (like aspect/crop measure-bounds.ps1) won't work cleanly on text-on-content (content has variance too). Need text-specific detection: look for tall bright-outlined characters in lower screen half. ~30-45 min to stand up the detector; should be reusable for future subtitle audits.
3. **HUD-visible cell measurement timing.** HUD hides after ~3s idle. Need to either (a) capture screenshot immediately after mouse-movement shows HUD, or (b) hover mouse over the HUD area to keep it visible. Decide at wake entry.
4. **PotPlayer preset re-use.** PotPlayer's default sub position is ~63 px bottom (measured 2026-04-20 wake close on Chainsaw Man). Verify this holds across fixtures or varies per-content.
5. **ASS-tag-override test construction.** Need an ASS file with explicit MarginV=20 vs default (0 or no-tag) to test whether Tankoban honors the tag or overrides. If no fixture has this explicitly, ffmpeg-generate a 10-second test clip with a custom ASS sidecar.

---

## 10. Verification procedure (wake-exit)

1. `agents/audits/subtitle_geometry_reference_2026-04-NN.md` lands at 250-350 lines.
2. At least one clear divergence finding documented with screenshot + pixel-level measurement.
3. Standard-match verdicts per sub-axis with pixel evidence.
4. Rule 17: Stop-Process VLC + PotPlayer + Tankoban + ffmpeg_sidecar.
5. Agent 3 posts RTC + gap-close ratification request.
6. MEMORY.md updated with any new learnings (reference-player sub margin conventions, Tankoban subtitle-pipeline quirks).

---

## 11. What NOT to include (explicit deferrals)

- **mpv subtitle geometry** — out of scope per `feedback_audit_reverification_scope.md`. Can be added in a later wake if Hemanth wants tri-player reference.
- **Subtitle styling axes** (font face, color, outline, background) — separate TODO if needed.
- **Multi-line subtitle handling** — if a rendered sub has 2 lines, measure from bottom of last line; don't treat multi-line as separate axis.
- **Localized subtitles in non-Latin scripts** — fixture restriction; measure only English/Latin-script subs in this audit.
- **Subtitle fade-in / fade-out** — timing axis, not geometry.
- **HDR-specific subtitle brightness** — separate concern, not in this geometry scope.

---

## 12. Pointer to existing reference material

- `agents/audits/comparative_player_2026-04-20_p2_subtitles.md` — Phase 2 pilot + subtitle fix context + reference-player defaults already documented (mpv 22 / VLC ~30 / PotPlayer ~63).
- `agents/audits/vlc_aspect_crop_reference_2026-04-20.md` — sister audit from same wake; uses the proven harness shape + standard-match framing.
- `feedback_audit_framing_standard_not_better_worse.md` — MATCHES STANDARD / DEVIATES FROM STANDARD verdict framing (no better/worse).
- `feedback_audit_reverification_scope.md` — PotPlayer primary + VLC added for this TODO per Hemanth 2026-04-20 directive; mpv stays docs-sourced.
- `feedback_mcp_smoke_discipline.md` — 5 rules for faster MCP smokes (Screenshot over Snapshot, batch clicks, etc.)
- `VideoPlayer.cpp subtitleBaselineLiftPx()` (around line 2482) — Tankoban's current baseline lift math (2% of canvas height since 2026-04-20 fix).
- `FrameCanvas.cpp [FrameCanvas aspect]` log at line ~1094 — carries `subLift=<px>` field for live Tankoban measurement.

---

**End of plan.**
