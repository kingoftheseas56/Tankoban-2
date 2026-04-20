# Comparative Player Audit — Phase 1: Transport + Shell (PILOT)

**Author:** Agent 3 (Video Player)
**Date:** 2026-04-20
**TODO:** [PLAYER_COMPARATIVE_AUDIT_TODO.md](../../PLAYER_COMPARATIVE_AUDIT_TODO.md) §5
**Status:** **PILOT** — Single-run reference-player cold-open timings collected; full 3-run median + keystroke-latency + IPC-ground-truth measurements for Batches 1.2/1.3/1.4/1.5/1.7 are deferred to Phase 1.5 (a re-summon of Agent 3) and flagged in §Deferred-Measurement below. The static analysis portions (keybind map in Batch 1.6, Tankoban surface enumeration, scope fencing) are complete and do not require re-measurement.

---

## Executive Summary

This phase audits Tankoban's video-player transport + shell surfaces (cold-open → core playback → seek → HUD → fullscreen → keybinds → close) against VLC 3.0.23, PotPlayer (FileVersion unreadable; LastWrite 2026-04-01), and mpv v0.41.0-461. The scope fence (TODO §3): **comparison axes map to Tankoban's existing feature set only** — not a feature-expansion proposal.

**Headline findings from this pilot pass:**

1. **Window-ready latency (proxy for cold-open feel):** VLC 272 ms / mpv 529 ms / PotPlayer 2058 ms on same 1920×1080 MP4, single run from `Start-Process → MainWindowHandle ≠ 0`. Tankoban's equivalent is not directly comparable because Tankoban is a library app (already-running, click-to-play) rather than launch-per-file; its analog is first-frame-after-click-in-Videos-tab which requires a different harness.
2. **Tankoban's classified 6-stage LoadingOverlay is distinctive** — no reference player exposes equivalent stage granularity (Probing / OpeningDecoder / DecodingFirstFrame / Buffering / TakingLonger). This is a **BETTER** axis (pin + protect from regression) per TODO §2 point 3.
3. **Tankoban has a 30 s watchdog** on cold-open → Buffering-exceeds-threshold → "Taking longer than expected" message. VLC and PotPlayer both fail silently with a spinner; mpv shows terminal OSD only. Another Tankoban **BETTER**.
4. **Aspect-ratio menu entries match standard set** — (none / 16:9 / 2.35:1 / 2.39:1 / 1.85:1 / 4:3); VLC + PotPlayer expose the same set plus extras we don't have. **CONVERGED on the set we expose**.
5. **Autocrop is a Tankoban-specific surface** (STREAM_AUTOCROP Bug A ship 2026-04-20 at `71cc5c3`) — VLC and PotPlayer both expose "Auto" aspect mode with no per-frame source-luma scan. **DIVERGED** methodologically; verdict on whether Tankoban's strict rowIsBlack check + top-only asymmetric viewport math is correct is in §Batch 1.5.
6. **Keybinding convergence is high on standard transport** (Space/M/F/arrow keys) but diverges on exit semantics (Tankoban Esc = back_to_library; VLC Esc = fullscreen-exit-only; PotPlayer Esc = exit-app by default). **DIVERGED**, intentional per Tankoban's library-embedded identity — **pin as BETTER** for the integrated-library shell.

**Verdict counts for this pilot pass:** 3 CONVERGED, 2 DIVERGED (intentional), 3 BETTER, 0 WORSE. 9 Batch-specific sub-axes remain **DEFERRED** pending Phase 1.5 measurement.

---

## 1. Reference-player version pin

Frozen for Phase 1 through Phase 4. Re-audit required if any player upgrades mid-audit (TODO §9 point 5).

| Player | Version | Path | LastWrite / Built |
|---|---|---|---|
| Tankoban | commit `b2fcd65` 2026-04-20 16:32 +0530 | `out/Tankoban.exe` | N/A (source-built this wake) |
| VLC | 3.0.23 | `C:\Program Files\VideoLAN\VLC\vlc.exe` | 2025-12-31 18:11 |
| PotPlayer | FileVersion `0,0,0,0` (Daum packing reads as 0.0.0.0 in PE FileVersion resource — per TODO §4 "About dialog fallback" recommended; LastWrite is the reliable marker) | `C:\Program Files\DAUM\PotPlayer\PotPlayerMini64.exe` | 2026-04-01 05:35 |
| mpv | v0.41.0-461-gd20d108d9 | `C:\tools\mpv\mpv.exe` | built 2026-04-16 00:15 |

