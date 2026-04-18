# Audit — Congress 6 Slice C + Slice D: Player state machine + Sidecar lifecycle/IPC (Slice C, primary) + Library UX — Continue Watching + next-episode detection + library→player handoff (Slice D, collapsed appendix) — 2026-04-18

By Agent 3 (Video Player). For Congress 6 (multi-agent Stremio Reference audit) gating STREAM_ENGINE_REBUILD P2/P3/P4.

**Slice D collapse note:** Per Congress 6 addendum COLLAPSE RULE, Slice D (Library UX) answers all 3 of its locked questions in ~20 min of first-pass reading by leveraging Slice C's already-mapped `next_video_update` + `Action::Load` traces. Slice D appended as §"Appendix (Slice D)" at the tail of this file — no separate `congress6_library_ux_2026-04-18.md` authored. Collapse decision rationale posted at chat.md pre-write commitment; Assistant 2 adversarial pass will verify collapse was honest not lazy.

Scope: Player state machine + Sidecar lifecycle events + IPC surface, observed against Stremio Reference. Observation-grade per Trigger C (loosened for Claude-in-audit-mode). Dual file:line citations. No Tankoban build/run. No fix prescription in this file — trivial in-situ fix notes, if any, would land in a separate post-audit commit; none identified this wake.

**Reference anchors (Stremio):**
- `stremio-core-development/stremio-core-development/src/models/player.rs` (1676 lines)
- `stremio-core-development/stremio-core-development/src/runtime/msg/event.rs` (162 lines)
- `stremio-core-development/stremio-core-development/src/runtime/runtime.rs`
- `stremio-core-development/stremio-core-development/src/models/common/eq_update.rs`
- `stremio-core-development/stremio-core-development/src/types/streams/streams_item.rs`
- `stremio-core-development/stremio-core-development/stremio-core-web/src/model/serialize_player.rs` (394 lines)
- `stremio-core-development/stremio-core-development/stremio-core-web/src/model/model.rs`
- `stremio-video-master/stremio-video-master/src/StremioVideo/StremioVideo.js` (113 lines)
- `stremio-video-master/stremio-video-master/src/HTMLVideo/HTMLVideo.js` (704 lines)

**Tankoban anchors:**
- [src/ui/pages/stream/StreamPlayerController.h](../../src/ui/pages/stream/StreamPlayerController.h) (174) + [StreamPlayerController.cpp](../../src/ui/pages/stream/StreamPlayerController.cpp) (284)
- [src/ui/player/LoadingOverlay.h](../../src/ui/player/LoadingOverlay.h) (97) + [LoadingOverlay.cpp](../../src/ui/player/LoadingOverlay.cpp) (168)
- [src/ui/player/SidecarProcess.h](../../src/ui/player/SidecarProcess.h) (291)
- [src/ui/player/VideoPlayer.cpp](../../src/ui/player/VideoPlayer.cpp) (3443)
- [native_sidecar/src/main.cpp](../../native_sidecar/src/main.cpp) (1511)
- [src/core/stream/StreamEngine.h](../../src/core/stream/StreamEngine.h) (320)

**R21 mtime spot-check 2026-04-18 audit session:** stremio-core-development 04-14 16:59, stremio-video-master 04-14 16:58 — both match motion-authoring snapshot. No drift to flag.

**Prior-art treatment:** [agents/audits/player_stremio_mpv_parity_2026-04-17.md](player_stremio_mpv_parity_2026-04-17.md) read as input. Re-derived this audit fresh against the 3-question sheet. Prior-art conflated `stream_state` (user prefs) with `streaming_server` model state (torrent stats); this audit separates them. Prior-art audit supersedes to `agents/audits/_superseded/` on Agent 0 integration close per Congress 6 handoff contract.

---

## Question 1 — Probe → Play Flow

> Trace `Action::Load` at `stremio-core/src/models/player.rs:140` through to the first `PlayerPlaying` event emit at `stremio-core/src/runtime/msg/event.rs:17`. What state transitions + side-effects happen, where is the sidecar-probe-equivalent triggered? Does Stremio's LoadingOverlay-equivalent have a state machine we should match?

### Observed (Stremio Reference)

**Load does NOT emit PlayerPlaying.** Action::Load at [player.rs:140-316](stremio-core/models/player.rs) is state-model setup only. The sequence:

1. **Pre-reset phase** (line 142-163):
   - If a previous Selected existed, emit `Event::TraktPaused` with the stale analytics context (mid-session replacement signal).
   - `item_state_update` at [player.rs:941](stremio-core/models/player.rs) advances the library_item state only if time_offset crossed `CREDITS_THRESHOLD_COEF` of duration AND a next_video exists.

2. **Assignment phase** (line 164-211):
   - `eq_update(&mut self.selected, Some(new_selected))` — line 164.
   - `meta_item` setup via `resource_update::<E>` with `ResourceAction::ResourceRequested` — line 165-189. Creates a pending `ResourceLoadable<MetaItem>` + returns Effects containing the async HTTP fetch Future.
   - `eq_update(&mut self.stream_state, None)` — line 190. Stream state (user prefs) cleared on every Load; re-populated by `Msg::Internal(Internal::StreamsChanged(_))` → `stream_state_update` at [player.rs:967](stremio-core/models/player.rs) which reads `StreamItemState` from the `StreamsBucket` keyed by `{meta_id, video_id}`.
   - `stream_update`, `subtitles_update`, `next_video_update`, `next_streams_update`, `next_stream_update` — lines 193-218. Each returns Effects containing HTTP Futures for addon fetches.
   - `update_streams_effects` — line 221-231. Emits `Internal::StreamLoaded` only when both Selected AND meta_item are materialized; gates StreamsBucket mutation.

3. **Analytics + lifecycle-flag phase** (line 269-296):
   - Populates `analytics_context` from library_item fields (id, type, name, video_id, time, duration, trakt).
   - `self.load_time = Some(E::now())` — line 293. **Wall-clock anchor for PlayerPlaying's load_time measurement.**
   - `self.loaded = false; self.ended = false; self.paused = None;` — lines 294-296. **These three are the Stremio-core equivalent of our "open is in flight" flag set.**

4. **Effect fan-out** (line 297-316): All generated Effects (18 joined) returned to Runtime, which schedules the async Futures. None of these effects trigger PlayerPlaying synchronously.

**No sidecar-probe-equivalent lives in stremio-core.** stremio-core is a pure state reducer — it orchestrates addon HTTP fetches (meta, subtitles, stream URLs) and mutates persisted state. Byte-level streaming (probe, transmux, range serving) is OUT of stremio-core's scope; it lives in `stremio-stream-server` (Node.js — libtorrent-sys FFI per Congress 5 R11 reframe) which exposes an HTTP URL. Stremio-core just hands the URL to the consumer.

**PlayerPlaying is emitted by the CONSUMER → CORE feedback loop, not by Load.** At [player.rs:613-647](stremio-core/models/player.rs), `Msg::Action(Action::Player(ActionPlayer::PausedChanged{paused}))`:
- `self.paused = Some(*paused);` — line 616.
- **First-time gate**: `if !self.loaded { self.loaded = true; Effects::msg(Msg::Event(Event::PlayerPlaying{ load_time: now - self.load_time, context: analytics_context })) }` — line 617-628. This is the ONLY emit site for `PlayerPlaying` at [runtime/msg/event.rs:17-20](stremio-core/runtime/msg/event.rs).
- Subsequent `PausedChanged` emits `TraktPaused` (pause→true) or `TraktPlaying` (pause→false) — lines 629-639.

**So the full Load → PlayerPlaying chain is a 4-hop dance:**

