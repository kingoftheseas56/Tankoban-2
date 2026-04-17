# Stream Mode Comparative Audit - Slice D Player UX, Buffering, Subtitles, Sidecar Open/Decode

Date: 2026-04-17
Auditor: Agent 7 (Codex)
Domain master: Agent 4 (Stream mode)
Assignment: Slice D of 6 in the stream-mode comparative audit programme
Commit prompt target: 665694e on claude/peaceful-kalam-42f30f
Audit state: Current working tree, read-only

Reference only. No `src/` or `native_sidecar/src/` files were modified.

## Scope

This audit compares Tankoban 2 stream-mode player behavior against Stremio's player stack for loading placeholders, buffering, player controls, audio and subtitle UX, player state, stream keybindings, sidecar open/probe/decode/subtitle behavior, first-frame diagnostics, aspect/cinemascope geometry, and adjacent player capabilities such as buffered range, seekability, HLS behavior, next episode, chapters, and performance telemetry.

`native_sidecar/src/` was read in the expanded Slice D scope for open/probe/decode/subtitle observations. This audit does not prescribe fixes and does not assert root causes as fact.

## Methodology

Repository context read:

- `AGENTS.md`, `agents/GOVERNANCE.md`, `agents/STATUS.md`, `agents/CONTRACTS.md`, `agents/REVIEW.md`, `agents/audits/README.md`, `agents/VERSIONS.md`.
- Closed programme TODOs: `PLAYER_UX_FIX_TODO.md`, `PLAYER_LIFECYCLE_FIX_TODO.md`, `STREAM_LIFECYCLE_FIX_TODO.md`, `PLAYER_PERF_FIX_TODO.md`.
- Current Tankoban stream/player files under `src/ui/pages`, `src/ui/pages/stream`, `src/ui/player`, and expanded sidecar files under `native_sidecar/src`.

Reference implementations read:

- Stremio web player: `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-web-development/src/routes/Player/Player.js`.
- Stremio control bar, buffering loader, audio menu, subtitles menu, video params, media capability, and shortcut-adjacent code under `stremio-web-development/src/routes/Player`.
- Stremio video element wrapper: `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-video-master/stremio-video-master/src/HTMLVideo/HTMLVideo.js`.
- Stremio HTML subtitles wrapper/parser/renderer under `stremio-video-master/stremio-video-master/src/withHTMLSubtitles`.
- Stremio core player model: `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/src/models/player.rs`.

Web references consulted:

- FFmpeg AVFormatContext docs: `max_analyze_duration` is the maximum data duration read by `avformat_find_stream_info`; `probesize` is the maximum data size read while determining input format. URL: https://ffmpeg.org/doxygen/2.5/structAVFormatContext.html
- MDN media buffering guide: `HTMLMediaElement.buffered` exposes downloaded `TimeRanges`, and custom players commonly use media time ranges for buffer/seek feedback. URL: https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Audio_and_video_delivery/buffering_seeking_time_ranges
- hls.js API: documents buffer-length, starvation, low/high buffer watchdog, nudge retry, audio track, and subtitle track controls. URL: https://nochev.github.io/hls.js/docs/API.html

No project build, test, or runtime playback was executed.

## Cross-Reference Buckets

Each finding is tagged with exactly one of the required Slice D buckets:

1. Resolved by PLAYER_UX_FIX Phase N Batch M
2. Resolved by PLAYER_LIFECYCLE_FIX Phase N
3. Resolved by cinemascope once-only-exception (ade3241 or 1f05316)
4. Resolved by STREAM_LIFECYCLE_FIX Phase N
5. Resolved by PLAYER_PERF_FIX Phase N
6. Slice A Phase 2.2/2.4/2.5/2.6.3 closed substrate already
7. Depends on Slice A Phase 2.3/3/4 (pending)
8. Genuinely new Slice D work

## Findings Per Comparison Axis

### Axis 1 - Loading Skeleton / Placeholder During Open

Finding D-01: Tankoban now has an opening overlay tied to sidecar player state.

Cross-reference status: Resolved by PLAYER_UX_FIX Phase 2 Batch 2.3.

Observations:

