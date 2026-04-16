# Player UX Fix TODO — startup latency / HUD freshness / subtitle geometry / HDR mapping / chip polish

**Owner: Agent 3 (Video Player domain master). Coordination: Agent 0. Review gate: Hemanth smoke per phase exit (Agent 6 review protocol is suspended; Rule 11 READY TO COMMIT lines remain mandatory).**

Created: 2026-04-16.
Provenance:
- Audit: `agents/audits/video_player_comprehensive_2026-04-16.md` (Agent 7, Codex — 274 lines, comparative vs mpv + IINA + QMPlay2 + VLC on-disk refs + mpv manual / libass headers / DXGI docs / PGS spec).
- Validation: Agent 3 in chat.md post at 2026-04-16 (5/5 hypotheses CONFIRMED with concrete file:line citations). Timing instrumentation deferred pending Hemanth's repro of the 30s blank case.
- Identity direction: IINA-identity continues from existing `VIDEO_PLAYER_FIX_TODO.md` (per `MEMORY.md` + `project_native_d3d11`). Phase 6 (Tracks/EQ/Filters polish) targets IINA as benchmark; P0/P1 architectural fixes are identity-agnostic.

---

## Context

Four user-visible symptoms flagged by Hemanth on the video player. Prior fix tracks (PLAYER_PERF_FIX, PLAYER_LIFECYCLE_FIX) closed specific audit findings (DXGI waitable cadence, D3D11_BOX source rect, sessionId filter, Shape 2 stop/open fence, VideoPlayer stop identity, SHM-routed GPU subtitle overlay) but did not scope the UX/latency/polish surface this TODO addresses.

Agent 7's comprehensive audit identified the fault pattern: the **first-frame event is structurally overloaded** as a gate for unrelated surfaces (video texture visibility, track-list delivery, media-info delivery, overlay SHM attachment, stats, HUD freshness). Reference players separate these explicitly (IINA's `.starting` / `.loaded` / `.playing` / `.idle` + mpv's independent property-notification model + QMPlay2's "Opening" + length-on-load + continuous buffer-info stream). Our current architecture collapses them. Secondary faults: two mechanically-silent event channels (sidecar `buffering` not dispatched in `on_video_event`; Qt `state_changed{opening}/{idle}` not handled in `onStateChanged`). Plus a subtitle overlay plane architecturally locked to the video rectangle (cannot express black-bar placement), plus a disposable HDR label/shader dropdown mismatch.

Agent 3 validation confirmed all 5 hypotheses with file:line evidence. No false positives. `feedback_session_lifecycle_pattern` does NOT apply here — this is a different fault class (display-state freshness + loading-UI-gap + overlay geometry), distinct from the session-lifecycle races closed by PLAYER_LIFECYCLE_FIX.

Empirical ranking (from audit + validation): P0 surfaces are (1) blank-startup-with-no-feedback and (2) cinemascope subtitle clipping. P1 surfaces are (3) stale HUD during switch and (4) HDR label/shader mismatch. P2 is Tracks/EQ/Filters polish depth. Timing instrumentation pending Hemanth repro will refine startup-phase priorities within Phase 1/2 (Phase 1 may split differently depending on whether stall is network I/O vs `find_stream_info` vs decoder init vs A/V sync wait — current phase shape is an early-metadata-decoupling bet that holds regardless).

## Objective

After this plan ships, a user can:

1. Open a video and see a visible loading indicator within ~200ms of clicking, not a black canvas.
2. See the opening video's filename, duration, and track list in the HUD before the first video frame renders (metadata decouples from first_frame).
3. Get buffering UI feedback during network stalls instead of silent 30s timeouts.
4. Switch from video A to video B and see the HUD reset immediately — no stale duration, stale timestamp, or stale track chip from the prior file.
5. Watch cinemascope (2.35:1 / 2.39:1) content with subtitles correctly placed in the letterbox black bars when libass positioning calls for it, not clipped inside the video rectangle.
6. See subtitles with correct aspect and VSFilter-compatible rendering on anamorphic sources (via `ass_set_storage_size` set correctly).
7. Pick an HDR tone-mapping algorithm from the FilterPopover and have that choice actually take effect in the shader — no silent mode-0 falls.
8. See the Tracks / EQ / Filters chips communicate open/closed/active state visually — not static always-the-same-label buttons.
9. Open the Tracks popover and see default/forced/external flags, language-name expansion, channel count, sample rate — matching IINA parity.
10. Save and switch EQ presets / profiles — not reset-to-flat on every open.

## Non-goals

