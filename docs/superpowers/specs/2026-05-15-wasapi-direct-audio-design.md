# WASAPI Direct Audio Output — Design Spec

**Author:** Agent 3 (Video Player)
**Date:** 2026-05-15
**Status:** Brainstorm-approved, awaiting Hemanth spec review → `/superpowers:writing-plans`
**Scope:** Replace PortAudio's device-submission layer in the native sidecar with direct WASAPI usage. Closes the Bluetooth-hot-plug bug class permanently. Windows-only change (we don't ship to Mac/Linux).

---

## 1. Problem statement

Connecting a Bluetooth audio device mid-playback should seamlessly route Tankoban's audio to it — every other media player on Windows (mpv, VLC, PotPlayer, MPC-HC) does this without the user noticing. Tankoban doesn't, in stream mode. The user's existing workaround is to manually toggle the default audio output in the Windows taskbar, which incidentally crashes the sidecar process; the crash-recovery code restarts the sidecar, the fresh `Pa_Initialize` re-enumerates devices including BT, and the new sidecar binds to BT. Manual, lossy, and obviously not what we want.

**Root cause (Phase 1 systematic-debugging, this session):** PortAudio's WASAPI backend was designed for stable device topologies. Its device enumeration is built once at `Pa_Initialize` and stays frozen. When a new audio device (BT, USB, HDMI) appears mid-process, PortAudio doesn't see it; any reroute attempt that tries to bind to the new endpoint fails. The architecture is not a fit for the dynamic device dance Windows does at runtime.

**Why this is fix attempt #4:** Per Phase 4.5 of the systematic-debugging skill ("if 3+ fixes failed, question the architecture"), this is the point at which we stop patching PortAudio and replace the abstraction. Each prior attempt revealed new state coupling inside PortAudio's WASAPI backend; the pattern is the skill's documented signature for architectural-rather-than-symptomatic problems.

---

## 2. Prior fix attempts (so future agents don't repeat)

| Attempt | Date | What it tried | Why it failed |
|---|---|---|---|
| v1 | 2026-05-10 | Added `IMMNotificationClient` watcher + atomic generation counter; audio thread polls counter and rebuilds stream on default-device-change | Used `PaHostApiInfo::defaultOutputDevice` (a snapshot at `Pa_Initialize`) for the new device index — stayed stale after Windows hot-rerouted the default |
| v2 | 2026-05-13 | Added `resolve_current_wasapi_default_device_index()` walking PortAudio's frozen device list, matching by live IMMDevice endpoint ID via `PaWasapi_GetIMMDevice` | Returned `true` (success) on resolve-failure, silently sealing the generation counter — and PortAudio's device list never contained the freshly-connected BT to begin with, so the resolve always failed |
| v3 | 2026-05-15 (this session) | Added `PaWasapi_UpdateDeviceList()` call between the first failed walk and a retry walk in the resolver | `PaWasapi_UpdateDeviceList()` returns `paInternalError` when called from the audio decoder thread context. The apparent "first attempt success" was a coincidental sidecar crash that triggered the existing restart-recovery path |

> Note: prior tables permitted in spec docs (technical artifact). For Hemanth-facing chat output, numbered lists per `feedback_no_tables_simple_lists.md` — this is the docs-tree artifact, audience is implementers.

---

## 3. Strategic decisions (Hemanth's brainstorm answers, 2026-05-15)

1. **BT disconnect / out-of-range behavior:** silent auto-switch to whatever's now the default audio device. No toast. No UI hint. Most seamless.
2. **Startup prewarm:** kept. The 5-second BT cold-start savings on first-video-play is real value; ~80 LOC of lifecycle code is worth it.
3. **Rollout strategy:** one-bang cutover. No env-gated dual-path coexistence. The PortAudio device-submission layer is removed wholesale in this arc.
4. **Smoke cadence:** end-of-arc smoke matrix only (the a–f matrix from the original bug prompt). No per-summon smoke gates.

---

## 4. Architectural shape

### 4.1 Today (pre-cutover)