- Tankoban connects `playerOpeningStarted`, `playerIdle`, `bufferingStarted`, `bufferingEnded`, and `firstFrame` to `LoadingOverlay` show/dismiss paths in `src/ui/player/VideoPlayer.cpp:1378`.
- `VideoPlayer::onStateChanged` emits `playerOpeningStarted(m_pendingFile)` on `opening` and `playerIdle()` on `idle` in `src/ui/player/VideoPlayer.cpp:791`.
- `LoadingOverlay::showLoading` displays a loading pill with the basename, and `showBuffering` displays buffering text in `src/ui/player/LoadingOverlay.cpp:22` and `src/ui/player/LoadingOverlay.cpp:35`.
- Stremio renders `BufferingLoader` while `video.state.buffering || !video.state.loaded` and no player error is present in `Player.js:895`.
- Stremio's `BufferingLoader` is a compact branded spinner/logo treatment in `BufferingLoader.js:9`.

Hypotheses:

Hypothesis — any remaining "blank during open" report after PLAYER_UX_FIX is more likely to be an event delivery or sidecar startup sequencing gap than absence of a player loading component, Agent 4 to validate.

### Axis 2 - Buffering Indicator During Playback Stall

Finding D-02: Playback stall overlay plumbing is present from sidecar HTTP read stalls to Qt UI.

Cross-reference status: Resolved by PLAYER_UX_FIX Phase 2 Batch 2.3.

Observations:

- The sidecar decode loop emits `buffering` on HTTP-style `EAGAIN`, timeout, I/O, or exit interruption, sleeps, and retries up to 60 times in `native_sidecar/src/video_decoder.cpp:1061`.
- The sidecar open worker forwards video decoder `buffering` and `playing` events in `native_sidecar/src/main.cpp:481`.
- Qt `SidecarProcess::processLine` filters stale session events, then dispatches `bufferingStarted` and `bufferingEnded` for `buffering` and `playing` events in `src/ui/player/SidecarProcess.cpp:397` and `src/ui/player/SidecarProcess.cpp:544`.
- `LoadingOverlay::showBuffering` and `dismiss` are connected in `src/ui/player/VideoPlayer.cpp:1378`.
- Stremio's HTMLVideo wrapper updates `buffering` from browser media events such as waiting/stalled/playing/canplay and from `readyState < HAVE_FUTURE_DATA` in `HTMLVideo.js:39` and `HTMLVideo.js:162`.

Hypotheses:

Hypothesis — visible buffering during transport stalls should now be covered for the HTTP demux path, but stalls before `open_worker` reaches decoder setup may still present as generic loading unless Agent 4 confirms matching event coverage, Agent 4 to validate.

### Axis 3 - Control Bar Shape / Behavior

Finding D-03: The dominant control-bar parity item remaining is buffered/seekable range visibility, not basic playback controls.

Cross-reference status: Depends on Slice A Phase 2.3/3/4 (pending).

Observations:

- Stremio passes `buffered={video.state.buffered}` into `ControlBar` in `Player.js:956`.
- Stremio's `SeekBar` receives `buffered`, `time`, and `duration`, then forwards the buffered value into the slider in `SeekBar.js:45`.
- MDN documents `buffered` as downloaded media `TimeRanges`, and notes custom players use these ranges for buffer/seek feedback.
- Tankoban's stream pre-open buffer overlay shows textual engine readiness via `StreamPlayerController::onBufferTimer` in `src/ui/pages/stream/StreamPlayerController.cpp:181`.
- Tankoban's `StreamPage::onBufferUpdate` currently discards the percent argument and only updates a label in `src/ui/pages/StreamPage.cpp:1725`.
- Tankoban's in-player controls receive duration and time via sidecar `time_update`, but no observed player control path exposes a continuous buffered range equivalent to Stremio's `buffered` slider data.

Hypotheses:

Hypothesis — a Stremio-like buffered range display needs either Slice A substrate telemetry for HTTP/range/torrent seekability or a conservative player-side approximation; without that substrate, the UI can show playback time but not how safe a seek is, Agent 4 to validate.

Finding D-04: Stream next-episode input exists, but Stremio exposes next-video as a visible control-bar affordance.

Cross-reference status: Resolved by STREAM_LIFECYCLE_FIX Phase 4.

Observations:

- Tankoban declares `streamNextEpisodeRequested` in `src/ui/player/VideoPlayer.h:114`.
- `StreamPage::onReadyToPlay` connects `VideoPlayer::streamNextEpisodeRequested` to stream next-episode handling in `src/ui/pages/StreamPage.cpp:1730`.
- Tankoban default keybindings include `Shift+N` for stream next episode in `src/ui/player/KeyBindings.cpp:3`.
- Stremio renders a control-bar next-video button when `nextVideo` exists in `ControlBar.js:121`.

Hypotheses:

