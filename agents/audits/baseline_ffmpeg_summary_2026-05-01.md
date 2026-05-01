# ffmpeg baseline summary — MAKE_MPV_SOLO Task 1

**Date:** 2026-05-01 ~11:30am – 12:20pm
**Driver:** Agent 3 (Video Player) via tankoctl dev bridge + windows-mcp
**Smoke owner (verdicts):** Hemanth
**Backend confirmed:** ffmpeg sidecar (sidecar-ipc events present; no MpvBackend traces)
**MCP LOCK:** held start-to-finish, ~50 minutes
**Build state:** out/Tankoban.exe = Apr 30 22:49 (post-Phase-2 close)

## Per-file results

| # | File | Verdict | Key observations |
|---|------|---------|---|
| 1 | Vinland Saga S02E01 | YELLOW | Subs not rendering; video cropped at top edge in windowed mode |
| 2 | Saiki Kusuo Ep 11 | YELLOW | Subs not rendering (same as Vinland) |
| 3 | The Apothecary Diaries S02E01 | YELLOW | Subs not rendering; rapid Right-arrow seeks don't accumulate (second press shows visual-flash but stays on same frame) |
| 4 | The Sopranos S06E01 | YELLOW | PGS subs DO render but sit too high; sub-position slider non-functional |
| 5 | The Boys S03E01 (Payback, HDR) | YELLOW | HDR colors looked acceptable (no complaint); top-edge sub clipping; seek-accumulation bug in BOTH directions (forward+backward) |
| 6 | Community S01E01 (Pilot, SDR) | YELLOW | Embedded English subs render; sub-position slider non-functional; seek-accumulation bug repro |

**Final tally: 0 GREEN / 6 YELLOW / 0 RED**

Per Hemanth's strategic call mid-run ("we're going to replace this player anyway, are we
spending time fixing it?"), we did NOT investigate any finding — just logged + moved on.
The baseline isn't a fix list, it's a comparison reference.

## Pattern findings (load-bearing for Task 4+ mpv comparison)

### A. Anime ASS subtitle dropout — three-for-three
Files 1, 2, 3 (Vinland, Saiki, Apothecary) all failed to render subtitles. Different
release groups, different sub formats, all anime. Implies either:
- Sticky default-track-Off setting on first-load for anime files
- Track-detection regression on anime ASS streams between Phase 2 close (last night) and now
- A sub-track-loading state that only fires after an explicit track-pick

This is a HIGH-VALUE comparison point for mpv: if mpv lights up default subs on the same
files automatically, that's a major user-facing win for the cutover.

### B. Sub-position slider broken on rendered subs — two-for-two where subs DO show
Files 4 (Sopranos PGS) and 6 (Community embedded SRT) both reported the height-adjustment
control as non-functional. Subs render at their authored Y-position; the slider doesn't
move them. Not investigated. Worth checking on mpv: does sub-position slider work on the
mpv path? (Phase 2.G shipped a Sub size +/- on mpv but the position slider routing may be
ffmpeg-only.)

### C. Seek-accumulation bug — UI-layer, backend-independent
Files 3 (Apothecary), 5 (Boys), 6 (Community) all reported: rapid double-tap of an arrow
key (forward and/or backward seek) does NOT accumulate. The second press shows the seek
visual flash but position stays at the first press's landing point. Reproduced in both
directions. Backend-independent — likely a UI scrub-debounce bug that mpv may inherit
unless the mpv UI path uses different scrub plumbing.

### D. Top-edge clipping — recurring shape
Files 1 (Vinland windowed video crop) and 5 (Boys top-of-screen sub clipping) both pointed
to the top edge. Likely the same widget-vs-frame geometry issue surfacing on different
content (video pixels in case 1, sub pixels in case 5). Not investigated.

### E. Frame stats are EXCELLENT on ffmpeg today
Across all files where mid-playback PERF data was captured (1, 2, 4, 5):
- 60-63 frames per ~1s window (rock solid 60Hz)
- 0 dropped frames across windowed captures (1 transient skip on Saiki resume)
- timer_interval p50 = 0.4-16.7ms (varies by file but consistent within each)
- draw p50 = 0.04-0.24ms (HDR slightly higher, expected)
- present p50 = 0.09-0.20ms
- DXGI queued=0 throughout (no GPU pipeline backpressure)

This is the BAR mpv must clear in Task 4+. Frame-pacing regressions on mpv would be a
real cutover blocker — the existing ffmpeg path is shipping clean.

### F. HDR rendering on ffmpeg — implicit pass
File 5 (The Boys S03E01 HDR HEVC 10-bit) drew NO HDR-color complaints from Hemanth.
That's the most important data point of the entire baseline for Task 4: the ffmpeg HDR
tone-mapping path is currently rendering HDR content acceptably to Hemanth's eye on his
display. The bar mpv must clear in Task 4 is "matches this acceptable result." Anything
worse than ffmpeg HDR today is a cutover regression.

## What this baseline does NOT cover

- Per-file media probe (codec, color primaries, HDR metadata, sub-track count) — could
  add via ffprobe on each file in a follow-up if needed for Task 4 comparison
- Mid-playback ipc_latency.log dump (the latency tracker writes on shutdown — will land
  on the cleanup kill below)
- mpv-side measurement of these same files — that's Task 4 territory
- Whether the seek-accumulation + sub-position bugs are in src/ or only on the UI key
  binding — explicitly out of scope per Hemanth's strategic guidance

## Implications for the rest of MAKE_MPV_SOLO

**Task 2 (remove §Q4 stream override):** Independent of these findings. Proceeds as planned.

**Task 3 (mpv mediaInfo bridge):** Implicitly motivated by Pattern A — if mpv doesn't
emit the rich subtitle-track list to the rest of the player, the same dropout class will
hit mpv. The mediaInfo bridge work in Task 3 needs to include subtitle-track plumbing,
not just the HDR/chapter/audio-device fields the audit listed.

**Task 4 (HDR + tone-mapping parity on mpv):** Pattern F is the bar. mpv HDR must look
equal-or-better than today's ffmpeg HDR on The Boys S03E01 (or a similar known-content
file Hemanth picks for Task 4). Pattern E sets the frame-pacing bar.

**Task 5 (clear error messages on mpv failures):** Independent of these findings. Proceeds
as planned.

**Tasks 6-10 (future):** Patterns B, C, D may be addressable in those — particularly the
seek-accumulation UI bug if it touches the same layer Tasks 6+ will refactor.

## Evidence files

- `agents/audits/baseline_ffmpeg_vinland_s02e01_115609.txt`
- `agents/audits/baseline_ffmpeg_saiki_ep11_115731.txt`
- `agents/audits/baseline_ffmpeg_apothecary_s02e01_120238.txt`
- `agents/audits/baseline_ffmpeg_sopranos_s06e01_120528.txt`
- `agents/audits/baseline_ffmpeg_boys_s03e01_121110.txt`
- `agents/audits/baseline_ffmpeg_community_s01e01_121626.txt`

## Cleanup

- Tankoban + sidecar killed via `scripts/stop-tankoban.ps1` per Rule 17 at end of run
- IPC latency log appended to `out/ipc_latency.log` on Tankoban shutdown (auto)
- MCP LOCK released