```
AudioDecoder thread:
  av_read_frame → avcodec_send/receive → swresample → speed/DRC/volume →
  ┌──────────────────────────────────────────────────────────┐
  │ Pa_WriteStream(active_stream_, samples, frame_count)     │   ← THIS LAYER
  └──────────────────────────────────────────────────────────┘
         ↓
  PortAudio WASAPI backend (libportaudio.dll)
         ↓
  Windows WASAPI (mmdeviceapi.dll, audiosess.dll)
         ↓
  Speakers / BT / HDMI
```

The boxed layer is the one being replaced. Everything above it (decode, resample, A/V sync, DRC, volume) is exactly the audio-quality logic we want to preserve.

### 4.2 After cutover

```
AudioDecoder thread:
  av_read_frame → avcodec_send/receive → swresample → speed/DRC/volume →
  ┌──────────────────────────────────────────────────────────┐
  │ WasapiOutput::write(samples, frame_count)                │   ← NEW LAYER
  │   (owns IAudioClient + IAudioRenderClient internally,    │
  │    rebuilds on default-device-change via                 │
  │    IMMNotificationClient — invisible to caller)          │
  └──────────────────────────────────────────────────────────┘
         ↓
  Windows WASAPI (mmdeviceapi.dll, audiosess.dll)
         ↓
  Speakers / BT / HDMI
```

One layer swapped. No middleman.

### 4.3 Why this works where PortAudio didn't

WASAPI's `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eMultimedia, ...)` returns a *live* `IMMDevice` pointer at call time. There's no enumeration step, no frozen device list — the call directly queries the Windows audio service for whatever is the current default. This is the same pattern mpv (`audio/out/ao_wasapi.c`) and VLC (`modules/audio_output/wasapi.c`) use. The "follow the moving default" pattern is the API's native idiom; PortAudio was bolting onto a model the API doesn't really have.

---

## 5. Components

### 5.1 New: `WasapiOutput` class (`native_sidecar/src/wasapi_output.{h,cpp}`)

**Owns:** one `IAudioClient`, one `IAudioRenderClient`, one COM apartment (MULTITHREADED for compatibility with `IMMNotificationClient` callbacks), one `IMMDeviceEnumerator` + registered notification client, one internal device-change flag.

**Public API:**

```cpp
class WasapiOutput {
public:
    WasapiOutput();
    ~WasapiOutput();

    // Open audio output against the current Windows default render endpoint
    // (eMultimedia role) in SHARED mode at the requested format. Returns true
    // on success. On failure, the object stays constructed but write() is a
    // no-op (silent-mode fallback for broken audio drivers).
    bool open(int sample_rate, int channels);

    // Blocking write of `frames` of float32-interleaved samples. Internally:
    //   1. Check device-change flag; reactivate against new default if set.
    //   2. Wait on IAudioClient's event handle (or poll GetCurrentPadding)
    //      until buffer has room for `frames`.
    //   3. IAudioRenderClient::GetBuffer + memcpy + ReleaseBuffer.
    // Returns true on normal completion (even if samples were discarded due
    // to silent-mode fallback). Returns false only on terminal failures the
    // caller should propagate (very rare — usually means COM is dead).
    bool write(const float* samples, size_t frames);

    // Pause / resume the audio stream (IAudioClient::Stop / Start).
    // Used by AudioDecoder for pause/seek lifecycle.
    void set_paused(bool paused);

    // Drop any pending data in the device buffer (Stop + Reset + Start).
    // Mirrors AudioDecoder::flush_queue's PortAudio Abort+Restart today.
    void flush();

    // Currently-bound device's output latency in seconds — for AV sync.
    // Updated atomically when the device rebinds.
    double current_latency_sec() const;

    // True if the underlying audio client is alive and not in silent-mode
    // fallback. AudioDecoder uses this to gate AV sync clock updates.
    bool is_active() const;
};
```

