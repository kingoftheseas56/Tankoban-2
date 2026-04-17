# Player Stremio/mpv Parity Fix TODO — Bringing VideoPlayer + sidecar to 1:1 Stremio/mpv feature parity before stream-mode rebuild opens

**Authored 2026-04-17 by Agent 0.** Derived from `agents/audits/player_stremio_mpv_parity_2026-04-17.md` (Agent 7 Codex audit) + `chat.md:3159-3243` (Agent 3 validation pass).

---

## Context

Agent 7 delivered a comparative audit of Agent 3's full player surface against Stremio Reference + mpv-master source + `C:\tools\mpv\mpv.exe` behavioral probe on 2026-04-17. Agent 3 validated the same day — confirming 3 P0s, re-ranking 3 P1s as non-gaps (HDR = product decision, speed = intentional architecture, opening-phase = in-flight elsewhere), and flagging 5 audit omissions.

Hemanth's decision (2026-04-17, post-validation): **stream-mode rebuild Congress is put on hold until the video player has everything Stremio's mpv player has.** This TODO drives that parity work.

Stream mode has already consumed more hours than the entire rest of the app combined. Before rebuilding it, we ensure the player it feeds into is Stremio-grade — otherwise the rebuild would deliver data through a player that can't surface it correctly. This TODO closes that precondition.

Validation output in chat.md:3159-3243 established:
- **Confirmed broken:** P0-1 buffered-range, P0-2 cache-pause state machine, P0-3 property-graph IPC, P1-1 precise seek, P1-3 dual-subtitle / secondary-sub-delay / sub-speed
- **In-flight elsewhere (not duplicated here):** P2-1 opening-phase detail → STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 + 2.1; P0-2 user-visible half → STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2
- **Re-ranked as non-gaps:** P1-2 HDR dropdown narrowing (product decision), P1-4 speed A/V correction (intentional two-path design)
- **Audit omissions added to TODO scope:** audio filter graph depth, frame-step reverse behavior, chapter-list observable

---

## Objective

Close every confirmed-broken gap the audit surfaced + Agent 3's omission flags, so the player exposes the observable state + command surface + rendering fidelity of a Stremio-wrapped libmpv player. At TODO completion, a streaming-mode rebuild Congress can open against a player that already has every IPC affordance the new stream architecture would need.

Success = the player, running against a local file OR a stream URL, exposes the same observable state categories mpv does (cache, seekable ranges, paused-for-cache, precise seek, dual-sub, sub-speed, tone-mapping breadth, audio filter chain) with sufficient UI surfacing that a user cannot tell the difference between our player and Stremio's at the parity level this audit scoped.

---

## Non-Goals (explicitly out of scope for this plan)