Hypothesis — if Agent 4 wants visible Stremio-level discoverability, this is UX polish on top of the closed lifecycle/input path, not a missing stream handoff primitive, Agent 4 to validate.

### Axis 4 - Audio Menu / Track Switching

Finding D-05: Tankoban's current audio menu is at least comparable to Stremio's baseline audio track menu for native FFmpeg streams.

Cross-reference status: Resolved by PLAYER_UX_FIX Phase 6 Batch 6.2.

Observations:

- Tankoban merges sidecar audio/subtitle tracks into a track popover payload in `src/ui/player/VideoPlayer.cpp:1845`.
- `TrackPopover` expands language, channels, sample rate, codec, default, and forced markers in `src/ui/player/TrackPopover.cpp:279`, and highlights the selected track in `src/ui/player/TrackPopover.cpp:367`.
- Stremio's `AudioMenu` maps available tracks with language labels and selected icons in `AudioMenu.tsx:36`.
- Stremio-video exposes HLS audio track ids/language/origin from hls.js in `HTMLVideo.js:255`, while hls.js also exposes audio track get/set APIs.

Hypotheses:

Hypothesis — any remaining audio parity issue is likely specific to HLS/adaptive stream substrate rather than the native FFmpeg audio menu UI, Agent 4 to validate.

### Axis 5 - Subtitle UI / Loading / Selection

Finding D-06: Tankoban covers embedded/addon/local subtitle controls, delay, vertical lift, and size, but Stremio has richer language/variant grouping and origin priority behavior.

Cross-reference status: Genuinely new Slice D work.

Observations:

- Tankoban `SubtitleMenu` exposes embedded, addon, and local subtitle sections; delay, vertical pixel offset, and size controls are present in `src/ui/player/SubtitleMenu.cpp:19`.
- Tankoban reconciles active external tracks and labels language/addon details in `src/ui/player/SubtitleMenu.cpp:307` and `src/ui/player/SubtitleMenu.cpp:318`.
- Stremio's `SubtitlesMenu` prioritizes origins, builds a language list, sorts variants by origin, and renders language and variant columns in `SubtitlesMenu.js:13`, `SubtitlesMenu.js:33`, `SubtitlesMenu.js:77`, and `SubtitlesMenu.js:156`.
- Stremio re-applies saved subtitle delay, size, and offset from player stream state in `Player.js:500`.
- Stremio's HTML subtitle wrapper exposes extra subtitle tracks, selected track id, delay, size, offset, text/background/outline color, and opacity props in `withHTMLSubtitles.js:58` and renders styled cue nodes in `withHTMLSubtitles.js:79`.

Hypotheses:

Hypothesis — users with multiple same-language subtitle variants may see more manual scanning in Tankoban than Stremio because Tankoban's current menu is source-section oriented while Stremio's is language/variant oriented, Agent 4 to validate.

Finding D-07: Subtitle rendering geometry has an explicit video-rect path after cinemascope work, but letterbox-area subtitle placement is intentionally not represented in the current renderer path.

Cross-reference status: Resolved by cinemascope once-only-exception (ade3241 or 1f05316).

Observations:

- Tankoban `FrameCanvas::fitAspectRect` computes centered aspect-fit video rectangles with even spare-pixel adjustment in `src/ui/player/FrameCanvas.cpp:408`.
- Tankoban draws the video into that rect and logs aspect diagnostics in `src/ui/player/FrameCanvas.cpp:900` and `src/ui/player/FrameCanvas.cpp:980`.
- Tankoban draws subtitle overlay SHM onto the same video rect, with a subtitle lift offset, and comments identify the current Option A rollback / video-sized overlay behavior in `src/ui/player/FrameCanvas.cpp:1036`.
- Sidecar `subtitle_renderer::set_frame_size` contains canvas/video-rect machinery, storage size, margins, and pixel-aspect handling in `native_sidecar/src/subtitle_renderer.cpp:268`.

Hypotheses:

Hypothesis — current code is consistent with the once-only exception's video-rect overlay posture; any renewed request for subtitles in letterbox bars would be a product/design decision rather than an unclosed cinemascope regression, Agent 4 to validate.

### Axis 6 - Player State Machine

Finding D-08: Session fencing and stop/open lifecycle protection are present; metadata now arrives before first frame.

Cross-reference status: Resolved by PLAYER_LIFECYCLE_FIX Phase 1.

Observations:

- `VideoPlayer::openFile` performs UI teardown, sets current/pending file state, and fences an active sidecar with `sendStopWithCallback` before issuing a new open in `src/ui/player/VideoPlayer.cpp:265`.
- `VideoPlayer::teardownUi` resets visible player state, track arrays, popovers, stats, and chips in `src/ui/player/VideoPlayer.cpp:486`.
- `SidecarProcess::sendOpen` regenerates a session id for each open in `src/ui/player/SidecarProcess.cpp:140`.
- `SidecarProcess::processLine` drops stale session events before emitting Qt signals in `src/ui/player/SidecarProcess.cpp:410`.
- The sidecar emits `state_changed{opening}` on open and sends `tracks_changed` / `media_info` after probe, before `first_frame`, in `native_sidecar/src/main.cpp:705` and `native_sidecar/src/main.cpp:325`.
- The sidecar `handle_stop` performs teardown and then emits `stop_ack` in `native_sidecar/src/main.cpp:727`.

Hypotheses:

Hypothesis — stale-event/lifecycle regressions found before PLAYER_LIFECYCLE_FIX should not be revalidated as Slice D issues unless Agent 4 can reproduce them against current session-id filtering and stop_ack ordering, Agent 4 to validate.

Finding D-09: There is no distinct "probe complete but first frame absent for too long" state in the observed UI protocol.

Cross-reference status: Genuinely new Slice D work.

Observations:

- The sidecar has `OPEN_PENDING`, `PLAYING`, `PAUSED`, and `IDLE` states in `native_sidecar/src/state_machine.h:7`.
- Sidecar open emits `state_changed{opening}` before starting `open_worker` in `native_sidecar/src/main.cpp:705`.
- Sidecar metadata events are sent after probe in `native_sidecar/src/main.cpp:325`, while `PLAYING` is set only in the first-frame callback in `native_sidecar/src/main.cpp:397`.
- `VideoPlayer::onFirstFrame` attaches video surfaces and starts frame polling in `src/ui/player/VideoPlayer.cpp:724`.
- Stremio core tracks `loaded` separately from stream state in `player.rs:97`, and its web/video wrappers carry media-level `loaded`, `buffering`, and event-driven state separately from UI menu state.

Hypotheses:

Hypothesis — the current Tankoban protocol can distinguish `opening`, metadata arrival, and `first_frame`, but may not classify a long interval between metadata and first frame into a specific user-facing or diagnostic state, Agent 4 to validate.

### Axis 7 - Keybindings In Stream Context

Finding D-10: Tankoban covers the main playback, audio, subtitle, seek, chapter, fullscreen, and stream-next keyboard surface; Stremio has a temporary speed-up gesture Tankoban does not mirror.

Cross-reference status: Genuinely new Slice D work.

Observations:

- Tankoban defaults include pause, seek, frame step, volume, fullscreen, audio cycle, subtitle cycle/toggle/menu, subtitle delay, audio delay, chapters, next/previous, and stream next episode in `src/ui/player/KeyBindings.cpp:3`.
- `VideoPlayer::keyPressEvent` dismisses popovers on Escape, handles seek keys, audio/subtitle keys, and chapter keys in `src/ui/player/VideoPlayer.cpp:2700` and `src/ui/player/VideoPlayer.cpp:2726`.
- Stremio handles temporary 2x playback via Space hold / mouse hold behavior and wheel volume adjustments in `Player.js:761`.

Hypotheses:

Hypothesis — Tankoban's desktop keyboard surface is functionally broad enough for stream mode; Stremio's press-and-hold speed gesture is a parity polish item, not a blocker for the reported load/freeze symptoms, Agent 4 to validate.

### Axis 8 - Open To First-Frame Timeline / Event Protocol

Finding D-11: The current event protocol is improved, but the open-to-first-frame timeline still has a diagnostic blind spot for "never produced first frame."

Cross-reference status: Genuinely new Slice D work.

Observations:

- `StreamPage::onSourceActivated` shows the stream buffer overlay, loads subtitles in parallel, and starts stream handoff in `src/ui/pages/StreamPage.cpp:1632`.
- `StreamPage::onReadyToPlay` hides the stream buffer overlay and calls `player->openFile(httpUrl, {}, 0, streamResumeSec)` in `src/ui/pages/StreamPage.cpp:1730` and `src/ui/pages/StreamPage.cpp:1892`.
- Native sidecar `open_worker` probes, emits tracks/media, then starts decoder and emits first-frame only from decoder callbacks in `native_sidecar/src/main.cpp:262`, `native_sidecar/src/main.cpp:325`, and `native_sidecar/src/main.cpp:397`.
- `video_decoder` fires first-frame events on the zero-copy and slow paths in `native_sidecar/src/video_decoder.cpp:659` and `native_sidecar/src/video_decoder.cpp:996`.
- Non-fatal packet/frame decode errors are emitted as `decode_error` but decoding continues in `native_sidecar/src/video_decoder.cpp:1123`.