1. UI dispatches `Action::Load(ActionLoad::Player(selected))` → [player.rs:140](stremio-core/models/player.rs) → state reset + async addon fetches + `self.load_time = now`, `self.loaded = false`. No PlayerPlaying.
2. Consumer (e.g. stremio-web's JS shell) also dispatches `StremioVideo` command `{type: 'command', commandName: 'load', commandArgs: {stream, autoplay: true, time}}` → [StremioVideo.js:28](stremio-video-master/StremioVideo/StremioVideo.js) → `selectVideoImplementation` → `new HTMLVideo(options)` → `video.dispatch(action)` → [HTMLVideo.js command('load') at :527-582](stremio-video-master/HTMLVideo/HTMLVideo.js). **Calls `command('unload')` first (full reset — pattern worth noting: destruction precedes construction).** Then sets `stream`, emits one synchronous burst of `onPropChanged('stream'/'loaded'/'paused'/'time'/'duration'/'buffering'/'buffered')` with pre-metadata values. Then async `getContentType(stream)` → HLS or direct `videoElement.src = stream.url`.
3. Browser's `<video>` element drives network + decode. `videoElement.onloadedmetadata` → `onPropChanged('loaded')` → HTMLVideo emits `propChanged('loaded', true)` — [HTMLVideo.js:77-79](stremio-video-master/HTMLVideo/HTMLVideo.js). Then `onplaying` → `onPropChanged('paused')` with `videoElement.paused == false`.
4. Consumer shell (stremio-web) observes `propChanged('paused', false)` and dispatches `Action::Player(ActionPlayer::PausedChanged{paused: false})` back to stremio-core → [player.rs:613](stremio-core/models/player.rs) → `!self.loaded` gate passes → `Event::PlayerPlaying { load_time: now - self.load_time }` emitted.

**`self.loaded` (stremio-core) vs `'loaded'` (HTMLVideo) are DIFFERENT BOOLEANS.** Stremio-core's `self.loaded` is "we've observed at least one PausedChanged since Load" — effectively "actually playing". HTMLVideo's `'loaded'` prop is `videoElement.readyState >= HAVE_METADATA` — metadata parsed. These semantics are often conflated in casual reading of the source; they're not the same signal.

### Observed (Tankoban)

**Our probe IS session-internal to the sidecar, not addon-fetched.** The flow for a magnet-backed stream:

1. `StreamPlayerController::startStream(imdbId, mediaType, s, e, selectedStream)` at [StreamPlayerController.cpp:33](../../src/ui/pages/stream/StreamPlayerController.cpp) →
   - Defensive `stopStream(StopReason::Replacement)` — line 40 (STREAM_LIFECYCLE_FIX Phase 2 Batch 2.1).
   - Magnet kind: starts `pollStreamStatus` loop at 300ms (first 100 ticks) → 1000ms. Polls `m_engine->streamFile(m_selectedStream)` which returns `StreamFileResult{ok, readyToStart, url, errorCode, fileProgress, downloadedBytes, fileSize, ...}` — [StreamEngine.h:72-86](../../src/core/stream/StreamEngine.h).
   - Each poll emits `bufferUpdate(statusText, percent)` — [StreamPlayerController.cpp:238](../../src/ui/pages/stream/StreamPlayerController.cpp). Status vocabulary: `"Resolving metadata..."`, `"Buffering... N% (X.X MB)"`, `"Connecting..."`, `"Metadata stalled. Torrent may be dead."` — [StreamPlayerController.cpp:183-232](../../src/ui/pages/stream/StreamPlayerController.cpp).
   - On `result.ok && result.readyToStart`: stops poll, calls `onStreamReady(url)` → emits `readyToPlay(url)` — [StreamPlayerController.cpp:281-284](../../src/ui/pages/stream/StreamPlayerController.cpp).

2. **Probe phase runs in the sidecar, post-`readyToPlay`.** `VideoPlayer::openFile(url)` is invoked by StreamPage on `readyToPlay`. `VideoPlayer` calls `SidecarProcess::sendOpen(path, startSec)` → sidecar `handle_open` emits `state_changed{opening}` at [native_sidecar/src/main.cpp:827](../../native_sidecar/src/main.cpp), then enqueues `open_worker` on a background thread.

3. **Sidecar `open_worker` emits a 6-event pipeline** between `state_changed{opening}` and `first_frame`:
   - `probe_start` at [main.cpp:293](../../native_sidecar/src/main.cpp) — wall-clock anchor captured at [main.cpp:271](../../native_sidecar/src/main.cpp).
   - `probe_done` at [main.cpp:320](../../native_sidecar/src/main.cpp) — analyze_duration_ms, stream_count, duration_ms payload.
   - `tracks_changed` at [main.cpp:407](../../native_sidecar/src/main.cpp) + `media_info` at [main.cpp:422](../../native_sidecar/src/main.cpp) — hoisted pre-first_frame by PLAYER_UX_FIX Phase 1.1 (sidecar metadata decoupling per [feedback_sidecar_metadata_decoupling](../../C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_sidecar_metadata_decoupling.md)).
   - `decoder_open_start` at [main.cpp:785](../../native_sidecar/src/main.cpp) + `decoder_open_done` at [main.cpp:537](../../native_sidecar/src/main.cpp) (from VideoDecoder callback post-avcodec_open2).
   - `first_packet_read` at [main.cpp:556](../../native_sidecar/src/main.cpp) — demuxer / network / disk I/O flowing.
   - `first_decoder_receive` at [main.cpp:577](../../native_sidecar/src/main.cpp) — first successful `avcodec_receive_frame`. **Rule-14 design pick (per [feedback comment at SidecarProcess.h:198-202](../../src/ui/player/SidecarProcess.h)): this, NOT first_packet_read, drives the LoadingOverlay DecodingFirstFrame stage — packet-read success can stall on decoder back-pressure, receive is honest "making progress".**
   - `first_frame` at [main.cpp:493](../../native_sidecar/src/main.cpp) — actual_w/h, codec, ptsSec, shmName, slotCount, slotBytes, wall_clock_delta_from_open_ms.
   - `state_changed{playing}` at [main.cpp:515](../../native_sidecar/src/main.cpp).

4. **`LoadingOverlay` stages are driven by the classified events.** Wiring in [VideoPlayer.cpp:1472-1528](../../src/ui/player/VideoPlayer.cpp):
   - `playerOpeningStarted(QString filename)` → `showLoading(filename)` → `setStage(Opening, filename)`
   - `probeStarted` → `setStage(Probing)` at [VideoPlayer.cpp:1501-1503](../../src/ui/player/VideoPlayer.cpp)
   - `decoderOpenStarted` → `setStage(OpeningDecoder)` at [VideoPlayer.cpp:1510-1512](../../src/ui/player/VideoPlayer.cpp)
   - `firstDecoderReceive` → `setStage(DecodingFirstFrame)` at [VideoPlayer.cpp:1526-1528](../../src/ui/player/VideoPlayer.cpp)
   - `firstFrame` → `dismiss()` at [VideoPlayer.cpp:1485-1486](../../src/ui/player/VideoPlayer.cpp)
   - `bufferingStarted` → `showBuffering()` → `setStage(Buffering)` at [VideoPlayer.cpp:1477-1478](../../src/ui/player/VideoPlayer.cpp) (post-first-frame HTTP stall)
   - 30s watchdog armed on openFile, fires `setStage(TakingLonger)` at [VideoPlayer.cpp:245-247](../../src/ui/player/VideoPlayer.cpp).

### Reference

Stremio has no classified-stage LoadingOverlay equivalent. `HTMLVideo.js`'s state vocabulary exposed to the UI layer is 4 orthogonal props (`loaded`, `paused`, `buffering`, `buffered`) + `stream` — [HTMLVideo.js:107-127](stremio-video-master/HTMLVideo/HTMLVideo.js). UI (stremio-web shell) derives any "label" it wants from combinations — not prescribed by HTMLVideo.

**The closest analytical parallel:** Stremio's load_time metric at [player.rs:620-625](stremio-core/models/player.rs) measures "Action::Load until first PausedChanged{false}" in milliseconds. Our sidecar's `first_frame.wall_clock_delta_from_open_ms` at [main.cpp:492](../../native_sidecar/src/main.cpp) measures "open_worker entry to first_frame emit". These measure almost-but-not-exactly the same interval — ours starts ~1 Qt event-loop tick later (open_worker is a detached thread, handle_open is the stdin thread) and ends when the first decoded frame is READY FOR PRESENTATION (not yet on screen), whereas Stremio's ends when the browser's `<video>` element has emitted `onplaying` (frame already on screen).

### Hypothesis — Agent N to validate

- **Hypothesis — Our LoadingOverlay classified-stage state machine is a NET ADDITION over Stremio, not a port.** Stremio's UI pattern is orthogonal-props-derived, not discrete-stages. Our 6-stage enum (`Opening → Probing → OpeningDecoder → DecodingFirstFrame → Buffering → TakingLonger`) is a Tankoban native choice matched to the native sidecar's probe granularity — Stremio's HTML-video layer has no such granularity to expose. **Integration implication for STREAM_ENGINE_REBUILD:** the rebuild's "cosmic Newell" parity bar should NOT try to mirror a Stremio LoadingOverlay that doesn't exist. Our classified stages are a legitimate Tankoban native strength (diagnosability per [feedback_evidence_before_analysis](../../C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_evidence_before_analysis.md)). **Agent 0 to validate at integration memo time.**

- **Hypothesis — Our `StreamPlayerController::readyToPlay` signal is semantically equivalent to Stremio's "URL handed from stremio-core to StremioVideo".** Both are "stream is now reachable over HTTP; consumer can issue GET with Range header". Neither implies "ffmpeg/browser has probed and decoded successfully". In both architectures, the actual "playback has started" signal is downstream — our `firstFrame` emit, Stremio's `PlayerPlaying` emit (which waits for the UI-level `paused=false` propChanged feedback loop). **Agent 0 to validate the equivalence at integration memo authoring.**

- **Hypothesis — Stremio does not expose a "probing" state at the UI layer because its probe work is server-side (stremio-stream-server's ffmpeg probe over HTTP Range requests).** By the time the URL is handed to HTMLVideo, probe is either complete or fully transparent. Our probe happens in the native sidecar AFTER the stream URL is ready (because the sidecar IS our probe+decode path — we don't have a separate server-side probe). This is an architectural difference, not a gap. Our classified stages are the RIGHT shape for OUR architecture; trying to collapse them to Stremio's 4-prop shape would HIDE diagnostic signal on slow opens. **Agent 4 (stream domain) to validate there's no pre-playback probe we're missing on the stream-server side that would duplicate sidecar probe work.**

---

## Question 2 — IPC Surface

> In `stremio-core-web/src/model/serialize_player.rs`, how is `stream_state` surfaced to the consumer — eagerly serialized on every change, lazily on request, or delta-only? Tells us whether our `contiguousHaveRanges` polling cadence matches Stremio's semantic cadence.

### Observed (Stremio Reference)

**Stremio's IPC pattern is "notify-changed-fields-only + consumer pulls full serialization on notify".** The mechanism at [runtime/runtime.rs:15-124](stremio-core/runtime/runtime.rs):

1. **Dispatch**: Consumer sends `RuntimeAction{field, action}` → `Runtime::dispatch` → `model.update_field(msg, field)` returns `(effects: Vec<Effect>, fields: Vec<M::Field>)`.
2. **Field-level dirty notification**: If `fields` is non-empty, `Runtime::handle_effects` emits `RuntimeEvent::NewState(fields)` at line 83-87. **The event carries only the field ENUM discriminants (e.g. `WebModelField::Player`), NOT the serialized payload.**
3. **Lazy pull by consumer**: Consumer (JS layer) listens for `NewState(fields)` and calls `model.get_state(WebModelField::Player)` per [model.rs:188-190](stremio-core-web/model/model.rs), which invokes `serialize_player::<WebEnv>(&player, &ctx, &streaming_server)` returning the full Player shape as `JsValue`.
4. **Event fan-out (independent channel)**: `Msg::Event(event)` resolutions go through `emit(RuntimeEvent::CoreEvent(event))` at line 111-112. **This is a SEPARATE channel from field-change notifications — `Event::PlayerPlaying/Stopped/NextVideo/Ended/TraktPaused/TraktPlaying` fire as discrete events, not as property changes.**

**Equality-gate on field dirtying**: `eq_update` at [common/eq_update.rs:4-11](stremio-core/models/common/eq_update.rs) returns `Effects::none()` (changed) vs `Effects::none().unchanged()` (no-op). The `unchanged` path does NOT add the parent field to the fields-changed vector. Therefore the NewState notification fires ONLY when a field's value actually differs from its prior state.

**`stream_state` specifically**: The serializer passes `player.stream_state.as_ref()` through at [serialize_player.rs:330](stremio-core-web/model/serialize_player.rs) — a raw `Option<&StreamItemState>` reference. The `StreamItemState` at [types/streams/streams_item.rs:29-95](stremio-core/types/streams/streams_item.rs) is NOT runtime playback state — it's USER PREFERENCES persisted per stream: `subtitle_track`, `subtitle_delay`, `subtitle_size`, `subtitle_offset`, `playback_speed`, `player_type`, `audio_delay`. Mutation sites are `Action::Player(ActionPlayer::StreamStateChanged{state})` at [player.rs:417-430](stremio-core/models/player.rs) which only fires when the user changes a preference, and `Internal::StreamsChanged` at [player.rs:763-765](stremio-core/models/player.rs) which re-loads from StreamsBucket on bucket mutation.

**Full Player shape** serialized on every `get_state(Player)` call — 11 top-level fields: `selected, stream, meta_item, subtitles, next_video, series_info, library_item, stream_state, intro_outro, title, addon` — [serialize_player.rs:107-120](stremio-core-web/model/serialize_player.rs). No per-field delta; consumer gets the full object and diffs client-side if it cares.

### Observed (Tankoban)

**Our IPC pattern is "push signals + eager payload per signal".** StreamPlayerController emits per-event:
- `bufferUpdate(QString statusText, double percent)` — every poll tick (300ms → 1000ms cadence), eager on every tick whether value changed or not — [StreamPlayerController.cpp:238](../../src/ui/pages/stream/StreamPlayerController.cpp). Equality-gating is implicit through the status-text change which usually differs each tick.
- `bufferedRangesChanged(QString infoHash, QList<QPair<qint64,qint64>> ranges, qint64 fileSize)` — **equality-deduped** at [StreamPlayerController.cpp:273-274](../../src/ui/pages/stream/StreamPlayerController.cpp) against `m_lastBufferedRanges` + `m_lastBufferedFileSize`. This dedupe pattern is semantically equivalent to Stremio's `eq_update` pattern — emission suppressed when snapshot matches prior.
- `readyToPlay(QString httpUrl)` — one-shot; emit-once-per-session.
- `streamFailed(QString message)` — one-shot on failure paths; always follows `stopStream(StopReason::Failure)` → `streamStopped(Failure)`.
- `streamStopped(StopReason reason)` — one per session terminus, 3-value enum (UserEnd, Replacement, Failure) — [StreamPlayerController.h:41-46](../../src/ui/pages/stream/StreamPlayerController.h).

SidecarProcess emits per-event — [SidecarProcess.h:146-238](../../src/ui/player/SidecarProcess.h):
- Per-frame / per-tick value updates: `timeUpdate(posSec, durSec)`, `subtitleText(text)`, `filtersChanged(state)` — fire at native cadence (e.g. ~250ms for timeUpdate, per decoder frame for subtitleText).
- Lifecycle events: `stateChanged(state)`, `firstFrame(payload)`, `bufferingStarted/Ended`, `endOfFile`, `errorOccurred`, `processClosed`, `processCrashed`.
- Classified probe events: `probeStarted/Done`, `decoderOpenStarted/Done`, `firstPacketRead`, `firstDecoderReceive` — one-shot per session, pre-first-frame.
- Tracks/media metadata: `tracksChanged(audio, subtitle, activeAudio, activeSub)`, `mediaInfo(info)` — post-probe, pre-first-frame.

**We emit eagerly and per-payload; consumers dedupe at the sink** (e.g. LoadingOverlay's `if (m_visible && m_opacity >= 0.98) update();` in-place text swap at [LoadingOverlay.cpp:36-45](../../src/ui/player/LoadingOverlay.cpp), or SeekSlider's repaint gating). The `bufferedRangesChanged` signal is the exception — it dedupes upstream at the emit site, like Stremio's field-level pattern.

### Reference

Prior-art audit at [player_stremio_mpv_parity_2026-04-17.md:11](player_stremio_mpv_parity_2026-04-17.md) noted that our IPC is "a fixed event/signal set" vs Stremio's `propValue`/`propChanged` observable property graph. That characterization is TRUE for HTMLVideo.js (property model per [StremioVideo.js:54-59](stremio-video-master/StremioVideo/StremioVideo.js)) but MISREPRESENTS stremio-core (which uses the field-dirty + full-serialization pattern, not per-property observability). **Stremio has two distinct IPC shapes** — stremio-core ↔ web (field-level dirty + full serialize) and stremio-video ↔ shell (property-level observe + per-prop changed). Our sidecar more closely resembles stremio-video's property model (per-event-kind signal with discrete payload); our StreamPlayerController more closely resembles a trimmed stremio-core (one-signal-per-domain-event with eager payload, no intermediate serialization step because Qt signals transport typed values directly).

### Hypothesis — Agent N to validate

- **Hypothesis — Our `contiguousHaveRanges` cadence IS roughly aligned with Stremio's semantic cadence, but via different mechanism.** Stremio's `stream_state` field dirties only when user changes a preference or the StreamsBucket mutates. Our `bufferedRangesChanged` signal dirties only when the actual byte-range snapshot changes (equality-deduped at emit site). Both achieve "notify-when-changed" semantics. Where we DIFFER: our poll-driven cadence at 300ms (fast) → 1000ms (slow) means we sample `StreamEngine::contiguousHaveRanges(hash)` at that interval regardless of whether new pieces completed. If piece completion rate is much faster than 1000ms, we under-sample. STREAM_ENGINE_REBUILD Phase 5 (stall detection) + HELP.md's requested `pieceFinished` signal would let us move to push-driven (emit on actual piece completion, not on poll tick), eliminating both over-sampling (repeated identical snapshots) and under-sampling (missed mid-interval changes). **Agent 4 to validate this informs P5 stall-detection design — if pieceFinished is the right push trigger, is `bufferedRangesChanged` the right downstream fan-out site?**

- **Hypothesis — Stremio's `stream_state` has NO byte-level/buffered-ranges component; our `bufferedRangesChanged` has no semantic equivalent in stremio-core's Player model.** The byte-level "how much is downloaded + seekable" lives in `streaming_server` model's torrent statistics (NOT `stream_state`), and is surfaced via a SEPARATE `WebModelField::StreamingServer` dirty notification + [serialize_streaming_server()](stremio-core-web/model/). UI clients that show a buffered-range overlay on the seek slider pull it from streaming_server's stats, not from Player. **Our single-signal architecture (bufferedRangesChanged ON StreamPlayerController, which is the player controller) collapses these two Stremio fields into one emit point.** This is a deliberate architectural choice — our player-side controller owns both the HTTP-readiness concern AND the buffered-ranges concern for its own session. It is NOT a gap. Prior-art audit P0 item 1 ("Stream-mode buffered/seekable state is not surfaced to the Qt player") was partially addressed by PLAYER_STREMIO_PARITY Phase 1 (`bufferedRangesChanged` + SeekSlider gray-bar shipped at `c510a3c`). **Agent 5 (library UX / consumer) to validate nothing downstream expects byte-ranges to come from a different signal source.**

- **Hypothesis — Stremio's full-serialize-on-dirty pattern is cheaper than it looks because of Rust/wasm_bindgen FFI specifics; directly porting the pattern to Qt C++ would be worse than our per-signal approach.** Stremio's `serialize_player` walks all 11 fields and constructs a ~300-field JsValue every call. In the Rust → wasm_bindgen boundary, this is optimized via borrow-semantics — most fields are `&ref` references (no deep copies). In Qt C++, emitting a QJsonObject with equivalent depth per signal would allocate + deep-copy. Our signal-per-domain-event pattern is the idiomatic Qt shape and avoids that overhead. **No action item; noted for integration memo context.**

---

## Question 3 — State Classification

> In `StremioVideo/StremioVideo.js` + `HTMLVideo.js`, how does the consumer distinguish mid-probe vs paused-for-cache vs playing? Three discrete states or a continuum? Informs our classified LoadingOverlay.

### Observed (Stremio Reference)

**Stremio's consumer-facing state is a CONTINUUM of 4 orthogonal boolean/value props**, not a discrete enum. Per [HTMLVideo.js:107-127](stremio-video-master/HTMLVideo/HTMLVideo.js) `observedProps` + [HTMLVideo.js:129-315](stremio-video-master/HTMLVideo/HTMLVideo.js) `getProp`:

- **`stream`** — the Stream object itself, or null. Null ≡ "no session loaded".
- **`loaded`** — `videoElement.readyState >= HAVE_METADATA`. Null if stream==null. True once moov atom / HLS master manifest parsed.
- **`paused`** — `!!videoElement.paused`. Null if stream==null.
- **`buffering`** — `videoElement.readyState < HAVE_FUTURE_DATA`. Null if stream==null. True when the decoder cannot advance (stalled / mid-seek / initial fill).
- **`buffered`** — single integer in ms: the END of the contiguous buffered range around currentTime — [HTMLVideo.js:169-182](stremio-video-master/HTMLVideo/HTMLVideo.js). Returns currentTime if no range contains currentTime. **This is the semantic closest to our `contiguousHaveRanges` but at MS granularity (playable time), not BYTES; a single scalar, not a range list.**

**State classification is DERIVED BY THE UI LAYER** from prop combinations (the shell, not HTMLVideo):
| stream | loaded | paused | buffering | UI label (canonical) |
|--------|--------|--------|-----------|----------------------|
| null   | null   | null   | null      | (unloaded — no overlay) |
| set    | false  | any    | true      | "Loading / Opening" (pre-metadata) |
| set    | true   | true   | false     | "Paused" (user-paused with playable buffer) |
| set    | true   | true   | true      | "Paused (buffering)" (rare — seek in paused state) |
| set    | true   | false  | true      | "Buffering" (post-first-frame stall) |
| set    | true   | false  | false     | "Playing" (fall-through) |

**This table is not encoded in HTMLVideo.** HTMLVideo emits the raw props; the shell builds the table. In the stremio-web JS layer (not in-scope for our audit), there's a React component that does this derivation — the point is that classification lives in UI code, not in the video abstraction.

**Event transitions feeding prop emits** — [HTMLVideo.js:27-98](stremio-video-master/HTMLVideo/HTMLVideo.js):
- `onpause` / `onplay` → emits `paused` change.
- `ontimeupdate` → emits `time` + `buffered` (fires every ~250ms during playback).
- `onwaiting` / `onseeking` / `onstalled` → emits `buffering=true` + `buffered`.
- `oncanplay` / `onplaying` / `onloadeddata` / `onseeked` → emits `buffering=false` + `buffered`.
- `onloadedmetadata` → emits `loaded`.
- `onended` → emits `'ended'` event separately (not a prop).

**No equivalent of "probing" stage.** The browser's `<video>` element treats probe as atomic ("am I ready to render at least one frame or not?") — `readyState` transitions `HAVE_NOTHING → HAVE_METADATA → HAVE_CURRENT_DATA → HAVE_FUTURE_DATA → HAVE_ENOUGH_DATA` but HTMLVideo only exposes two cutoffs: `HAVE_METADATA` (→ `loaded`) and `HAVE_FUTURE_DATA` (→ !`buffering`). The in-between states are opaque.

### Observed (Tankoban)

**Our LoadingOverlay has 6 DISCRETE stages** — [LoadingOverlay.h:45-52](../../src/ui/player/LoadingOverlay.h):
- `Opening` — post-sendOpen ack (state_changed{opening})
- `Probing` — probe_file in flight (probe_start)
- `OpeningDecoder` — avcodec_open2 in flight (decoder_open_start)
- `DecodingFirstFrame` — first avcodec_receive_frame returned (first_decoder_receive)
- `Buffering` — HTTP stall post-first-frame (sidecar `buffering` event)
- `TakingLonger` — 30s watchdog fired without first_frame

**Stages are NOT orthogonal** — they're a linear progression with two branches (TakingLonger is a timeout from any pre-first-frame stage; Buffering is post-first-frame only). `setStage(stage, filename)` mutates in place without re-fade if already visible — [LoadingOverlay.cpp:36-45](../../src/ui/player/LoadingOverlay.cpp). `dismiss()` fades out on firstFrame or bufferingEnded or playerIdle.

**Separately**, our tracks + paused state flow through the same IPC:
- `paused` state: sidecar emits `state_changed{paused}` / `state_changed{playing}` — [main.cpp:890,907,920](../../native_sidecar/src/main.cpp). These are direct user-intent signals (pause/resume commands), not buffer-stall signals.
- Buffering state: `bufferingStarted/Ended` from sidecar HTTP stall retry path at [main.cpp:588,597](../../native_sidecar/src/main.cpp).

**So our architecture has TWO classification layers:**
1. Pre-first-frame pipeline: 4 discrete stages (Opening/Probing/OpeningDecoder/DecodingFirstFrame) + TakingLonger timeout fallback — driven by session-scoped one-shot sidecar events.
2. Post-first-frame runtime: 3 discrete stages (Playing / Buffering / Paused) — driven by sidecar state_changed + bufferingStarted/Ended events.

The StreamPlayerController's `bufferUpdate(statusText, percent)` is a THIRD, upstream classification layer — active only during the startup-poll phase BEFORE sendOpen fires. Vocabulary at [StreamPlayerController.cpp:191-232](../../src/ui/pages/stream/StreamPlayerController.cpp): "Resolving metadata...", "Buffering... N% (X MB)", "Metadata stalled. Torrent may be dead.", "Connecting...". This classification is embedded in the status text (not a discrete stage enum) — the consumer parses or displays the string verbatim.

### Reference

Stremio's architecture has ~2.5 classification layers:
1. HTMLVideo orthogonal props (loaded / paused / buffering / buffered / stream).
2. UI-side derived state table (implicit in shell).
2.5. streaming_server model separately surfaces torrent/stream-session state (Torrent peers, dlSpeed, etc.) via its own `WebModelField::StreamingServer` dirty channel — this is the CLOSEST analogue to our `StreamPlayerController::bufferUpdate` pre-playback status text. Neither is Player-scoped.

### Hypothesis — Agent N to validate

- **Hypothesis — Our 6-stage discrete pre-first-frame classification is a DELIBERATE departure from Stremio, justified by our sidecar having native ffmpeg probe granularity Stremio's browser-based HTMLVideo cannot expose.** Stremio's browser can't distinguish "probe in flight" from "decoder opening" from "first packet read" — they're all pre-`HAVE_METADATA`. Our sidecar emits explicit events for each; letting them drive distinct UI labels leverages a real architectural advantage. Collapsing to Stremio's `loaded`/`buffering` cutoffs would hide diagnostic signal useful for slow-open triage per [feedback_evidence_before_analysis](../../C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_evidence_before_analysis.md). **This is a PARITY-PLUS, not a parity gap.** Integration memo should explicitly mark this as intentional, not a gap to close. **Agent 0 to validate at integration memo authoring.**

- **Hypothesis — Post-first-frame, our `Buffering` (HTTP stall) stage + `state_changed{paused}` are the direct analogues to Stremio's `buffering`+`paused` orthogonal props, and ARE semantically aligned.** Our emit sites at sidecar [main.cpp:588/597](../../native_sidecar/src/main.cpp) fire from the same type of conditions as Stremio's `onwaiting`/`oncanplay` (HTML5 video network/decode stall events). Where we differ: Stremio emits at the BROWSER level for any-reason stall (network OR decode back-pressure); our `buffering` is emitted specifically from the HTTP-read retry loop at [native_sidecar/src/video_decoder.cpp:1077-1123](../../native_sidecar/src/video_decoder.cpp) (per prior-art audit) — narrower trigger conditions. **This is a minor surface narrowing, not a gap — decoder back-pressure stalls on local-file playback don't benefit from a "Buffering" indicator, and they'd be misleading (the UI would blame the stream when the issue is decode perf).** Validation: Agent 4 to confirm STREAM_ENGINE_REBUILD P5 stall detection mechanism (sidecar-side only, driven by av_read_frame stall, not by piece-waiter starvation — the piece-waiter should never stall the sidecar's HTTP read under correct prioritization).

- **Hypothesis — The `contiguousHaveRanges` + SeekSlider gray-bar pattern we shipped in PLAYER_STREMIO_PARITY Phase 1 is Tankoban-native; it has no direct Stremio analogue.** Stremio's `buffered` prop is a single scalar (end-of-contiguous-buffer at currentTime in ms) — doesn't support the multi-range gray-bar shape we paint. mpv has `demuxer-cache-state.seekable-ranges` which is a range list (prior-art audit noted this). Our shape is closer to mpv's than to Stremio's. **This is a legitimate Tankoban architectural strength over Stremio's HTMLVideo layer; should be preserved through STREAM_ENGINE_REBUILD.** The rebuild's P5 push-driven update on pieceFinished (assuming HELP.md lands) would make this MORE responsive than a poll-driven mpv cache-state reader. **Agent 4B to validate that `contiguousHaveRanges(hash)` stays on the frozen API surface (per Congress 5 API-freeze commitment) and can continue to be driven off pieceFinished in post-P5 architecture.**

---

## Integration memo feeders (raw material only — Agent 0 authors actual memo)

Cross-slice concerns to surface when Agent 0 authors the Congress 6 integration memo:

- **For Slice A (Agent 4, stream primary):** The `readyToPlay(url)` → `openFile(url)` handoff at StreamPage is the equivalent of Stremio's `selected.stream.url` → StremioVideo `command('load')` transition. Our handoff is Qt signal-based; Stremio's is property-set on a wrapper class. Both fire after the HTTP URL is reachable but before ffmpeg/browser has probed. STREAM_ENGINE_REBUILD P2 (piece-waiter async) changes the cadence at which `streamFile(.).readyToStart` goes true, which changes when `readyToPlay` fires — lifecycle invariant: readyToPlay must fire exactly once per session, post-replacement defensive stop at startStream's entry. [StreamPlayerController.cpp:39-40](../../src/ui/pages/stream/StreamPlayerController.cpp) + [StreamPlayerController.cpp:177-181](../../src/ui/pages/stream/StreamPlayerController.cpp).

- **For Slice B (Agent 4, sources/torrent):** The `pieceFinished` signal Agent 4B committed to in HELP.md is the push-trigger for replacing our poll-driven `bufferedRangesChanged` emit cadence. Downstream fan-out boundary today is `StreamPlayerController::pollBufferedRangesOnce()` — a single-method, pull-driven contract. Converting to push preserves the public signal signature; only the internal trigger changes. **API-freeze compatible.** [StreamPlayerController.h:78-93](../../src/ui/pages/stream/StreamPlayerController.h).

- **For Slice D (Agent 3 next wake — Library UX):** Our `StreamProgress::schemaVersion=1` hardening landed at `ad2bc65` (PLAYER_STREMIO_PARITY Phase 0 Batch 0.1 per CLAUDE.md dashboard). Library-side Continue Watching reads `time_offset`/`duration` ms integers directly off the saved payload — same shape as Stremio's `library_item.state.time_offset`/`duration` at [player.rs:276-279](stremio-core/models/player.rs). Next-episode detection logic in Stremio is at `next_video_update` at [player.rs:992-1045](stremio-core/models/player.rs): iterate `meta_item.videos`, find current video, take next index, filter by `series_info.season != 0 || current.season == next.season`. Our equivalent (to be discovered in Slice D). Known collapse criterion: if all 3 Slice D questions answer in <30 min of reading (mostly by pointing to already-covered Slice C and Agent 5's library surface), Slice D becomes an appendix here. **Decision deferred to Slice D session per Congress 6 handoff contract.**

- **For Prior-Art Audit supersession:** [agents/audits/player_stremio_mpv_parity_2026-04-17.md](player_stremio_mpv_parity_2026-04-17.md) has 3 P0 / 5 P1 / 3 P2 items. On integration close, this audit (Slice C) supersedes its coverage of the IPC surface + state classification + probe→play flow. **P0 item 1 ("Stream-mode buffered/seekable state is not surfaced") is PARTIALLY SHIPPED at `c510a3c` — the `bufferedRangesChanged` + SeekSlider gray-bar is live; mpv-style cache-buffering-state / paused-for-cache semantics are NOT.** **P0 item 3 ("fixed event surface vs queryable property graph")** is architecturally re-framed by this audit as 2-shape IPC (stremio-core field-dirty + stremio-video property-observe); our shape aligns with stremio-video, not with stremio-core, and that alignment is fine. **Neither P0 finding should remain open post-integration** — P0-1 partially closed by Phase 1 ship, P0-3 re-framed as non-gap.

- **R21 mtime check passed** — stremio-core-development + stremio-video-master snapshots unchanged from motion authoring. No drift to flag for Agent 0 at integration close.

---

## Summary

**Q1 — Probe → Play flow:** `Action::Load` is setup-only; PlayerPlaying fires 4 hops later via consumer→core PausedChanged feedback after HTMLVideo's `onplaying` DOM event. No in-core probe stage. Our sidecar's classified 6-event pipeline (probe_start → first_frame) is a Tankoban native strength, not a Stremio port.

**Q2 — IPC surface:** Stremio stremio-core uses field-dirty-notify + consumer-pulls-full-serialization; stremio-video uses per-property observe + per-change emit. Our Qt signals align with the stremio-video shape. `stream_state` in Stremio is USER PREFERENCES (subtitle track / delay / offset / playback speed / player type), not runtime playback state — prior-art audit conflated with streaming_server torrent-stats state. Our `bufferedRangesChanged` equality-dedupe IS semantically equivalent to Stremio's field-level eq_update gate.

**Q3 — State classification:** Stremio's consumer-facing model is a 4-prop CONTINUUM (stream / loaded / paused / buffering), not discrete stages. Classification happens in UI shell code, not in HTMLVideo. Our 6-stage LoadingOverlay is a PARITY-PLUS driven by the native sidecar's superior probe granularity; it should be preserved, not collapsed to Stremio's cutoffs. Post-first-frame, our `Buffering`/`state_changed{paused}` do align semantically with Stremio's `buffering`/`paused` props.

**Integration implication:** STREAM_ENGINE_REBUILD P2/P3/P4 can land without pretending to mirror a Stremio LoadingOverlay that doesn't exist. Preserve:
- `readyToPlay` one-shot-per-session lifecycle invariant.
- `bufferedRangesChanged` equality-dedupe at emit site (moves to push-driven on pieceFinished per HELP.md ACK; API-freeze compatible).
- LoadingOverlay 6-stage discrete vocabulary (intentional departure from Stremio).
- 3-layer bufferUpdate status-text → sidecar probe stages → post-first-frame buffering separation (intentional architecture, not a gap to unify).

---

---

# Appendix (Slice D) — Library UX: Continue Watching + next-episode detection + library→player handoff — 2026-04-18

**Scope:** Library-UX surface immediately adjacent to Slice C (Player). Congress 6 locked 3 questions (Q1-D / Q2-D / Q3-D). **Collapse to appendix exercised per Congress 6 escape hatch** — each question leverages Slice C's already-mapped `next_video_update` + `Action::Load` traces + deep_links module; no separate audit file warranted. Assistant 2 verifies collapse honesty.

**R21 mtime spot-check at appendix-write time:** stremio-core-development 04-14 16:59 unchanged from motion snapshot. No drift.

**Stremio anchors for Slice D:**
- `stremio-core-development/stremio-core-development/src/models/continue_watching_preview.rs` (125 lines)
- `stremio-core-development/stremio-core-development/src/types/library/library_item.rs` (lines 44-110)
- `stremio-core-development/stremio-core-development/src/deep_links/mod.rs` (6 `"stremio:///player/..."` emit sites at :281, :367, :423, :476, :514, :542, :581)
- `stremio-core-development/stremio-core-development/src/models/player.rs` (anchors already mapped in Slice C body — Action::Load at :140, next_video_update at :992-1045, next_stream_update at :1095-1115)
- `stremio-core-development/stremio-core-development/src/types/resource/stream.rs` (binge_group at :998, is_binge_match at :141-143)

**Tankoban anchors for Slice D:**
- [src/core/stream/StreamProgress.h](../../src/core/stream/StreamProgress.h) (203 lines, entirely inline header — namespaces StreamProgress + StreamChoices)
- [src/ui/pages/stream/StreamContinueStrip.cpp](../../src/ui/pages/stream/StreamContinueStrip.cpp) (348 lines)
- [src/ui/pages/StreamPage.cpp](../../src/ui/pages/StreamPage.cpp) (key handoff sites)
- [src/ui/pages/stream/StreamPlayerController.{h,cpp}](../../src/ui/pages/stream/StreamPlayerController.h) (already mapped in Slice C)

---

## Question 1-D — Continue Watching computation

> How is the "continue watching" list computed — watched-percentage threshold, last-position recency, bingeGroup affinity, or a combination? Line anchors.

### Observed (Stremio Reference)

**Gate at [library_item.rs:52-56](stremio-core/types/library/library_item.rs) — 3 conjunctive predicates, NONE involving watched-percentage:**
```rust
pub fn is_in_continue_watching(&self) -> bool {
    self.r#type != "other"
        && (!self.removed || self.temp)
        && self.state.time_offset > 0
}
```
- `type != "other"` — excludes non-video library items (e.g. settings / placeholder types).
- `!removed || temp` — keeps items marked temporary even if removed flag is set (edge: during mid-library-sync state).
- `time_offset > 0` — "has the user started watching?" cutoff at literally the first millisecond of playback.

**NO watched-percentage threshold at the gate.** A video at 99% watched is still "in continue watching" as long as it's not finalized-removed. That's why `CREDITS_THRESHOLD_COEF` advancement at Slice C player.rs:547 matters — it flips `time_offset` to 0 when the user crosses the credits threshold, which DROPS the item from CW via the above gate. The gate itself is dumb; the side-effect of item_state_update at [player.rs:941-964](stremio-core/models/player.rs) (mapped in Slice C) is what evicts watched items.

**Sort + cap at [continue_watching_preview.rs:56-122](stremio-core/models/continue_watching_preview.rs):**
```rust
library.items.values()
    .filter_map(|li| if li.is_in_continue_watching() || has_notification { Some(li) } else { None })
    .sorted_by(|a, b| b_time.cmp(&a_time))  // DESC
    .take(CATALOG_PREVIEW_SIZE)
```
Where `a_time` = newest `notification.video_released` date OR `library_item.mtime` fallback.

**Inclusion rule expansion:** items with active notifications also pass the filter even if `is_in_continue_watching()` returns false. The notification carrier makes a show with an unwatched episode a CW candidate without ever having been started.

### Observed (Tankoban)

**Gate + build at [StreamContinueStrip.cpp:66-212](../../src/ui/pages/stream/StreamContinueStrip.cpp):**
- Read entire `allProgress("stream")` payload via CoreBridge at [StreamContinueStrip.cpp:79](../../src/ui/pages/stream/StreamContinueStrip.cpp).
- Per-entry gate: key prefix `"stream:"`, `positionSec >= MIN_POSITION_SEC` — [StreamContinueStrip.cpp:102-108](../../src/ui/pages/stream/StreamContinueStrip.cpp).
- Collapse multiple entries per series to `mostRecent` by IMDB id, keeping the max `updatedAt` record — [StreamContinueStrip.cpp:125-137](../../src/ui/pages/stream/StreamContinueStrip.cpp). Series-level roll-up that Stremio does NOT do (Stremio has one LibraryItem per series; we have per-episode records).
- `StreamProgress::isFinished(state)` = `finished:true || percent >= 90.0` at [StreamProgress.h:61-66](../../src/core/stream/StreamProgress.h).
- Split: finished series → async next-unwatched-episode fetch; in-progress → direct render.
- Sort by `updatedAt` DESC; cap at `MAX_ITEMS`.

### Reference

**Notable divergences we should surface in integration memo:**

1. **Stremio keeps showing finished items until `CREDITS_THRESHOLD_COEF` advancement flips `time_offset` to 0.** We filter finished items via the 90% percentage threshold at `isFinished`, then route them through async next-unwatched fetch to display a next-episode card instead. Our shape is a UX upgrade (cleaner CW strip for finished series) but introduces the async fan-out cost Stremio doesn't pay.

2. **Stremio uses notification carrier as a secondary inclusion trigger** (new episode released → show appears in CW even if never started). Tankoban has no analogue in the CW strip code path — MetaAggregator fetches series meta on-demand, not as a notification pull. If we ever add "new episode" notifications, the CW inclusion rule would need to mirror Stremio's `|| has_notification` branch.

3. **Stremio keys CW by LibraryItem (series-level); Tankoban keys by episode (stream:{imdb}:s{season}:e{episode}).** Stremio's library is per-series with a `state.video_id` pointer to the current video; our StreamProgress bucket is a flat map. Our `mostRecent` collapse at [StreamContinueStrip.cpp:125-137](../../src/ui/pages/stream/StreamContinueStrip.cpp) recreates Stremio's per-series semantics on top of a per-episode store. Functionally equivalent; structurally different.

4. **Stremio's 5-line gate function + sort is smaller than our ~150-line computation.** Per-episode key-parsing, async meta-fetch orchestration, finished-series next-up lookup, poster resolution — all added complexity from the per-episode storage shape. Not a bug; a consequence of storage-shape difference.

### Hypothesis — Agent N to validate

- **Hypothesis — Tankoban's 90%-isFinished threshold + async next-unwatched fetch is an intentional UX upgrade over Stremio's "show until credits advance time_offset to 0" shape.** Stremio's shape works for long-form movies where credits-threshold drops the item organically. For serialized TV with many episodes, Stremio's shape leaves finished episodes in the CW strip until the user manually dismisses or the credits-advancement fires from next-video auto-play. Ours actively nudges users to the next unwatched episode. **Agent 5 to validate this is the right UX call and should stay post-rebuild — Stremio's simpler gate may be the right reduction if the async fetch cost is too high; ours may be a better fit if Tankoban targets binge-watch users.**

- **Hypothesis — Missing notification-carrier inclusion is a gap IF we ever add episode notifications; it's a non-gap today.** Tankoban has no notification subsystem in this wake's audit scope (Slice D). If ever added, the CW strip would need a parallel include rule at [StreamContinueStrip.cpp:95-108](../../src/ui/pages/stream/StreamContinueStrip.cpp). Flag for future surface planning only, not for STREAM_ENGINE_REBUILD scope.

---

## Question 2-D — Next-episode detection

> How does the library consume addon-provided streams for next-episode detection? Is this the bingeGroup mechanism, or a separate path?

### Observed (Stremio Reference)

**Stremio has TWO distinct "next" mechanisms — one for next-EPISODE (sequential), one for next-STREAM (source-selection):**

1. **`next_video_update` at [player.rs:992-1045](stremio-core/models/player.rs)** — SEQUENTIAL next EPISODE. Iterates `meta_item.videos`, finds the current video by `id`, takes index + 1, filters the next entry by:
   ```rust
   next_season != 0 || current_season == next_season
   ```
   — excludes season-0 (specials) transitions unless we were already in season 0. Returns an `Option<Video>` used to populate `player.next_video` for the UI's "up next" card. **Does NOT use `bingeGroup`.**

2. **`next_stream_update` at [player.rs:1095-1115](stremio-core/models/player.rs)** — `bingeGroup`-keyed next STREAM. Iterates `next_streams` (loaded from addon via `next_streams_update` triggered when `next_video` is set), finds the first stream where `next_stream.is_binge_match(current_stream)` at [stream.rs:141-143](stremio-core/types/resource/stream.rs):
   ```rust
   pub fn is_binge_match(&self, other_stream: &Stream) -> bool {
       eq(&self.behavior_hints.binge_group, &other_stream.behavior_hints.binge_group)
   }
   ```
   where `binge_group: Option<String>` at [stream.rs:998](stremio-core/types/resource/stream.rs) is an addon-provided opaque string. Used by auto-play-next to pre-select a stream source matching the user's current pick.

**addon_transport/ is a red herring for Q2-D.** It's a generic HTTP/IPFS addon resource fetcher ([mod.rs + http_transport/ + unsupported_transport.rs](stremio-core/addon_transport/)). It surfaces addon manifest + catalog + meta + stream resources via `fetch_api`/`fetch_cinemeta` calls. Not a next-episode detector — next-episode is computed entirely from `meta_item.videos` after the addon has returned.

### Observed (Tankoban)

**We also have TWO mechanisms, but shaped differently:**

1. **`StreamProgress::nextUnwatchedEpisode` at [StreamProgress.h:105-132](../../src/core/stream/StreamProgress.h)** — FIRST-UNWATCHED-OF-ANY across a flat (season, episode) list. Iterates in ascending order, returns the first whose StreamProgress record is absent OR `!isFinished(state)`. If every pair is finished, returns `{0, 0}` signaling "series fully watched." Used by StreamContinueStrip to compute the "next up" card for finished series.

2. **`StreamChoices::saveSeriesChoice(imdbId, choice)` at [StreamProgress.h:181-200](../../src/core/stream/StreamProgress.h)** — per-series source memory keyed on `bingeGroup`. Written when the user picks a source for S1E1 AND that Stream payload carries a `behaviorHints.bingeGroup`. On S1E2 open, if no per-episode `saveChoice` is set, the series-level entry's bingeGroup is used to highlight a matching stream card in the incoming list. **Same bingeGroup semantics as Stremio's `is_binge_match`, but stored per-series in QSettings rather than computed per-action.**

### Reference

**Notable divergences:**

1. **Stremio's next-episode detector is sequential-from-current (index + 1);** Tankoban's is first-unwatched-of-any-ordered-list. The user experience diverges when the user skips episode 4, watches 5 and 6, then opens the series again:
   - Stremio's `next_video_update`: "next" = episode 7 (index + 1 of current-video 6).
   - Tankoban's `nextUnwatchedEpisode`: "next up" = episode 4 (first-unfinished).

   Both are valid UX calls. Ours handles out-of-order viewing better; Stremio's handles in-order binge better. Neither is broken.

2. **Tankoban's next-stream (`saveSeriesChoice`) is PASSIVE — written-on-select, read-on-next-episode-open;** Stremio's `next_stream_update` is ACTIVE — fires during Action::Load at player.rs:193-218 (mapped in Slice C) as part of the Effects fan-out. We compute later and less often. Not a gap — just a cadence difference.

3. **Stremio's bingeGroup exists on `Stream.behavior_hints` directly;** Tankoban's bingeGroup is stored via `StreamChoices::saveSeriesChoice` payload. Addon protocol compatibility is preserved — we read `behaviorHints.bingeGroup` from the same addon-returned Stream struct; we just also persist the preference at a different layer.

### Hypothesis — Agent N to validate

- **Hypothesis — Our first-unwatched vs Stremio's sequential-next-episode is a UX choice, not a gap; both are defensible.** Assistant 1's original motion-drafting note ("Library UX gap plausibly ~zero") looks supported — there's no structural deficiency here, just two different valid UX philosophies for handling non-linear viewing. **Agent 5 to validate this is the UX call we want to keep through STREAM_ENGINE_REBUILD.**

- **Hypothesis — Our `StreamChoices::saveSeriesChoice` is a clean functional equivalent to Stremio's `next_stream_update(is_binge_match)`, just with different timing** (on user-select vs on player-load). No rebuild action required; no API-freeze implication. **Agent 4B to validate nothing on the source-selection side needs updating for rebuild window.**

---

## Question 3-D — Library → player handoff

> Is there a single library-card → stream-selection → player-load flow we should match, or multiple entry paths? Does selection flow through `Action::Load` at player.rs:140 (already mapped in your Slice C) or through a separate `ctx/library.rs` dispatcher?

### Observed (Stremio Reference)

**Single URL-shaped handoff.** Library card click → deep-link URL → router → single `Action::Load(ActionLoad::Player(Selected))` dispatch → player.rs:140 (mapped in Slice C Q1).

**6 deep-link emit sites ALL produce the same URL shape** at [deep_links/mod.rs](stremio-core/deep_links/mod.rs):
- `:281` — `LibraryItem → LibraryItemDeepLinks.player` (via `streams_item` lookup).
- `:367` — `MetaItemDeepLinks.player` (YouTube default-video-id branch).
- `:423` — `VideoDeepLinks.player` (Video + ResourceRequest).
- `:476` — `VideoDeepLinks.player` (Video + stream_request + meta_request).
- `:514`, `:542`, `:581` — other variants for StreamDeepLinks / channels / etc.

**URL format (canonical):**
```
stremio:///player/{encoded_stream}/{stream_transport_url}/{meta_transport_url}/{type}/{meta_id}/{video_id}
```

**Router parses → `Action::Load(ActionLoad::Player(Selected{stream, stream_request, meta_request, subtitles_path}))` → player.rs:140.** No alternative entry path into player state; `ctx/library.rs` (`update_library.rs` in actuality — no `library.rs` file exists at ctx/ per brief, motion draft approximation) is for library BUCKET mutation (add/remove/mark-watched), not player navigation.

**External player is a branch OFF the same URL** at [deep_links/mod.rs:174-175](stremio-core/deep_links/mod.rs) (VidHub iOS/macOS x-callback URL variants). Still computed from the same `stream_request + streaming_server_url`. Exception path only.

### Observed (Tankoban)

**Signal-slot chain, no URL serialization boundary:**

1. `StreamLibraryLayout::showClicked(imdbId)` → `StreamPage::showDetail(imdbId)` at [StreamPage.cpp:190-191](../../src/ui/pages/StreamPage.cpp). Library card click emits a Qt signal.
2. `StreamDetailView` populates stream-source picker grid from addon-returned streams.
3. User clicks a source → `StreamDetailView::playRequested` OR `sourceActivated` signal → `StreamPage::onPlayRequested` / `onSourceActivated` at [StreamPage.cpp:139-141](../../src/ui/pages/StreamPage.cpp).
4. `StreamPage::beginSession(epKey, params, origin)` at [StreamPage.cpp:164](../../src/ui/pages/StreamPage.cpp) (session-state initialization).
5. `StreamPlayerController::startStream(imdbId, mediaType, season, episode, selectedStream)` at [StreamPage.cpp:170-171](../../src/ui/pages/StreamPage.cpp) + [StreamPlayerController.cpp:33-85](../../src/ui/pages/stream/StreamPlayerController.cpp) — Slice C entry.
6. Poll loop → `readyToPlay(url)` → `VideoPlayer::openFile(url)` → sidecar probe pipeline (Slice C Q1 trace).

**Auto-launch path** (resumed series with saved bingeGroup) — [StreamPage.cpp:170-185](../../src/ui/pages/StreamPage.cpp) — also terminates at `startStream`. 2s countdown timer + "Pick different" cancel button before fire.

**Trailer-paste path** (user pastes YouTube URL) — [StreamPage.cpp:156-172](../../src/ui/pages/StreamPage.cpp) — constructs a synthetic `SelectedStream` with `origin = "trailer-paste"` and calls `startStream` directly. Converges at the same entry point.

### Reference

**Convergence pattern identical.** Both Stremio and Tankoban have multiple ENTRY points (library card / meta details / search result / trailer paste) that all converge to a SINGLE handoff function:
- Stremio: `Action::Load(ActionLoad::Player(Selected))` at player.rs:140.
- Tankoban: `StreamPlayerController::startStream(...)` at StreamPlayerController.cpp:33.

**Divergences worth surfacing:**

1. **Serialization boundary — Stremio has one (URL string); Tankoban doesn't.** The URL boundary enables cross-device/cross-process flows (paste URL, external app launch, OS deep-link registration, cloud-sync "resume on another device"). Tankoban's in-process Qt signal chain can't support any of those without a new URL layer. **Not a gap for rebuild scope** — Hemanth's product direction (single-device desktop player) doesn't have those requirements on the roadmap.

2. **Selected shape — Stremio carries `{stream, stream_request, meta_request, subtitles_path}`;** Tankoban carries `(imdbId, mediaType, season, episode, selectedStream)`. Ours is tuple-shaped Qt-native; Stremio's is struct-shaped Rust-native. Both identify the same "user picked this stream for this episode/movie" semantic. **Functional equivalence holds.**

3. **Replacement defensive stop — Tankoban's `stopStream(StopReason::Replacement)` at startStream's entry** (mapped in Slice C) is our analogue to Stremio's trakt_event_effects at player.rs:142-149 — both handle "new Load arrives while previous session active" cleanly. Semantic match.

### Hypothesis — Agent N to validate

- **Hypothesis — Tankoban's lack-of-URL-boundary at the library→player handoff is a deliberate simplicity win for single-device desktop product scope.** Adding URL serialization for cross-device/external-launch support would be bespoke and currently unjustified. Confirmed non-gap for STREAM_ENGINE_REBUILD scope. **Agent 0 to note in integration memo that Q3-D found no gap.**

- **Hypothesis — `StreamPlayerController::startStream` is the stable single entry point to preserve through STREAM_ENGINE_REBUILD.** All entry paths (library card, auto-launch, trailer paste) converge at this signature; API-freeze per Congress 5 already covers this (5 public methods incl. `startStream`). **Agent 4 to validate startStream signature + StopReason enum + Stream struct shape ALL stay frozen through P6.**

---

## Appendix §Summary — Slice D in one paragraph

**Q1-D Continue Watching:** Stremio uses a 3-predicate gate (`type != "other" && (!removed || temp) && time_offset > 0`) plus notification-carrier inclusion; sort by newest notification or mtime DESC; take CATALOG_PREVIEW_SIZE. Tankoban keys CW by episode not series, filters finished episodes via 90%-threshold, routes finished series through an async next-unwatched fetch for next-episode cards. Tankoban's shape is a UX upgrade for binge-watching; Stremio's is simpler. **No gap; agent 5 validates UX call.** **Q2-D Next-episode detection:** Stremio splits next-EPISODE (sequential via `next_video_update` iterating meta_item.videos) from next-STREAM (bingeGroup-keyed via `next_stream_update` + `is_binge_match`). Tankoban does the same split but semantically differently: our nextUnwatchedEpisode is first-unwatched-of-any-ordered-list (not sequential), and our `StreamChoices::saveSeriesChoice` is passive-persisted (not active-computed at Load). Both valid; **no gap; agent 5 validates UX call.** addon_transport/ is a generic resource fetcher, not a next-episode detector — red herring in the brief's wording. **Q3-D Library→player handoff:** Single-entry convergence in both. Stremio serializes as `stremio:///player/{...}` URL at deep_links/mod.rs:281+; dispatches to Action::Load at player.rs:140. Tankoban uses Qt signal-slot chain terminating at `StreamPlayerController::startStream(...)`. Stremio has URL-boundary enabling cross-device flows; Tankoban doesn't, and doesn't need to. **No gap for rebuild scope.**

## Appendix §Integration memo feeders (Slice D)

- **For Agent 0 integration memo:** Slice D found NO structural gaps requiring rebuild-phase action. 3 validation asks for Agent 5 (UX UX UX — all three CW / next-episode-detection / handoff questions ultimately resolve to "Agent 5 confirms the UX choice is what we want"). 0 API-freeze amendments needed (`startStream` + `StopReason` + `Stream` struct already covered by Congress 5 freeze).
- **For Slice C material leverage:** Slice D Q2 next-episode detection fully leverages Slice C's player.rs:992-1045 + 1095-1115 traces (next_video_update + next_stream_update). Slice D Q3 library→player handoff fully leverages Slice C's Action::Load at player.rs:140 trace. Demonstrates collapse-to-appendix was honest — Slice D had no fresh territory for primary-audit treatment, and would have padded a separate file with Slice-C-material restatement.
- **For Prior-Art Audit supersession:** No Slice D-specific prior-art audit exists (Library UX was not separately audited by Agent 7 in the 2026-04-15-to-04-17 audit programme window). Clean supersession landscape for Slice D.
- **For R21 mtime discipline:** stremio-core-development 04-14 16:59 spot-checked at Slice D appendix-write time, matches motion snapshot. No citation drift.