- **Rewriting the sidecar against libmpv.** We keep our direct-FFmpeg sidecar. Parity is feature-level, not implementation-level. If libmpv becomes necessary later, it's a separate Congress.
- **Expanding the HDR dropdown surface** (audit P1-2). PLAYER_UX_FIX Phase 5 narrowed `{hable, reinhard}` intentionally because `bt2390/mobius/clip/linear` fell silently to `mode=0` (Off) — the shader at [FrameCanvas.h:446](src/ui/player/FrameCanvas.h#L446) only implements 3 algorithms. Expansion is shader work deferred to its OWN TODO if Hemanth prioritizes; not in this scope. **See Phase 6 reconsideration below — it IS in scope, but only if we're willing to do the shader work for real.**
- **Changing the playback-speed architecture** (audit P1-4). Clock rate (`g_clock.set_rate`) + `swr_set_compensation` ±5% drift correction at [SidecarProcess.h:107](src/ui/player/SidecarProcess.h#L107) is our mpv-equivalent of `audio-speed-correction` + `video-speed-correction`. Intentional two-path design per Player Polish Phase 4. No refactor.
- **Implementing Stremio's streaming-server HTTP layer.** We don't wrap an HTTP server; we pass torrent URLs directly to FFmpeg with reconnect + timeouts. HLS transcoding, video probing service, and `/tracks/<url>` endpoint are stream-mode rebuild Congress territory, not player parity.
- **Full stream-state transport across player IPC.** Seed/peer counts, swarm health, torrent statistics are Agent 4/4B domain for the rebuild Congress. This TODO covers only what the PLAYER needs to know (buffered ranges, seekability, cache state) — not the full torrent-state surface.
- **Chromecast / TV / YouTube / iframe implementation branches.** Stremio's selectVideoImplementation.js dispatches across 7 impls; we're Windows-native desktop only. Out of scope forever.

---

## Agent Ownership

**Primary:** Agent 3 (Video Player) — owns `VideoPlayer.*`, `FrameCanvas.*`, `SidecarProcess.*`, `ShmFrameReader.*`, `SubtitleOverlay.*`, `SeekSlider.*`, `LoadingOverlay.*`, `resources/shaders/`, and entire `native_sidecar/`.

**Cross-domain (coordination required, not full ownership):**
- **Agent 4B (Sources)** — Phase 1.1 needs `StreamEngine::contiguousHaveRanges()` or equivalent snapshot API that surfaces per-file have-bitmap. Agent 4B owns `TorrentEngine` which holds the piece-level have state. Coordinate via HELP.md — Agent 4B has already indicated Axes 1+3 pre-offer standing for cross-domain TorrentEngine support (see STATUS.md:89-102).
- **Agent 4 (Stream mode)** — Phase 1.2 new IPC event `buffered_ranges` may flow through `StreamPlayerController` as consumer; Agent 4 owns that integration point.
- **Agent 5 (Library UX)** — no touches expected. SeekSlider is Agent 3's domain.

**Agent 0 (Coordinator)** — commits + dashboard refreshes per Rule 13. Phase-boundary sweeps per Rule 11.

---

## Phase 1 — Buffered/seekable range surface end-to-end (closes P0-1)

**Layer:** Streaming-layer (substrate → IPC → UI).

**Why P0:** This is the single most user-visible streaming gap. mpv paints gray-bar on seek slider showing `demuxer-cache-state.seekable-ranges`; Stremio's HTMLVideo derives `buffered` from the media element's buffered range. Our SeekSlider paints only progress + chapter ticks ([SeekSlider.cpp:52-96](src/ui/player/SeekSlider.cpp#L52)). User drags slider to 15 minutes, pieces [21-22] aren't cached, seek hangs silently — this is exactly the Mode B symptom reproduced this session.

### Batch 1.1 — Substrate API: contiguous-have snapshot

**Owner:** Agent 4B (Sources — TorrentEngine adjacent) with Agent 3 consumer in 1.2.

Add `StreamEngine::contiguousHaveRanges(infoHash, fileIdx) → std::vector<PieceRange>` returning sorted non-overlapping `{startByte, endByte}` ranges of fully-downloaded pieces within the selected file's byte range. Wraps existing `TorrentEngine::contiguousBytesFromOffset` + piece-have query extending to full bitmap walk, not just head-from-offset.

Implementation note: `TorrentEngine` already exposes `contiguousBytesFromOffset` and `pieceRangeForFileOffset`; the new API is an additive wrapper that iterates the file's piece range and merges consecutive have-pieces into byte ranges. Zero new state in TorrentEngine — pure read projection of `libtorrent::torrent_status::pieces` bitfield. Thread-safe via existing m_mutex.

**Files:**
- `src/core/stream/StreamEngine.h` (add `contiguousHaveRanges` declaration + `PieceRange` struct if not present)
- `src/core/stream/StreamEngine.cpp` (implementation)
- `src/core/torrent/TorrentEngine.h` (new helper `haveBitmap(hash) → QBitArray` if not cheaply composable from existing APIs)

**Coordination:** Post in chat.md with HELP line to Agent 4B before starting. Agent 4B Phase 2.3 (event-driven piece waiter) is adjacent — this API is read-only so shouldn't conflict.

### Batch 1.2 — Sidecar-side `buffered_ranges` event emission

**Owner:** Agent 3.

Sidecar receives periodic updates (or on-demand query) of contiguous-have byte ranges from main-app side, emits `buffered_ranges` event with `{file_index, ranges: [{start_byte, end_byte}, ...], total_size}` payload. Rate-limited to 1 Hz to avoid IPC spam during heavy piece arrival.

Two-sided design decision (Agent 3's call): either (a) main-app polls StreamEngine.contiguousHaveRanges + pushes to sidecar via new `set_buffered_ranges` command, or (b) sidecar queries up via a new event subscription model. Option (a) fits our current IPC shape better (command + event, not property observation); (b) is property-graph-flavored and should defer to Phase 7.

**Default pick:** Option (a) — additive, no IPC-architecture change.

**Files:**
- `src/ui/player/SidecarProcess.h` (new `sendSetBufferedRanges(ranges)` command declaration + new `bufferedRangesUpdated(ranges)` signal)
- `src/ui/player/SidecarProcess.cpp` (command serializer + event parser)
- `native_sidecar/src/main.cpp` (command handler that stores last-known ranges + emits event — sidecar is consumer of app-side data, not generator)
- `native_sidecar/src/protocol.h` (add `buffered_ranges` event type)

### Batch 1.3 — Qt signal wiring + VideoPlayer → SeekSlider propagation

**Owner:** Agent 3.

VideoPlayer subscribes to `SidecarProcess::bufferedRangesUpdated`, forwards to SeekSlider via new `SeekSlider::setBufferedRanges(QVector<PieceRange>)` method. Only active in stream-mode (check `PersistenceMode` at [VideoPlayer.h:63-85](src/ui/player/VideoPlayer.h#L63)); local files don't get pointless overlay.

**Files:**
- `src/ui/player/VideoPlayer.h/.cpp` (new slot + signal forwarding)
- `src/ui/player/SeekSlider.h/.cpp` (new `setBufferedRanges` + internal `m_bufferedRanges` storage)

### Batch 1.4 — SeekSlider paint: gray-bar overlay

**Owner:** Agent 3.

Paint ranges as semi-transparent gray bar layer between background and progress fill. Respects existing chapter-tick overlay (drawn on top of buffered layer). LoadingOverlay / HUD integration (optional "buffered to X:XX" text readout) can bundle here or be Batch 1.5 — Agent 3's call.

**Files:**
- `src/ui/player/SeekSlider.cpp` (paint event additions — isolate-commit: paint layer is pure additive below existing fill)

### Phase 1 exit criteria

- Stream playback on a torrent URL shows gray-bar on seek slider matching what's cached
- Local file playback shows no gray-bar (stream-mode-only UX preserved)
- Buffered ranges update live as pieces arrive (<2s lag)
- Dragging slider into buffered range = instant seek; into unbuffered = existing pause-for-cache flow (Phase 2 deepens)
- Rule 6 applies: Agent 3 runs `build_and_run.bat` + smokes a stream before declaring Phase 1 done

---

## Phase 2 — Cache-pause state machine deepening (closes P0-2 beyond STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2)

**Layer:** Streaming-layer + mpv-layer (playback machinery).

**Why P0:** STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2 closes the user-visible half (LoadingOverlay classified stages + 30s first-frame watchdog). But mpv's `paused-for-cache` is a deeper player state: cache-fill progress %, resume threshold, restart-after-seek buffering semantics. Agent 3's validation noted "cache-fill-progress propagation is additional future scope" — this phase IS that future scope.

**Dependency:** Phase 2 of STREAM_PLAYER_DIAGNOSTIC_FIX must ship first (LoadingOverlay stages + watchdog). This TODO's Phase 2 extends on that substrate.

### Batch 2.1 — `paused_for_cache` structured state in sidecar

Sidecar's current shape (video_decoder.cpp:1077-1123): HTTP stall → emit `buffering` → retry 500ms × 60 → timeout. No structured "we are in cache-pause state, here's our progress" reporting.

Add `CacheState` struct emitted as `cache_state` event with fields:
- `paused_for_cache: bool` — true while stalled
- `cache_duration_sec: float` — currently-cached forward duration
- `cache_bytes_ahead: int64` — bytes between read pointer and cache-end
- `raw_input_rate_bps: int64` — observed input bandwidth
- `eta_resume_sec: float` — estimated time to resume (cache_bytes_needed / raw_input_rate, capped at 60s)

Event rate: 2 Hz during cache-pause, silent otherwise (save IPC bandwidth).

**Files:**
- `native_sidecar/src/video_decoder.cpp` (state tracking in HTTP stall loop)
- `native_sidecar/src/main.cpp` (new `cache_state` event wiring)
- `native_sidecar/src/protocol.h` (event type + payload shape)

### Batch 2.2 — Qt IPC parser + VideoPlayer signal + LoadingOverlay integration

VideoPlayer emits `cacheStateChanged(CacheState)`. LoadingOverlay's `Buffering…` state upgrades to `Buffering — %d%% (resumes in ~%ds)` when `cache_state` is active. Honest messaging when ETA is unknown (hide resume text if raw_input_rate = 0).

**Files:**
- `src/ui/player/SidecarProcess.h/.cpp`
- `src/ui/player/VideoPlayer.h/.cpp`
- `src/ui/player/LoadingOverlay.h/.cpp`

### Batch 2.3 — Seek-into-unbuffered-range UX

When user seeks via SeekSlider to byte range NOT in buffered ranges (Phase 1 data), LoadingOverlay shows cache-pause state immediately (anticipatory, not reactive). Seek request still fires (priorities flow to Agent 4/4B substrate for piece fetch); UI matches Stremio/mpv "we're waiting for cache" feel.

**Files:**
- `src/ui/player/VideoPlayer.cpp` (seek intercept + LoadingOverlay pre-fire)
- `src/ui/player/SeekSlider.cpp` (seek-into-unbuffered classification)

### Phase 2 exit criteria

- Stream playback that stalls shows structured cache-pause UI with % + ETA where measurable
- Stall recovery transitions cleanly back to playing state without flicker
- Seek into unbuffered range immediately shows cache-pause (not 30s silent wait then buffering)
- Cache-fill progress matches real piece arrival within 2s
- Rule 6 + Hemanth smoke on known-slow stream before phase exit

---

## Phase 3 — Precise seek (closes P1-1)

**Layer:** mpv-layer (playback machinery).

**Why P1:** Current `av_seek_frame(-1, ts, AVSEEK_FLAG_BACKWARD)` at [video_decoder.cpp:1061-1074](native_sidecar/src/video_decoder.cpp#L1061) snaps to previous keyframe — user-visible when seeking to chapter boundaries, for subtitle sync verification, or frame-stepping. mpv's `--hr-seek` decodes from previous keyframe UP TO target pts, discarding frames before target. Parity is straightforward.

### Batch 3.1 — Decode-to-target helper after backward seek

After existing backward seek + flush, loop `avcodec_receive_frame` discarding frames with `pts < target_pts`, stop at first frame where `pts >= target_pts`. Configurable via new command `set_seek_mode(fast|exact)`; default stays `fast` for compat with current UX expectations.

**Files:**
- `native_sidecar/src/video_decoder.cpp` (seek block extension + mode flag)
- `native_sidecar/src/main.cpp` (new `set_seek_mode` command handler)
- `src/ui/player/SidecarProcess.h/.cpp` (new `sendSetSeekMode(mode)`)

### Batch 3.2 — Chapter-jump + frame-step auto-exact + settings persistence

Chapter navigation should auto-use exact-mode regardless of user setting (chapter boundaries are UX-critical). Frame-step already fires one-frame decode, no change needed — but verify (see Phase 8 Batch 8.1). Settings persistence: store user's seek-mode preference in VideoPlayer's per-show / per-file prefs along with aspect-override and audio-track.

**Files:**
- `src/ui/player/VideoPlayer.cpp` (chapter nav hooks + seek-mode persistence)
- `src/core/CoreBridge.cpp` (if per-show prefs extend here)

### Phase 3 exit criteria

- Seek to arbitrary pts in exact mode lands on exactly that pts (±1 frame)
- Seek to arbitrary pts in fast mode lands on previous keyframe (current behavior preserved)
- Chapter nav uses exact mode regardless of user setting
- Subtitle sync across seeks verified for text + ASS + PGS
- Settings persist per-show

---

## Phase 4 — Subtitle depth: dual-sub + secondary-sub-delay + sub-speed (closes P1-3)

**Layer:** mpv-layer.

**Why P1:** We have single-sub with delay, visibility, style, external-file load, and libass ASS/SSA/PGS/DVD-bitmap rendering. Missing: `secondary-sid` (two active tracks simultaneously), `secondary-sub-delay` (independent delay on track 2), `sub-speed` (timing scalar — necessary for non-1.0x playback subtitle sync).

### Batch 4.1 — Sidecar secondary-sub rendering

libass supports multiple active tracks via separate `ASS_Renderer` instances with different configs. Add second renderer + second overlay composite layer. Render order: primary bottom, secondary top-offset (typical mpv presentation: primary centered, secondary above progress OR above primary — Agent 3 UX call).

**Files:**
- `native_sidecar/src/subtitle_renderer.h/.cpp` (second ASS_Renderer + composite path)
- `native_sidecar/src/main.cpp` (new `set_secondary_sub` + `set_secondary_sub_delay` + `set_sub_speed` handlers)
- `native_sidecar/src/protocol.h` (event + command additions)

### Batch 4.2 — Qt IPC + SubtitleMenu dual-track UI + delay/speed controls

SubtitleMenu gets "Secondary subtitle" submenu mirroring primary track picker (language/group filtering preserved). New settings popover entries: secondary delay slider, sub-speed slider.

**Files:**
- `src/ui/player/SidecarProcess.h/.cpp`
- `src/ui/player/VideoPlayer.h/.cpp`
- `src/ui/player/SubtitleMenu.h/.cpp`
- `src/ui/player/SettingsPopover.h/.cpp` (if that's where delay lives)

### Phase 4 exit criteria

- Two subtitle tracks render simultaneously without overlap
- Independent delay per track
- Sub-speed scalar adjusts subtitle timing relative to video
- All prior single-sub flows preserved (no regression on primary-only case)
- Settings persist per-show

---

## Phase 5 — Audio filter graph depth (audit omission, mpv-parity)

**Layer:** mpv-layer.

**Why in scope (per Agent 3 validation):** mpv exposes `--af` for arbitrary libavfilter chain composition (lavfi-complex syntax). We have a fixed-shape `set_filters(deinterlace + BCS + normalize + interpolate)` at [SidecarProcess.h:63](src/ui/player/SidecarProcess.h#L63). If we want mpv EQ breadth, this needs expansion.

### Batch 5.1 — libavfilter chain composer in sidecar

Refactor our fixed-shape filter application to a chainable `AVFilterGraph` built dynamically from a user-supplied filter spec string. Retain fast paths for common cases (our existing BCS/normalize chain) but allow custom chains to override.

**Files:**
- `native_sidecar/src/audio_decoder.cpp` (if that's the filter-graph owner; likely a new `audio_filter_graph.cpp/.h`)
- `native_sidecar/src/main.cpp` (new `set_audio_filter_chain(spec)` command)

### Batch 5.2 — Qt IPC + EQ popover surface for user-accessible filters

Existing EQ popover (from PLAYER_UX_FIX Phase 6) gets "Advanced" disclosure with preset chains (loudnorm variants, equalizer band presets matching mpv `@` preset syntax if feasible). Power users get a text input for raw filter spec; guardrail against malformed strings that crash libavfilter.

**Files:**
- `src/ui/player/SidecarProcess.h/.cpp`
- `src/ui/player/VideoPlayer.cpp`
- `src/ui/player/EqPopover.cpp` (or wherever the EQ UI lives — PLAYER_UX_FIX Phase 6 added this)

### Batch 5.3 — Preset curation + default preservation

Curated preset set matching mpv's common usage: "Night mode" (loudnorm + dialog enhance), "Speech boost", "Movie theater" (surround downmix tuned), "Off" (bypass). Default on app start remains "Off" so no regression.

**Files:**
- `src/ui/player/EqPopover.cpp` (preset data)
- `resources/` if preset data JSON-externalized

### Phase 5 exit criteria

- Presets render without audible artifacts on test files
- Advanced custom filter spec works for at least one non-preset chain
- Malformed spec gracefully falls back to "Off" with user-visible error (no crash)
- Default behavior unchanged on app install/upgrade

---

## Phase 6 — HDR / tone-mapping expansion (P1-2 reconsidered under parity mandate)

**Layer:** mpv-layer (shader work).

**Reconsideration note:** Audit ranked this P1; Agent 3 re-ranked as "product decision, not gap." Under Hemanth's "everything Stremio's mpv player has" mandate, this IS in scope — because mpv DOES expose 12+ tone-mapping algorithms and Stremio's libmpv-wrapped player inherits them all. PLAYER_UX_FIX Phase 5 narrowed our dropdown honestly because the shader didn't implement the algorithms; Phase 6 here implements them.

**Prerequisite:** Shader work is harder than IPC/Qt work. Agent 3 may want to pull in mpv's libplacebo source as shader reference (vendored now in `Stremio Reference\mpv-master\` — specifically libplacebo submodule if present, or mpv's built-in tone-mapping source at `mpv-master/video/out/gpu/user_shaders.c` + `mpv-master/video/csputils.c`).

### Batch 6.1 — BT.2390 (ITU-R recommendation) shader implementation

Most commonly-used HDR-to-SDR mapping. Standard recommendation from ITU. Good starting point.

**Files:**
- `resources/shaders/video_d3d11.hlsl` (new `applyToneMapBT2390` function + mode-switch)
- `src/ui/player/FrameCanvas.h` (mode constant)
- `src/ui/player/FilterPopover.cpp` (dropdown entry — no longer falls to Off silently)

### Batch 6.2 — Mobius (smooth roll-off) shader implementation

Popular for reducing highlight clipping. Complements Reinhard/Hable with softer top-end.

**Files:**
- `resources/shaders/video_d3d11.hlsl`
- `src/ui/player/FrameCanvas.h`
- `src/ui/player/FilterPopover.cpp`

### Batch 6.3 — Reconsider BT.2446a / ST2094-10 / ST2094-40

Dynamic metadata tone-mapping (ST2094-*) requires per-frame metadata extraction from HDR10+/Dolby Vision streams. Decide after 6.1 + 6.2: if our HDR-content corpus is HDR10 static-metadata-only, defer these three. Hemanth's call.

### Phase 6 exit criteria

- At least 4 working algorithms in dropdown: Reinhard, Hable, BT.2390, Mobius
- Dropdown honest (every option does what it says)
- HDR test file renders without silent fall-to-Off
- Rule 6 + Hemanth visual smoke across test files

---

## Phase 7 — Property-graph IPC architecture (P0-3, deferred decision)

**Layer:** Stremio-layer + mpv-layer (architectural substrate).

**Why deferred, not dropped:** Agent 3's validation: "not cheaply retrofittable... right forum is the stream-mode rebuild Congress itself." But under Hemanth's parity mandate, we reconsider. Key question: after Phases 1-6 ship, is the command+event IPC still limiting? If so, this phase executes. If our parity surface is achievable on the existing IPC, Phase 7 is deferred forever.

**Decision gate:** After Phase 6 exits, Agent 3 + Agent 0 assess whether any remaining Stremio/mpv-parity feature we haven't shipped REQUIRES property-graph semantics to implement. If yes, Phase 7 executes. If no, Phase 7 archives as "investigated, not needed."

### Provisional batches (not detailed until decision gate hits)

- **7.1** Property registry in sidecar (static property list with type, get/set/observe handlers)
- **7.2** IPC protocol extension: `get_property(name)` + `observe_property(name, subscriber)` + `property_changed(name, value)` events
- **7.3** Qt-side property model + VideoPlayer property subscriptions for cache/track/chapter state
- **7.4** Migration path for existing commands/events → property equivalents (dual-wire during migration, command+event as the fallback for external consumers)

### Phase 7 exit criteria (provisional)

- At least one property observable end-to-end (`demuxer-cache-state` equivalent) via new transport
- No regression on existing command+event flows
- Decision locked: migrate all OR keep dual-wire indefinitely (either is valid)

---

## Phase 8 — Polish: audit omissions + verification gaps

**Layer:** mpv-layer (minor).

### Batch 8.1 — Frame-step reverse verification

Agent 3's validation flagged: "I believe we do this already but would need to re-read video_decoder.cpp frame_step handler to confirm." Verify `sendFrameStep(backward=true)` actually seeks to previous keyframe + decodes forward to target frame (mpv's `frame-back-step`). If not, fix to match.

**Files:**
- `native_sidecar/src/video_decoder.cpp` (verify/fix)

### Batch 8.2 — Chapter-list edit (observable)

Mpv exposes `chapter-list` as observable (scripts can modify). We forward static metadata only. Low priority — most users don't edit chapters — but adding an observable surface here would be Phase 7 infrastructure if it lands.

**Files:**
- `native_sidecar/src/main.cpp` (if Phase 7 has landed, make chapters observable)
- `src/ui/player/SidecarProcess.h/.cpp`

### Phase 8 exit criteria

- Frame-step reverse verified or fixed to match mpv semantics
- Chapter-list observable (or documented as intentionally display-only if Phase 7 didn't ship)

---

## Scope decisions locked in

- **Parity is feature-level, not implementation-level.** We keep direct-FFmpeg sidecar. No libmpv wrap.
- **Phase 1 is the single gate for stream-mode rebuild Congress.** Phases 2-8 can run alongside the Congress or after, per Hemanth's preference — but Phase 1 closure is the readiness signal.
- **Phase 6 (HDR expansion) IS in scope** under parity mandate, despite PLAYER_UX_FIX Phase 5's narrowed-dropdown product decision. Phase 5's decision is superseded by this TODO.
- **Phase 7 (property-graph IPC) is gate-decided after Phase 6.** Don't commit to retrofit before knowing if the existing IPC is actually limiting.
- **STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2 (LoadingOverlay stages + watchdog) is PREREQUISITE for this TODO's Phase 2.** Must ship first.
- **Agent 4B HELP coordination needed for Phase 1.1.** StreamEngine substrate API addition — coordinate via HELP.md or chat.md before Batch 1.1 starts.

---

## Isolate-commit candidates

- Batch 1.4 (SeekSlider paint overlay) — pure additive paint-event layer, no behavior change if ranges vector is empty
- Batch 3.1 (decode-to-target helper) — new code path gated by `set_seek_mode(exact)`; `fast` remains default
- Batch 5.3 (preset curation) — data-only, zero logic change
- Batches 6.1 + 6.2 (individual tone-mapping algorithms) — each one additive to the dropdown

---

## Existing functions / utilities to reuse (not rebuild)

- **`TorrentEngine::contiguousBytesFromOffset`** ([TorrentEngine.cpp:1205](src/core/torrent/TorrentEngine.cpp#L1205)) — head-from-offset contiguous-have, Phase 1.1 extends to per-range bitmap walk
- **`TorrentEngine::pieceRangeForFileOffset`** — file offset → piece index translation, already used by StreamEngine
- **`FrameCanvas::setCropAspect`** + `cropOverride` — shipped at `44e7174`, cinemascope/aspect already handled; don't dupe
- **`sendSetSubDelay`** ([SidecarProcess.h:54](src/ui/player/SidecarProcess.h#L54)) — Phase 4 mirrors shape for `sendSetSecondarySubDelay`
- **`g_clock.set_rate` + `swr_set_compensation` ±5%** ([main.cpp:943,1429-1472](native_sidecar/src/main.cpp#L943)) — Phase 3's seek-mode extension lives in same family of rate-control primitives
- **`libass` ASS_Renderer** ([subtitle_renderer.cpp:138-159](native_sidecar/src/subtitle_renderer.cpp#L138)) — Phase 4 instantiates second renderer, same library
- **`AVFilterGraph`** (libavfilter, already linked) — Phase 5 uses same lib, different composition shape
- **`[PERF]` log + `frames_written_delta` counter** ([video_decoder.cpp:969-978](native_sidecar/src/video_decoder.cpp#L969)) — reuse for Phase 2 cache-state observability instead of bespoke logging
- **`LoadingOverlay` 2-state base** ([LoadingOverlay.cpp:22-52](src/ui/player/LoadingOverlay.cpp#L22)) — Phase 2 extends, doesn't replace
- **STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.1 sidecar events** (probe_start/probe_done/decoder_open_start/decoder_open_done/first_packet_read/first_decoder_receive already live) — available for Phase 2's cache-state event semantics

---

## Review gates

**Review protocol is SUSPENDED** (Agent 6 decommissioned 2026-04-16). Phase-exit approval is Hemanth smoke directly.

- **Phase 1 exit:** Hemanth smokes a stream, confirms gray-bar updates live + seek into buffered = instant.
- **Phase 2 exit:** Hemanth smokes a known-slow stream, confirms structured cache-pause UI.
- **Phase 3 exit:** Hemanth smokes chapter jumps + subtitle sync at various pts.
- **Phase 4 exit:** Hemanth smokes dual-subtitle rendering on a file with two sub tracks.
- **Phase 5 exit:** Hemanth smokes presets + advanced chain on test content.
- **Phase 6 exit:** Hemanth visually verifies each tone-mapping algorithm on HDR test files.
- **Phase 7 exit (if executed):** Hemanth verifies no IPC regression on existing flows.
- **Phase 8 exit:** Hemanth spot-checks frame-step reverse.

Rule 11 mandatory on every batch: `READY TO COMMIT` line → Agent 0 sweep.

---

## Open design questions (domain master — Agent 3 — decides per Rule 14)

1. **Phase 1.2 IPC shape — Option (a) app-pushes vs (b) sidecar-queries.** Default (a). Flip to (b) if Phase 7 ends up landing first (unlikely).
2. **Phase 2.3 seek-into-unbuffered-range UX** — fire LoadingOverlay immediately on seek intent, or wait for actual stall? Agent 3's UX call.
3. **Phase 4 secondary-subtitle positioning** — above primary, above progress bar, or user-configurable? Agent 3 + visual smoke.
4. **Phase 5 advanced filter spec input** — free-text box (power-user, crash-prone) vs preset-only (safe, less capable)? Agent 3's call.
5. **Phase 6.3 ST2094-* dynamic metadata algorithms** — defer to after HDR10+ content corpus assessment? Hemanth + Agent 3.
6. **Phase 7 decision gate criteria** — what concrete unmet parity feature would trigger property-graph retrofit? Agent 3 defines after Phase 6 exits.

---

## What NOT to include (explicit deferrals)

- **Stream-mode rebuild Congress.** On hold per Hemanth 2026-04-17 until this TODO completes.
- **STREAM_PLAYER_DIAGNOSTIC_FIX work.** Separate TODO, prerequisite for this TODO's Phase 2. Don't duplicate.
- **Torrent-state / seed-peer UI transport** (audit P1-5). Stream-mode rebuild Congress territory.
- **Stremio addon protocol integration.** Separate Congress concern.
- **Chromecast / TV / YouTube implementations.** Out of scope forever.
- **libmpv wrap / sidecar rewrite.** Parity is feature-level.
- **HLS transcoding / video probing service.** Stream-mode rebuild territory.

---

## Rule 6 + Rule 11 application

**Rule 6:** Agent 3 runs `build_and_run.bat` (main app build — honor-system per contracts-v2) AND smokes the feature before declaring any batch done. `native_sidecar/build.ps1` is agent-runnable from bash per contracts-v2; use it for sidecar-only batches.

**Rule 11:** Every batch that verifies (compiles + feature works) gets a `READY TO COMMIT — [Agent 3, STREMIO_PARITY Phase X.Y]: <msg> | files: a, b, c` line in chat.md. Agent 0 sweeps. No git from Agent 3.

**Rule 10 shared-file coordination:** Phase 1.1 touches `src/core/torrent/*` (Agent 4B domain). Post in chat.md before starting; Agent 4B's standing Axes 1+3 pre-offer (per STATUS.md:89-102) covers this cross-domain touch.

---

## Verification procedure (end-to-end once all planned phases ship)

1. **Phase 1 smoke:** Stream a torrent. Observe gray-bar fills behind playhead as pieces arrive. Drag slider into gray = instant seek. Drag into not-gray = Phase 2 cache-pause UI.
2. **Phase 2 smoke:** Low-seed torrent. Observe structured `Buffering — 45% (resumes in ~8s)` UI vs silent `Buffering…`.
3. **Phase 3 smoke:** Chapter-jump should land exact. Frame-step on seek should be frame-precise.
4. **Phase 4 smoke:** Dual-sub file (e.g., English + Japanese) shows both simultaneously. Delay each independently. Speed scalar at 1.5x keeps subs in sync with audio.
5. **Phase 5 smoke:** Apply "Night mode" preset, verify dialog clarity improvement. Apply custom chain, verify no crash on malformed spec.
6. **Phase 6 smoke:** HDR test file (HDR10) renders cleanly under each of Reinhard / Hable / BT.2390 / Mobius. Visual comparison against mpv playing same file.
7. **Phase 7 smoke (if executed):** No regression on any command+event flow. Property-observable end-to-end for at least `demuxer-cache-state` equivalent.
8. **Phase 8 smoke:** Frame-step reverse matches mpv's frame-back-step behavior on a test file.

**Global acceptance:** open any stream OR local file, user cannot distinguish our player from Stremio's at the parity level scoped in `agents/audits/player_stremio_mpv_parity_2026-04-17.md`.

---

## Next steps post-approval

1. Hemanth reviews TODO, approves shape + phase ordering + scope decisions.
2. Agent 0 commits this TODO + updates `CLAUDE.md` Active Fix TODOs table + `MEMORY.md` Active repo-root fix TODOs line in same commit.
3. Agent 0 posts `REQUEST AUDIT` or `HELP` coordination line to Agent 4B re: Phase 1.1 `contiguousHaveRanges` API — or Agent 3 posts it when starting Phase 1.
4. Agent 3 picks up Phase 1 Batch 1.1 when capacity allows. STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1.2 + 2.1 already queued ahead of this TODO's Phase 2 (prerequisite).
5. Stream-mode rebuild Congress opens AFTER Phase 1 exits at minimum; Hemanth decides whether to wait for all 8 phases or proceed after Phase 1 readiness signal.