- Audio filter chain expansion beyond what FilterPopover currently offers (loudnorm scope stays as-is).
- Per-band EQ shader reimplementation (current FFmpeg `equalizer` filter string path is kept; only UX surface changes).
- New subtitle formats (VobSub, WebVTT extensions beyond current). libass + PGS only.
- Thumbnail scrubbing previews (deferred to separate polish track if pursued).
- Gesture / remote-control input support.
- Any work on `NATIVE_D3D11_TODO.md` / `PLAYER_POLISH_TODO.md` deferred items.
- Anything scoped by `PLAYER_PERF_FIX_TODO.md` Phase 4 (P1 cleanup — CV hop / mutex split / A/V gate decouple stays capacity-gated).
- Any work on the previously-deprioritized cinemascope asymmetric-letterbox cosmetic bug (`feedback_cinemascope_aspect_deprioritized`). This TODO addresses subtitle GEOMETRY on cinemascope, not viewport-math letterbox asymmetry — distinct surfaces.

## Agent Ownership

**Primary (Agent 3, video player domain):**
- `src/ui/player/VideoPlayer.{h,cpp}`
- `src/ui/player/SidecarProcess.{h,cpp}`
- `src/ui/player/FrameCanvas.{h,cpp}`
- `src/ui/player/TrackPopover.{h,cpp}`
- `src/ui/player/EqualizerPopover.{h,cpp}`
- `src/ui/player/FilterPopover.{h,cpp}`
- `src/ui/player/SubtitleMenu.{h,cpp}` (if touched for overlay changes)
- `native_sidecar/src/main.cpp`
- `native_sidecar/src/video_decoder.cpp`
- `native_sidecar/src/subtitle_renderer.cpp`
- `native_sidecar/src/overlay_shm.{h,cpp}`
- `resources/shaders/video_d3d11.hlsl` (if overlay shader changes for canvas-plane path)

**Secondary (announce-before-touch per Rule 10):**
- `src/ui/MainWindow.cpp/.h` — if buffering signal needs routing through MainWindow
- `CMakeLists.txt` / `native_sidecar/CMakeLists.txt` — only if new files added (Phase 4b may add a protocol-message type)
- `resources/resources.qrc` — if new SVG icons added for chip state indicators

Per contracts-v2: Agent 3 runs `native_sidecar/build_qrhi.bat` themselves for any sidecar-touching batch.

---

## Phase 1 — Metadata decoupling (P0 foundation)