Hypotheses:

Hypothesis — a user report of "never loaded" after StreamEngine readiness may map to the interval after `openFile` and before any `first_frame` event, with insufficient current classification of whether the delay is probe, decoder init, packet read, decode output, or surface attach, Agent 4 to validate.

### Axis 9 - Probe Behavior

Finding D-12: HTTP stream probing is a likely high-priority Slice D investigation area because current FFmpeg probe settings permit multi-second open latency before the player can render.

Cross-reference status: Genuinely new Slice D work.

Observations:

- `demuxer::probe_file` sets HTTP reconnect options, `timeout=60000000`, `rw_timeout=30000000`, `probesize=20000000`, and `analyzeduration=10000000`, then calls `avformat_open_input` and `avformat_find_stream_info` in `native_sidecar/src/demuxer.cpp:33`.
- `video_decoder` opens the URL again for decode with similar HTTP options and then calls `avformat_find_stream_info` in `native_sidecar/src/video_decoder.cpp:196`.
- FFmpeg documents `max_analyze_duration` as the maximum duration of data read by `avformat_find_stream_info`, and `probesize` as maximum data size for input/container probing.
- Stremio obtains `videoParams` primarily from behavior hints / stream-server stats such as hash, size, and filename in `fetchVideoParams.js:3`, then the HTMLVideo wrapper relies on browser media events and capability checks in `HTMLVideo.js:39`, `HTMLVideo.js:681`, and `HTMLVideo.js:696`.

Hypotheses:

Hypothesis — measured 10 second class "took forever to load" symptoms may correlate with the explicit 10,000,000 microsecond analyze duration and/or repeated open/probe work, but Agent 4 needs timing logs to attribute the delay precisely, Agent 4 to validate.

### Axis 10 - Decode-Stall / First-Frame-Absence Diagnostic Surface

Finding D-13: Tankoban has performance logs and buffering events, but the observed UI protocol does not expose a Stremio/hls.js-like stall recovery taxonomy.

Cross-reference status: Genuinely new Slice D work.

Observations:

- `video_decoder` tracks first-frame state, performance vectors, buffering state, and stall count in `native_sidecar/src/video_decoder.cpp:432`.
- `video_decoder` emits one-second `[PERF]` logs with frames, drops, blend, present, and total timing in `native_sidecar/src/video_decoder.cpp:969`.
- `FrameCanvas` has waitable swapchain scheduling and 1Hz `[PERF]` frame canvas logging in `src/ui/player/FrameCanvas.cpp:495` and `src/ui/player/FrameCanvas.cpp:809`.
- `AVSyncClock` starts from audio updates and reports playback position in `native_sidecar/src/av_sync_clock.cpp:7` and `native_sidecar/src/av_sync_clock.cpp:88`.
- hls.js documents buffer watchdog periods, playhead nudge behavior, and fatal buffer-stall escalation after retries.

Hypotheses:

Hypothesis — a "frozen frame while time advances" report could involve decode output, presentation, SHM/D3D11 transfer, or AV-sync position reporting; the current logs contain pieces but may not correlate frame identity, time updates, and UI presentation in one user-visible state, Agent 4 to validate.

### Axis 11 - Aspect Ratio / Cinemascope Geometry

Finding D-14: Aspect-fit and overlay geometry work is present and instrumented; remaining 16:9 top-bar or letterbox-placement concerns are validation items against the once-only exception.

Cross-reference status: Resolved by cinemascope once-only-exception (ade3241 or 1f05316).

Observations:

- `FrameCanvas::fitAspectRect` centers aspect-fit geometry with integer adjustment in `src/ui/player/FrameCanvas.cpp:408`.
- The video draw pass calculates source/frame aspect and sets a video viewport in `src/ui/player/FrameCanvas.cpp:900` and `src/ui/player/FrameCanvas.cpp:966`.
- Aspect diagnostic logging writes source/widget/DPR/video-rect/forced-aspect data in `src/ui/player/FrameCanvas.cpp:980`.
- Subtitle overlay is drawn inside the same video rect in `src/ui/player/FrameCanvas.cpp:1036`.
- `native_sidecar/src/main.cpp:1107` currently treats `set_canvas_size` as a no-op after the Option A rollback.

Hypotheses:

Hypothesis — if 16:9 still shows a top-only black bar, the current diagnostic lines are the right evidence to inspect first; this audit cannot state whether that symptom is still reproducible without runtime validation, Agent 4 to validate.

### Axis 12 - Other Adjacent Player Capabilities

Finding D-15: HLS/adaptive buffering, bitrate/quality selection, and browser-level codec fallback are cross-slice substrate concerns rather than isolated player-UX defects.

Cross-reference status: Slice A Phase 2.2/2.4/2.5/2.6.3 closed substrate already.

Observations:

- Stremio `HTMLVideo` uses browser `canPlayType`, HLS support, and stream behavior hints to decide whether it can play a stream in `HTMLVideo.js:681`.
- Stremio creates hls.js for HLS streams when supported in `HTMLVideo.js:552`.
- hls.js exposes ABR, buffer starvation, max buffer, audio track, and subtitle track APIs.
- Tankoban stream handoff currently obtains a local HTTP URL from StreamEngine and then asks the native FFmpeg sidecar to open that URL in `src/ui/pages/StreamPage.cpp:1892`.
- Slice A already owns engine/server substrate questions; this Slice D audit only notes when player UX depends on those substrate signals.

Hypotheses:

Hypothesis — HLS/transcoding/adaptive bitrate gaps should remain cross-slice and substrate-scoped unless Agent 4 can tie a user-visible symptom to existing player-side event misuse, Agent 4 to validate.

Finding D-16: Player performance telemetry exists after PLAYER_PERF_FIX, but user-facing diagnostics remain sparse.

Cross-reference status: Resolved by PLAYER_PERF_FIX Phase 3.

Observations:

- Sidecar decode and canvas performance logging exists in `native_sidecar/src/video_decoder.cpp:969` and `src/ui/player/FrameCanvas.cpp:809`.
- FrameCanvas uses a waitable swapchain loop when available in `src/ui/player/FrameCanvas.cpp:218` and `src/ui/player/FrameCanvas.cpp:495`.
- The player stats popover can be hidden/reset during teardown in `src/ui/player/VideoPlayer.cpp:486`.

Hypotheses:

Hypothesis — PLAYER_PERF_FIX gives Agent 4 enough low-level telemetry to validate "frozen but clock moving" reports, but it does not by itself convert those reports into user-facing recovery states, Agent 4 to validate.

## Cross-Cutting Observations

- The old "no loader / blank player" category appears closed in the current tree by PLAYER_UX_FIX and PLAYER_LIFECYCLE_FIX.
- The old "stale old stream event changed the new player" category appears closed by session-id filtering and stop/open fencing.
- Stremio leans on browser media element state (`loaded`, `buffering`, `buffered`, `canPlayType`) and hls.js buffer machinery; Tankoban uses an FFmpeg sidecar and therefore must manufacture equivalent user-facing state from probe, decoder, transport, AV-sync, and surface events.
- Tankoban's current strongest parity surface is the native player UI: audio menu, subtitle menu basics, popover teardown, keyboard coverage, loader, buffering pill, and performance logs.
- Tankoban's current weakest parity surface is not a missing widget; it is the classification of long open/decode intervals into actionable states.

## Priority Ranking

P0:

- D-12 Probe behavior. Long HTTP probe/analyze paths can directly explain "took forever to load" symptoms. Status: Genuinely new Slice D work.
- D-11 Open-to-first-frame diagnostic blind spot. This is the closest match for "never loaded after source ready." Status: Genuinely new Slice D work.

P1:

- D-13 Decode-stall / first-frame-absence diagnostic surface. This covers frozen-frame / time-advancing ambiguity. Status: Genuinely new Slice D work.
- D-03 Buffered/seekable range visibility. Stremio gives users buffer feedback; Tankoban currently does not expose a comparable in-player range. Status: Depends on Slice A Phase 2.3/3/4 (pending).

P2:

- D-06 Subtitle language/variant grouping and origin priority. Tankoban covers controls but is less Stremio-like for multi-variant subtitle selection. Status: Genuinely new Slice D work.
- D-09 Missing explicit post-probe/pre-first-frame state. This is a diagnostic/protocol clarity gap. Status: Genuinely new Slice D work.
- D-14 Aspect/cinemascope validation. Code is instrumented and mostly closed, but runtime confirmation remains important for reported top-bar symptoms. Status: Resolved by cinemascope once-only-exception.

P3 / closed or polish:

- D-01 Loading overlay. Closed by PLAYER_UX_FIX.
- D-02 Buffering overlay. Closed by PLAYER_UX_FIX.
- D-04 Stream next episode. Lifecycle/input closed; visible button is polish.
- D-05 Audio menu. Closed by PLAYER_UX_FIX.
- D-07 Subtitle geometry posture. Closed by cinemascope exception unless product scope changes.
- D-08 Lifecycle/session fencing. Closed by PLAYER_LIFECYCLE_FIX.
- D-10 Temporary 2x press/hold gesture. New but polish.
- D-15 HLS/adaptive substrate. Cross-slice/Slice A boundary.
- D-16 Performance telemetry. Closed by PLAYER_PERF_FIX for telemetry, not necessarily user-facing diagnostics.

## Reference Comparison Matrix

| Axis | Stremio reference behavior | Tankoban current behavior | Bucket |
|---|---|---|---|
| Loading skeleton | BufferingLoader while not loaded or buffering (`Player.js:895`) | LoadingOverlay on `opening`, dismissed on first frame/idle (`VideoPlayer.cpp:1378`) | Resolved by PLAYER_UX_FIX Phase 2 Batch 2.3 |
| Playback buffering | HTML media waiting/stalled/readyState drive buffering (`HTMLVideo.js:39`, `HTMLVideo.js:162`) | Sidecar HTTP stalls emit buffering/playing to overlay (`video_decoder.cpp:1061`, `SidecarProcess.cpp:544`) | Resolved by PLAYER_UX_FIX Phase 2 Batch 2.3 |
| Control bar buffered range | `buffered` passed to SeekBar (`ControlBar.js:107`, `SeekBar.js:45`) | No observed continuous buffered range in in-player seek UI | Depends on Slice A Phase 2.3/3/4 (pending) |
| Next episode | Visible next button if `nextVideo` (`ControlBar.js:121`) | `Shift+N` and stream next signal/handler exist | Resolved by STREAM_LIFECYCLE_FIX Phase 4 |
| Audio menu | Language/selected audio menu (`AudioMenu.tsx:36`) | TrackPopover shows language, codec, channels, default/forced | Resolved by PLAYER_UX_FIX Phase 6 Batch 6.2 |
| Subtitle menu | Language/variant/origin-priority UI (`SubtitlesMenu.js:13`) | Source-section menu with embedded/addon/local, delay, offset, size | Genuinely new Slice D work |
| State fencing | Core selected stream state plus video state | Session id filtering and stop/open fence | Resolved by PLAYER_LIFECYCLE_FIX Phase 1 |
| Open to first frame | Browser media events plus loaded/buffering state | Probe -> metadata -> decoder -> first_frame; no explicit post-probe timeout state observed | Genuinely new Slice D work |
| Probe | Browser/player capability and stream-server video params | FFmpeg probe/open on sidecar, plus second decoder open | Genuinely new Slice D work |
| Decode stall | Browser/hls.js buffering and watchdog concepts | Decode logs, buffering events, AV sync, canvas perf logs | Genuinely new Slice D work |
| Aspect geometry | Browser video element 100% black background (`HTMLVideo.js:20`) | Native aspect-fit rect and same-rect overlay; diagnostics present | Resolved by cinemascope once-only-exception |
| HLS/adaptive | hls.js integration and ABR/buffer APIs | Native FFmpeg sidecar path; substrate-owned | Slice A Phase 2.2/2.4/2.5/2.6.3 closed substrate already |

## Implementation Strategy Options For Agent 4 (Non-Prescriptive)

Option A - Preserve current UI and improve diagnostic classification:

- Keep the LoadingOverlay/SubtitleMenu/TrackPopover structure as-is.
- Add validation around probe timing, decoder timing, first-frame timeout, and frozen-frame correlation using existing logs and minimal new event names if needed.
- This option targets the highest-priority P0/P1 uncertainty without reopening closed UI polish.

Option B - Add Stremio-like buffer/seek feedback once substrate telemetry is ready:

- Wait for Slice A Phase 2.3/3/4 substrate decisions before shaping in-player buffered range display.
- Map stream seekability/buffer windows into a seekbar overlay only when the data is semantically reliable.
- This option avoids pretending a torrent/HTTP/progressive stream has the same `TimeRanges` guarantees as an HTML media element.

Option C - Improve subtitle variant discoverability:

- Treat current subtitle controls as functionally closed, but compare Stremio's language/variant grouping against Tankoban's source-section grouping.
- Keep existing delay/offset/size controls and local subtitle flow intact.
- This option is isolated from decode/open diagnostics and could be validated independently.

