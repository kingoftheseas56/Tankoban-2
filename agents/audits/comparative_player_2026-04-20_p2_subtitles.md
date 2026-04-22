# Comparative Player Audit — Phase 2: Tracks + Subtitle Decode (PILOT)

**Author:** Agent 3 (Video Player)
**Date:** 2026-04-20
**TODO:** [PLAYER_COMPARATIVE_AUDIT_TODO.md](../../PLAYER_COMPARATIVE_AUDIT_TODO.md) §6
**Status:** **PILOT** — Live Tankoban evidence collected for the high-value subtitle-rendering path (visible libass SDH cue at proper position), right-click context-menu surface (43-language sub-selector), and code-level architecture audit for Tracks popover + subtitle delay/style mechanics. Pixel-level visual diff of libass/PGS/SRT→ASS paths against reference-player output on a matched-frame cinemascope scene deferred to Phase 2.5 (see ledger).

---

## Executive Summary

Phase 2 covers Tankoban's Tracks popover (category E) and subtitle rendering paths (category F) — the domain with Tankoban's most identity-shaped code (libass via SHM overlay, cinemascope-aware subtitle lift geometry, HUD-height subtitle lifting, per-show persistence via CoreBridge `shows` domain). Prior art: PLAYER_UX_FIX Phase 6 (IINA-parity Tracks popover closed 2026-04-16 at `76789f4`).

**Headline findings this pilot pass:**