**Why:** Agent 7 audit Symptom 1 + Symptom 2, both P0. Validation confirmed `tracks_changed` + `media_info` are emitted from inside the first-frame callback ([main.cpp:402, :418](native_sidecar/src/main.cpp#L402)) even though the sidecar has the `tracks_payload` populated at probe time ([main.cpp:309](native_sidecar/src/main.cpp#L309)). On slow-open paths this gates the HUD from seeing metadata that's already known internally. IINA's `.loaded` state model at `MPV_EVENT_FILE_LOADED` is the reference — tracks + duration delivered there, `.playing` waits for `MPV_EVENT_VIDEO_RECONFIG`. This is the foundation phase — nothing else in this TODO lands cleanly without metadata arriving early.

### Batch 1.1 — Emit tracks_changed + media_info after probe_file in open_worker

**Scope:** Move the `tracks_changed` + `media_info` emissions out of the first-frame callback (`on_video_event` lambda, `main.cpp:341`) to fire immediately after `probe_file(path)` returns successfully in `open_worker`. The first_frame emission path keeps `state_changed{playing}` only. `tracks_payload` is already captured into the lambda; hoisting the two emissions to post-probe is a move, not a duplicate.

**Files:**
- `native_sidecar/src/main.cpp` (open_worker, on_video_event lambda)

**Success:** After opening a slow-to-first-frame file (HEVC 10-bit, large mkv, network URL), Qt-side `onTracksChanged` + `onMediaInfoChanged` fires within ~100ms of `probe_file` returning, BEFORE first decoded frame. Logged via existing `AVSYNC_DIAG` at probe boundary.

**Isolate-commit: yes** — foundation for Phase 2/3; validate empirically before piling.

### Batch 1.2 — Handle state_changed{opening} + {idle} in VideoPlayer

**Scope:** `VideoPlayer::onStateChanged` ([VideoPlayer.cpp:576-585](src/ui/player/VideoPlayer.cpp#L576)) currently only handles `"paused"` and `"playing"`. Extend to handle `"opening"` (emitted by sidecar at [main.cpp:656](native_sidecar/src/main.cpp#L656) on every sendOpen — already reaches Qt, just not dispatched) and `"idle"`. Emissions wire to signals (e.g., `playerOpeningStarted(filename)`, `playerIdle()`) that Phase 2 Batch 2.3 will consume for the Loading HUD indicator.

**Files:**
- `src/ui/player/VideoPlayer.{h,cpp}`

**Success:** Opening a file produces a single `playerOpeningStarted` signal on sendOpen. Closing/teardown produces `playerIdle`. Existing `paused`/`playing` dispatch unchanged.

**Isolate-commit: no** — composes with 2.3; can ship together at Phase 2 close if Agent 3 prefers.

**Phase 1 exit criteria:** both emissions routed. No UI binding yet (that's Phase 2). HUD shows no new surface — strict plumbing phase. Hemanth smoke: open a file, observe `[VideoPlayer]` debug log shows opening signal fires on sendOpen + tracks/media arrive pre-first-frame on slow opens.

---

## Phase 2 — Loading UX (P0)

**Why:** Audit Symptom 1, validation 2 CONFIRMED "double fault": (a) sidecar `buffering` event at [video_decoder.cpp:983](native_sidecar/src/video_decoder.cpp#L983) dispatched to `on_video_event` but the lambda has no case → silently dropped, never reaches `write_event`, never reaches Qt; (b) Qt `state_changed{opening}` handled in Phase 1.2 but no UI indicator. This phase wires both channels end-to-end and ships the visible indicator. Without this, a user opening a network URL sees 30s of black canvas with zero UI feedback.

### Batch 2.1 — Sidecar on_video_event cases for buffering + playing

**Scope:** Add missing `else if (event == "buffering") { write_event(...); }` + `else if (event == "playing") { write_event(...); }` cases to the `on_video_event` lambda at `main.cpp:341`. Both payloads are empty strings; buffering fires at every HTTP-stall retry tick; playing fires when the stall clears ([video_decoder.cpp:1006](native_sidecar/src/video_decoder.cpp#L1006)). Sidecar rebuild required (Agent 3 runs per contracts-v2).

**Files:**
- `native_sidecar/src/main.cpp`

**Success:** `sidecar_debug_live.log` shows `SEND_EVENT: buffering` during a deliberately-slow network read (weak-swarm stream test); `SEND_EVENT: playing` when the stall clears.

**Isolate-commit: yes** — sidecar-only, verifiable via log inspection before Qt side wires.

### Batch 2.2 — SidecarProcess buffering/playing dispatch + signals

**Scope:** Extend `SidecarProcess::processLine` dispatch at [SidecarProcess.cpp:383-531](src/ui/player/SidecarProcess.cpp#L383) with cases for `"buffering"` + `"playing"` events. Emit corresponding Qt signals `bufferingStarted()` + `bufferingEnded()`. Session-gated per existing allowlist rules (buffering IS session-scoped per sidecar sessionId payload — include in session filter).

**Files:**
- `src/ui/player/SidecarProcess.{h,cpp}`

**Success:** `_player_debug.txt` shows `RECV: buffering` + matching `bufferingStarted` signal emission on a slow-read scenario.

**Isolate-commit: no** — composes with 2.3.

### Batch 2.3 — Loading/Buffering HUD indicator

**Scope:** New `LoadingOverlay` widget (or reuse `CenterFlash`/`VolumeHud` display pattern if a fit) bound to three signals: `playerOpeningStarted(filename)` from Phase 1.2 → shows "Loading — <filename>" text centered; `bufferingStarted()` → shows "Buffering…" spinner; `firstFrameReceived` / `bufferingEnded` → hides. Visual style matches existing `VolumeHud` / `CenterFlash` noir aesthetic (no color per `feedback_no_color_no_emoji`). Animation: 200ms fade-in on opening start, persist until hidden. Size + position: centered over video canvas, subordinate to user controls (dismissed on user input if desired — Agent 3 decides).

**Files:**
- `src/ui/player/LoadingOverlay.{h,cpp}` (NEW — add to VideoPlayer CMakeLists; Rule 7 chat.md post required)
- `src/ui/player/VideoPlayer.{h,cpp}` (instantiate + wire signals)
- `CMakeLists.txt` (add new source; chat.md heads-up required)

**Success:** Opening a slow network file shows "Loading — filename.mkv" within ~200ms; fades to "Buffering…" if stall detected; disappears when first frame renders. No timing gaps visible to user.

**Isolate-commit: yes** — user-visible surface; isolate for targeted smoke.

**Phase 2 exit criteria:** End-to-end Loading + Buffering UX visible on network URL playback. Hemanth smoke: pick a stream URL known to take >2s on first frame + a weak-swarm torrent. Observe indicators.

---

## Phase 3 — HUD reset on video switch (P1)

**Why:** Audit Symptom 2, validation 4 CONFIRMED. `teardownUi` at [VideoPlayer.cpp:391-418](src/ui/player/VideoPlayer.cpp#L391) resets data arrays but NOT visible labels: `m_timeLabel`, `m_durLabel`, `m_seekBar` (value + duration), `m_trackChip`, `m_eqChip`, `m_filterChip`, `m_statsBadge`, `m_durationSec` member, open popover contents. Until first `time_update` + `tracks_changed` from new session, HUD shows prior file's data. Compounds with Phase 1/2 — after Phase 1 lands, tracks arrive earlier, but teardownUi still holds stale timestamps until first new `time_update` (minimum ~1s per sidecar emission cadence at [main.cpp:640](native_sidecar/src/main.cpp#L640)).

### Batch 3.1 — teardownUi resets HUD to loading state

**Scope:** Extend `teardownUi` with explicit resets for all user-visible labels: time → `"—:—"`, duration → `"—:—"`, seekbar value → 0, seekbar duration → 0, track chip → `"Tracks"` (generic), EQ chip → `"EQ"`, filter chip → `"Filters"`, stats badge hidden, any open popovers dismissed + their contents cleared, `m_durationSec = 0`. Composes with Phase 2.3: the Loading overlay visually occupies this transient "clean" state before first metadata arrives.

**Files:**
- `src/ui/player/VideoPlayer.cpp` (teardownUi body)

**Success:** Switch from video A (1h42m duration, subtitles on) to video B. HUD shows `—:— / —:—` + generic chip labels within the frame following click, not A's 1h42m stale data.

**Isolate-commit: yes** — user-visible surface, behavioral regression risk on clean-close path (make sure popover dismissal + data clear composes cleanly with session-end teardown from PLAYER_LIFECYCLE_FIX Phase 3).

**Phase 3 exit criteria:** Rapid-switch scenario shows clean HUD reset at every switch. Hemanth smoke: open video A, play 30s, switch to video B mid-playback, observe HUD transition. Repeat 3-4 times rapid.

---

## Phase 4 — Subtitle geometry fixes (P0, split)

**Why:** Audit Symptom 3, validation 5 CONFIRMED (two-part). Part A is a one-line fix to a real bug. Part B is an architectural change that requires sidecar-main protocol extension. Split to allow A to ship immediately and B to bake.

### Batch 4.1 (isolate) — Fix ass_set_storage_size zeroing

**Scope:** [subtitle_renderer.cpp:197-204](native_sidecar/src/subtitle_renderer.cpp#L197) currently calls `ass_set_storage_size(renderer_, 0, 0)`. libass docs say storage_size must be unscaled source video dimensions for correct aspect / blur / transforms / VSFilter-compatible behavior. Reference: mpv [sd_ass.c:767-771](C:/Users/Suprabha/Downloads/Video%20player%20reference/mpv-master/mpv-master/sub/sd_ass.c#L767) + VLC [libass.c:431-438](C:/Users/Suprabha/Downloads/Video%20player%20reference/secondary%20reference/vlc-master/vlc-master/modules/codec/libass.c#L431). Pass video stream width/height (same as frame_size in our current setup where storage == video == canvas for overlay). When Phase 4b lands and frame_size becomes canvas-size, storage stays video-stream-size.

**Files:**
- `native_sidecar/src/subtitle_renderer.cpp`

**Success:** ASS subtitles on anamorphic sources (non-square pixels) render with correct aspect. Rotation + blur effects in ASS styles render correctly. Sidecar rebuild required.

**Isolate-commit: yes** — trivial one-line fix, validate behavioral delta before Phase 4b architectural work.

### Batch 4.2 — Canvas-sized overlay plane (architectural)

**Scope:** Overlay SHM currently sized to video-stream dims ([overlay_shm.h:12-19](native_sidecar/src/overlay_shm.h#L12)) + main-app draws overlay in video viewport ([FrameCanvas.cpp:976](src/ui/player/FrameCanvas.cpp#L976)). For cinemascope subs in letterbox bars, overlay plane must be canvas-sized. Requires:

1. **Protocol extension:** new `set_canvas_size(w, h)` command from main-app to sidecar, sent on FrameCanvas resize events. Sidecar stores canvas dims separately from video dims.
2. **Sidecar side:** `SubtitleRenderer::set_frame_size(canvas_w, canvas_h)` called with CANVAS dims + `ass_set_storage_size(video_w, video_h)` kept (per 4.1). Set `ass_set_margins` based on letterbox geometry (top/bottom pad for cinemascope in 16:9). Overlay SHM sized to canvas dims. PGS rect compositing: rescale from video-plane coordinates (PGS spec: Presentation Composition Segment records video dims; rects are relative) into canvas-plane coordinates with letterbox offsets applied.
3. **Main-app side:** `FrameCanvas::drawTexturedQuad` sets overlay viewport to FULL canvas (not video viewport) before overlay quad draw. Two `SetViewport` calls: one for video quad, one for overlay quad.
4. **Resize handling:** `FrameCanvas::resizeEvent` emits resize to SidecarProcess → `set_canvas_size` command → sidecar resizes overlay SHM + reconfigures libass. Debounced (no per-pixel commands during drag-resize).

Reference: mpv's `mp_osd_res` model at [sub/osd.c:315-562](C:/Users/Suprabha/Downloads/Video%20player%20reference/mpv-master/mpv-master/sub/osd.c#L315) — OSD/subtitle resolution separate from video resolution, with margins.

**Files:**
- `native_sidecar/src/main.cpp` (new command dispatch, set_canvas_size handler)
- `native_sidecar/src/subtitle_renderer.{h,cpp}` (frame/storage separation, PGS rect rescale)
- `native_sidecar/src/overlay_shm.{h,cpp}` (canvas-sized resize + header update)
- `src/ui/player/SidecarProcess.{h,cpp}` (sendSetCanvasSize API)
- `src/ui/player/FrameCanvas.cpp` (resize signal + overlay viewport fix in drawTexturedQuad)
- `src/ui/player/OverlayShmReader.{h,cpp}` (dim handling on resize)

**Success:** Cinemascope 2.35:1 content in 16:9 window — ASS subtitles configured with bottom margin render in bottom letterbox bar (matching mpv default `sub-use-margins`). PGS rects land at their correct screen-plane positions. Resize the window mid-playback — overlay resizes without SHM corruption; subs reposition correctly.

**Isolate-commit: yes** — substantial cross-boundary change, isolate for targeted regression detection.

**Phase 4 exit criteria:** Cinemascope subtitles land in intended positions (in-video OR letterbox depending on libass placement rules). Window resize doesn't break overlay. Hemanth smoke matrix per audit's Gap #6: 2.35:1 ASS dialogue, 2.35:1 ASS signs, 2.35:1 PGS, 16:9 PGS, anamorphic source, external SRT, windowed + fullscreen.

---

## Phase 5 — HDR mapping (P1)

**Why:** Audit Symptom 4, validation 1 CONFIRMED. FilterPopover exposes 6 HDR algorithms but shader only maps 3 (`reinhard`/`aces`/`hable`). `bt2390`/`mobius`/`clip`/`linear` silently fall to mode 0 (Off). User-visible lie. Comment at [VideoPlayer.cpp:1245](src/ui/player/VideoPlayer.cpp#L1245) acknowledges "will be removed from FilterPopover in Batch 3.5 or at phase exit" — nobody cleaned up.

**Open design decision for Hemanth before Agent 3 starts this phase:**
- **Path A (shrink):** remove `bt2390`/`mobius`/`clip`/`linear` from FilterPopover dropdown. Expose only `{Off, reinhard, hable}` (+ `aces` if reviving — but `aces` is dead code in FilterPopover). Simplest, ships in 1 batch, ~20 LOC.
- **Path B (expand):** implement `bt2390`/`mobius`/`clip`/`linear` in the HDR shader. References: FFmpeg's `zscale` + `tonemap` filters + mpv's `gpu-next` HDR path. Substantial — shader work + parameter surfaces. 3-5 batches, ~300-500 LOC.

Agent 0 recommendation: **Path A** unless Hemanth specifically wants the expanded algorithm set. Users who need bt2390 etc. are a small power-user slice; simpler to ship what works honestly than to commit to implementing four more tonemap modes.

### Batch 5.1 — HDR dropdown alignment (path chosen)

**Scope:** Per Hemanth's Path choice.

**Files:**
- `src/ui/player/FilterPopover.cpp` (dropdown entries)
- `src/ui/player/VideoPlayer.cpp` (HDR mapping if Path A removes dead branches)
- Potentially `resources/shaders/video_d3d11.hlsl` + supporting shader machinery if Path B.

**Success:** Every FilterPopover HDR option produces a distinct shader behavior; no silent-no-op labels.

**Isolate-commit: yes** — user-visible behavioral change, isolate for smoke.

**Phase 5 exit criteria:** FilterPopover HDR dropdown honest. Hemanth smoke: pick each HDR option on HDR content, observe visible output difference (A/B toggle between options should show a change for every choice).

---

## Phase 6 — Tracks / EQ / Filters chip polish (P2, IINA-identity)

**Why:** Audit Symptom 4 depth. Prior work (`VIDEO_PLAYER_FIX_TODO.md` IINA-identity, partially shipped) got basic popovers wired; this phase closes the polish + feature gaps the audit identified against IINA. P2 only — ships after P0/P1 (Phases 1-5) close. Identity-locked to IINA per existing project direction.

### Batch 6.1 — Chip active/open/disabled state indicators

**Scope:** Common chip stylesheet at [VideoPlayer.cpp:777](src/ui/player/VideoPlayer.cpp#L777) has normal + hover states only. Add active/open/disabled/loading states. Active = EQ/Filter is applied (non-default). Open = popover currently showing (pressed-look). Disabled = no tracks available / no file open. Loading = waiting for metadata post-open. Apply to all 4 chips (Tracks, EQ, Filters, Playlist).

**Files:**
- `src/ui/player/VideoPlayer.cpp` (chip style + state transition logic)

**Success:** Open the EQ popover — chip shows "pressed" state. Apply an EQ preset — chip shows "active" indicator. Close the popover — returns to normal. Close file — all chips show "disabled" state.

**Isolate-commit: no** — composes with 6.2+ chip text richness.

### Batch 6.2 — Tracks popover metadata richness (IINA parity)

**Scope:** `TrackPopover::populate` ([TrackPopover.cpp:276](src/ui/player/TrackPopover.cpp#L276)) currently shows title + language + track-id + codec. IINA's `MPVTrack` model at [MPVTrack.swift:52+](C:/Users/Suprabha/Downloads/Video%20player%20reference/iina-develop/iina/MPVTrack.swift#L52) shows: default flag, forced flag, image flag, selected state, external flag, external filename, codec, demux dimensions, channel count, channel layout, sample rate, FPS. Readable title expands ISO639 language codes.

Add to our popover: default/forced markers (small text badge), external source indicator + filename, channel count for audio (e.g., "5.1" / "Stereo"), sample rate for audio, ISO639 language expansion ("en" → "English"), selected-state visual (checkmark / bold / highlight).

**Files:**
- `src/ui/player/TrackPopover.{h,cpp}`
- Potentially sidecar `native_sidecar/src/main.cpp` if track payload needs enriching (check existing `tracks_payload` — it should already have channel/sample/default/forced from FFmpeg stream metadata; just unused on display side).

**Success:** Opening a multi-track file (e.g., a Bluray rip with 3 audio tracks + 4 subtitle tracks with default/forced flags) shows fully-annotated list matching IINA's surface.

**Isolate-commit: yes** — UX-visible, bounded scope.

### Batch 6.3 — EQ presets + profile persistence

**Scope:** `EqualizerPopover` currently has sliders + reset + DRC — no presets, no profile save/load. IINA's `QuickSettingViewController.swift:303-536` builds preset + user-profile menu. Add preset menu (fixed presets: Flat, Rock, Pop, Jazz, Classical, Bass Boost, Treble Boost, Vocal Boost — standard graphic EQ presets). Add "Save as custom preset" → prompt for name → QSettings persistence. Load applies slider state. Shape matches QMPlay2 equalizer dock at [EqualizerGUI.cpp:94+](C:/Users/Suprabha/Downloads/Video%20player%20reference/QMPlay2-master/src/modules/AudioFilters/EqualizerGUI.cpp#L94).

**Files:**
- `src/ui/player/EqualizerPopover.{h,cpp}`

**Success:** Open EQ, pick "Rock" preset → sliders + filter apply. Save custom profile. Switch files, return to player, custom profile still present.

**Isolate-commit: yes**.

### Batch 6.4 — Popover dismiss consistency

**Scope:** Audit Gap #12 — dismiss behavior inconsistent across Tracks/EQ/Filters/Playlist/SubtitleMenu. Tracks/Filters/Playlist dismissed via `mousePressEvent` outside-click at [VideoPlayer.cpp:2393](src/ui/player/VideoPlayer.cpp#L2393). EQ dismissed internally via event filter. SubtitleMenu path unknown. Unify — every chip-opened popover dismissed on: outside click, ESC key, chip re-click, opening another chip. Chip re-click closes (toggle). ESC global handler.

**Files:**
- `src/ui/player/VideoPlayer.cpp`
- `src/ui/player/EqualizerPopover.cpp` (remove internal event filter if centralized)
- Potentially `KeyBindings.cpp` for ESC handling

**Success:** All popovers dismiss identically. Opening chip B closes open chip A. ESC dismisses any open popover.

**Isolate-commit: no** — composes with 6.1.

**Phase 6 exit criteria:** Full IINA-parity polish on HUD chip behavior. Hemanth smoke: inventory per audit Gap #10 — multi-language files, forced/default subs, external subs, commentary tracks, HDR content with active filter state, keyboard-only navigation, popover dismiss matrix.

---

## Scope decisions locked in

- **IINA-identity continues** for Phase 6 polish (inherited from existing project direction per MEMORY.md + `VIDEO_PLAYER_FIX_TODO.md`). Do NOT reopen identity question.
- **Path A (shrink HDR dropdown) is the default** for Phase 5; Hemanth override required for Path B before Batch 5.1 starts.
- **Subtitle geometry split** — 4.1 (trivial storage_size fix) ships before 4.2 (architectural overlay-plane change). 4.2 is its own isolate-commit.
- **Loading indicator styling** is no-color (`feedback_no_color_no_emoji`) — monochrome, matches VolumeHud / CenterFlash aesthetic.
- **EQ presets are fixed** (8 standard presets), not dynamically configurable in the dropdown. User profiles append to the list.
- **Sidecar rebuild self-service** — Agent 3 runs `build_qrhi.bat` themselves per contracts-v2 for any sidecar-touching batch. No Hemanth gate.
- **No multi-window support** (inherits from prior identity calls — not a pop-out-tracks surface).

## Isolate-commit candidates (per Rule 11)

- Batch 1.1 (tracks/media decoupling from first_frame)
- Batch 2.1 (sidecar buffering/playing event dispatch)
- Batch 2.3 (Loading overlay widget)
- Batch 3.1 (teardownUi HUD reset)
- Batch 4.1 (ass_set_storage_size fix)
- Batch 4.2 (canvas-sized overlay architectural)
- Batch 5.1 (HDR dropdown alignment)
- Batch 6.2 (Tracks metadata richness)
- Batch 6.3 (EQ presets)

Phase boundaries: 1.2 + 2.2 + 6.1 + 6.4 land with their composing partners.

## Existing functions/utilities to reuse

- `VolumeHud::showWithMessage` style at [VolumeHud.cpp:73+](src/ui/player/VolumeHud.cpp#L73) — reuse for LoadingOverlay animation pattern (Batch 2.3).
- `CenterFlash::showBriefly` fade logic at [CenterFlash.cpp:32+](src/ui/player/CenterFlash.cpp#L32) — QPropertyAnimation pattern for 200ms fades.
- `ContextMenuHelper::addDangerAction` (wherever it lives) — for any destructive menu items added in Phase 6.
- `QDir::mkpath` + `QSettings` for EQ preset persistence (Batch 6.3).
- `resources/shaders/video_d3d11.hlsl` `ps_overlay` entry point — extend viewport/coords in Phase 4.2, don't rewrite.
- Existing `tracks_payload` JSON construction in `open_worker` ([main.cpp:309+](native_sidecar/src/main.cpp#L309)) — already has default/forced/channel/sample fields; Batch 6.2 consumes, no enrichment needed.

## Review gates

Agent 6 review protocol is SUSPENDED. Per phase exit:

- Agent 3 posts `READY TO COMMIT — [Agent 3, PLAYER_UX_FIX Phase <N> Batch <X.Y>]: <one-line> | files: a.cpp, b.h`.
- Agent 3 runs sidecar rebuild (per contracts-v2) for any sidecar-touching batch; posts `BUILD_EXIT=0` + output tail.
- Hemanth smokes per phase exit criteria.
- Agent 0 sweeps commits + bumps CLAUDE.md dashboard.

(Preserved for Agent 6 reactivation: `READY FOR REVIEW — [Agent 3, PLAYER_UX_FIX Phase <N> Batch <X.Y>]: ...` template — do not post unless Agent 6 is revived.)

## Open design questions Agent 3 decides as domain master

1. LoadingOverlay widget — new class or reuse `CenterFlash`? Your call based on state-lifecycle fit.
2. Buffering indicator — same widget as Loading (mode switch) or separate widget? Lean toward same (mode switch) for visual consistency.
3. Phase 1.1 — should `media_info` + `tracks_changed` fire BEFORE the sidecar `open_pending` → `opening` state transition, or AFTER? Either way is defensible; you decide.
4. Phase 4.2 `set_canvas_size` — debounce interval on drag-resize (50ms? 100ms? none — send on mouse-up only)?
5. Phase 6.2 — should channel-count be rendered inline with title (`"English · 5.1 · AC3"`) or as a secondary row? IINA uses inline; match that unless you see a Qt layout reason to differ.
6. Phase 6.3 preset set — 8 standard presets (Flat/Rock/Pop/Jazz/Classical/Bass Boost/Treble Boost/Vocal Boost) or a different canonical set? Your pick.
7. Phase 6.4 — should ESC dismissing a popover also kill the focus? Yes by default; flag if you see reasons otherwise.

## What NOT to include (explicit deferrals)

- **Timing instrumentation per audit Gap #1-#2** — deferred pending Hemanth's 30s blank-case repro. If Phase 1+2 don't close the symptom, instrumentation is a follow-up.
- **Phase 4b tone-mapping shader work for bt2390/mobius/clip/linear** — deferred unless Hemanth chooses Path B on Phase 5.
- **External subtitle fuzzy-search UI** — not in scope. Audit didn't flag.
- **Thumbnail scrubbing on seekbar hover** — not in scope.
- **Video filter real-time preview** (before applying) — not in scope.
- **Gamut mapping for HDR** — not in scope (tone-mapping only).
- **Per-track audio offset persistence** (separate from per-device offset that already exists) — not in scope.
- **Keyboard shortcut for Loading indicator dismiss** — not needed; hide is state-driven.
- **Loading indicator for warm-cache instant-open paths** — not needed if first_frame fires within ~50ms (no flash to avoid).

## Rule 6 + Rule 11 application

Standard. Every batch: Agent 3 builds (sidecar via `build_qrhi.bat` self-service per contracts-v2; main app remains Hemanth-only per contracts-v2 tier split) → validates → posts READY TO COMMIT. Agent 0 sweeps at phase boundary (or sooner if tree pressure warrants). Per `feedback_one_fix_per_rebuild` — one batch, one rebuild, one smoke. No batching of code into pre-commit rollup.

## Verification procedure (Hemanth smoke end-to-end)

1. **Phase 1 smoke:** open a local HEVC 10-bit file with multiple audio + subtitle tracks. Observe debug log: `[VideoPlayer] tracks_changed` arrives within ~200ms of `SEND open`, BEFORE first `d3d11_texture` event. Repeat with a network URL (same pre-first-frame track delivery expected).
2. **Phase 2 smoke (loading):** open a stream URL known to take >2s first frame. Observe: "Loading — <filename>" overlay appears within 200ms of click. Fades to "Buffering…" if stall detected. Disappears on first frame.
3. **Phase 2 smoke (buffering):** play a weak-swarm torrent known to have mid-stream stalls. Deliberately throttle network. Observe: "Buffering…" overlay appears during stall, disappears when playback resumes. Log shows `RECV: buffering` + `RECV: playing` signal flow.
4. **Phase 3 smoke:** play video A for 30s, switch to video B mid-playback. Observe HUD during switch window: time label → `—:—`, duration → `—:—`, seekbar → 0, chips → generic labels, popovers closed. Transition < 1 frame stale from prior file.
5. **Phase 4.1 smoke:** play anamorphic content (SAR != 1). ASS subtitles render with correct aspect (no stretched text). Rotated ASS styles render correctly.
6. **Phase 4.2 smoke:** play 2.35:1 cinemascope content with ASS subs configured for bottom placement. Subs land in the bottom letterbox bar (black area), not clipped at video edge. Repeat with PGS subs — land at their intended screen-plane positions. Resize window mid-playback — overlay resizes, subs reposition.
7. **Phase 5 smoke:** open HDR content. Cycle through each FilterPopover HDR dropdown option. Every option produces a visibly different tone-mapping result. No silent mode-0 falls.
8. **Phase 6.1 smoke:** open EQ popover → chip shows "pressed" state. Apply Rock preset → chip shows "active" indicator. Close popover → chip returns to normal. Close file → all chips show "disabled."
9. **Phase 6.2 smoke:** open a Bluray rip with 3 audio tracks (English 5.1 default, English commentary 2.0, Spanish 5.1) + 4 subtitle tracks (English default, English forced signs, Spanish, French). Open Tracks popover. Each entry shows language (expanded), channel count, default/forced markers, selected-state indicator.
10. **Phase 6.3 smoke:** open EQ popover, pick "Rock" preset → sliders snap + filter applies audibly. Adjust manually, save as "Custom1". Switch file, return — "Custom1" is in preset menu, applies correctly.
11. **Phase 6.4 smoke:** open Tracks → outside-click closes. Open Tracks → ESC closes. Open Tracks → click EQ chip → Tracks closes + EQ opens. Open EQ → click EQ chip → EQ closes (toggle). Repeat for Filters + Playlist.
12. **Regression check:** run PLAYER_LIFECYCLE_FIX + PLAYER_PERF_FIX smoke matrix (rapid file switch + HEVC 10-bit + subs + DXGI waitable cadence). No regressions from this TODO's changes.
13. **Cinemascope regression check:** the previously-deprioritized asymmetric-letterbox cosmetic bug (`feedback_cinemascope_aspect_deprioritized`) MUST NOT be fixed as a side-effect; this TODO touches subtitle plane, not viewport math. Confirm viewport letterbox geometry is unchanged (asymmetry still present and UNCHANGED).
14. **HDR regression check:** non-HDR content unaffected by Phase 5 changes — FilterPopover HDR dropdown still shows all working options, Off still applies correctly.
15. **End-to-end (comprehensive):** 10-minute session exercising: startup from cold, 3-4 rapid file switches, pause/resume, seek, track switch, subtitle switch mid-playback, EQ preset apply + save, cinemascope file with subs, HDR file with tone-mapping. HUD behavior clean throughout.

---

## Next steps post-approval

- Agent 0 posts routing announcement in chat.md when Hemanth ratifies the plan.
- Agent 3 executes phased per Rule 6 + Rule 11 + feedback_commit_cadence.
- Agent 6 reviews per phase exit [dormant — Hemanth smokes directly, READY FOR REVIEW lines not posted].
- Agent 0 commits at phase boundaries per feedback_commit_cadence (not mid-batch accumulation).
- MEMORY.md Active repo-root fix TODOs line updated to include PLAYER_UX_FIX_TODO.md.
- Drift-check posts NOT required — Agent 7 Trigger B is SUSPENDED for lifecycle/UX work (prior memory `feedback_prototype_agent_pacing`).

---

**End of plan.**