Option D - Defer HLS/adaptive parity to cross-slice substrate:

- Keep HLS/transcoding/adaptive bitrate out of Slice D player UI unless Agent 4 reproduces a symptom in the current native sidecar path.
- Use Slice A's substrate findings as the authoritative source for stream engine/server capability decisions.

## Cross-Slice Findings Appendix

- Buffered-range UI: player UX needs a range or seekability signal, but the source of truth likely belongs to Slice A substrate work. Cross-reference: Depends on Slice A Phase 2.3/3/4 (pending).
- HLS/adaptive playback: Stremio's parity advantage is partly hls.js/browser substrate, not a simple Qt control-bar omission. Cross-reference: Slice A Phase 2.2/2.4/2.5/2.6.3 closed substrate already.
- Probe/open latency: sidecar probe settings are in expanded Slice D read scope, but any change in stream server readiness thresholds or file-serving semantics crosses into Slice A. Cross-reference: Genuinely new Slice D work, with substrate dependency to validate.
- Cinemascope/subtitle geometry: current player code follows the once-only exception and Option A rollback. Runtime reports should be validated against current diagnostics before reopening geometry work. Cross-reference: Resolved by cinemascope once-only-exception.
- Lifecycle/stop/replacement: StreamPage and VideoPlayer include replacement/user/failure stop distinctions and stop/open fences. Cross-reference: Resolved by STREAM_LIFECYCLE_FIX Phase 4 and PLAYER_LIFECYCLE_FIX Phase 1.

## Gaps Agent 4 Should Close In Validation

1. Capture a timed trace from stream source activation to StreamEngine readiness, `VideoPlayer::openFile`, sidecar `opening`, `tracks_changed`, `media_info`, decoder start, and `first_frame`.
2. Measure whether `demuxer::probe_file` reaches the 10 second analyze window on the reported slow-loading stream.
3. Measure whether the decoder performs a second expensive URL open/probe after the initial demuxer probe.
4. Confirm whether users can see the LoadingOverlay for the entire post-open/pre-first-frame interval.
5. Confirm whether `buffering` events fire before first frame or only after decoder packet read begins.
6. Confirm whether a decode path can emit repeated `decode_error` events without surfacing a terminal or classified UI state.
7. Correlate sidecar `[PERF]` frame counters with FrameCanvas `[PERF]` counters during any "frozen frame while time advances" repro.
8. Confirm whether `AVSyncClock` can advance time when no new video frame has been presented.
9. Confirm whether SHM or D3D11 frame counters continue changing during a frozen-frame repro.
10. Validate whether seekbar UI can safely show any buffered/seekable range for the current local HTTP stream without Slice A telemetry.
11. Confirm whether `StreamPage::onBufferUpdate` ignoring percent is intentional after player handoff.
12. Compare Stremio subtitle language/variant grouping against Tankoban's current source-section grouping using a stream with multiple same-language subtitle variants.
13. Confirm whether saved subtitle language/id preferences survive addon vs embedded selection changes in stream mode.
14. Confirm whether subtitle delay/offset/size persistence applies consistently to embedded, addon, and local subtitle paths.
15. Validate 16:9, 21:9, and anamorphic samples with current aspect diagnostics enabled.
16. Confirm whether `set_canvas_size` no-op is still the intended behavior for all subtitle formats after Option A rollback.
17. Validate PGS/DVD subtitle placement inside the video rect for cinemascope content.
18. Confirm whether `Shift+N` is discoverable enough or whether a visible next-video affordance belongs to Stream UX parity.
19. Confirm whether long-press temporary 2x speed from Stremio is in or out of Tankoban's desired desktop UX scope.
20. Confirm that stale events from an old sidecar session cannot affect a replacement stream under rapid stream switching.
21. Confirm that `stop_ack` ordering still holds when stop occurs during a long probe.
22. Confirm whether HLS/adaptive bitrate parity is explicitly out of Slice D validation unless Slice A changes the substrate.

## Audit Boundary Notes

- This audit is advisory and comparative only.
- No source files under `src/` or `native_sidecar/src/` were modified.
- No project compile, run, playback, or automated test was performed.
- Root-cause statements are intentionally framed as hypotheses for Agent 4 validation.
- Closed PLAYER_UX_FIX, PLAYER_LIFECYCLE_FIX, STREAM_LIFECYCLE_FIX, PLAYER_PERF_FIX, Slice A substrate, and cinemascope once-only-exception work was not reopened except where the current Slice D prompt explicitly required cross-reference.