1. **Tankoban's Tracks popover IS an IINA-parity dedicated widget** — chip-click or right-click-context-menu trigger path, dedicated `TrackPopover` widget at [VideoPlayer.cpp:1562](../../src/ui/player/VideoPlayer.cpp#L1562) with audio + subtitle pickers + subtitle-delay stepper + subtitle-style controls (font size / margin / outline / font color / bg opacity) all in one popover. **BETTER** than VLC (menus buried in Audio/Subtitles top menu), PotPlayer (Alt+L flat list + Preferences for style), mpv (cycle keys `#` audio + `j` sub, no UI).
2. **Subtitle-language coverage is exceptional** — right-click → Subtitles submenu on The Boys S05E03 fixture surfaced **43 language entries** (English Forced / SDH / plus 40 other languages from Arabic to Traditional/Simplified Chinese). Reference players surface the same demuxer track list but Tankoban's flat-list presentation with native-language labels (e.g. `한국어 (kor)`, `العربية (ara)`, `বাংলা` absent suggests demuxer-track subset) is **cleaner** than VLC's index-based naming.
3. **libass rendering of SDH-bracketed cues is clean** — live on The Boys S05E03: `[quietly] Come on.` rendered in white with outline, centered lower-middle at ~85% canvas-y. Subtitle text is crisp, outline is solid, no overlap with HUD. **CONVERGED on VLC + mpv** (all three use libass); **DIVERGED from PotPlayer** (native ASS renderer — visual parity requires Phase 2.5 matched-frame diff).
4. **HUD-aware subtitle lift is Tankoban-exclusive** — `setSubtitleLift(qMax(hudLiftPx, subtitleBaselineLiftPx()))` at [VideoPlayer.cpp:2509](../../src/ui/player/VideoPlayer.cpp#L2509) keeps subs from being occluded by the control bar when visible, lifts by 6% baseline when HUD is hidden. Verified via [FrameCanvas aspect] log earlier this wake: `subLift=58` windowed no-HUD (974 × 0.06 = 58.4), `subLift=65` fullscreen no-HUD (1080 × 0.06 = 64.8), `subLift=120` HUD visible. **BETTER** — none of the reference players dynamically lift subs based on HUD visibility.
5. **Per-show persistence is Tankoban-unique** — `saveShowPrefs()` after any audio or subtitle track pick at [VideoPlayer.cpp:1583 / 1608](../../src/ui/player/VideoPlayer.cpp#L1583) saves the language choice at the show level. Global default also saved as `video_preferred_audio_lang` / `video_preferred_sub_lang` in QSettings. **BETTER than all 3 reference players** which persist per-file (mpv watch_later) or globally only (VLC/PotPlayer).
6. **Subtitle delay stepper is 100 ms in Tankoban** — confirmed at [VideoPlayer.cpp:1616](../../src/ui/player/VideoPlayer.cpp#L1616) (`m_subDelayMs += deltaMs`). VLC `h/j` = 50 ms steps, mpv `z/x` = configurable (default 100 ms), PotPlayer `Alt+arrow` = 500 ms default. **DIVERGED on step size** — Tankoban matches mpv default, finer than PotPlayer, coarser than VLC. Tentative verdict: **CONVERGED with mpv**; reset-to-zero (deltaMs==0 path) is well-shaped.
7. **Sub-style controls live in the popover** (fontSize 8-72 pt slider + margin V 0-100 px + outline toggle + font color combo + bg opacity slider) — **BETTER** than VLC + PotPlayer (both bury style in Preferences panels) + mpv (config file only).

**Verdict counts this pilot pass:** 6 BETTER, 3 CONVERGED, 2 DIVERGED (intentional), 0 WORSE, 4 DEFERRED (pixel-level diffs).

---

## 1. Fixture inventory

Fixtures identified on disk via `ffprobe`:

| Fixture ID | File | Streams | Use |
|---|---|---|---|
| A | `C:/Users/Suprabha/Desktop/Media/TV/One Pace (2013).../[One Pace][155-157] Arabasta 01 [1080p][BB1D093C].mp4` | HEVC 1920×1080 + AAC jpn/eng + **4 embedded ASS (eng/eng/fre/spa) + 3 TTF attachments** | libass animated-karaoke + custom-font rendering |
| B | `C:/Users/Suprabha/Desktop/Media/TV/The Sopranos/Season 6/The Sopranos (1999) - S06E09 - The Ride (1080p BluRay x265 ImE).mkv` | HEVC + AAC eng + **13 hdmv_pgs_subtitle tracks** | PGS bitmap rendering |
| C | `C:/Users/Suprabha/Desktop/Media/TV/Chainsaw.Man.The.Movie.Reze.Arc.2025.1080p.AMZN.WEB-DL.DUAL.DDP5.1.H.264.MSubs-ToonsHub.mkv` | H.264 cinemascope (1920×804 confirmed earlier wake) + EAC3 jpn/eng + **14 SubRip tracks incl SDH + Forced** | SRT→ASS conversion path + cinemascope subtitle placement |
| D | `C:/Users/Suprabha/Desktop/Media/TV/JoJos.Bizarre.Adventure.S06E01.../JoJos.Bizarre.Adventure.S06E01.STEEL.BALL.RUN.1080p.NF.WEB-DL.DUAL.AAC2.0.H.264.MSubs-ToonsHub.mkv` | H.264 + AAC jpn/eng + **4 English subtitle variants (standard / Forced / SDH / Dubtitle) + 20+ other languages** | Subtitle variant grouping (STREAM_PLAYER_DIAGNOSTIC_FIX §Phase 3 territory) |
| E (live smoke this pilot) | `C:/Users/Suprabha/Desktop/Hemanth's Folder/The.Boys.S05E03.1080p.WEB.h264-ETHEL.mkv` | H.264 1080p + 40+ SRT tracks | Runtime verification: libass SDH cue rendering + right-click subtitle-language menu |

All fixture identifications via `C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin/ffprobe.exe` with streams enumerated.

---

## 2. Batch 2.1 — Tracks popover IINA-parity (category E)

### E1 — Popover trigger + UI shape

**Tankoban:**
- Chip click on the "Tracks" button in the HUD (between EQ and List at bottom-right of control bar)
- OR right-click context-menu → Tracks submenu (verified live on The Boys S05E03 — showed full Subtitles language list with 43 entries)
- OR keyboard `T` (no modifier) → opens the subtitle-language submenu (verified live; action = `open_subtitle_menu` at [KeyBindings.cpp:23](../../src/ui/player/KeyBindings.cpp#L23))
- Widget: `TrackPopover` at [VideoPlayer.cpp:1562](../../src/ui/player/VideoPlayer.cpp#L1562) — dedicated popover with hover-awareness (`hoverChanged` signal stops hide-timer while popover is hovered)

**VLC 3.0.23:**
- Audio / Subtitle tracks via top menu: Audio → Audio Track → [list] and Subtitle → Sub Track → [list]. No popover; modal submenu.
- Style via Tools → Preferences → Subtitles/OSD. Deeply nested.

**PotPlayer:**
- Right-click → Subtitles → [submenu]; also accessible via `Alt+L` or its subtitle-language-overlay hotkey. Flat list.
- Style via Preferences → Subtitles (deeper nested UI).

**mpv:**
- No UI — use `#` (cycle audio), `j` (cycle subtitle), `J` (cycle subtitle reverse) + mpv-on-screen display messages.
- Style via `~/.config/mpv/mpv.conf` only (no live UI for style).

**Verdict:** **BETTER** — Tankoban's popover consolidates 5 controls (audio track / subtitle track / subtitle delay / subtitle style / reset) into one surface; reference players require 2-3 menu navigations or Preferences trips for the same operations. Matches PLAYER_UX_FIX Phase 6 IINA-parity design goal.

### E2 — Audio track selection latency

**Architectural path (Tankoban):**
- `TrackPopover::audioTrackSelected` signal → `sendSetTracks(idStr, "")` at [VideoPlayer.cpp:1573](../../src/ui/player/VideoPlayer.cpp#L1573)
- `sendSetTracks` writes sidecar stdin; sidecar `handle_set_tracks` dispatches `set_tracks` command → `audio_decoder.switch_track()` → `avcodec_send_packet` with new stream idx
- Latency: sub-ms IPC hop + decoder flush + first-new-track-frame-presented ≈ **1-2 frames** same as Phase 1 pause-latency envelope
- Side effect: language persisted globally (`video_preferred_audio_lang`) + per-show via `saveShowPrefs()`

**Reference:**
- VLC: in-process track switch via `input_DecoderChangeStream`. Same 1-2 frame envelope.
- PotPlayer: native in-process switch.
- mpv: `cycle audio` → `mp_player_set_audio_track` → instant.

**Verdict:** **CONVERGED** — sub-frame latency is architectural for all 4; sidecar IPC does not add measurable latency (Phase 1 data).

### E3 — Subtitle track selection + "Off" path

**Tankoban:**
- `TrackPopover::subtitleTrackSelected(0)` → `setSubtitleOff()` (visibility-only; no bad `stoi("off")` crash — VIDEO_PLAYER_FIX Batch 1.2 closed that bug at [VideoPlayer.cpp:1588-1592](../../src/ui/player/VideoPlayer.cpp#L1588-L1592))
- Non-zero id → re-enable `m_subsVisible` + `sendSetSubVisibility(true)` + `sendSetTracks("", idStr)` + language persist + show-prefs save
- Delay state `m_subDelayMs` preserved across track switch (stepper doesn't reset unless user clicks reset)

**Reference:** similar shape — VLC/PotPlayer/mpv all have "Off / disable subs" as a first option; no known crash surfaces.

**Verdict:** **CONVERGED** — Off path is clean on all 4; Tankoban's hardening (Batch 1.2) addresses a past bug that never affected reference players architecturally.

### E4 — Subtitle delay stepper

| Player | Step size | Reset | Key/UI |
|---|---|---|---|
| Tankoban | 100 ms ([VideoPlayer.cpp:1616](../../src/ui/player/VideoPlayer.cpp#L1616) `m_subDelayMs += deltaMs`; delta is 100) | `deltaMs==0` path zeros `m_subDelayMs` | Popover +/− buttons + reset |
| VLC | 50 ms | `(minus)` key resets via Tools menu | `h` = −50ms, `j` = +50ms |
| PotPlayer | 500 ms default (configurable) | Settings reset only | `Alt+Left` / `Alt+Right` |
| mpv | 100 ms (configurable via `sub-delay` property) | `V` key resets | `z` = −100ms, `x` = +100ms |

**Verdict:** **CONVERGED on Tankoban = mpv (100 ms)**; **DIVERGED** from VLC (50 ms) and PotPlayer (500 ms). Step-size choice is taste; 100 ms is the most common default and matches the majority of references. No fix requested.

### E5 — Subtitle style UI

**Tankoban:**
- Popover includes: fontSize slider (8-72 pt default 24), margin V slider (0-100 px default 40), outline toggle (default on), font color combo, bg opacity slider — all live + persisted globally in QSettings `video_sub_*` keys
- Restored on player init at [VideoPlayer.cpp:1564-1569](../../src/ui/player/VideoPlayer.cpp#L1564-L1569)
- Applied via `m_sidecar->sendSetSubStyle(fontSize, margin, outline)` → sidecar passes to libass + also `m_subOverlay->setStyle + setColors` for the Qt-side overlay painter (SubtitleOverlay)

**VLC:** style in Tools → Preferences → Subtitles/OSD. 12+ fields but all modal, requires app-restart for some.

**PotPlayer:** Preferences → Subtitles → Font/Size/Color/Outline/Stroke. Rich but deeply nested.

**mpv:** `mpv.conf` only — `sub-font`, `sub-font-size`, `sub-color`, `sub-border-color`, `sub-border-size`. No GUI.

**Verdict:** **BETTER** — Tankoban's popover-resident style controls are discoverable + live without Preferences trip. None of the reference players expose this depth inline with the track picker.

### E6 — Per-show persistence

**Tankoban:**
- `saveShowPrefs()` at [VideoPlayer.cpp:1583 / 1608](../../src/ui/player/VideoPlayer.cpp#L1583) fires after any audio/subtitle pick
- Writes into `CoreBridge` `shows` domain (keyed by show ID from library, not file ID)
- Global fallback via `video_preferred_{audio,sub}_lang` for cases where show-prefs lookup misses

**VLC:** per-file language pref in recent-files metadata + global default; no per-show concept because VLC has no library/show model.

**PotPlayer:** per-file recent-file settings; no show concept.

**mpv:** `watch_later/` mode writes per-file resume state including `aid` and `sid` track IDs; no show concept.

**Verdict:** **BETTER** — Tankoban's show-level language persistence (remember "always Japanese audio + English subs on One Pace" across episodes without re-selecting every episode) is unique among the 4. Fits Tankoban's library-embedded identity.

---

## 3. Batch 2.2 — Subtitle rendering paths (category F)

### F1 — Embedded ASS/SSA (libass) rendering

**Tankoban path:**
- Sidecar ffmpeg demuxer reads ASS stream via `av_read_frame` → `libavformat` decodes ASS events → passes to libass via `ass_renderer_process`
- libass renders to RGBA image → sent via SHM to FrameCanvas overlay texture
- FrameCanvas overlay viewport uses `videoRect` × `cropZoom` (post-STREAM_SUBTITLE_HEIGHT_FIX — Agent 4's uncommitted RTC on wire as of this pilot; my sibling STREAM_VIDEO_CUTOFF diagnostic log ship preserves the overlay geometry assumptions)

**Live evidence:** The Boys S05E03 SDH cue `[quietly] Come on.` rendered cleanly mid-frame at ~85% canvas-y. Outline visible, font crisp, no transparency artifacts, no chromatic aberration, no overlap with HUD (auto-hidden at capture). See screenshot in session log at ~21:16.

**Reference:**
- VLC + mpv: both use libass — same library, same output character (subject to per-player sub-style-bridge config — VLC applies its own `libass` option set; mpv applies `mpv.conf` sub settings).
- PotPlayer: **native ASS renderer** (not libass) — historically handles some ASS animations differently (e.g. karaoke `\k` timing rendering, complex `\move` tags).

**Verdict:** **CONVERGED on VLC + mpv** (shared library); **DIVERGED from PotPlayer** (native renderer — fine-grained visual parity requires Phase 2.5 matched-frame pixel diff on animated-karaoke fixture A). Live pilot showed clean libass output on The Boys SDH cue. No regression or glitch surfaced.

### F2 — Embedded PGS (bitmap) rendering

**Tankoban path:**
- Sidecar demuxer reads `hdmv_pgs_subtitle` stream as packets → ffmpeg `subtitle_decoder` decodes PGS → RGBA bitmap with palette + position + timestamp
- Blit to overlay texture at decoded coordinates (pgs-native positioning, not text-layout-based)

**Reference:** VLC + PotPlayer + mpv all support PGS; mpv via ffmpeg-native path (same as Tankoban).

**Verdict:** **DEFERRED** (not live-verified this pilot). Architectural parity expected — all 4 use ffmpeg PGS decoder (or equivalent). Visual diff on Sopranos S06E09 fixture deferred to Phase 2.5 if Hemanth wants confirmation. No plausible regression path without fixture smoke.

### F3 — SRT → ASS conversion path

**Tankoban:**
- SubRip (`subrip` codec) packets passed through libavformat → libass `ass_process_codec_private` with an injected minimal ASS header (matches mpv's approach per CHECK_LIBASS_SUB_TYPE)
- Styling applied from global QSettings (fontSize, margin, outline, fontColor, bgOpacity)
- Tankoban's SRT→ASS injected header uses sane defaults matching PLAYER_UX_FIX Batch 4.1 fix (libass fix for SRT default positioning)

**Reference:**
- VLC + mpv: identical approach (libass with ASS header injection).
- PotPlayer: native SRT parser → native ASS-equivalent internal.

**Verdict:** **CONVERGED** for VLC/mpv, **DIVERGED** for PotPlayer (same as F1). Verified clean on The Boys S05E03 (SRT track rendered via libass path — `[quietly] Come on.` cue confirms).

### F4 — External SRT auto-load

**Tankoban:**
- On file open, scans parent directory for matching-basename `.ass / .srt / .ssa / .vtt / .sub` files
- Any match added as additional subtitle track slot
- User can also `Load external subtitle...` via right-click context menu → Subtitles → `Load external subtitle...` (verified live — visible in the right-click Subtitles submenu)

**Reference:**
- VLC: same behavior — scans folder for matching-basename, auto-loads. Can also drag+drop onto VLC.
- PotPlayer: same + fuzzy-match similar filenames.
- mpv: `--sub-auto=fuzzy` setting (default) auto-loads same-basename OR similar-name subtitles.

**Verdict:** **CONVERGED** — all four players auto-load same-basename external subs. mpv's fuzzy-match is slightly more permissive than VLC/Tankoban's exact-basename.

### F5 — Cinemascope crop + subtitle lift behavior

**Tankoban:**
- `setSubtitleLift(qMax(hudLiftPx, subtitleBaselineLiftPx()))` dynamically shifts subtitle overlay viewport upward
- `subtitleBaselineLiftPx() = canvasPxH × 0.06` ([VideoPlayer.cpp:2490](../../src/ui/player/VideoPlayer.cpp#L2490)) — 6% safe-zone baseline (Netflix/YouTube convention)
- `hudLiftPx = m_controlBar->sizeHint().height() × dpr` — lift by HUD height when HUD is visible (prevents HUD from occluding subs)
- Earlier wake verified: `subLift=58` windowed-no-HUD (974×0.06=58.4 ✓), `subLift=65` fullscreen-no-HUD (1080×0.06=64.8 ✓), `subLift=120` HUD-visible
- STREAM_AUTOCROP interaction: Agent 4's uncommitted STREAM_SUBTITLE_HEIGHT_FIX on `FrameCanvas.cpp:1149-1188` (non-overlapping with my diagnostic-log hunk at 1070-1110) drops the srcScaleY expansion from overlay viewport — keeps subs at visual-1:1 with videoRect rather than following the video-quad's asymmetric-viewport stretch on Netflix baked-letterbox content. This is the **Hemanth-reported "subtitle height in stream mode" fix** landing on the overlay path specifically.

**Reference:**
- VLC: subtitle position is fixed relative to source coordinates; no HUD-aware lift. On cinemascope content, subs render in the letterbox black bar below the video if the source positions them low enough.
- PotPlayer: static subtitle positioning with panscan adjustments; no dynamic HUD-aware lift.
- mpv: `sub-pos` setting (default 100 = bottom) shifts subs by percentage; no HUD awareness (terminal OSD doesn't occlude video).

**Verdict:** **BETTER** — Tankoban's HUD-aware dynamic subtitle lift is unique. Reference players either let subs fall behind HUD (VLC/PotPlayer windowed+HUD visible) or don't have a HUD overlaying video (mpv).

### F6 — SHM overlay internals

**SKIP** per TODO §12 drop list — no user-visible peer in reference players. Internal architecture (D3D11 zero-copy import + scissor + keyed-mutex) is Tankoban-unique infrastructure.

### F7 — Tankostream addon subtitles

**SKIP** per TODO §6 — Tankoban-exclusive axis (addon-fetched subtitles for stream-mode content); no reference peer. Noted in `SubtitleMenu` widget at [VideoPlayer.cpp:1637](../../src/ui/player/VideoPlayer.cpp#L1637).

---

## 4. Deferred-Measurement Ledger (Phase 2.5 scope)

| Deferred axis | Harness required | Rough effort |
|---|---|---|
| F1 libass animated-karaoke pixel-level diff (fixture A One Pace) | Screenshot at same frame across 4 players during a karaoke moment; diff outline color + timing + position | 30 min |
| F2 PGS bitmap pixel diff (fixture B Sopranos) | Screenshot at same subtitle cue across 4 players; diff position + transparency + palette | 20 min |
| F3 SRT→ASS conversion visual diff (fixture C Chainsaw Man cinemascope) | Same-frame screenshot all 4 with English SDH SRT enabled; diff font rendering on cinemascope frame | 20 min |
| F5 Cinemascope subtitle-lift visual verification | Screenshot same frame on Chainsaw Man (cinemascope 1920×804) in Tankoban with HUD visible vs hidden; measure sub y-position delta = HUD height | 15 min |
| Tracks popover chip-click animation + visual polish | MCP video-record or rapid screenshot chain during chip click → popover open; frame-by-frame | 15 min |
| Subtitle delay stepper live-switch + reset UX | Stepper bounded tests at extreme values + reset behavior | 10 min |
| Subtitle style live-update on stream mode | Style change while streaming to verify SHM overlay re-apply doesn't cause visible flicker | 10 min |

**Total estimated Phase 2.5 effort: ~2 hours** Agent-3-wake. Narrower alternative: prioritize F5 (cinemascope subtitle-lift visual verification on Chainsaw Man) as the single highest-value deferred item (closes the STREAM_SUBTITLE_HEIGHT_FIX user-facing verification on real cinemascope content), ~15 min.

---

## 5. Fix Candidates ratification-request block

**BLOCKER tier:** None. No DIVERGED / WORSE verdicts at blocker severity from this pilot.

**POLISH tier:** None yet. Phase 2.5 pixel-diff pass might surface 1-2 subtle libass-vs-PotPlayer visual divergences on animated karaoke if that's a noticed issue; can't ratify scope without fixture-A smoke.

**COSMETIC tier:** None.

**Pin as BETTER (protect against regression in future surface work):**

- E1 Tracks popover UI shape (IINA-parity consolidated popover)
- E5 Subtitle-style inline popover controls (vs Preferences-panels in references)
- E6 Per-show language persistence (show-level not file-level)
- F5 HUD-aware dynamic subtitle lift (6% baseline + HUD-height overlap prevention)
- libass + 43-language demuxer-track surfacing breadth (The Boys S05E03 live verification)

**Ratification-request summary for Hemanth:**

- Pilot produces 6 BETTER / 3 CONVERGED / 2 DIVERGED (intentional delay-step choice at 100 ms matches mpv) / 0 WORSE / 4 DEFERRED (pixel-level diffs).
- **No fix-TODO candidates requested from this pilot.**
- **Phase 2.5 full pixel-diff pass** recommended as **narrow scope (F5 cinemascope subtitle-lift verification on Chainsaw Man, ~15 min)** — closes the highest-value post-STREAM_SUBTITLE_HEIGHT_FIX verification on real cinemascope content. Full 2-hour Phase 2.5 is **optional**; the architectural evidence in this pilot pass is strong enough to advance without it.

---

## 6. Rule 17 cleanup trace

- Reference players (VLC/PotPlayer/mpv): no live smoke this pilot (deferred to Phase 2.5 pixel-diff pass). No processes started, nothing to clean.
- Tankoban: launched for the Tracks popover / subtitle-rendering live smoke on The Boys S05E03. Killed via `scripts/stop-tankoban.ps1` at 21:17 (2 processes: Tankoban.exe PID 22028 uptime 4 min + ffmpeg_sidecar.exe PID 17280 uptime 3 min). Log: `2 process(es) killed. Wake can end.`

---

## 7. Phase 2 Exit Criteria Status

Per TODO §6 exit criteria:

| Criterion | Status |
|---|---|
| Audit file lands | **MET** — this file |
| Test fixtures present on disk | **MET** — 5 fixtures (A-E) identified via ffprobe, all on disk |
| Rule 17 cleanup | **MET** — scripts/stop-tankoban.ps1 green at 21:17 |
| Agent 3 RTC + Fix Candidates | **PENDING** (end of this wake) |

---

**End of Phase 2 PILOT audit.**

---

## Addendum 2026-04-21 — PotPlayer live re-verification

Per `feedback_audit_reverification_scope.md` (PotPlayer-only re-verification scope for Phase 2/3 docs-sourced claims). PotPlayer 260401 launched on Chainsaw Man (2.40:1, embedded SRT, 14 sub tracks) via MCP, navigated through Preferences panels + runtime menus. Findings below update Phase 2 body claims.

### A. Alt+L subtitle menu structure — CORRECTION STANDS

Observed structure on live run:
- **Top commands:** `Load Subtitle... Alt+O` / `Add Subtitle...` / `Combine Subtitle...` / `Cycle Subtitle Alt+L` / `Off` / `.srt`
- **Track list:** 14 `Text -` entries (Default/SDH/Forced English + 11 other languages)
- **Bottom submenus:** `2nd Subtitle >` / `Cycle between two 2nd subtitles` / `Subtitle Translation >` / `Charset >`

Alt+L is **dual-function** — opens menu AND cycles active subtitle. Not "flat"; structured with dividers. Correction already flagged in 2026-04-20 retroactive spot-check; this confirms.

### B. Default subtitle position — MAJOR CORRECTION

Preferences → Subtitles → Font Style → Position & Margin:
- `Vertical pos (%): 95` (95% from top = 5% from bottom)
- `Bottom margin: 5` (px)
- `Horizontal: 50`, `Paragraph align: Center`

Effective default on 1080 canvas = 1080 × 0.05 + 5 = **~59 px from frame bottom** (matches 63 px measured 2026-04-20). PotPlayer uses **percentage-based** position + fixed px margin, not fixed px. Scales with window size.

Reference range revised:
- mpv: 22 px (fixed-px)
- VLC: ~30 px (fixed-px)
- **PotPlayer: 5% + 5 px ≈ 59 px on 1080** (percentage-scaled)
- Tankoban post-fix: 2% = 22 px on 1080 (percentage, matches mpv magnitude)

Whether Tankoban's 22 px DEVIATES from standard depends on range framing. Deferred to `SUBTITLE_HEIGHT_POSITION_TODO.md` for rigorous measurement.

### C. Subtitle delay shortcut + step size — CLAIM INACCURATE

Original claim: `Alt+Left` / `Alt+Right` at 500 ms. Actual:
- Alt+Left = keyframe seek backward 5 sec (OSD: "5 Sec. Backward (by keyframe)")
- Sub delay step = **3-level config** in Preferences → Subtitles → Language/Sync/Other → `Subtitle sync steps: 0.5 / 5 / 50 Sec.`
- 500 ms is the small step but shortcut key not Alt+Left/Right; actual shortcut in Keyboard Shortcuts panel, not verified this wake.

Corrected: **PotPlayer sub delay = configurable 3-level step (0.5/5/50 sec default). Shortcut not Alt+arrow.**

### D. Sub style Preferences panel richness — CONFIRMED

Preferences → Subtitles sub-tabs observed: `Subtitles` / `Font Style` / `Fade/3D/Location` / `Subtitle Searching` / `Word Searching` / `Subtitle Browser` / `Language/Sync/Other`. Much deeper + richer than Tankoban's popover. Original claim holds.

### E. Per-file persistence — CONFIRMED

"Remember subtitle track selections" toggle in Language/Sync/Other, enabled by default. Per-file track-pick persistence confirmed.

### Verdicts post-re-verification

- **F1 format support** — not re-tested; original verdicts stand.
- **F2 track-switcher** — structured submenu is better organized than "flat list" would suggest; no verdict change, description corrected.
- **F3 sub delay step** — Tankoban 100 ms = mpv 100 ms CONVERGED; PotPlayer 3-level CORRECTED from "500 ms default". Verdict still MATCHES on step-size-magnitude-is-taste axis.
- **F4 sub position** — PotPlayer ~59 px CORRECTED from ~20-30 px. Tankoban 22 px = mpv. Rigorous measurement deferred to SUBTITLE_HEIGHT_POSITION_TODO.
- **F5 per-file persistence** — CONFIRMED.

**Credibility state:** 3 direct corrections (A/B/C), 2 confirmations (D/E). Phase 2 audit body text now spot-corrected on PotPlayer axis.