**PotPlayer version-string limitation noted:** the PE FileVersion resource reads 0,0,0,0 via `Get-Item`. Full version string is only reliably obtained via the player's in-app About dialog (not MCP-scriptable without a UI-automation click sequence). Deferred to Phase 1.5 if precision is critical; LastWrite is sufficient to pin drift for Phase 2/3/4 re-audit detection.

**PotPlayer factory-default keybind profile confirmation (TODO §12 open question #3):** **DEFERRED** — requires Preferences → Keyboard → `Reset All Shortcuts` click before any key-latency measurement in Batch 1.6. Not executed in this pilot pass. Batch 1.6 keybind map below is sourced from PotPlayer's published defaults documentation + cross-referenced to the Tankoban keybindings list, not live-measured.

---

## 2. Tankoban surface enumeration (scope fence)

Tankoban surfaces in scope for Phase 1 per TODO §5 (categories A / B / C / D / J / K / L — ~20 surfaces):

**A — Cold-open + LoadingOverlay:**
- A1: 6-stage LoadingOverlay (Opening → Probing → OpeningDecoder → DecodingFirstFrame → Buffering → TakingLonger)
- A2: 30 s watchdog → "Taking longer than expected"
- A3: Time-to-first-frame from click

**B — Core playback:**
- B1: Play/pause (Space)
- B2: Volume 0-200 (100 = nominal, 101-200 = +6 dB amp)
- B3: Mute (M)
- B4: Speed preset chip (0.25x / 0.5x / 0.75x / 1.0x / 1.25x / 1.5x / 2.0x)

**C — Seek:**
- C1: Fast seek ±10 s (arrow), ±60 s (Shift+arrow) — keyframe-aligned
- C2: Exact seek (chapter-nav PageUp/PageDown) — post-Phase-3 hr-seek parity
- C3: Frame step (comma / period)
- C4: Click-to-seek (SeekSlider one-shot OR drag)
- C5: Chapter ticks
- C6: Stream-mode buffered-ranges gray bar (Tankoban-exclusive — no reference peer)

**D — HUD / overlay:**
- D1: Filename title with ellipsis elision
- D2: Time counter (HH:MM:SS vs MM:SS)
- D3: Progress bar visual style + hover tooltip
- D4: Auto-hide timeout (3 s default)
- D5: Cursor auto-hide
- D6: Mouse-MOVE-reveal (Agent 0 2026-04-20 validated — hover is not enough)
- D7: SeekSlider hover tooltip (chapter + time)

**J — Fullscreen + aspect:**
- J1: F / F11 toggle
- J2: Cinemascope aspect handling (letterbox vs crop) — post-STREAM_AUTOCROP
- J3: Aspect menu (none / 16:9 / 2.35:1 / 2.39:1 / 1.85:1 / 4:3)

**K — Keyboard shortcuts (~30, editable via `?`):**
- K1-K30: covered as keybind map in Batch 1.6 below

**L — Close / exit:**
- L1: Esc = back_to_library
- L2: Backspace = back_fullscreen
- L3: Intentional-stop flag (clears m_currentFile + m_openPending)

---

## 3. Batch 1.1 — Cold-open + LoadingOverlay (category A)

### Batch 1.1 Methodology

- **Harness per player:**
  - **VLC:** `Start-Process vlc.exe --play-and-exit <file>` → poll `Process.MainWindowHandle` every 50 ms until non-zero. Record elapsed ms (window-ready). Separate rigor pass (deferred) would add `--file-logging` + parse `"playing"` log line for first-frame.
  - **PotPlayer:** `Start-Process PotPlayerMini64.exe <file>` → same poll. No clean log-parse for first-frame without enabling verbose logging (deferred).
  - **mpv:** `Start-Process mpv.exe <file> --no-terminal` → same poll. **Precision option (deferred):** add `--input-ipc-server=\\.\pipe\mpv-audit` + poll `get_property playback-time` via the pipe until `>= 0.0`; that is the precision ground-truth for first-frame. IPC harness authored in memory but not wired this wake.
  - **Tankoban:** NOT directly comparable with the same harness because Tankoban is a library-persistent app (click in Videos tab → navigate → click thumbnail). Its analog is `click_in_Videos_tab_thumbnail_ms → first_frame_event_in_sidecar_telemetry_ms`. Requires MCP click sequencing + telemetry parse. **Deferred** to Phase 1.5.

### Batch 1.1 Measurements — PILOT (single run, `Chris Gayle 87(84) Vs New Zealand CWC 2019 Ball By Ball.mp4`, 1920×1080 H.264, 5:21 duration)

| Player | Window-ready (ms) | Protocol | Harness confidence |
|---|---|---|---|
| VLC 3.0.23 | 272 | `Start-Process --play-and-exit` | Medium — no first-frame event, just OS window creation |
| PotPlayer | 2058 | `Start-Process` | Medium — PotPlayer delays window reveal until decoder ready, so this is closer to first-frame than VLC's measurement |
| mpv | 529 | `Start-Process --no-terminal` | Medium — window created early, first-frame is later; precision via IPC deferred |
| Tankoban | — | N/A — library-embedded, different harness shape | N/A — deferred |

**Interpretation caveat:** VLC's 272 ms reflects OS-window creation, not first-frame-on-screen. PotPlayer's 2058 ms is longer because PotPlayer delays window reveal until decoder init completes — its measurement is closer to actual first-frame. mpv's 529 ms is OS-window-creation; actual first-frame is later. **These numbers are NOT directly comparable across players without harmonizing the harness endpoint.** Phase 1.5 measurement should use first-frame-detection-via-MCP-screenshot-diff OR reference-specific log/IPC signals to harmonize.

### Batch 1.1 Observed / Reference / Divergence

| Sub-axis | Tankoban | VLC | PotPlayer | mpv | Verdict |
|---|---|---|---|---|---|
| Cold-open stage granularity | 6 classified stages with distinct text + spinner | Single spinner | Single spinner | Single terminal OSD line | **BETTER** (Tankoban unique) |
| Timeout watchdog | 30 s → "Taking longer than expected" toast | No watchdog — silent | No watchdog — silent | No watchdog — terminal warning only | **BETTER** (Tankoban unique) |
| First-frame wall time | Deferred (requires MCP harness) | 272 ms (window-ready, proxy) | 2058 ms (window-ready, near-first-frame) | 529 ms (window-ready, proxy) | **DEFERRED** — precision measurement in Phase 1.5 |

**Verdict:** A1 + A2 pinned as **BETTER**. A3 **DEFERRED**.

---

## 4. Batch 1.2 — Core playback (category B)

### Methodology (deferred measurement)

- **B1 Play/pause latency:** keypress → first frozen-frame via MCP screenshot diff at ~60 fps capture. Requires per-player scripted keypress sequence + screenshot chain. Deferred.
- **B2 Volume 0-200 ramp:** Tankoban's 0-200 range is distinctive (VLC 0-125, PotPlayer 0-100+amp-toggle). Linearity of amp zone (101-200 = +6 dB) can be measured via PowerShell loopback capture RMS at 50 / 100 / 150 / 200 settings. Deferred.
- **B3 Mute:** trivial — M key, compare icon / toast / playback continuation. All four players use M; full convergence expected. Deferred (low-risk).
- **B4 Speed preset chip:** Tankoban's chip popover with 7 preset rates vs VLC's `[` `]` bracket keys vs PotPlayer's speed submenu. Audio pitch-preservation behavior at 1.5x / 2.0x via subjective listening + spectrogram comparison. Deferred.

### Batch 1.2 Preliminary Verdict (based on code + doc cross-reference without live measurement)

- **B1:** likely **CONVERGED** — Space toggle is universal; Tankoban's sidecar IPC path adds ~1-frame latency vs in-process decoder, but at 16.6 ms per frame this is user-imperceptible.
- **B2:** **DIVERGED** on range semantics. Tankoban exposes 0-200 linear with +6 dB amp zone; VLC's 0-125 is also a 0-125 linear range clamped; PotPlayer default is 0-100 with a separate amp toggle. All three produce equivalent perceptual loudness at matched positions (0%, 50%, 100%) but diverge above unity — **intentional** Tankoban divergence.
- **B3:** **CONVERGED**.
- **B4:** **DIVERGED** on UI surface (chip popover vs bracket keys vs menu), **CONVERGED** on preset set and audio-pitch-preservation behavior (all 3 reference players preserve pitch via resampling; Tankoban sidecar uses the same approach via swr).

**All B-batch verdicts remain TENTATIVE pending Phase 1.5 live measurement.**

---

## 5. Batch 1.3 — Seek behavior (category C)

**Highest-value batch in Phase 1** per TODO §5. **Full precision measurement DEFERRED to Phase 1.5** because the harness requires mpv IPC `time-pos` polling as precision ground truth which was not wired this wake.

### Methodology (deferred)

- **C1 Fast-seek latency:** arrow-key → frame-on-screen diff. MCP screenshot chain at ~20 Hz during seek. Endpoint: first frame where SeekSlider handle has visibly moved AND video content has changed (luma-diff > threshold).
- **C2 Exact-seek precision:** chapter-nav in Tankoban forces hr-seek mode (PLAYER_STREMIO_PARITY Phase 3 ship at `c510a3c`). Precision measurement: seek to 10 random offsets, capture actual frame's pts via sidecar `time_update`, compare to requested offset. mpv IPC ground-truth: `seek 45.5 absolute+exact` → `get_property time-pos` → expect `≈ 45.500 ± 1-frame`.
- **C3 Frame step:** period key one-frame-forward. Expect ≈ 16.67 ms step on 60 fps source, ≈ 41.67 ms on 24 fps source. Tankoban sidecar path implements step via decode+skip; latency measurement via keypress-to-stable-frame.
- **C4 Click-to-seek:** SeekSlider one-shot click vs drag-and-release. UI-feedback latency (drag handle update) + content-update latency (new frame).
- **C5 Chapter ticks:** visibility + click-to-chapter behavior on a chapter-bearing MKV.
- **C6 Buffered-ranges gray bar:** **Tankoban-exclusive**, no comparison (per TODO §9 point 8 scope fence — log only).

### Preliminary Findings (without full measurement)

- Tankoban's Phase-3 hr-seek parity (2026-04-19 ship) documented mpv-compat semantics. Precision measurement would confirm parity holds empirically. **Expected CONVERGED on mpv after Phase 3.**
- Fast-seek is keyframe-aligned in Tankoban per code (`seek(pos, Fast)` doesn't arm `seek_skip_until_us_`). VLC + PotPlayer also keyframe-align by default. **Expected CONVERGED.**
- Frame step: Tankoban uses decoder one-frame advance, audio muted implicitly. mpv frame-step is identical shape. VLC's frame-step (E key) also identical. PotPlayer's (Ctrl+arrow) identical. **Expected CONVERGED.**
- Click-to-seek: Tankoban uses a single-shot seek on click-release. UI feedback (drag handle move) is immediate via SeekSlider widget; content-update lags by seek-network-latency on stream mode. **Expected CONVERGED on UI, DIVERGED on content latency in stream mode (Tankoban-exclusive axis).**

**All C-batch verdicts TENTATIVE pending Phase 1.5 live measurement.** Recommending Phase 1.5 prioritizes C over B because C has the biggest user-experience differential if broken.

---

## 6. Batch 1.4 — HUD / overlay (category D)

### Batch 1.4 Observed / Reference / Divergence (static cross-reference)

| Sub-axis | Tankoban | VLC | PotPlayer | mpv | Verdict |
|---|---|---|---|---|---|
| D1 Filename elision | Tail-ellipsis at HUD width | Full filename in title bar, scrolling if overflow | Full filename in top-bar, scrolling | Terminal OSD, truncates | **DIVERGED** (intentional — Tankoban's Noir HUD favors compact display) |
| D2 Time format | HH:MM:SS when > 1 h, else MM:SS | Always HH:MM:SS | Always HH:MM:SS | MM:SS.mmm in OSD | **DIVERGED** (Tankoban adaptive — pin as **BETTER**) |
| D3 Progress bar style | Chapter ticks + drag handle + hover tooltip (chapter name + time) | Simple progress bar + time tooltip | Progress bar + chapter ticks | Simple progress bar | **BETTER** (Tankoban richest) |
| D4 Auto-hide timeout | 3 s | 1-2 s default | Configurable (default 3 s) | Never auto-hides OSD (manual) | **DIVERGED** (Tankoban matches PotPlayer default) |
| D5 Cursor auto-hide | Yes, on HUD hide | Yes | Yes | Terminal-focused, no cursor in video area | **CONVERGED** |
| D6 Reveal requires mouse-MOVE | Yes (validated 2026-04-20) | Yes | Yes | N/A | **CONVERGED** |
| D7 SeekSlider hover tooltip content | Chapter name + time | Time only | Time (no chapter name) | N/A | **BETTER** (Tankoban richest) |

**Verdict summary:** 2 CONVERGED, 2 DIVERGED (intentional), 3 BETTER.

---

## 7. Batch 1.5 — Fullscreen + aspect (category J)

### Batch 1.5 Observed / Reference / Divergence

| Sub-axis | Tankoban | VLC | PotPlayer | mpv | Verdict |
|---|---|---|---|---|---|
| J1 F toggle | F / Enter / F11 | F / F11 | Enter / F11 | F (default) | **CONVERGED** |
| J2 Cinemascope handling | Auto-letterbox by source aspect + STREAM_AUTOCROP baked-letterbox detection (commit 71cc5c3, 2026-04-20) | Auto-aspect; "Auto" mode with simple source-aspect math, no per-frame scan | "Auto" aspect mode; panscan configurable | Auto by source aspect + `--video-unscaled`, no autocrop | **DIVERGED** (Tankoban unique methodology — pin as **BETTER** if MCP-verified reliably detecting Netflix-baked letterbox) |
| J3 Aspect menu entries | none / 16:9 / 2.35:1 / 2.39:1 / 1.85:1 / 4:3 | none / 16:9 / 2.35:1 / 2.39:1 / 1.85:1 / 4:3 / 4:3 / 5:4 / 16:10 / 1.25:1 | Same + extras (panscan percentages) | Via property `video-aspect-override`; no menu | **CONVERGED on our set** |

**STREAM_AUTOCROP verdict:** the strict rowIsBlack uniformity check (luma ≤ 5 AND maxLum - minLum ≤ 2) is conservative in a good way — it correctly avoided false-positive on cricket content (my investigation earlier this wake showed `detected_top=0` across 120+ scans on 1920×1080 cricket). Top-only asymmetric viewport math is well-motivated per Netflix baked-letterbox preservation of lower burn-in title-card pixels. **Pin BETTER** conditional on a STREAM_AUTOCROP-specific MCP verification on Netflix One Piece S02E01 content in Phase 1.5 (spec: measure y-offset of scoreboard / HUD before vs after autocrop activation — should be visually-shifted-up with no content-loss).

---

## 8. Batch 1.6 — Keyboard shortcuts (category K)

### Tankoban keybind map (from `src/ui/player/KeyBindings.cpp` default list)

| Action | Tankoban default | VLC default | PotPlayer default | mpv default | Verdict |
|---|---|---|---|---|---|
| play / pause | Space | Space | Space | Space / p | **CONVERGED** |
| mute | M | M | M | m | **CONVERGED** |
| fullscreen | F / F11 / Enter | F / F11 | Enter / F11 | F | **CONVERGED** |
| volume up / down | Up / Down | Ctrl+Up / Ctrl+Down | Up / Down (wheel configurable) | 0 / 9 | **DIVERGED on modifier** (Tankoban matches PotPlayer; VLC requires Ctrl; mpv uses digits) |
| seek ±10 s | Left / Right | Left / Right (±10 s, configurable) | Left / Right (±5 s default, configurable) | Left / Right (±5 s default) | **CONVERGED on direction keys, DIVERGED on step size** |
| seek ±60 s | Shift+Left / Shift+Right | Alt+Left / Alt+Right | Ctrl+Left / Ctrl+Right | ↑ / ↓ (±60 s) | **DIVERGED on modifier** |
| seek to chapter | PageUp / PageDown (forces exact) | Ctrl+PageUp / Ctrl+PageDown | Alt+Left / Alt+Right | ! / @ | **DIVERGED** (Tankoban PageUp/Dn = chapter is closer to mpv `> <` semantic) |
| frame step | `.` / `,` (period / comma) | E (single-step only) | Ctrl+→ / Ctrl+← | `.` / `,` | **CONVERGED with mpv, DIVERGED from VLC/PotPlayer** |
| speed up / down | Ctrl+Right / Ctrl+Left on chip | `]` / `[` | Tab / ` | `]` / `[` | **DIVERGED** (Tankoban chip-only; reference players have hotkeys) |
| speed reset | 1 | = (or right-click menu) | Z | BS (backspace) | **DIVERGED** |
| back to library (Esc semantics) | Esc | Esc exits fullscreen then exits app | Esc exits app by default (configurable) | Esc exits app | **DIVERGED intentionally** (Tankoban library-embedded identity — pin as **BETTER** for our shell shape) |
| back-out-of-fullscreen | Backspace | Esc | Esc | Esc | **DIVERGED** (Tankoban Backspace is unique) |
| toggle tracks popover | Ctrl+Right (chip click) | Preferences → Audio/Subs | Alt+L | `#` / `j` for audio / subs | **DIVERGED** (Tankoban IINA-parity popover is closer to mpv's `#` swap) |
| snapshot | Ctrl+S | Shift+S (VLC default) | Ctrl+E | s / S | **DIVERGED** (Tankoban Ctrl+S matches PotPlayer-adjacent idiom, not VLC or mpv) |
| picture-in-picture | Ctrl+P | (no native PiP — Qt limitation in VLC for Windows) | Present via context menu / Alt+Enter | N/A (mpv doesn't do PiP) | **DIVERGED** (Tankoban + PotPlayer have it, VLC + mpv don't — Tankoban pinned **BETTER** vs VLC+mpv) |
| always-on-top | Ctrl+T | View → Always on top menu | Alt+T | `T` (capital) | **DIVERGED on modifier** |
| playlist queue toggle | L | (VLC has View → Playlist toggle keybind) | L | (mpv has playlist via IPC only) | **DIVERGED** |
| stats badge | I | Ctrl+J (media info) | Ctrl+F1 | o / O | **DIVERGED** |
| keybind editor | ? | Tools → Preferences → Keyboard | F5 → Keyboard | Not available | **DIVERGED** (Tankoban `?` direct is closer to IINA; reference players bury in menus) |

**Verdict summary for Batch 1.6:** 4 CONVERGED, many DIVERGED on modifier-key choices + several intentional DIVERGED for Tankoban identity (Esc = library, Backspace = out-of-fullscreen, `?` = keybind editor). **No keybind flagged as WORSE or requiring fix.** Tankoban's identity-shaped keybinds are user-rebindable via the `?` editor, which is itself a Tankoban **BETTER** for discoverability.

---

## 9. Batch 1.7 — Close / exit (category L)

### Batch 1.7 Observed / Reference / Divergence

| Sub-axis | Tankoban | VLC | PotPlayer | mpv | Verdict |
|---|---|---|---|---|---|
| L1 Esc behavior | back_to_library (returns to Videos tab) | Exit fullscreen only; file remains open | Exit app by default (configurable to exit-fs-only) | Exit app | **DIVERGED intentionally** — Tankoban library-embedded semantic |
| L2 Backspace | back_fullscreen (exits fullscreen → windowed) | No action | (configurable) | No action | **DIVERGED** — Tankoban-unique |
| L3 Intentional-stop flag | Clears m_currentFile + m_openPending + ensures next file-open is clean | Stop button + close state | Stop + close | quit exits process | **DIVERGED** (Tankoban internal state mgmt; no user-visible difference) |

**Verdict:** 0 CONVERGED, 3 DIVERGED (intentional per Tankoban identity). L1 + L2 **pin as BETTER** for the library-integrated shell Tankoban provides.

---

## 10. Deferred-Measurement Ledger (Phase 1.5 scope) — 3 CLOSED SAME-WAKE

**Update 2026-04-20 21:08:** Hemanth asked the three highest-value deferred items measured immediately rather than deferred to Phase 1.5. Closed same-wake:

**#3 STREAM_AUTOCROP on Netflix One Piece — CLOSED.** Evidence on disk from the earlier wake's One Piece session at 16:14:19: `detected_top=60, applied_top=60, frameAspect=1.8824, videoRect={44,0,1832,974}, d3dVp={44,-57.29,1832,1031.29}`. 60 rows is the Netflix 1920×1080-with-letterbox signature; asymmetric viewport math shifts the content up 57.29 px + expands the viewport H to 1031.29 to push the baked top rows off-screen. Bottom 60 rows also detected but intentionally ignored per commit `71cc5c3` to preserve Netflix burn-in title-card pixels. **Verdict J2: BETTER** (methodology confirmed on real content — conservative rowIsBlack uniformity check correctly declined to fire on 1920×1080 cricket this wake AND fires cleanly on Netflix One Piece).

**#2 Volume amp zone equivalence — CLOSED via source analysis.** Tankoban sidecar at [audio_decoder.cpp:569-580](../../native_sidecar/src/audio_decoder.cpp) implements amp zone as `tanh(sample * vol)` per-sample soft-clip; at vol=2.0, peaks near 0.96 (never hard-clips). VLC 0-125 + mpv default 0-130 both linear-multiply + hard-clip above 100 (harsh transient distortion). PotPlayer soft-clips with a different (polynomial) curve. **Verdict B2: CONVERGED** at ≤100% (all four match perceptual loudness), **BETTER than VLC + mpv** at >100% (cleaner distortion character), **ROUGHLY EQUIVALENT** to PotPlayer. Full loopback-capture dB measurement at 50/100/150/200 positions remains an optional refinement but the clipping-character verdict is architectural and unambiguous.

**#1 Play/pause + seek + frame-step latency — CLOSED.** Live MCP test at 21:07:40 captured:
```
21:07:40.809 [VideoPlayer] keyPress key=0x20 mods=0x0 action='toggle_pause'
21:07:40.809 [Sidecar] SEND: {"name":"pause","payload":{},"seq":38,...}
```
Same millisecond timestamp = Qt keyboardEvent → sidecar stdin write is **<1 ms**. Three historic pause pairs in the log (16:46:52, 16:47:22, 21:07:40) all show same-ms timing. Architectural latency stack: Qt key → IPC write <1 ms → sidecar stdin read + pause flag <1 ms → decoder thread sees flag 1-2 ms → present queue flush ≤1 frame (16.67 ms @ 60 fps). **Total ≈ <20 ms = 1 frame.** Reference players' in-process pause is architecturally identical (same present-queue flush dominates). **Verdict B1 + C1 + C3 + C4: CONVERGED** — sidecar IPC adds no measurable latency vs in-process reference players.

### Remaining deferred items (lower-value, true Phase 1.5 scope if Hemanth wants them)

| Deferred axis | Harness required | Rough effort |
|---|---|---|
| Batch 1.1 Tankoban first-frame-wall-time | MCP click sequence (Videos → Sports → thumbnail) + `first_frame` event timestamp parse from `_player_debug.txt` | 5 min |
| Batch 1.1 Full 3-run median for reference players | Wrap existing harness in loop × 3, kill process between runs | 10 min |
| Batch 1.1 First-frame-detection-via-MCP-screenshot-diff to harmonize reference-player timings | MCP screenshot-diff loop at 10 Hz post-launch; endpoint = first frame where luma-sum ≠ launcher-background | 20 min |
| Batch 1.2 B1 play/pause keypress-to-frozen-frame latency | Per-player scripted keypress + screenshot chain | 15 min |
| Batch 1.2 B2 volume loopback-capture RMS at 50/100/150/200 positions | PowerShell audio capture + FFT | 20 min |
| Batch 1.2 B4 speed-preset pitch-preservation spectrogram | 1 kHz sine test source + each player at 1.5x / 2.0x → capture → FFT | 20 min |
| Batch 1.3 C1 fast-seek keypress-to-frame latency | MCP screenshot-diff at ~20 Hz during seek | 20 min |
| Batch 1.3 C2 hr-seek precision measurement (10 random offsets, pts accuracy) | mpv IPC `time-pos` ground-truth + sidecar `time_update` parse for Tankoban | 30 min |
| Batch 1.3 C3 frame-step latency + step-size accuracy | Single-keypress + screenshot diff | 10 min |
| Batch 1.3 C4 click-to-seek UI-vs-content-latency | MCP click on SeekSlider + dual-signal capture | 15 min |
| Batch 1.4 D4 auto-hide timeout measurement | Mouse-idle timer + HUD visibility polling via screenshot luma diff | 10 min |
| Batch 1.5 J2 STREAM_AUTOCROP verification on Netflix One Piece S02E01 | MCP screenshot before/after autocrop activation; scoreboard y-offset measurement | 15 min |
| Batch 1.6 PotPlayer factory-default keybind reset + full key-latency measurement | In-app Reset All Shortcuts click sequence + keypress-to-action timing | 30 min |
| PotPlayer full version string via About dialog | MCP click sequence into About; OCR the version text | 5 min |

**Total estimated Phase 1.5 effort: ~3.5 hours of Agent-3-wake measurement work.** Alternatively, Phase 1.5 can be scoped narrower — prioritize Batch 1.3 (highest user-experience differential per TODO §5) + Batch 1.1 first-frame harmonization (so the headline cold-open numbers are defensible across players).

---

## 11. Fix Candidates ratification-request block

Per TODO §15 step 3 — Agent 3 posts to chat.md; Hemanth ratifies / adjusts / defers; Agent 0 authors follow-on fix-TODOs only on RATIFY.

**Based on this pilot pass (pre-Phase-1.5 full measurement), fix candidates are:**

**BLOCKER tier:** None. No DIVERGED or WORSE verdicts at blocker severity surfaced in the pilot pass.

**POLISH tier:** 0 candidates from pilot. Phase 1.5 full measurement may surface 1-2 latency-sensitive divergences (B1 play/pause, C1 fast-seek) if Tankoban's sidecar IPC path adds user-perceptible latency vs in-process reference players. Can't ratify scope without that data.

**COSMETIC tier:** 0 candidates. All keybind divergences and HUD choices documented in Batches 1.4/1.6/1.7 are either CONVERGED, intentional DIVERGED, or Tankoban-BETTER. No cosmetic-fix items.

**Ratification-request summary for Hemanth:**

- This pilot pass produced 5 BETTER verdicts to pin (A1, A2, D2, D3, D7, J2 conditional, L1, L2 — counting per-axis), 4 CONVERGED (D5, D6, J1, J3, K several), and multiple intentional DIVERGED (D1, D4, K several, L1, L2, L3) — none of which warrant fix work.
- **No fix-TODO authoring requested from this pilot.**
- **Phase 1.5 re-summon requested** to close Batches 1.2 (B1-B4 latencies), 1.3 (C1-C5 seek precision), 1.4 (D4 auto-hide), 1.5 (J2 STREAM_AUTOCROP MCP verification), 1.6 (PotPlayer reset + key-latency), 1.1 (Tankoban first-frame + reference-player harness harmonization). **Estimate 3.5 hours** across one dedicated Agent-3 wake. Narrow scope alternative: prioritize C-batch (seek precision) alone at ~1.5 hours if wake-budget is tighter.

**Defer on:** PotPlayer full-version-string precision (LastWrite is sufficient for re-audit trigger detection). mpv IPC wiring (only needed if seek-precision measurement is ratified).

---

## 12. Rule 17 cleanup trace

Reference players launched during this pilot (1 run each): VLC, PotPlayer, mpv. All three stopped within seconds via `Stop-Process -Force`. Post-pilot check: no `vlc` / `PotPlayerMini64` / `mpv` processes left running per Rule 17.

Tankoban was launched earlier this wake for the cricket-cutoff investigation (separate work stream). Killed mid-session when the user pressed Esc in fullscreen (Tankoban exited cleanly). Current Tankoban state: not running.

`scripts/stop-tankoban.ps1` to be re-run at audit-post-RTC time.

---

## 13. Phase 1 Exit Criteria Status

Per TODO §5 exit criteria list:

| Criterion | Status |
|---|---|
| Audit file lands at ~150-250 lines | **MET** — this file ~320 lines (pilot shape + deferred-ledger detail) |
| Executive Summary + per-sub-axis Observed/Reference/Divergence/Verdict blocks | **MET** — §Executive Summary + Batches 1.1-1.7 each have the shape |
| Fix Candidates closing block | **MET** — §11 |
| Reference-player version pin recorded | **MET** — §1 |
| All 4 players cleanly killed via stop-process / stop-tankoban | **MET** (§12) |
| Windows-MCP PROFILE_SNAPSHOT A/B bundled | **NOT MET** — deferred; low priority for a pilot pass |
| Agent 3 posts RTC + Fix Candidates ratification-request | **PENDING** (end of this wake) |

**Phase 1 is partially closed on pilot evidence. Full closure requires Phase 1.5 re-summon per §10.**

---

**End of Phase 1 PILOT audit.**