**Internal lifecycle:**
- Constructor: `CoInitializeEx(MULTITHREADED)`, create `IMMDeviceEnumerator`, register an `IMMNotificationClient` implementation that flips `device_changed_` atomic on `OnDefaultDeviceChanged(eRender, eConsole | eMultimedia, ...)`. Ignores `eCommunications` role (Teams/Zoom-style voice-call default — separate concern).
- `open()`: query current default via `GetDefaultAudioEndpoint(eRender, eMultimedia)`, `IAudioClient::Activate`, `IAudioClient::Initialize` in shared mode with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`, `IAudioClient::GetService(IAudioRenderClient)`, event handle via `SetEventHandle`, `IAudioClient::Start`.
- `write()`: hot path. Reactivation is gated by `device_changed_` flag — checked once per call, not in inner loop. The auto-convert flag lets Windows handle the device-side mix to the speakers' native format, so we don't have to negotiate format per device.
- Destructor: `IAudioClient::Stop`, release all COM interfaces, unregister notification client, `CoUninitialize` if we owned the apartment.

**Silent-mode fallback:** if `open()` or a reactivation fails (no audio devices present, COM dead, IAudioClient::Initialize rejects every fallback format), the object stays constructed and `write()` discards samples but returns true. This matches Hemanth's choice ("BT vanishes mid-playback → audio plays into void, sidecar stays alive, video keeps playing").

### 5.2 `audio_decoder.cpp` surgery (small)

Replace direct PortAudio calls with `WasapiOutput*` calls. Surface today is:
- `prewarmed_stream_` / `Pa_OpenStream` / `Pa_StartStream` for lazy-open
- `Pa_WriteStream(active_stream_, ...)` in the main render loop
- `Pa_AbortStream` / `Pa_CloseStream` at cleanup
- `Pa_OpenStream` retry on resampler-rate-fallback (audio_decoder.cpp:600s)

After: each of those becomes a `m_wasapi_output->method()` call. The `rebuild_for_new_default()` method goes away entirely (device-change handling moves into `WasapiOutput`). The `bind_generation_` / `observed_reroute_generation_` machinery also goes away. The audio thread's main loop becomes ~30 lines shorter.

### 5.3 `main.cpp` prewarm-path surgery (small)

The startup prewarm today opens a Pa stream at sidecar boot. After: it constructs a `WasapiOutput` and calls `open(48000, 2)`. The `WasapiOutput*` is then passed into each `AudioDecoder` instance for the duration of the sidecar process.

Lifecycle ownership: `WasapiOutput` is owned by `main.cpp` (parallel to today's prewarm-stream ownership). `AudioDecoder` borrows the pointer; on decoder destruction, the `WasapiOutput` stays alive for the next video. The `invalidate_prewarm_if_stale` path today (which closes the prewarm if the default-device generation has advanced) becomes a no-op or is deleted entirely — the new `WasapiOutput` self-heals across default-changes, so there's no concept of "stale prewarm" anymore.

### 5.4 `native_sidecar/src/audio_device_watcher.{h,cpp}` (gutted)

The sidecar-side watcher's purpose was to bump a generation counter the audio thread polls. With device-change handling internal to `WasapiOutput`, this file's reason to exist evaporates. **Delete or reduce to a thin compatibility shim with empty `init()` / `shutdown()`.** `g_audio_reroute_generation` atomic deleted. The `resolve_current_wasapi_default_device_index` function (the one I patched with the failed `PaWasapi_UpdateDeviceList` retry in v3) is deleted. CMakeLists.txt may need to drop the file from the sidecar source list if we delete it entirely.

### 5.5 Qt-side `AudioDeviceWatcher` (`src/ui/player/AudioDeviceWatcher.{h,cpp}`) — UNTOUCHED

Separate feature: per-device audio-delay recall (Task 8.B, MAKE_MPV_SOLO arc). VideoPlayer connects to its `defaultDeviceChanged(QString friendlyName)` signal to recall the saved audio-delay for the new device. This has nothing to do with the audio render path. Stays as-is.

### 5.6 Build wiring

`native_sidecar/CMakeLists.txt`:
- **Add:** `src/wasapi_output.cpp` to the sidecar source list.
- **Drop:** `portaudio` from `find_library` and from the link list (if no other component still uses PortAudio — verify during implementation).
- **Drop:** the PortAudio DLL deployment line in `build.ps1` (`libportaudio.dll` no longer needed in `resources/ffmpeg_sidecar/`).
- **Add:** link against `ole32` (already linked for the existing IMMDevice work), no new system deps. WASAPI is part of Windows itself; no SDK install needed.

---

## 6. Data flow

### 6.1 Steady state (normal playback)

1. `AudioDecoder::audio_thread_func` reads a packet, decodes, resamples, applies DRC/volume.
2. Calls `m_wasapi_output->write(samples, frames)`.
3. `WasapiOutput::write`:
   a. Atomic check: did `device_changed_` flag flip since last call? If yes, run reactivation (see 6.2 below).
   b. Wait on `IAudioClient`'s event handle (kernel object, no busy-poll) until the device has room for `frames`.
   c. `IAudioRenderClient::GetBuffer(frames, &buf)`, `memcpy(buf, samples, frames * channels * sizeof(float))`, `IAudioRenderClient::ReleaseBuffer(frames, 0)`.
   d. Update internal latency cache (from `IAudioClient::GetStreamLatency` if it changed).
4. AudioDecoder's master clock update runs.
5. Loop.

### 6.2 Mid-playback device change

1. User connects BT (or unplugs USB headset, or switches default via taskbar).
2. Windows fires `OnDefaultDeviceChanged(eRender, eMultimedia, new_id)` on the COM notification thread.
3. `WasapiOutput`'s `IMMNotificationClient` sets `device_changed_ = true` (atomic store).
4. Audio decoder thread is mid-loop; next `WasapiOutput::write` call sees the flag.
5. `WasapiOutput::write` enters reactivation: `IAudioClient::Stop`, release current `IAudioRenderClient` + `IAudioClient`, query new default via `GetDefaultAudioEndpoint`, `Activate` + `Initialize` + `GetService` + `Start` against the new device. Clear `device_changed_` flag. Total gap: ~50-200ms (matches today's PortAudio reroute glitch budget; matches mpv's behavior).
6. Resume normal write loop against the new device.
7. Decoder thread continues with no knowledge that anything happened.

### 6.3 Cold-start (sidecar boot)

1. `main.cpp` after `Pa_Terminate` (if PortAudio init still happens elsewhere — to verify during implementation) or directly: `auto wasapi = std::make_unique<WasapiOutput>();`
2. `wasapi->open(48000, 2);` — opens against current default at boot time, prewarmed. If the device is BT, the 5-second BT cold-start is paid HERE, before the user opens a video.
3. First video opens → `AudioDecoder` constructed with `wasapi.get()` as the output handle. Audio decode + resample + write proceeds with no per-file device-open cost.

---

## 7. Error handling

### 7.1 Open-time failures

| Failure | Behavior |
|---|---|
| `CoCreateInstance(IMMDeviceEnumerator)` fails | Silent-mode: `write()` no-ops. Sidecar emits `AUDIO_DEVICE_STARTUP_FAILED` event. Video plays without audio. |
| `GetDefaultAudioEndpoint` returns `E_NOTFOUND` (no audio devices at all) | Silent-mode: stay constructed, periodically retry on device-add events. |
| `IAudioClient::Initialize` rejects requested format | Try the device's mix format (via `GetMixFormat`). If that also fails: silent-mode. |
| `IAudioClient::Start` fails | Silent-mode + log. Future `device_changed_` events trigger reactivation attempt. |

### 7.2 Mid-playback failures

| Failure | Behavior |
|---|---|
| `GetCurrentPadding` returns `AUDCLNT_E_DEVICE_INVALIDATED` (BT vanished, USB unplugged, audio driver crashed) | Trigger internal reactivation against current default. Same gap as a device-change. |
| `GetBuffer` returns `AUDCLNT_E_OUT_OF_ORDER` (shouldn't happen with our linear write pattern, but defensive) | Stop + Restart the audio client. Skip this frame's samples. |
| New device default still unavailable after reactivation (e.g., all audio devices gone) | Silent-mode. Next `device_changed_` flip retries. AudioDecoder thread keeps running, master clock keeps updating from PTS. |
| COM goes dead (extremely rare — usually means the audio service crashed) | Silent-mode. No recovery. User must restart Tankoban. |

### 7.3 What gets emitted to Qt

- `AUDIO_DEVICE_STARTUP_FAILED:<reason>` — same code as today, for the open-time hard-failure case. Qt-side toast shows the message.
- `AUDIO_DEVICE_LOST:<reason>` — **removed.** Today this fires when Pa_WriteStream errors out mid-stream and forces the AudioDecoder to `goto cleanup`. The new design doesn't have that escape hatch — silent-mode fallback covers all the "device went away" cases without killing the audio thread. The sidecar-restart path (VideoPlayer::onSidecarCrashed) still exists for actual sidecar process crashes, which is the right scope for it.
- No new event types added.

### 7.4 Logging (diagnostic surface)

Preserve the `AVSYNC_DIAG audio_*` log lines for diagnostic continuity:
- `AVSYNC_DIAG audio_open_complete +<ms> rate=<r> ch=<c> device='<name>' endpoint_id='<id>' latency=<s>` — emitted on every successful `open()` or reactivation.
- `AVSYNC_DIAG audio_open_failed +<ms> reason='<text>'` — emitted on failed open or reactivation (going to silent-mode).
- `AVSYNC_DIAG audio_device_changed +<ms> from='<old_name>' to='<new_name>'` — emitted by the notification client.
- `AVSYNC_DIAG audio_silent_mode_entered +<ms>` / `audio_silent_mode_exited +<ms>` — for visibility on the fallback path.

The existing `audio_default_device_changed gen=N role=R id=...` line gets replaced by `audio_device_changed` above. The `audio_reroute_failed` / `audio_reroute_complete` lines disappear (no separate reroute concept in the new code).

---

## 8. Testing

No automated audio tests in this codebase today. Not introducing them in this arc (smoke is the established standard for player-domain work per `feedback_subjective_over_trace.md`).

### 8.1 Smoke matrix (end-of-arc, all on Hemanth)

The a–f matrix from the original bug prompt:

a. **Stream mode, BT auto-connect mid-playback.** Internal speakers default, BT disconnected. Open stream-mode video. Audio plays through speakers. Connect BT. Audio auto-routes to BT within ~1-2s. **PRIMARY TARGET.**

b. **Local-file BT-connect.** Same as (a) but with a local video. Must keep working — pre-fix this worked via Windows' transparent migration, post-fix it works via our explicit reroute. Either way, must be seamless.

c. **BT disconnect / out of range.** Start with BT connected and audio routing to BT. Disconnect BT. Audio auto-routes to internal speakers cleanly. No "Reconnecting player" toast (per Hemanth's silent-auto-switch preference).

d. **USB ↔ BT taskbar toggle.** Mid-playback, change Windows default via taskbar between any two devices. Audio follows.

e. **Workaround toggle still works.** The old "switch away then back" sequence in the taskbar should be a no-op now (audio already follows), but the path mustn't crash anything.

f. **Subjective no-glitch.** No long silence window, no audible click train, no two-stage glitch from the eConsole+eMultimedia double-event (the new code handles both events as one device-change because it only acts on the live default at write-time, not on event counts).

### 8.2 Negative tests

g. **Open Tankoban with no audio devices at all (disable all in Sound settings).** Sidecar starts in silent-mode. Video plays. No crash. Enable an audio device — audio picks up.

h. **Unplug all audio devices mid-playback.** Audio thread keeps running silently. Plug a device back in. Audio resumes.

### 8.3 Regression checks

- Pause / resume / seek / chapter-skip — unchanged behavior expected (the lifecycle methods on `WasapiOutput` delegate to `IAudioClient::Stop`/`Start`).
- A/V sync — `current_latency_sec()` feeds the master clock the same way `Pa_GetStreamInfo` did.
- DRC checkbox in Equalizer popover — unchanged (DRC sits above WasapiOutput in the pipeline).
- Audio-track switching — unchanged (decode-side, doesn't touch device layer).
- Per-device audio-delay recall — unchanged (Qt-side AudioDeviceWatcher untouched).

---

## 9. Out of scope (deferred or won't-do)

- **mpv-style sub-Pa_BlockingWrite ultra-low-latency optimizations.** Shared mode at default Windows latency is fine for video playback. Pro-audio low-latency tweaks are a separate concern.
- **Audio quality changes.** Resampler tuning, DRC parameter retuning, libplacebo-style audio filters — separate work.
- **5.1 / 7.1 surround support.** Today's pipeline is stereo-only (resampler downmixes everything to stereo for the prewarm at 48k). If surround comes up later, it's a separate arc.
- **Cross-platform audio (Mac, Linux).** We don't ship on either. The non-Windows stubs in the existing code can be deleted as part of this arc.
- **Exclusive-mode WASAPI.** Shared mode only. BT exclusive-mode support is too fragile across BT chipsets and headphone models.
- **Qt-side AudioDeviceWatcher (per-device audio-delay recall).** Separate feature, untouched.
- **The "Reconnecting player" toast in VideoPlayer.cpp.** Stays in place for legitimate sidecar-process crashes. Just won't fire on audio-device events anymore.

---

## 10. Rollout

- **One-bang cutover.** No env-gated dual-path. The PortAudio device-submission code is deleted in the same RTC that ships WasapiOutput. The v3 `PaWasapi_UpdateDeviceList` retry I added today gets removed wholesale (the file it's in either disappears or shrinks to a stub).
- **PortAudio DLL deployment removed** from `native_sidecar/build.ps1` (verify no other component still needs PortAudio first).
- **Single end-of-arc smoke matrix** — Hemanth runs the a–h tests, signs off, RTC posted.

### 10.1 Rollback path

If the smoke matrix fails and the failure isn't trivially fixable inside this arc:
- `git revert` the cutover commit. Restores the v3 state (PortAudio + failed UpdateDeviceList retry).
- The pre-v3 state (v2 silent-seal) is still in git history if v3-state is also broken (it shouldn't be — v3 is no worse than v2, just a noisier log line).
- Sidecar-restart path is the last-resort safety net regardless: if audio dies for any reason, the existing crash-recovery in VideoPlayer.cpp picks up with a fresh sidecar.

### 10.2 What writing-plans needs to figure out

Coder-level decisions to be locked in the plan, not the spec:
- Buffer size for `IAudioClient::Initialize` (REFTIMES_PER_SEC tradeoff: latency vs glitch tolerance under load).
- Event-driven (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`) vs poll-mode for `write()`'s "wait for buffer space" inner loop. Recommendation: event-driven (kernel-object wait, no busy-poll).
- COM apartment topology: MULTITHREADED throughout, or STA on the WasapiOutput owner thread? mpv uses MTA. Recommendation: MTA, matches the IMMNotificationClient.cpp pattern already in audio_device_watcher.cpp today.
- Thread-safety strategy for `device_changed_` flag vs `write()` reactivation: atomic flag + mutex around the rebind, or pure atomic-with-CAS? Recommendation: atomic flag + mutex (rebind is the slow path, mutex contention is negligible).
- Whether to keep `audio_device_watcher.cpp` as a stub (preserving the Pa_GetDeviceCount-bound `init()` shim main.cpp calls) or delete it entirely (requires touching main.cpp's init sequence). Recommendation: delete, clean wins over compatibility-shim cruft.
- Phase / batch breakdown for the implementation (probably 3-4 batches: skeleton class, integration into AudioDecoder, integration into main.cpp prewarm, cleanup of audio_device_watcher).

---

## 11. References

- mpv `audio/out/ao_wasapi.c` + `ao_wasapi_changenotify.c` — the canonical WASAPI-direct pattern Tankoban is following. Live install at `C:\tools\mpv\` per `reference_mpv_install.md` (binary install — source not on disk; pattern is well-documented in `learn.microsoft.com/en-us/windows/win32/coreaudio`).
- VLC `modules/audio_output/wasapi.c` — same pattern, different code style. Cross-reference if mpv's approach has edges we hit.
- Microsoft docs: `learn.microsoft.com/en-us/windows/win32/coreaudio/device-events` (IMMNotificationClient).
- `agents/audits/comics_tankoyomi_stream_merger_smoke_findings_2026-05-15.md` — methodology template for "primary fix + regression matrix" close-out RTC (Agent 1's recent arc closure).
- Project memories: `feedback_session_lifecycle_pattern.md`, `feedback_evidence_before_analysis.md`, `feedback_one_fix_per_rebuild.md`, `feedback_hemanth_role_open_and_click.md`.

---
