# Audit - av_sub_sync_after_stall - 2026-04-21

By Agent 7 (Codex). For Agent 3 (Video Player) with Agent 4 (Stream mode) context.
Reference comparison: mpv, IINA, VLC, Stremio shell/html player wrappers.
Scope: A/V/subtitle resync behavior after an HTTP-backed stream stops delivering bytes for roughly 4-30 seconds and later resumes. Focus is trigger -> recovery action -> clock/subtitle consequences. Out of scope: cold-open latency, torrent scheduling tactics, UI polish unrelated to recovery semantics.

References consulted:
- Tankoban local code: `src/ui/player/VideoPlayer.cpp`, `src/ui/player/SidecarProcess.cpp`, `src/ui/pages/StreamPage.cpp`, `src/core/stream/StreamEngine.cpp`, `native_sidecar/src/{main.cpp,video_decoder.cpp,audio_decoder.cpp,av_sync_clock.cpp,subtitle_renderer.cpp}`
- Tankoban local evidence logs: `out/sidecar_debug_DURFIX_WAKE2_AVSYNC_132331.log`, `out/stream_telemetry_DURFIX_WAKE2_AVSYNC_132331.log`
- mpv source: local copy at `C:\Users\Suprabha\Downloads\Stremio Reference\mpv-master\mpv-master` and official upstream:
  - [mpv player/playloop.c](https://github.com/mpv-player/mpv/blob/master/player/playloop.c)
  - [mpv player/audio.c](https://github.com/mpv-player/mpv/blob/master/player/audio.c)
  - [mpv player/sub.c](https://github.com/mpv-player/mpv/blob/master/player/sub.c)
  - [mpv manual properties](https://mpv.io/manual/master/#properties)
- IINA official upstream:
  - [IINA README](https://github.com/iina/iina/blob/develop/README.md)
  - [iina/MPVProperty.swift](https://github.com/iina/iina/blob/develop/iina/MPVProperty.swift)
  - [iina/PlayerCore.swift](https://github.com/iina/iina/blob/develop/iina/PlayerCore.swift)
- VLC official upstream:
  - [vlc src/input/es_out.c](https://github.com/videolan/vlc/blob/master/src/input/es_out.c)
  - [vlc src/clock/input_clock.c](https://github.com/videolan/vlc/blob/master/src/clock/input_clock.c)
  - [vlc src/input/decoder.c](https://github.com/videolan/vlc/blob/master/src/input/decoder.c)
- Stremio official upstream:
  - [stremio-video src/ShellVideo/ShellVideo.js](https://github.com/Stremio/stremio-video/blob/master/src/ShellVideo/ShellVideo.js)
  - [stremio-video src/HTMLVideo/HTMLVideo.js](https://github.com/Stremio/stremio-video/blob/master/src/HTMLVideo/HTMLVideo.js)

## Observed behavior (in our codebase)

1. Tankoban's stream stack does detect and log recovery, but only at the telemetry layer. `StreamEngine` writes `stall_detected` and `stall_recovered` events (`src/core/stream/StreamEngine.cpp:1055-1104`), and the wake-2 evidence log shows seven recoveries on pieces 365, 366 (twice), 369, 370, 371, and 372 (`out/stream_telemetry_DURFIX_WAKE2_AVSYNC_132331.log:229-811`). `StreamPage` does not consume that recovery edge or recovery metadata; it only polls `stats.stalled` and forwards the boolean to `VideoPlayer::setStreamStalled()` (`src/ui/pages/StreamPage.cpp:1885-1906`).

2. During a stall, Tankoban suppresses misleading HUD time updates, but this is explicitly a UI patch, not a transport/clock reset. `VideoPlayer::onTimeUpdate()` comments that sidecar `timeUpdate` keeps coming from the audio PTS clock while video is dry, so the player pins the seek slider and time label (`src/ui/player/VideoPlayer.cpp:978-1000`). `VideoPlayer::setStreamStalled()` only shows or dismisses the buffering overlay (`src/ui/player/VideoPlayer.cpp:700-737`).

3. The sidecar's recovery signal is only "stall ended, hide buffering". `video_decoder.cpp` emits `buffering` when HTTP reads return `EAGAIN`/`ETIMEDOUT`/`EIO`, and emits `playing` on the first successful post-stall read (`native_sidecar/src/video_decoder.cpp:1332-1404`). `main.cpp` maps those to JSON events with no extra payload (`native_sidecar/src/main.cpp:609-628`). `SidecarProcess` maps `playing` directly to `bufferingEnded()` and nothing more (`src/ui/player/SidecarProcess.cpp:570-579`).

4. Tankoban's playback clock is audio-driven and is not explicitly re-anchored on stall recovery. `AudioDecoder` updates `AVSyncClock` only after PortAudio accepts audio (`native_sidecar/src/audio_decoder.cpp:648-650`). `AVSyncClock::update()` only re-anchors on startup or on a large forward audio jump; `seek_anchor()` is only called from seek paths (`native_sidecar/src/av_sync_clock.cpp:7-34`, `81-95`, `native_sidecar/src/audio_decoder.cpp:402-405`). No recovery-time `seek_anchor`, flush, or reset path was found in the audited player/sidecar files.

5. Video recovery is "catch up to the audio clock by dropping frames". `VideoDecoder` compares each video frame PTS against `clock_->position_us()` and drops frames that are too far behind (`native_sidecar/src/video_decoder.cpp:771-778`). The wake-2 stall log shows this in the exact failure window: video fell behind by 35.5 s at 74.250 s PTS and later reached 408 ms at 110.500 s PTS entirely through frame dropping (`out/sidecar_debug_DURFIX_WAKE2_AVSYNC_132331.log:143-173`).

6. Subtitle timing is not jointly recovered with audio. Subtitle packets are queued on the decode thread using packet PTS/duration (`native_sidecar/src/video_decoder.cpp:1422-1431`, `native_sidecar/src/subtitle_renderer.cpp:413-431`). Rendering then uses the current video PTS plus configured subtitle delay (`native_sidecar/src/video_decoder.cpp:1158-1161`, `native_sidecar/src/subtitle_renderer.cpp:221-233`, `595-606`, `727-731`, `916-918`). That means subtitle presentation follows the video PTS the decoder is currently drawing, not a separate recovery anchor shared with the audio master clock.

7. Net result: Tankoban has three separate behaviors during recovery rather than one coordinated resync policy.
- Stream layer: telemetry says "stall recovered".
- Player/UI layer: boolean stall flag clears the overlay.
- Decoder layer: video drop loop chases the audio clock; subtitle renderer keeps using current video PTS.

## Reference behavior

### Quick matrix

| Reference | Trigger that starts recovery | What happens on recovery | Subtitle consequence |
|---|---|---|---|
| mpv | Cache low enough to require wait, plus actual underrun evidence (`player/playloop.c:739-781`) | Enters `paused-for-cache`, updates internal pause state, clears underruns only after full recovery; timestamp reset can escalate to `reset_playback_state()` (`player/audio.c:892-897`) | Subtitle updates are keyed to playback/video PTS and can return "not ready yet" until demux catches up (`player/sub.c:163-169`) |
| IINA | No separate transport trigger found; it reads mpv's cache properties on network streams (`PlayerCore.swift:2678-2685`) | UI/model mirrors libmpv's `paused-for-cache`, cache state, and cache time; no separate IINA-side decoder reset found in audited files | Inherits mpv behavior |
| VLC | Buffering state / PCR lateness / clock discontinuity (`src/input/es_out.c:1127-1253`, `3699-3712`, `src/clock/input_clock.c:230-277`) | Resets input/output clock references, waits decoders, rebases system origin, sets new PCR, flushes audio/video/SPU decoder outputs (`src/input/decoder.c:2758-2836`) | Subtitle/SPU channel is explicitly flushed with the rest of the pipeline |
| Stremio | Desktop shell observes mpv `paused-for-cache`; HTML fallback observes DOM `stalled` / `playing` (`ShellVideo.js:75-77`, `173-185`; `HTMLVideo.js:60-67`, `162-167`) | Shell wrapper delegates recovery to mpv; HTML wrapper delegates to browser media element | Inherits underlying player behavior; no separate wrapper-level A/V/sub resync logic found |

### mpv

1. mpv has an explicit cache-pause state. The public properties `paused-for-cache` and `cache-buffering-state` are documented as "playback is paused because of waiting for the cache" and the percentage until unpause ([manual](https://mpv.io/manual/master/#properties); local copy `DOCS/man/input.rst:2687-2692`).

2. The trigger is not "any slow read". `player/playloop.c:739-781` checks whether cache is low enough to matter and whether there was a real demux/output underrun. If so, it sets `paused_for_cache`, updates the internal pause state, and only clears underrun flags after cache recovery. That is materially different from Tankoban's current "audio clock keeps running, UI hides the lie".

3. mpv also has an escalation path when timestamps jump discontinuously. `player/audio.c:892-897` warns "Reset playback due to audio timestamp reset" and calls `reset_playback_state()`.

4. Subtitles are updated against playback/video PTS and can explicitly report "not ready, wait for more demuxer data". `player/sub.c:163-169` documents `update_subtitles()` returning false if the player should wait for new demuxer data, and `player/sub.c:126-152` shows subtitle updates keyed to `video_pts`.

### IINA

1. IINA is explicitly based on mpv ([README](https://github.com/iina/iina/blob/develop/README.md), line 19). The audited source did not expose a separate transport or decoder recovery layer on top of mpv.

2. The source that was verified does expose mpv's cache state directly. `iina/MPVProperty.swift:141-154` contains `demuxerCacheTime`, `pausedForCache`, and `cacheBufferingState`. `iina/PlayerCore.swift:2678-2685` reads those properties for network streams and stores them into `info.pausedForCache`, `info.cacheTime`, and `info.bufferingState`.

3. Observation: in the audited IINA files, the app layer mirrors cache state and buffering percentage from mpv. I did not find an IINA-specific decoder flush/re-anchor path for post-stall recovery in the files fetched for this audit. Inference: IINA's recovery behavior for this case is functionally mpv's recovery behavior with IINA UI on top.

### VLC

1. VLC has the strongest explicit "hard resync" pipeline of the four references audited.

2. On buffering entry, VLC resets clock references and enters buffering mode. `src/input/es_out.c:1101-1114` calls `input_clock_Reset()`, resets the input clock, clears last PCR, and marks `b_buffering = true`.

3. On buffering exit, VLC computes whether it has enough buffered duration; if not, it keeps reporting cache progress (`src/input/es_out.c:1164-1188`). Once buffering is done, it waits for decoders, rebases the system origin, resets the main clock, and sets a fresh first PCR (`src/input/es_out.c:1190-1253`).

4. VLC's input clock also detects discontinuities and resets its reference/drift tracking when stream and system time diverge too much or go backward. `src/clock/input_clock.c:246-277` logs unexpected stream discontinuity and resets the reference point; `src/clock/input_clock.c:334-342` shows the reset implementation.

5. Decoder flush is not audio/video-only. `src/input/decoder.c:2758-2836` flushes the audio stream, video output, and subtitle/SPU channel. That is the clearest source-backed contrast with Tankoban's current recovery path, which only dismisses buffering UI and lets video drop against the existing audio clock.

### Stremio

1. Stremio desktop shell is a thin mpv wrapper for this scenario. `src/ShellVideo/ShellVideo.js:75-77` observes `paused-for-cache`, `cache-buffering-state`, and `demuxer-cache-time`. `src/ShellVideo/ShellVideo.js:173-185` maps `paused-for-cache` and `seeking` into the wrapper's `buffering` property.

2. I did not find a separate Stremio shell-side decoder flush or clock-reset path in the audited wrapper file. Observation: Stremio desktop exposes mpv's buffering state; inference: the actual recovery semantics remain libmpv's.

3. The HTML fallback has the same architectural pattern, but delegated to the browser: `src/HTMLVideo/HTMLVideo.js:60-67` uses `onstalled` and `onplaying`, and `src/HTMLVideo/HTMLVideo.js:162-167` defines buffering as `readyState < HAVE_FUTURE_DATA`. Again, no wrapper-side A/V/sub resync logic was found there.

## Gaps (ranked P0 / P1 / P2)

**P0 (user-blocking or severely degrading):**

- Tankoban does not have a coordinated post-stall resync primitive. Observed: stream recovery stops at telemetry (`src/core/stream/StreamEngine.cpp:1055-1104`) and UI overlay dismissal (`src/ui/pages/StreamPage.cpp:1885-1906`, `src/ui/player/SidecarProcess.cpp:570-579`). Reference: mpv pauses playback for cache (`player/playloop.c:739-781`), VLC resets/rebases clocks and waits decoders (`src/input/es_out.c:1190-1253`). Impact: after long or repeated starvation windows, audio, video, and subtitles can re-enter playback on different implicit clocks.

- Tankoban's active recovery mechanism is video-only frame dropping against a still-running audio master clock. Observed: `VideoDecoder` drops late frames based on `clock_->position_us()` (`native_sidecar/src/video_decoder.cpp:771-778`), while `AudioDecoder` keeps advancing the clock from accepted audio writes (`native_sidecar/src/audio_decoder.cpp:648-650`). Reference: mpv avoids that drift by pausing for cache, and VLC escalates to full clock reset/flush. Impact: the observed wake-2 failure mode matches this architecture exactly: video recovered from 35.5 s behind to 408 ms via frame drop, while the session still sounded/appeared desynced (`out/sidecar_debug_DURFIX_WAKE2_AVSYNC_132331.log:143-173`).

- Tankoban does not flush or re-anchor subtitle state on long-stall recovery. Observed: subtitle packets keep being queued by packet PTS and rendered at current video PTS plus delay (`native_sidecar/src/video_decoder.cpp:1422-1431`, `1158-1161`; `native_sidecar/src/subtitle_renderer.cpp:221-233`, `595-606`, `727-731`). Reference: VLC flushes the SPU channel during decoder flush (`src/input/decoder.c:2801-2807`), and mpv's subtitle update path explicitly waits for demux readiness (`player/sub.c:163-169`). Impact: subtitle presentation can follow recovered video PTS while audio remains on the older running clock, which is a visible subtitle/audio mismatch even after video visually "catches up".

**P1 (user-visible shortfall):**

- `stall_recovered` exists in stream telemetry but not in the player protocol. Observed: `StreamEngine` emits the event (`src/core/stream/StreamEngine.cpp:1104`), but `StreamPage` only consumes `stats.stalled` and not recovery metadata (`src/ui/pages/StreamPage.cpp:1900-1906`). Reference: mpv, IINA, and Stremio expose cache-pause state at the player boundary; VLC exposes buffering/PCR reset at the decoder/clock boundary. Impact: Tankoban cannot make recovery policy depend on recovery cause, elapsed stall time, or recurrence count because that information never reaches the sidecar/player resync point.

- Tankoban's sidecar "playing" event is too weak to drive resync. Observed: `main.cpp` emits empty `playing` payload and `SidecarProcess` turns it into `bufferingEnded()` only (`native_sidecar/src/main.cpp:619-626`, `src/ui/player/SidecarProcess.cpp:570-579`). Reference: VLC uses buffering completion as the moment to wait decoders, reset main clock, and set PCR (`src/input/es_out.c:1190-1253`). Impact: the current event is suitable for UI, not for A/V/sub pipeline repair.

**P2 (polish / observability):**

- Tankoban now has good buffering diagnostics, but not recovery diagnostics at the same level of fidelity. Observed: `cache_state` exposes bytes ahead, input rate, ETA, and cache duration (`native_sidecar/src/video_decoder.cpp:1345-1388`), but there is no parallel "recovered, re-anchored, flushed N decoders, new anchor=P" event. Reference: mpv exposes cache state and cache pause as public properties; VLC has explicit loggable clock-reset stages. Impact: debugging future regressions will stay guess-heavy unless a recovery path is added and instrumented.

## Hypothesized root causes (Agent 3 / Agent 4 to validate)

- **Hypothesis -** Tankoban currently has no recovery-time boundary where audio, video, and subtitle state are rejoined. Stream recovery stops at `stats.stalled=false` and sidecar `playing`, so the old audio master clock survives the stall while video repairs itself by frame drop and subtitles keep rendering against current video PTS. **Agent 3 to validate.**

- **Hypothesis -** Short-stall and long-stall recoveries are being treated identically even though the codebase now has evidence for multi-recovery cascades of 12-22 s each. A single boolean `stalled` flag is too little state to choose between mpv-style cache pause and VLC-style hard reset. **Agent 3 and Agent 4 to validate.**

- **Hypothesis -** SubtitleRenderer is correct for steady-state playback but underpowered for recovery because its timing is driven by packet PTS ingestion plus render-at-video-PTS, with no explicit flush/replay boundary on network starvation recovery. **Agent 3 to validate.**

## Concrete fix options (advisory, cross-referenced)

1. **Option A - Adopt mpv/IINA/Stremio-style cache-pause semantics as the default recovery path.**
   Trigger: first confirmed decoder-level starvation (`av_read_frame` retry branch or stream-engine stall promoted to sidecar).
   Action: freeze the authoritative playback clock, stop emitting advancing `time_update`, and resume only once buffered duration crosses a recovery threshold.
   Attribution: mpv `paused-for-cache` / `cache-buffering-state` (`player/playloop.c:739-781`, manual `paused-for-cache`), mirrored by IINA (`PlayerCore.swift:2678-2685`) and Stremio shell (`ShellVideo.js:75-77`, `173-185`).

2. **Option B - Add a VLC-style hard resync path for long stalls or discontinuities.**
   Trigger: elapsed stall time above a threshold, repeated stall cascade inside a short window, or recovered PTS/PCR jump above a drift threshold.
   Action: flush audio/video/subtitle decoders, wait until decoders are ready, reset/rebase the master clock, then restart output from a fresh recovered anchor.
   Attribution: VLC buffering completion and PCR reset path (`src/input/es_out.c:1190-1253`, `3699-3712`, `src/input/decoder.c:2758-2836`, `src/clock/input_clock.c:246-277`).

3. **Option C - Promote `stall_recovered` from telemetry into the player protocol and make recovery policy explicit.**
   Trigger: stream-engine `stall_detected` / `stall_recovered` plus elapsed duration and piece metadata.
   Action: sidecar/player receives a structured recovery event and chooses soft resume vs hard reset based on elapsed stall, recurrence count, and current clock drift.
   Attribution: Tankoban already has the stream-layer telemetry (`src/core/stream/StreamEngine.cpp:1055-1104`) but currently collapses the player view to a boolean stall flag (`src/ui/pages/StreamPage.cpp:1900-1906`).

4. **Option D - Make subtitles participate in recovery instead of passively following recovered video PTS.**
   Trigger: any recovery path that qualifies as "long stall" or "clock discontinuity".
   Action: clear subtitle renderer state and replay from the recovered anchor, or drive subtitle render time from the same recovered master time used to restart audio/video.
   Attribution: VLC flushes the SPU path with the decoder flush (`src/input/decoder.c:2801-2807`); mpv subtitle update explicitly waits for demux readiness (`player/sub.c:163-169`).

5. **Option E - Use a tiered hybrid: mpv-style pause first, VLC-style reset only when drift proves it is needed.**
   Trigger: ordinary short stalls use cache pause; repeated or severe stalls escalate once measured A/V drift or recovery latency crosses a threshold.
   Action: keep the common case cheap and invisible, while still having a deterministic "full repair" path for 15-30 s starvation windows.
   Attribution: mpv shows the soft-path pattern; VLC shows the hard-path pattern. Tankoban already has the telemetry needed to distinguish the two classes, but not the player-side state machine yet.

## Recommended follow-ups (advisory)

- Verify whether the desired product behavior is "freeze all three timelines on starvation" (mpv family) or "resume aggressively, then hard-reset only on drift/discontinuity" (VLC family). The code can support either, but the product target should be explicit before implementation.
- Reproduce the wake-2 case with one added measurement: log the audio clock, current video PTS, and subtitle render PTS at every `stall_detected` and `stall_recovered` boundary. That will quickly separate "clock never paused" from "renderer replay lag" from "decoder flush missing".
- Investigate whether `AVSyncClock::update()` should treat long read-starvation recovery as a re-anchor-worthy event, not only seeks and unexpected forward audio jumps.
- Investigate whether subtitle recovery should be driven by a shared "recovered master PTS" rather than by continuing to render against whichever video PTS the drop loop has reached.
