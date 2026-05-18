# WASAPI Direct Audio Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PortAudio's audio-device-submission layer in the native sidecar with direct WASAPI usage, closing the Bluetooth-hot-plug bug class permanently. Audio quality stack (resampler / DRC / A/V clock / volume) remains untouched.

**Architecture:** New `WasapiOutput` class owns one `IAudioClient` + `IAudioRenderClient` + `IMMNotificationClient`. Default-device changes (BT connect, USB plug, taskbar toggle) trigger internal reactivation, invisible to caller. `AudioDecoder::audio_thread_func` swaps its `Pa_*` calls for `WasapiOutput::*` calls. `main.cpp`'s startup prewarm becomes a `WasapiOutput::open()` call. The sidecar `audio_device_watcher.{h,cpp}` file is deleted entirely — its responsibilities move into `WasapiOutput`. The Qt-side `AudioDeviceWatcher` (per-device audio-delay recall, separate feature) is untouched.

**Tech Stack:** C++20, Windows WASAPI (`audioclient.h`, `mmdeviceapi.h`), MinGW build chain for native sidecar, no new system dependencies (WASAPI is part of Windows; `ole32` already linked).

**Spec reference:** [docs/superpowers/specs/2026-05-15-wasapi-direct-audio-design.md](../specs/2026-05-15-wasapi-direct-audio-design.md)

**Rollout:** One-bang cutover. No env-gated dual-path. End-of-arc smoke matrix only.

**Brotherhood role:** Agent 3 (Video Player). Hemanth-driven mode.

---

## File Structure

**Files to create:**

| Path | Responsibility |
|---|---|
| `native_sidecar/src/wasapi_output.h` | `WasapiOutput` class declaration: public API (`open` / `write` / `set_paused` / `flush` / `current_latency_sec` / `is_active`), Pimpl-style internals declared inline (no separate impl class header) |
| `native_sidecar/src/wasapi_output.cpp` | Full implementation: COM init, `IMMDeviceEnumerator` + `IMMNotificationClient` registration, `IAudioClient` lifecycle, event-driven `write()` loop, internal reactivation on default-device-change, silent-mode fallback |

**Files to modify:**

| Path | Change |
|---|---|
| `native_sidecar/src/audio_decoder.h` | Replace `PaStream* prewarmed_stream`/`prewarmed_latency`/`bind_generation` constructor params + members with `WasapiOutput* wasapi` constructor param. Remove `rebuild_for_new_default()` method declaration. Remove `bind_generation_` / `observed_reroute_generation_` members. Remove `prewarmed_stream_` / `prewarmed_latency_` members. Remove `active_stream_` member. Remove `stream_mutex_` member. Add `m_wasapi` pointer member. |
| `native_sidecar/src/audio_decoder.cpp` | Replace all `Pa_*` calls with `m_wasapi->*` calls. Add `ScopedComInit` at top of `audio_thread_func` (mirrors existing pattern from `audio_device_watcher.cpp:43`). Delete `rebuild_for_new_default()` definition entirely. Delete the reroute-detection block at lines 970-984 (replaced by transparent reactivation inside `WasapiOutput::write()`). Delete the prewarm-reuse branch in audio open. |
| `native_sidecar/src/main.cpp` | Replace `audio_device_watcher::init()` call with `WasapiOutput` construction. Replace startup `Pa_OpenStream` prewarm with `WasapiOutput::open(48000, 2)`. Pass `WasapiOutput*` to each `AudioDecoder` instance. Remove `Pa_Initialize` / `Pa_Terminate` calls. Remove `audio_device_watcher::shutdown()` call. |
| `native_sidecar/CMakeLists.txt` | Add `src/wasapi_output.cpp` to source list. Remove `src/audio_device_watcher.cpp` from source list. Remove `find_path(PORTAUDIO_INCLUDE_DIR ...)` + `find_library(PORTAUDIO_LIBRARY ...)` + their inclusion in `target_link_libraries` (only if no other component still links PortAudio — verify Step 7.1). Confirm `ole32` stays linked (already linked for existing IMMDevice work). |
| `native_sidecar/build.ps1` | Remove `libportaudio.dll` from the DLL deployment list. |

**Files to delete:**

| Path | Reason |
|---|---|
| `native_sidecar/src/audio_device_watcher.h` | Functionality moves into `WasapiOutput` (the IMMNotificationClient lives inside the new class, generation-counter pattern obsolete). |
| `native_sidecar/src/audio_device_watcher.cpp` | Same. |

**Files explicitly untouched:**

- `src/ui/player/AudioDeviceWatcher.h/cpp` (Qt side, per-device audio-delay recall — separate feature)
- `src/ui/player/VideoPlayer.cpp` (no Qt-side audio API changes)
- `src/ui/player/SidecarProcess.cpp` (no IPC contract changes; `AUDIO_DEVICE_STARTUP_FAILED` error event preserved exactly)
- All `src/core/manga/*`, `src/core/torrent/*`, `src/core/stream/*` (different domains)

---

## Task 1: WasapiOutput skeleton (header + ctor/dtor + IMMNotificationClient)

**Files:**
- Create: `native_sidecar/src/wasapi_output.h`
- Create: `native_sidecar/src/wasapi_output.cpp`

**Goal:** Standalone-compilable skeleton. The class can be constructed and destructed; `IMMNotificationClient` registers and flips the `device_changed_` atomic on `OnDefaultDeviceChanged`. `open()`/`write()`/`set_paused()`/`flush()` are stubs returning safe defaults. Nothing else in the codebase calls this yet.

- [ ] **Step 1.1: Create `native_sidecar/src/wasapi_output.h`**

```cpp
#pragma once

// WasapiOutput — direct WASAPI shared-mode audio output for the native
// sidecar. Replaces PortAudio's device-submission layer. Owns one
// IAudioClient + IAudioRenderClient and rebuilds them internally when
// Windows' default audio render endpoint changes (BT connect, USB plug,
// taskbar toggle). The audio decoder calls write() / set_paused() /
// flush() and never has to know about device transitions.
//
// Design: docs/superpowers/specs/2026-05-15-wasapi-direct-audio-design.md
// Replaces: PortAudio + audio_device_watcher's atomic-generation pattern.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#ifdef _WIN32
struct IMMDeviceEnumerator;
struct IAudioClient;
struct IAudioRenderClient;
typedef void* HANDLE;
#endif

class WasapiOutput {
public:
    WasapiOutput();
    ~WasapiOutput();

    WasapiOutput(const WasapiOutput&) = delete;
    WasapiOutput& operator=(const WasapiOutput&) = delete;

    // Open audio output against the current Windows default render endpoint
    // (eMultimedia role) in SHARED mode. Format: 32-bit float interleaved,
    // sample_rate Hz, `channels` channels (1 or 2; we always feed stereo).
    // Returns true on success. On failure (no audio devices, COM dead,
    // IAudioClient::Initialize rejects), the object stays constructed in
    // silent-mode: write() returns true and discards samples; future
    // device-change events still trigger reactivation attempts.
    bool open(int sample_rate, int channels);

    // Blocking write of `frames` float-interleaved samples (frames *
    // channels floats total). Returns true on normal completion (including
    // silent-mode discard — caller can't distinguish, by design). Returns
    // false only on terminal failures the caller should escalate.
    bool write(const float* samples, std::size_t frames);

    // Pause / resume the underlying IAudioClient. Idempotent.
    void set_paused(bool paused);

    // Drop any data queued in the device buffer (Stop + Reset + Start).
    // Mirrors AudioDecoder::flush_queue's existing Abort+Restart pattern.
    void flush();

    // Currently-bound device output latency in seconds; for A/V sync.
    double current_latency_sec() const;

    // True when an IAudioClient is alive; false in silent-mode fallback.
    bool is_active() const;

private:
    bool initialize_client_locked(int sample_rate, int channels);
    void close_client_locked();
    bool reactivate_locked();

#ifdef _WIN32
    class NotifyImpl;
    IMMDeviceEnumerator*  enumerator_ = nullptr;
    NotifyImpl*           notify_ = nullptr;
    IAudioClient*         audio_client_ = nullptr;
    IAudioRenderClient*   render_client_ = nullptr;
    HANDLE                event_handle_ = nullptr;
    bool                  com_owned_ = false;
#endif

    int sample_rate_ = 48000;
    int channels_ = 2;
    std::uint32_t buffer_frames_ = 0;
    std::atomic<bool> device_changed_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> paused_{false};
    std::atomic<double> latency_sec_{0.0};
    mutable std::mutex client_mutex_;
};
```

- [ ] **Step 1.2: Create `native_sidecar/src/wasapi_output.cpp` with skeleton + IMMNotificationClient**

```cpp
#include "wasapi_output.h"

#include <cstdio>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace {
bool com_init_ok(HRESULT hr) {
    return hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;
}
}  // namespace

// ---------------------------------------------------------------------------
// IMMNotificationClient subclass. The COM notification thread fires
// OnDefaultDeviceChanged whenever Windows' default render endpoint moves
// (BT connect / USB plug / taskbar toggle). We listen for eRender +
// (eConsole | eMultimedia) and set the owner's device_changed_ flag.
// eCommunications (voice-call default) is ignored — separate concern.
//
// Owner pointer is detached before unregister to keep a racing callback
// from poking a freed object.
// ---------------------------------------------------------------------------
class WasapiOutput::NotifyImpl : public IMMNotificationClient {
public:
    explicit NotifyImpl(WasapiOutput* owner) : owner_(owner) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                    ERole role,
                                                    LPCWSTR new_id) override {
        if (flow != eRender) return S_OK;
        if (role != eConsole && role != eMultimedia) return S_OK;
        char id_utf8[256] = {0};
        if (new_id) {
            int n = WideCharToMultiByte(CP_UTF8, 0, new_id, -1,
                                        id_utf8, sizeof(id_utf8) - 1,
                                        nullptr, nullptr);
            if (n <= 0) id_utf8[0] = 0;
        }
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_device_changed role=%d id='%s'\n",
                     static_cast<int>(role), id_utf8);
        WasapiOutput* owner = owner_;
        if (owner) {
            owner->device_changed_.store(true, std::memory_order_release);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    void detach_owner() { owner_ = nullptr; }

private:
    std::atomic<ULONG> ref_{1};
    WasapiOutput* owner_ = nullptr;
};

WasapiOutput::WasapiOutput() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (!com_init_ok(hr)) {
        std::fprintf(stderr,
                     "WasapiOutput: CoInitializeEx failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        return;
    }
    com_owned_ = (hr == S_OK);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr) || !enumerator_) {
        std::fprintf(stderr,
                     "WasapiOutput: CoCreateInstance(MMDeviceEnumerator) failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        enumerator_ = nullptr;
        return;
    }

    notify_ = new NotifyImpl(this);
    hr = enumerator_->RegisterEndpointNotificationCallback(notify_);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "WasapiOutput: RegisterEndpointNotificationCallback failed hr=0x%08lx\n",
                     static_cast<unsigned long>(hr));
        notify_->detach_owner();
        notify_->Release();
        notify_ = nullptr;
    } else {
        std::fprintf(stderr,
                     "WasapiOutput: IMMNotificationClient registered\n");
    }
}

WasapiOutput::~WasapiOutput() {
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        close_client_locked();
    }
    if (enumerator_ && notify_) {
        enumerator_->UnregisterEndpointNotificationCallback(notify_);
    }
    if (notify_) {
        notify_->detach_owner();
        notify_->Release();
        notify_ = nullptr;
    }
    if (enumerator_) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    if (com_owned_) {
        CoUninitialize();
        com_owned_ = false;
    }
}

// Stubs — implemented in subsequent tasks.
bool WasapiOutput::open(int, int) { return false; }
bool WasapiOutput::write(const float*, std::size_t) { return true; }
void WasapiOutput::set_paused(bool) {}
void WasapiOutput::flush() {}
double WasapiOutput::current_latency_sec() const { return latency_sec_.load(); }
bool WasapiOutput::is_active() const { return active_.load(); }

bool WasapiOutput::initialize_client_locked(int, int) { return false; }
void WasapiOutput::close_client_locked() {}
bool WasapiOutput::reactivate_locked() { return false; }

#else  // !_WIN32

// Non-Windows stub. Builds clean; never emits audio. Tankoban ships on
// Windows only, so this is just to keep the source compilable in any
// cross-platform tooling.
WasapiOutput::WasapiOutput() = default;
WasapiOutput::~WasapiOutput() = default;
bool WasapiOutput::open(int, int) { return false; }
bool WasapiOutput::write(const float*, std::size_t) { return true; }
void WasapiOutput::set_paused(bool) {}
void WasapiOutput::flush() {}
double WasapiOutput::current_latency_sec() const { return 0.0; }
bool WasapiOutput::is_active() const { return false; }
bool WasapiOutput::initialize_client_locked(int, int) { return false; }
void WasapiOutput::close_client_locked() {}
bool WasapiOutput::reactivate_locked() { return false; }

#endif  // _WIN32
```

- [ ] **Step 1.3: Add the file to the sidecar CMakeLists.txt (additive only — leave PortAudio + audio_device_watcher in place for now)**

Edit `native_sidecar/CMakeLists.txt`, find the source list (search for `audio_decoder.cpp`), and add `src/wasapi_output.cpp` next to it:

```cmake
# Existing line (find it):
    src/audio_decoder.cpp
    src/audio_device_watcher.cpp
# Add right after:
    src/wasapi_output.cpp
```

- [ ] **Step 1.4: Build verify — sidecar must compile clean**

Run: `powershell -File native_sidecar/build.ps1`
Expected: `BUILD OK` (or equivalent — the build script prints `Build complete: ...ffmpeg_sidecar.exe`). Zero warnings, zero errors. The new wasapi_output.cpp.obj line appears at ~45% of the build.

If the build fails: read the last 30 lines of the build log, fix the compile error in `wasapi_output.h` or `wasapi_output.cpp` (most likely cause: missing include, typo in HRESULT/REFIID syntax, mismatched namespace usage). Re-run.

- [ ] **Step 1.5: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 1 — wasapi_output.{h,cpp} skeleton with IMMNotificationClient registration + ctor/dtor + stub methods. Standalone-compilable; nothing else calls it yet. Build green via native_sidecar/build.ps1. Spec at docs/superpowers/specs/2026-05-15-wasapi-direct-audio-design.md.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/src/wasapi_output.h, native_sidecar/src/wasapi_output.cpp, native_sidecar/CMakeLists.txt, agents/chat.md
```

---

## Task 2: WasapiOutput::open() + close_client_locked()

**Files:**
- Modify: `native_sidecar/src/wasapi_output.cpp` (replace stubs for `open` / `close_client_locked` / `initialize_client_locked`)

**Goal:** `open()` queries Windows' current default render endpoint, activates `IAudioClient`, initializes it in shared-event-driven mode at the requested sample rate / channels, gets `IAudioRenderClient`, starts the stream. `close_client_locked()` reverses the lifecycle. Silent-mode fallback on any failure.

- [ ] **Step 2.1: Replace `WasapiOutput::open()` stub**

Replace this block:

```cpp
bool WasapiOutput::open(int, int) { return false; }
```

with:

```cpp
bool WasapiOutput::open(int sample_rate, int channels) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (!enumerator_) {
        std::fprintf(stderr,
                     "WasapiOutput::open: no IMMDeviceEnumerator (silent mode)\n");
        active_.store(false, std::memory_order_release);
        return false;
    }
    sample_rate_ = sample_rate;
    channels_    = channels;
    return initialize_client_locked(sample_rate, channels);
}
```

- [ ] **Step 2.2: Replace `WasapiOutput::initialize_client_locked()` stub**

Replace this block:

```cpp
bool WasapiOutput::initialize_client_locked(int, int) { return false; }
```

with:

```cpp
// Open an IAudioClient against the current default render endpoint
// (eMultimedia role). Called under client_mutex_. On any failure, leaves
// audio_client_ / render_client_ null and active_ = false (silent mode).
//
// AUDCLNT_STREAMFLAGS_EVENTCALLBACK — kernel event wakes us when buffer
//   has room; no busy-poll. event_handle_ is signaled by WASAPI.
// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | _SRC_DEFAULT_QUALITY — Windows
//   resamples our 48k stereo float to whatever the device wants. Keeps
//   us format-agnostic across BT/USB/HDMI device formats.
bool WasapiOutput::initialize_client_locked(int sample_rate, int channels) {
    close_client_locked();

    IMMDevice* device = nullptr;
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || !device) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='GetDefaultAudioEndpoint hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        return false;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr) || !audio_client_) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='IMMDevice::Activate hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        device->Release();
        audio_client_ = nullptr;
        return false;
    }

    WAVEFORMATEXTENSIBLE fmt = {};
    fmt.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels       = static_cast<WORD>(channels);
    fmt.Format.nSamplesPerSec  = static_cast<DWORD>(sample_rate);
    fmt.Format.wBitsPerSample  = 32;
    fmt.Format.nBlockAlign     = (fmt.Format.nChannels * fmt.Format.wBitsPerSample) / 8;
    fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
    fmt.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask          = (channels == 2)
                                  ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
                                  : SPEAKER_FRONT_CENTER;
    fmt.SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    constexpr REFERENCE_TIME kBufferDuration = 100 * 10000;   // 100 ms
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                      | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                      | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                   kBufferDuration, 0,
                                   reinterpret_cast<WAVEFORMATEX*>(&fmt),
                                   nullptr);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='IAudioClient::Initialize hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        audio_client_->Release();
        audio_client_ = nullptr;
        device->Release();
        return false;
    }

    if (!event_handle_) {
        event_handle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (!event_handle_) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='CreateEvent failed GetLastError=%lu' (silent mode)\n",
                     GetLastError());
        audio_client_->Release();
        audio_client_ = nullptr;
        device->Release();
        return false;
    }
    hr = audio_client_->SetEventHandle(event_handle_);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='SetEventHandle hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        audio_client_->Release();
        audio_client_ = nullptr;
        device->Release();
        return false;
    }

    UINT32 buf_frames = 0;
    if (SUCCEEDED(audio_client_->GetBufferSize(&buf_frames))) {
        buffer_frames_ = buf_frames;
    }

    REFERENCE_TIME stream_latency_100ns = 0;
    if (SUCCEEDED(audio_client_->GetStreamLatency(&stream_latency_100ns))) {
        latency_sec_.store(static_cast<double>(stream_latency_100ns) / 1e7,
                           std::memory_order_release);
    }

    hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr) || !render_client_) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='GetService(IAudioRenderClient) hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        audio_client_->Release();
        audio_client_ = nullptr;
        render_client_ = nullptr;
        device->Release();
        return false;
    }

    hr = audio_client_->Start();
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_open_failed reason='IAudioClient::Start hr=0x%08lx' (silent mode)\n",
                     static_cast<unsigned long>(hr));
        render_client_->Release();
        render_client_ = nullptr;
        audio_client_->Release();
        audio_client_ = nullptr;
        device->Release();
        return false;
    }

    device->Release();
    active_.store(true, std::memory_order_release);
    paused_.store(false, std::memory_order_release);

    std::fprintf(stderr,
                 "AVSYNC_DIAG audio_open_complete rate=%d ch=%d buffer_frames=%u latency=%.3fs\n",
                 sample_rate, channels, buffer_frames_,
                 latency_sec_.load(std::memory_order_acquire));
    return true;
}
```

- [ ] **Step 2.3: Replace `WasapiOutput::close_client_locked()` stub**

Replace this block:

```cpp
void WasapiOutput::close_client_locked() {}
```

with:

```cpp
// Tear down IAudioClient + IAudioRenderClient + event handle. Safe to
// call when nothing is open (idempotent). Called under client_mutex_.
void WasapiOutput::close_client_locked() {
    if (audio_client_) {
        audio_client_->Stop();
    }
    if (render_client_) {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (event_handle_) {
        CloseHandle(event_handle_);
        event_handle_ = nullptr;
    }
    buffer_frames_ = 0;
    active_.store(false, std::memory_order_release);
}
```

- [ ] **Step 2.4: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors.

If the build fails: most likely cause is a typo in a WASAPI symbol name (e.g., `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT` requires `<ksmedia.h>` which is pulled in transitively by `<audioclient.h>` but verify if compile errors mention it; if so, add `#include <ksmedia.h>` after `<audioclient.h>`). Fix and re-run.

- [ ] **Step 2.5: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 2 — WasapiOutput::open() + close_client_locked() + initialize_client_locked(). Opens IAudioClient in SHARED + EVENTCALLBACK + AUTOCONVERTPCM mode against the current default eMultimedia render endpoint at 48k stereo float; gets IAudioRenderClient; starts the stream. Silent-mode fallback on any open-time failure. Build green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/src/wasapi_output.cpp, agents/chat.md
```

---

## Task 3: WasapiOutput::write() + reactivate_locked()

**Files:**
- Modify: `native_sidecar/src/wasapi_output.cpp` (replace stubs for `write` / `reactivate_locked`)

**Goal:** `write()` is the hot path — it copies `frames` of samples into the device buffer, blocking on the WASAPI event handle when there's no room. On `device_changed_` flag flip, it calls `reactivate_locked()` to close the current client and open a fresh one against the new default — invisible to the caller.

- [ ] **Step 3.1: Replace `WasapiOutput::write()` stub**

Replace this block:

```cpp
bool WasapiOutput::write(const float*, std::size_t) { return true; }
```

with:

```cpp
// Blocking write of `frames` of float-interleaved samples. The hot path.
//
// 1. If device_changed_ flag was flipped by the notification client,
//    silently reactivate against the new default (~50-200ms gap).
// 2. If we're in silent-mode (active_ == false), discard samples and
//    return true so the caller's loop keeps running.
// 3. Otherwise, write samples into the device buffer in chunks bounded
//    by IAudioClient::GetCurrentPadding's available-space hint. When
//    full, wait on event_handle_ for the device to drain.
// 4. On AUDCLNT_E_DEVICE_INVALIDATED (BT vanished, USB unplugged),
//    trigger reactivation and continue with the remaining samples.
bool WasapiOutput::write(const float* samples, std::size_t frames) {
    std::lock_guard<std::mutex> lock(client_mutex_);

    // Device-change handling — silent reactivation against new default.
    if (device_changed_.exchange(false, std::memory_order_acq_rel)) {
        reactivate_locked();
    }

    if (!active_.load(std::memory_order_acquire) || !audio_client_ || !render_client_) {
        // Silent-mode: discard samples, return success so decoder keeps
        // decoding (audio plays into the void). Master clock continues
        // updating from PTS; user sees video keep playing.
        return true;
    }

    if (paused_.load(std::memory_order_acquire)) {
        // Caller asked us to pause; discard samples while paused.
        return true;
    }

    const float* cursor = samples;
    std::size_t remaining = frames;
    const std::uint32_t buffer_frames = buffer_frames_;
    const int channels = channels_;

    while (remaining > 0) {
        UINT32 padding = 0;
        HRESULT hr = audio_client_->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            // Active device just went away (BT yanked, USB unplugged).
            // Reactivate against whatever's the new default.
            if (!reactivate_locked()) {
                return true;   // silent-mode fallback; consume remaining
            }
            continue;
        }
        if (FAILED(hr)) {
            std::fprintf(stderr,
                         "WasapiOutput::write GetCurrentPadding failed hr=0x%08lx — silent mode\n",
                         static_cast<unsigned long>(hr));
            close_client_locked();
            return true;
        }

        const UINT32 available = (buffer_frames > padding) ? (buffer_frames - padding) : 0;
        if (available == 0) {
            // Buffer full — wait for the kernel event to fire.
            // 200 ms is a generous bound; the period is ~10 ms in shared
            // mode, so an event should arrive well within this window.
            DWORD waited = WaitForSingleObject(event_handle_, 200);
            if (waited == WAIT_TIMEOUT) continue;
            if (waited != WAIT_OBJECT_0) {
                std::fprintf(stderr,
                             "WasapiOutput::write WaitForSingleObject ret=%lu — silent mode\n",
                             waited);
                close_client_locked();
                return true;
            }
            continue;
        }

        const UINT32 to_write = static_cast<UINT32>(
            std::min<std::size_t>(remaining, available));
        BYTE* dst = nullptr;
        hr = render_client_->GetBuffer(to_write, &dst);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            if (!reactivate_locked()) return true;
            continue;
        }
        if (FAILED(hr) || !dst) {
            std::fprintf(stderr,
                         "WasapiOutput::write GetBuffer failed hr=0x%08lx — silent mode\n",
                         static_cast<unsigned long>(hr));
            close_client_locked();
            return true;
        }

        std::memcpy(dst, cursor, static_cast<std::size_t>(to_write) * channels * sizeof(float));
        hr = render_client_->ReleaseBuffer(to_write, 0);
        if (FAILED(hr)) {
            std::fprintf(stderr,
                         "WasapiOutput::write ReleaseBuffer failed hr=0x%08lx — silent mode\n",
                         static_cast<unsigned long>(hr));
            close_client_locked();
            return true;
        }

        cursor    += static_cast<std::size_t>(to_write) * channels;
        remaining -= to_write;
    }

    return true;
}
```

- [ ] **Step 3.2: Replace `WasapiOutput::reactivate_locked()` stub**

Replace this block:

```cpp
bool WasapiOutput::reactivate_locked() { return false; }
```

with:

```cpp
// Tear down the current client and open a fresh one against whatever
// Windows currently considers the eRender + eMultimedia default. Returns
// true if the new client started cleanly; false if we fell into silent
// mode (no audio device available). Called under client_mutex_.
bool WasapiOutput::reactivate_locked() {
    std::fprintf(stderr,
                 "AVSYNC_DIAG audio_reactivate_begin\n");
    close_client_locked();
    const bool ok = initialize_client_locked(sample_rate_, channels_);
    if (ok) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_reactivate_complete latency=%.3fs\n",
                     latency_sec_.load(std::memory_order_acquire));
    } else {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_reactivate_failed (silent mode until next device-change)\n");
    }
    return ok;
}
```

- [ ] **Step 3.3: Add `<algorithm>` and `<cstring>` to the includes at the top of `wasapi_output.cpp`**

Find the existing include block (just after `#include "wasapi_output.h"` at the top):

```cpp
#include "wasapi_output.h"

#include <cstdio>
```

Replace with:

```cpp
#include "wasapi_output.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
```

(`std::min` needs `<algorithm>`; `std::memcpy` needs `<cstring>`.)

- [ ] **Step 3.4: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors.

- [ ] **Step 3.5: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 3 — WasapiOutput::write() + reactivate_locked(). Event-driven write loop (WaitForSingleObject on the kernel event handle, no busy-poll). Internal reactivation on device_changed_ flag flip OR AUDCLNT_E_DEVICE_INVALIDATED — silent to the caller. Silent-mode fallback on every failure path so decoder thread never dies on audio errors. Build green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/src/wasapi_output.cpp, agents/chat.md
```

---

## Task 4: WasapiOutput lifecycle methods (set_paused, flush)

**Files:**
- Modify: `native_sidecar/src/wasapi_output.cpp` (replace stubs for `set_paused` / `flush`)

**Goal:** Pause/resume and flush behavior parity with today's PortAudio path. Pause = `IAudioClient::Stop` (audio engine stops draining the buffer). Flush = `IAudioClient::Stop + Reset + Start` (drops any pending samples in the device buffer; used on seek so old audio doesn't play after the seek lands).

- [ ] **Step 4.1: Replace `WasapiOutput::set_paused()` stub**

Replace this block:

```cpp
void WasapiOutput::set_paused(bool) {}
```

with:

```cpp
// Pause / resume the IAudioClient. Idempotent: calling pause(true) twice
// is a no-op; same for resume.
void WasapiOutput::set_paused(bool paused) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    const bool was_paused = paused_.exchange(paused, std::memory_order_acq_rel);
    if (was_paused == paused) return;
    if (!audio_client_) return;
    if (paused) {
        audio_client_->Stop();
    } else {
        audio_client_->Start();
    }
}
```

- [ ] **Step 4.2: Replace `WasapiOutput::flush()` stub**

Replace this block:

```cpp
void WasapiOutput::flush() {}
```

with:

```cpp
// Drop any data queued in the device buffer. Called on seek / flush_queue
// so old audio doesn't briefly play after the seek lands. Stop+Reset+Start.
void WasapiOutput::flush() {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (!audio_client_) return;
    audio_client_->Stop();
    audio_client_->Reset();
    if (!paused_.load(std::memory_order_acquire)) {
        audio_client_->Start();
    }
}
```

- [ ] **Step 4.3: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors.

- [ ] **Step 4.4: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 4 — WasapiOutput::set_paused() (IAudioClient::Stop/Start, idempotent) and WasapiOutput::flush() (Stop+Reset+Start, used on seek/flush_queue). The class is now feature-complete; integration into AudioDecoder + main.cpp follows in Tasks 5 and 6. Build green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/src/wasapi_output.cpp, agents/chat.md
```

---

## Task 5: Integrate WasapiOutput into AudioDecoder

**Files:**
- Modify: `native_sidecar/src/audio_decoder.h`
- Modify: `native_sidecar/src/audio_decoder.cpp`

**Goal:** AudioDecoder calls `WasapiOutput::*` methods instead of `Pa_*` functions. Deletes the reroute-detection logic at lines 970-984 (replaced by transparent reactivation inside `WasapiOutput::write()`). Deletes `rebuild_for_new_default()`. Deletes `prewarmed_stream_` / `bind_generation_` / `observed_reroute_generation_` / `active_stream_` / `stream_mutex_` members. Adds `m_wasapi` member pointer.

**Read first:** [native_sidecar/src/audio_decoder.h](../../../native_sidecar/src/audio_decoder.h) (current state, ~155 lines) and [native_sidecar/src/audio_decoder.cpp](../../../native_sidecar/src/audio_decoder.cpp) (current state, ~1058 lines). The PortAudio-touching surface is concentrated in three regions:
- Constructor params + members (audio_decoder.h:38-65, audio_decoder.cpp:69-77)
- Stream open: prewarm-reuse branch + lazy-open branch (audio_decoder.cpp:540-705)
- Main render loop: reroute poll + Pa_WriteStream (audio_decoder.cpp:961-1014)
- Cleanup: Pa_AbortStream + Pa_CloseStream (audio_decoder.cpp:1039-1044)

Plus `rebuild_for_new_default` (audio_decoder.cpp:201-280) goes away entirely.

- [ ] **Step 5.1: Rewrite `native_sidecar/src/audio_decoder.h`**

Replace the entire current file contents with:

```cpp
#pragma once

#include "av_sync_clock.h"
#include "volume_control.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

// Callback for audio events (audio_ready, eof, error).
// Called from the audio thread — must be thread-safe.
using AudioEventCb = std::function<void(const std::string& event,
                                        const std::string& detail)>;

class FilterGraph;
class WasapiOutput;

class AudioDecoder {
public:
    // wasapi: shared WasapiOutput owned by main.cpp; survives across video
    // opens. Must be non-null and already open() for audio to play; if null
    // or not is_active(), the decoder runs in silent-mode (resampling +
    // clock updates continue; samples discarded at the WasapiOutput layer).
    AudioDecoder(AVSyncClock* clock, VolumeControl* volume, AudioEventCb on_event,
                 FilterGraph* audio_filter = nullptr,
                 WasapiOutput* wasapi = nullptr);
    ~AudioDecoder();

    // Start audio decode from `path` at `start_seconds`. Non-blocking.
    // audio_stream_index: which stream to decode (-1 = best).
    void start(const std::string& path, double start_seconds, int audio_stream_index = -1);

    // Stop and join (blocks up to ~5s).
    void stop();

    bool running() const { return running_.load(std::memory_order_acquire); }

    void pause();
    void resume();
    void flush_queue();
    void seek(double position_sec);

    // Batch 4.1 — Player Polish Phase 4 A/V drift correction.
    // Main-app's SyncClock derives a clock velocity from per-frame render
    // latency; the main app forwards it via the `set_audio_speed` JSON
    // command. The audio thread polls speed_ before each swr_convert and
    // applies swr_set_compensation on change. Range clamped to [0.95, 1.05]
    // to match Kodi ActiveAE m_maxspeedadjust.
    void set_speed(double speed);

    // Batch 4.3 — Player Polish Phase 4 Dynamic Range Compression.
    // Simple soft-knee feed-forward compressor applied post-volume in the
    // audio thread: threshold -12 dB, ratio 3:1, attack 10 ms, release
    // 100 ms. Off by default; user toggles via EqualizerPopover's DRC
    // checkbox.
    void set_drc_enabled(bool on);

private:
    void audio_thread_func(std::string path, double start_seconds, int audio_stream_index);

    AVSyncClock*   clock_;
    VolumeControl* volume_;
    AudioEventCb   on_event_;
    FilterGraph*   audio_filter_ = nullptr;
    WasapiOutput*  wasapi_ = nullptr;

    std::thread         thread_;
    std::atomic<bool>   stop_flag_{false};
    std::atomic<bool>   running_{false};

    // Pause support via condition variable (no poll-based wait)
    std::mutex              pause_mutex_;
    std::condition_variable pause_cv_;
    std::atomic<bool>       paused_{false};

    // Seek request
    std::mutex          seek_mutex_;
    std::atomic<bool>   seek_pending_{false};
    double              seek_target_sec_ = 0.0;

    // Batch 4.1 — current requested playback speed.
    std::atomic<double> speed_{1.0};

    // Batch 4.3 — DRC toggle.
    std::atomic<bool> drc_enabled_{false};
};
```

What changed vs the old header:
- Constructor takes `WasapiOutput* wasapi` instead of `PaStream* prewarmed_stream` + `double prewarmed_latency` + `uint32_t bind_generation`.
- New member `wasapi_`.
- Removed members: `prewarmed_stream_`, `prewarmed_latency_`, `bind_generation_`, `observed_reroute_generation_`, `active_stream_`, `stream_mutex_`.
- Removed method declaration: `rebuild_for_new_default()`.

- [ ] **Step 5.2: Update the constructor implementation in `audio_decoder.cpp`**

Find this block (around audio_decoder.cpp:65-77):

```cpp
AudioDecoder::AudioDecoder(AVSyncClock* clock, VolumeControl* volume, AudioEventCb on_event,
                           FilterGraph* audio_filter,
                           PaStream* prewarmed_stream,
                           double prewarmed_latency,
                           uint32_t bind_generation)
    : clock_(clock)
    , volume_(volume)
    , on_event_(std::move(on_event))
    , audio_filter_(audio_filter)
    , prewarmed_stream_(prewarmed_stream)
    , prewarmed_latency_(prewarmed_latency)
    , bind_generation_(bind_generation)
{
}
```

Replace with:

```cpp
AudioDecoder::AudioDecoder(AVSyncClock* clock, VolumeControl* volume, AudioEventCb on_event,
                           FilterGraph* audio_filter,
                           WasapiOutput* wasapi)
    : clock_(clock)
    , volume_(volume)
    , on_event_(std::move(on_event))
    , audio_filter_(audio_filter)
    , wasapi_(wasapi)
{
}
```

- [ ] **Step 5.3: Add `#include "wasapi_output.h"` at the top of `audio_decoder.cpp`**

Find the existing include block at the top:

```cpp
#include "audio_decoder.h"
```

Add right after it (before the `extern "C"` block):

```cpp
#include "audio_decoder.h"

#include "wasapi_output.h"
```

- [ ] **Step 5.4: Delete `rebuild_for_new_default()` definition entirely**

Find the entire function definition at audio_decoder.cpp:201-280 (starts with `bool AudioDecoder::rebuild_for_new_default(int sample_rate, int out_channels,`, ends with the closing `}` after `return true;` and the comment about the `audio_reroute_complete` log).

Delete the entire block. Also delete the descriptive comment block immediately preceding it (audio_decoder.cpp:191-200, the one about `BLUETOOTH_HOT_PLUG_FIX 2026-05-15` / etc.).

- [ ] **Step 5.5: Delete the `#include "audio_device_watcher.h"` line in audio_decoder.cpp**

Search for `audio_device_watcher.h` in audio_decoder.cpp and delete the `#include` line. (It's near the top of the file's include block.)

- [ ] **Step 5.6: Rewrite the audio stream open section in audio_decoder.cpp**

Locate the comment block + open logic around audio_decoder.cpp:530-705. The structure today is:

1. Branch 1 (lines ~548-565): `if (prewarmed_stream_)` — reuse prewarm; emits audio_ready.
2. Branch 2 (lines ~566-705): lazy-open via `Pa_OpenStream` + retry on resampler rate mismatch.

Replace the entire branch structure with a single unified path that delegates to `WasapiOutput`. Specifically:

Find this block (it starts around audio_decoder.cpp:530 with a comment about prewarm and ends around 705 with `on_event_("audio_ready", "");`):

```cpp
    // [Existing comments about prewarm + lazy-open + race policy etc.]
    observed_reroute_generation_ = bind_generation_;

    // --- PortAudio stream: use pre-warmed if available, else open lazily ---
    PaStream* pa_stream = nullptr;
    PaStream* pa_stream_owned = nullptr;
    double actual_latency = PA_LATENCY_SEC;
    PaError pa_err = paNoError;
    if (prewarmed_stream_) {
        // [Prewarm reuse logic — Pa_GetStreamInfo etc.]
        // ...
        on_event_("audio_ready", "");
    } else {
        // [Lazy-open logic — output_params setup, Pa_OpenStream, retry on rate mismatch, Pa_StartStream]
        // ...
        on_event_("audio_ready", "");
    }
```

Replace it with:

```cpp
    // WASAPI_DIRECT_AUDIO 2026-05-15 — single unified output path. The
    // WasapiOutput (owned by main.cpp, shared across all decoder lifetimes)
    // was opened at sidecar startup against the current default device.
    // Mid-playback device changes are handled transparently inside
    // WasapiOutput::write(); the decoder thread is unaware.
    //
    // If wasapi_ is null or in silent-mode (open() failed at sidecar boot
    // or all audio devices were absent), write() returns true while
    // discarding samples. The decoder runs normally, master clock updates
    // from PTS, video plays — just silently.
    double actual_latency = wasapi_ ? wasapi_->current_latency_sec() : 0.0;
    if (wasapi_ && wasapi_->is_active()) {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_ready_signal +%.0fms (wasapi latency=%.3fs)\n",
                     ms_since(), actual_latency);
    } else {
        std::fprintf(stderr,
                     "AVSYNC_DIAG audio_ready_signal +%.0fms (silent mode — no audio device)\n",
                     ms_since());
    }
    if (clock_) clock_->set_output_latency(actual_latency);
    on_event_("audio_ready", "");
```

Note: this removes the resampler-rate retry path (which was a PortAudio-specific quirk: if Pa_OpenStream rejected the file's native rate, we'd fall back to 48k and rebuild swresample). With WASAPI's `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` flag (set in Task 2 Step 2.2), Windows handles any rate-mismatch internally — our swresample always outputs at 48k, WASAPI takes that, and the device's native rate is irrelevant to us. The retry logic is no longer needed.

The `sample_rate` variable that today gets reassigned in the lazy-open path now stays at its initially-computed value (audio_decoder.cpp:483: `int sample_rate = prewarmed_stream_ ? 48000 : in_sample_rate;`). Since we no longer have a prewarmed_stream_ concept and we want everything to be 48k (matching what WasapiOutput::open is called with in main.cpp Task 6), change that line to:

Find audio_decoder.cpp:483 (approximately):

```cpp
    int sample_rate = prewarmed_stream_ ? 48000 : in_sample_rate;
```

Replace with:

```cpp
    // Always output at 48k stereo to match WasapiOutput's open params
    // (see main.cpp WasapiOutput::open(48000, 2) at sidecar startup).
    // swresample handles rate conversion from whatever the file's native
    // rate is. The 5-second BT cold-start cost is paid at sidecar boot
    // inside WasapiOutput::open, not here.
    int sample_rate = 48000;
```

- [ ] **Step 5.7: Replace the reroute-poll block in the main render loop**

Find this block (audio_decoder.cpp:961-984, the one with the comment "AUDIO_HOT_DEVICE_REROUTE — poll the reroute counter"):

```cpp
            // AUDIO_HOT_DEVICE_REROUTE — poll the reroute counter before
            // each blocking Pa_WriteStream. If the COM notification thread
            // bumped the generation (BT connect, USB plug, HDMI switch),
            // rebuild on the new WASAPI default BEFORE writing — otherwise
            // we'd emit one final buffer to the dying device. ~50-200ms
            // audible gap during reroute matches the OS audio reroute
            // baseline; deliberately no crossfade (would require a parallel
            // stream open and is timing-fragile for negligible benefit).
            {
                const uint32_t cur_gen = audio_device_watcher::g_audio_reroute_generation.load(
                    std::memory_order_acquire);
                if (cur_gen != observed_reroute_generation_) {
                    std::fprintf(stderr,
                                 "audio: reroute observed gen=%u (was %u); rebuilding stream\n",
                                 cur_gen, observed_reroute_generation_);
                    if (!rebuild_for_new_default(sample_rate, out_channels,
                                                 actual_latency, &pa_stream_owned,
                                                 ms_since())) {
                        on_event_("error", "AUDIO_DEVICE_LOST:reroute open failed");
                        goto cleanup;
                    }
                    observed_reroute_generation_ = cur_gen;
                }
            }
```

Delete it entirely. Device-change handling moves into `WasapiOutput::write()` (Task 3 Step 3.1).

- [ ] **Step 5.8: Replace the Pa_WriteStream call in the main render loop**

Find this block (audio_decoder.cpp:986-1014, the actual write):

```cpp
            // Write to PortAudio (blocking)
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write +%.0fms pts=%.3fs\n",
                             ms_since(), pts_us / 1e6);
            }
            bool stream_missing = false;
            {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                if (!active_stream_) {
                    stream_missing = true;
                } else {
                    pa_err = Pa_WriteStream(active_stream_, out_buf.data(),
                                            static_cast<unsigned long>(converted));
                }
            }
            if (stream_missing) goto cleanup;
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write_returned +%.0fms\n", ms_since());
                first_write_logged = true;
            }
            if (pa_err != paNoError && pa_err != paOutputUnderflowed) {
                std::fprintf(stderr, "AudioDecoder: Pa_WriteStream error: %s\n",
                             Pa_GetErrorText(pa_err));
                if (pa_err != paOutputUnderflowed) {
                    on_event_("error", std::string("AUDIO_DEVICE_LOST:") + Pa_GetErrorText(pa_err));
                    goto cleanup;
                }
            }
```

Replace with:

```cpp
            // WASAPI direct — single non-throwing call. WasapiOutput handles:
            //   - Silent-mode fallback (no audio device → discard samples).
            //   - Device-change reactivation (BT connect/disconnect, USB
            //     plug, taskbar toggle) — invisible to us.
            //   - AUDCLNT_E_DEVICE_INVALIDATED recovery (BT yanked mid-play).
            // Never goto cleanup on a write — audio errors no longer kill
            // the decoder thread.
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write +%.0fms pts=%.3fs\n",
                             ms_since(), pts_us / 1e6);
            }
            if (wasapi_) {
                wasapi_->write(out_buf.data(), static_cast<std::size_t>(converted));
            }
            if (!first_write_logged) {
                std::fprintf(stderr, "AVSYNC_DIAG audio_first_pa_write_returned +%.0fms\n", ms_since());
                first_write_logged = true;
            }
```

The two `AVSYNC_DIAG audio_first_pa_write*` log lines stay (preserves diagnostic continuity; we can rename them later in a separate cleanup pass). The error event emission paths are gone.

- [ ] **Step 5.9: Replace the cleanup block**

Find this block (audio_decoder.cpp:1023-1044):

```cpp
cleanup:
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        active_stream_ = nullptr;
    }
    // Don't abort/close the pre-warmed stream — it's owned by main.cpp and
    // shared across sessions. Closing it would defeat the whole point of
    // pre-warming (we'd pay the 5s cold-start on the next file).
    //
    // AUDIO_HOT_DEVICE_REROUTE — pa_stream_owned tracks every stream THIS
    // decoder opened (initial lazy-open AND any rebuild during reroute);
    // rebuild_for_new_default closes the previous owned stream when it
    // swaps in a new one, so at cleanup time pa_stream_owned points at
    // most to the latest one. Prewarm stays bound to whatever device it
    // was opened against; main.cpp closes it on next handle_open if stale
    // or at sidecar shutdown.
    if (pa_stream_owned) {
        // Abort (not stop) for immediate silence — Pa_StopStream drains the
        // buffer which causes audible lingering after the player is closed.
        Pa_AbortStream(pa_stream_owned);
        Pa_CloseStream(pa_stream_owned);
    }
```

Replace with:

```cpp
cleanup:
    // WASAPI direct — no per-decoder audio resources to release.
    // wasapi_ is owned by main.cpp and survives across decoder lifetimes;
    // we never close it from here. Any audio that was queued inside the
    // device buffer at the moment of cleanup will be discarded naturally
    // when the next decoder calls wasapi_->flush() at its open or seek.
```

- [ ] **Step 5.10: Update the flush_queue() / seek() / pause() / resume() methods**

These methods today operate on `active_stream_` via `stream_mutex_`. They now delegate to `wasapi_`.

Find `AudioDecoder::flush_queue()`. The body today (roughly):

```cpp
void AudioDecoder::flush_queue() {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (active_stream_) {
        Pa_AbortStream(active_stream_);
        Pa_StartStream(active_stream_);
    }
}
```

Replace the body with:

```cpp
void AudioDecoder::flush_queue() {
    if (wasapi_) wasapi_->flush();
}
```

Find `AudioDecoder::pause()`. The body today (roughly):

```cpp
void AudioDecoder::pause() {
    paused_.store(true, std::memory_order_release);
    // ... existing cv signaling ...
}
```

Add to the body (right before the cv signal):

```cpp
    if (wasapi_) wasapi_->set_paused(true);
```

Find `AudioDecoder::resume()` and add to the body (right before the cv signal):

```cpp
    if (wasapi_) wasapi_->set_paused(false);
```

`seek()` doesn't directly touch the stream — it sets `seek_pending_` for the audio thread to pick up. The thread's seek-handling logic uses `flush_queue` internally (or already-flushed packet state). No change needed in `seek()` itself.

- [ ] **Step 5.11: Add `ScopedComInit` at the top of `audio_thread_func`**

WASAPI calls need COM-MTA on the calling thread. Today's PortAudio handled COM internally. After the cutover, the audio thread itself needs to be MTA-init'd so it can call `WasapiOutput::write()` (which calls IAudioClient/IAudioRenderClient methods).

Find the top of `audio_decoder.cpp:286` (the start of `audio_thread_func`):

```cpp
void AudioDecoder::audio_thread_func(
    std::string path,
    double start_seconds,
    int audio_stream_index)
{
#ifdef _WIN32
    // Pro Audio = lowest scheduler latency on Windows. Audio jitter directly
    // maps to audible glitches, so this thread gets the strongest priority
    // hint MMCSS offers. Released by RAII on any return.
    MmcssScope mmcss(L"Pro Audio");
#endif
```

Replace with:

```cpp
void AudioDecoder::audio_thread_func(
    std::string path,
    double start_seconds,
    int audio_stream_index)
{
#ifdef _WIN32
    // WASAPI_DIRECT_AUDIO — WasapiOutput's IAudioClient + IAudioRenderClient
    // calls require COM-MTA on this thread. Today's PortAudio handled COM
    // init internally; we now do it explicitly. RPC_E_CHANGED_MODE is OK —
    // means someone else already init'd, our writes still work.
    struct ScopedComInit {
        HRESULT hr;
        bool owned;
        ScopedComInit() {
            hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            owned = (hr == S_OK);
        }
        ~ScopedComInit() {
            if (owned) CoUninitialize();
        }
    } com_init;

    // Pro Audio = lowest scheduler latency on Windows. Audio jitter directly
    // maps to audible glitches, so this thread gets the strongest priority
    // hint MMCSS offers. Released by RAII on any return.
    MmcssScope mmcss(L"Pro Audio");
#endif
```

Also add `#include <objbase.h>` at the top of audio_decoder.cpp (near the other Windows includes) if not already present — provides `CoInitializeEx`.

- [ ] **Step 5.12: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors.

Likely compile errors at this stage and their fixes:
- `'PaStream' was not declared` → leftover Pa-typed local in audio_decoder.cpp. Search for `PaStream`, `pa_stream`, `Pa_` and clean up.
- `'audio_device_watcher' has not been declared` → leftover reference. Search and remove.
- Linker errors for `Pa_*` symbols → main.cpp still uses PortAudio (will be fixed in Task 6). Only fix audio_decoder.cpp linker errors now.
- `'CoInitializeEx' was not declared` → missing `#include <objbase.h>` or `#include <combaseapi.h>`. Add to the Windows include block.

- [ ] **Step 5.13: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 5 — audio_decoder.h/cpp surgery. Constructor takes WasapiOutput* instead of PaStream*/prewarmed_latency/bind_generation. rebuild_for_new_default() definition deleted (~80 LOC). Reroute-poll block at line 970-984 deleted. Pa_WriteStream replaced with WasapiOutput::write(). cleanup block simplified (no per-decoder PA resources to release). flush_queue() / pause() / resume() delegate to wasapi_. audio_thread_func gets ScopedComInit for COM-MTA. All sample-rate logic locked to 48k (WASAPI's AUTOCONVERTPCM handles device-side rate matching). Build green at sidecar level; main.cpp link errors expected and fixed in Task 6.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:receiving-code-review] | files: native_sidecar/src/audio_decoder.h, native_sidecar/src/audio_decoder.cpp, agents/chat.md
```

---

## Task 6: Integrate WasapiOutput into main.cpp prewarm path

**Files:**
- Modify: `native_sidecar/src/main.cpp`

**Goal:** Sidecar startup constructs a `WasapiOutput`, opens it at 48k stereo, and passes the pointer to each `AudioDecoder` construction. Sidecar shutdown destructs the `WasapiOutput`. The PortAudio prewarm path is removed entirely; `Pa_Initialize` / `Pa_Terminate` / `audio_device_watcher::init` / `audio_device_watcher::shutdown` calls all go away.

**Read first:** [native_sidecar/src/main.cpp](../../../native_sidecar/src/main.cpp). The PortAudio-touching surface is:
- Startup: `Pa_Initialize` + prewarm-stream open (search for `Pa_Initialize`).
- `audio_device_watcher::init()` call (search for `audio_device_watcher::`).
- `AudioDecoder` constructions: ~3 sites in `open_worker` and `set_tracks_worker` (search for `AudioDecoder*` or `new AudioDecoder`).
- Shutdown: `Pa_Terminate` + `audio_device_watcher::shutdown` (search for `Pa_Terminate`).
- `invalidate_prewarm_if_stale` calls (search; remove the call and the helper if it has no other callers).

- [ ] **Step 6.1: Add `#include "wasapi_output.h"` near the top of main.cpp**

Find the existing include block (search for `#include "audio_decoder.h"`). Add right after it:

```cpp
#include "audio_decoder.h"
#include "wasapi_output.h"
```

- [ ] **Step 6.2: Remove `#include "audio_device_watcher.h"` from main.cpp**

Search for `audio_device_watcher.h` in main.cpp and delete the `#include` line.

- [ ] **Step 6.3: Remove `Pa_Initialize`, prewarm-stream open, and `audio_device_watcher::init()` at startup**

Locate the startup block. It will contain (near the top of `main()`):

```cpp
    PaError pa_err = Pa_Initialize();
    if (pa_err != paNoError) {
        std::fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(pa_err));
        return 1;
    }
    audio_device_watcher::init();
    // ... prewarm stream open: Pa_OpenStream with default output device,
    // captures into a local PaStream* prewarmed_stream, double prewarmed_latency,
    // uint32_t bind_generation. Spans ~50-100 lines.
```

Replace the entire block (Pa_Initialize + audio_device_watcher::init + the prewarm Pa_OpenStream + its associated bookkeeping) with:

```cpp
    // WASAPI_DIRECT_AUDIO 2026-05-15 — one shared audio output for the
    // sidecar process. Opens against the current default device at sidecar
    // boot (paying the 5-second BT cold-start NOW, not on first video play).
    // Survives every video open until sidecar shutdown. Default-device
    // changes mid-process trigger internal reactivation; the decoder
    // borrowing this pointer never has to know.
    auto wasapi = std::make_unique<WasapiOutput>();
    if (!wasapi->open(48000, 2)) {
        std::fprintf(stderr,
                     "main: WasapiOutput::open failed at sidecar start — running in silent mode\n");
    }
```

The local variable name is `wasapi`. It is passed by `.get()` to each AudioDecoder construction below.

- [ ] **Step 6.4: Update AudioDecoder constructions**

Search main.cpp for `new AudioDecoder(`. There are ~3 sites (in `open_worker` and possibly `set_tracks_worker`). Each looks like:

```cpp
    AudioDecoder* adec = new AudioDecoder(&g_clock, &g_volume, on_audio_event,
                                          afilt, prewarmed_stream,
                                          prewarmed_latency, bind_generation);
```

Replace each one with:

```cpp
    AudioDecoder* adec = new AudioDecoder(&g_clock, &g_volume, on_audio_event,
                                          afilt, wasapi.get());
```

- [ ] **Step 6.5: Remove `invalidate_prewarm_if_stale` calls and helper**

Search main.cpp for `invalidate_prewarm_if_stale`. Delete:
- All call sites.
- The function definition (if defined in main.cpp).
- Any helper variables like `prewarmed_stream` / `prewarmed_latency` / `bind_generation` that were the inputs.

This was the "the prewarmed PortAudio stream's bound device is stale" check from the v1 fix. WasapiOutput handles its own staleness internally; no external invalidation needed.

- [ ] **Step 6.6: Remove `Pa_Terminate` and `audio_device_watcher::shutdown` at sidecar exit**

Locate the shutdown block. Search for `Pa_Terminate` in main.cpp. The block looks like:

```cpp
    // AUDIO_HOT_DEVICE_REROUTE — unregister IMMNotificationClient BEFORE
    // Pa_Terminate so any in-flight notification finishes before we
    // dismantle the COM state.
    audio_device_watcher::shutdown();
    Pa_Terminate();
```

Replace with:

```cpp
    // WASAPI_DIRECT_AUDIO — wasapi_ destructor (when unique_ptr falls out
    // of scope at function return) unregisters IMMNotificationClient,
    // releases IAudioClient + IAudioRenderClient, and CoUninitializes the
    // main thread's COM apartment in the correct order.
    wasapi.reset();
```

- [ ] **Step 6.7: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors.

If there are still linker errors for `Pa_*` symbols, search the rest of main.cpp for any remaining `Pa_` calls and remove them. There should be NONE left in main.cpp after this task.

- [ ] **Step 6.8: Quick functional check — sidecar starts cleanly**

Run: `out\Tankoban.exe --version` (if Tankoban supports a version flag) OR briefly start Tankoban via `build_and_run.bat` and confirm:
- No crash on startup (check `out/_player_debug.txt` if any).
- Log line `WasapiOutput: IMMNotificationClient registered` appears in `sidecar_debug_live.log` (project root).
- Log line `AVSYNC_DIAG audio_open_complete rate=48000 ch=2 ...` appears.
- No `AVSYNC_DIAG audio_open_failed` (assuming a normal audio device is present).
- No `audio_device_watcher: registered` line (the old code is gone).

Kill Tankoban after 5 seconds (don't need to play anything). Use `scripts/stop-tankoban.ps1` per Rule 17.

- [ ] **Step 6.9: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 6 — main.cpp prewarm-path surgery. Pa_Initialize + audio_device_watcher::init + Pa_OpenStream prewarm REMOVED. Replaced with WasapiOutput construction + open(48000, 2) at sidecar boot. All AudioDecoder constructions now pass wasapi.get() instead of prewarmed_stream/prewarmed_latency/bind_generation. invalidate_prewarm_if_stale call sites + helper removed. Pa_Terminate + audio_device_watcher::shutdown REMOVED at shutdown — wasapi.reset() handles ordering correctly via unique_ptr destructor. Sidecar starts clean; AVSYNC_DIAG audio_open_complete appears in log. Build green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/src/main.cpp, agents/chat.md
```

---

## Task 7: Decommission PortAudio + audio_device_watcher

**Files:**
- Modify: `native_sidecar/CMakeLists.txt`
- Modify: `native_sidecar/build.ps1`
- Delete: `native_sidecar/src/audio_device_watcher.h`
- Delete: `native_sidecar/src/audio_device_watcher.cpp`

**Goal:** Remove PortAudio from the build entirely. Delete the obsolete `audio_device_watcher.{h,cpp}` files. Confirm sidecar still builds and runs without the PortAudio DLL.

- [ ] **Step 7.1: Verify no other sidecar source still uses PortAudio**

Run: `grep -r "Pa_\|portaudio\|PaStream\|PaWasapi" native_sidecar/src/`

Expected: ZERO matches (or matches only in comments slated for removal). If any `Pa_*` call remains in a `.cpp` file, identify the file and reason — most likely it's a stray include or a comment reference. Remove the dead reference.

- [ ] **Step 7.2: Delete the audio_device_watcher files**

Run:
```
git rm native_sidecar/src/audio_device_watcher.h
git rm native_sidecar/src/audio_device_watcher.cpp
```

Or via PowerShell if `git rm` is not the preferred path (it should be — keeps git history clean): `Remove-Item native_sidecar/src/audio_device_watcher.h, native_sidecar/src/audio_device_watcher.cpp`

- [ ] **Step 7.3: Remove `audio_device_watcher.cpp` from `native_sidecar/CMakeLists.txt`**

Edit `native_sidecar/CMakeLists.txt`. Find the source list (where `src/audio_decoder.cpp` and `src/wasapi_output.cpp` are listed) and delete the `src/audio_device_watcher.cpp` line.

- [ ] **Step 7.4: Remove PortAudio from `native_sidecar/CMakeLists.txt`**

Find and delete these lines (or their equivalent):

```cmake
set(PORTAUDIO_ROOT "C:/tools/portaudio" CACHE PATH "PortAudio install prefix")
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h HINTS "${PORTAUDIO_ROOT}/include")
find_library(PORTAUDIO_LIBRARY portaudio HINTS "${PORTAUDIO_ROOT}/lib")
```

In the `target_include_directories` and `target_link_libraries` blocks, remove the `${PORTAUDIO_INCLUDE_DIR}` and `${PORTAUDIO_LIBRARY}` entries respectively.

Confirm `ole32` stays in the WIN32 link block (it's needed for `CoInitializeEx` etc. that `wasapi_output.cpp` uses — verify with a grep on CMakeLists for `ole32`).

- [ ] **Step 7.5: Remove `libportaudio.dll` from `native_sidecar/build.ps1` DLL deployment**

Edit `native_sidecar/build.ps1`. Search for `libportaudio.dll` in the DLL deployment lists (there are usually two lists: "Core DLLs" and similar). Remove the entries.

- [ ] **Step 7.6: Build verify**

Run: `powershell -File native_sidecar/build.ps1`
Expected: BUILD OK. Zero warnings, zero errors. The output ffmpeg_sidecar.exe is smaller (no PortAudio DLL deployed).

Confirm `libportaudio.dll` is NOT in `resources/ffmpeg_sidecar/`:
Run: `ls resources/ffmpeg_sidecar/ | grep -i portaudio`
Expected: NO output (or "file not found").

- [ ] **Step 7.7: Full main-app build verify (sanity check Qt-side wasn't broken by sidecar changes)**

Run: `/build-verify main` (which runs `cmake --build out --parallel --target Tankoban`)
Expected: OK. The main app links no PortAudio (it never did directly — only the sidecar binary did).

If a `libportaudio.dll` reference somehow leaked into the main app's build (unlikely but possible if a CMake configure-script left a stale dependency cache), check `out/CMakeCache.txt` and clean the build dir if needed.

- [ ] **Step 7.8: Quick sidecar startup smoke (no audio play yet)**

Same as Task 6 Step 6.8 — launch via `build_and_run.bat`, confirm:
- Sidecar starts.
- `WasapiOutput: IMMNotificationClient registered` log line appears.
- No errors related to missing PortAudio DLL.

Kill via `scripts/stop-tankoban.ps1`.

- [ ] **Step 7.9: Post READY TO COMMIT line in `agents/chat.md`**

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO Task 7 — PortAudio decommission. native_sidecar/src/audio_device_watcher.{h,cpp} DELETED. native_sidecar/CMakeLists.txt: removed PortAudio find_path + find_library + include + link; removed src/audio_device_watcher.cpp from source list. native_sidecar/build.ps1: removed libportaudio.dll from deployment list. Sidecar binary now ships without libportaudio.dll. Both sidecar AND main app builds green. Sidecar boot smoke clean. Audio pipeline is now pure WASAPI direct end-to-end; the bug class addressed by v1/v2/v3 is eliminated by construction.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /simplify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: native_sidecar/CMakeLists.txt, native_sidecar/build.ps1, agents/chat.md (audio_device_watcher.h and audio_device_watcher.cpp are deleted, not modified)
```

---

## Task 8: End-of-arc smoke matrix (Hemanth-driven)

**Files:** None. This task is human-driven; Agent 3 watches logs and posts the verification-evidence document.

**Goal:** Confirm the BT-hot-plug bug class is closed AND no audio regressions were introduced. Per Hemanth's brainstorm answer (Section 3 of spec), smoke happens only at end-of-arc.

**Setup before each smoke point:**
- Kill any running Tankoban / ffmpeg_sidecar processes (`scripts/stop-tankoban.ps1` or PowerShell `Get-Process -Name ... | Stop-Process -Force`).
- Truncate or archive `sidecar_debug_live.log` so a fresh capture is unambiguous.
- Launch via `build_and_run.bat`.

**Smoke matrix:**

- [ ] **Step 8.a: Stream-mode, BT auto-connect mid-playback (PRIMARY TARGET)**

  1. Disconnect BT from Windows.
  2. Launch Tankoban → Stream mode → start any stream-mode video.
  3. Confirm audio plays through internal speakers (10s of playback).
  4. Connect BT headphones.
  5. Wait ~3-5s.
  6. **Expected:** audio routes to BT seamlessly. No "Reconnecting player" toast. No video glitch. No restart-from-beginning.

  Verify in `sidecar_debug_live.log`:
  - `AVSYNC_DIAG audio_device_changed role=...` line appears on BT connect.
  - `AVSYNC_DIAG audio_reactivate_begin` followed by `AVSYNC_DIAG audio_reactivate_complete latency=...` lines appear.
  - NO `audio_open_failed` lines after the reactivate.

- [ ] **Step 8.b: Local-file BT-connect**

  Same as 8.a but with a local video file from disk. Audio should auto-route to BT identically.

- [ ] **Step 8.c: BT disconnect / out-of-range**

  1. Start with BT connected and audio routing to BT.
  2. Disconnect BT (or move out of range until Windows drops the connection).
  3. **Expected:** audio auto-routes to internal speakers seamlessly. No toast. No video glitch.

  Verify in `sidecar_debug_live.log`: `audio_device_changed` + `audio_reactivate_begin` + `audio_reactivate_complete` sequence fires.

- [ ] **Step 8.d: Manual taskbar toggle**

  Mid-playback (with BT connected and audio on BT), open the Windows taskbar audio output picker and switch to internal speakers, then back to BT.
  **Expected:** audio follows both transitions seamlessly. Each transition logs an `audio_device_changed` + `audio_reactivate_*` triplet.

- [ ] **Step 8.e: Workaround toggle still works (regression check)**

  The pre-fix workaround was: connect BT mid-playback → audio doesn't switch → user toggles default away then back. After the fix this is now redundant, but the path mustn't crash anything. Reproduce the workaround sequence and confirm:
  - Audio routes correctly throughout.
  - No sidecar crash ("Reconnecting player" toast must NOT appear).

- [ ] **Step 8.f: Subjective no-glitch check**

  Across all of 8.a-8.e, listen for:
  - Long silence windows (>500ms is a regression).
  - Click trains (more than one click per transition is a regression).
  - Audio underflow / glitch during the reactivation gap.

  The reactivation gap is naturally ~50-200ms (matches the existing audio-reroute baseline). One brief click per transition is acceptable.

- [ ] **Step 8.g: No-audio-device negative test**

  1. Open Windows Sound settings → Disable all audio output devices.
  2. Launch Tankoban → Stream mode → start a stream-mode video.
  3. **Expected:** sidecar starts in silent-mode. Video plays normally. No crash.
  4. Re-enable an audio output device (speakers or BT).
  5. **Expected:** audio picks up automatically within ~3-5s (next `OnDefaultDeviceChanged` event triggers reactivation).

- [ ] **Step 8.h: AV sync regression check**

  Play a video with dialogue (any film or TV with lip-sync sensitivity). Watch for 30+ seconds.
  **Expected:** lip-sync is tight, no audio drift. WasapiOutput's reported latency feeds the master clock the same way Pa_GetStreamInfo did, so sync should be unchanged.

- [ ] **Step 8.i: Pause / resume / seek regression check**

  Mid-playback, pause + resume + seek to various positions.
  **Expected:** clean transitions. No leftover audio after pause. No old audio briefly playing after seek (the WasapiOutput::flush() call in AudioDecoder::flush_queue handles this).

- [ ] **Step 8.j: Post arc-close summary in `agents/chat.md`**

Once all of 8.a through 8.i pass, post:

```
READY TO COMMIT — [Agent 3, WASAPI_DIRECT_AUDIO arc CLOSED post-smoke 2026-05-XX ~HH:MMam/pm. 8-task arc shipped end-to-end. Bug class addressed: BT auto-connect mid-playback now routes audio to BT seamlessly in BOTH stream mode AND local-file mode, no workaround toggle needed, no sidecar restart, no video glitch. PortAudio fully decommissioned (audio_device_watcher.{h,cpp} deleted, libportaudio.dll no longer deployed). Smoke matrix a-i all green: BT-connect (stream + local), BT-disconnect, taskbar toggle, workaround-path no-crash, subjective no-glitch, no-audio-device negative test, AV sync regression check, pause/resume/seek regression check. The v1/v2/v3 architectural fight with PortAudio's frozen device enumeration is over — replaced with direct WASAPI shared-mode + IMMNotificationClient that mpv and VLC use. Spec: docs/superpowers/specs/2026-05-15-wasapi-direct-audio-design.md. Plan: docs/superpowers/plans/2026-05-15-wasapi-direct-audio.md.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion, /build-verify, /superpowers:requesting-code-review, /superpowers:receiving-code-review, /superpowers:systematic-debugging] | files: agents/chat.md
```

---

## Recovery / rollback

If any task between 1-7 results in an unrecoverable break (build fails after multiple attempts, OR sidecar crashes on startup post-cutover, OR smoke matrix fails on every test):

1. **Triage which task introduced the regression.** Each task has a clean build state; if Task N built green but Task N+1 broke things, the diff between them isolates the cause.
2. **Revert Task N+1's commit** via `git revert <sha>` (Agent 0 will have batched commits, so find the relevant sweep commit and revert).
3. **File a follow-up RTC** describing what failed and what evidence (sidecar log lines, build error messages).

If Task 7 (PortAudio decommission) is the breaker, the previous state (with PortAudio still linked but the v3 silent-seal still bandaged via `PaWasapi_UpdateDeviceList`) is at least a working sidecar — just not a finished fix. Don't push the half-decommissioned state to main.

The sidecar-restart path in `VideoPlayer::onSidecarCrashed` remains untouched throughout this arc and is the last-resort safety net.

---

## Self-review

After writing this plan, against the spec:

**1. Spec coverage:**
- Section 1 problem statement → addressed by entire arc (all tasks contribute).
- Section 2 prior attempts → context for Task 5's deletion of `rebuild_for_new_default` + the v3 `PaWasapi_UpdateDeviceList` retry.
- Section 3 strategic decisions → BT-disconnect silent: Task 3's silent-mode fallback in `write()`. Prewarm kept: Task 6's `wasapi->open(48000, 2)` at sidecar boot. One-bang rollout: Tasks 5-7 happen in one arc. Smoke at end: Task 8.
- Section 4 architectural shape → Tasks 1-7 together produce exactly the "after" pipeline.
- Section 5 components → 5.1 WasapiOutput class: Tasks 1-4. 5.2 audio_decoder.cpp: Task 5. 5.3 main.cpp: Task 6. 5.4 audio_device_watcher gutted: Task 7. 5.5 Qt-side: untouched (no task — explicit non-action).
- Section 6 data flow → 6.1 steady state: Tasks 3 + 5. 6.2 mid-playback device change: Tasks 1 + 3's reactivate_locked. 6.3 cold-start prewarm: Task 6.
- Section 7 error handling → 7.1 open-time: Task 2's failure paths all log AVSYNC_DIAG and return false. 7.2 mid-playback: Task 3's AUDCLNT_E_DEVICE_INVALIDATED handler. 7.3 events: Task 5 removes AUDIO_DEVICE_LOST emission. 7.4 logging: Tasks 2 + 3 + 6 add the new AVSYNC_DIAG lines.
- Section 8 testing: Task 8.
- Section 9 out-of-scope: respected throughout.
- Section 10 rollout: respected (one-bang, end-smoke).
- Section 11 references: no task action needed (these are documentation pointers for implementers).

**2. Placeholder scan:** Searched for "TBD", "TODO", "fill in", "appropriate error handling", "similar to". One TBD-equivalent existed in Task 8.j ("2026-05-XX ~HH:MMam/pm") — that's intentional (the timestamp is unknown until the arc actually closes; implementer fills in). All code blocks contain complete code.

**3. Type / method consistency:**
- `WasapiOutput::open(int sample_rate, int channels)` declared in Task 1.1, defined in Task 2.1, called in Task 6.3 with (48000, 2) — match.
- `WasapiOutput::write(const float* samples, std::size_t frames)` declared in 1.1, defined in 3.1, called in 5.8 with `out_buf.data()` + `(std::size_t)converted` — match.
- `WasapiOutput::set_paused(bool paused)` declared in 1.1, defined in 4.1, called in 5.10's pause/resume — match.
- `WasapiOutput::flush()` declared in 1.1, defined in 4.2, called in 5.10's flush_queue — match.
- `WasapiOutput::current_latency_sec() const` declared in 1.1, defined inline in 1.2 (returns `latency_sec_.load()`), called in 5.6 (audio_open_complete log + clock_->set_output_latency) — match.
- `WasapiOutput::is_active() const` declared in 1.1, defined inline in 1.2, called in 5.6 — match.
- `AudioDecoder` constructor signature: declared in 5.1 (`WasapiOutput* wasapi = nullptr`), defined in 5.2 with matching params, called in 6.4 with `wasapi.get()` — match.
- `audio_decoder.cpp` includes `wasapi_output.h` (5.3) before using `WasapiOutput*` (5.2) — consistent.
- `main.cpp` includes `wasapi_output.h` (6.1) before constructing `WasapiOutput` (6.3) — consistent.

No type drift detected.

**4. Scope check:** This plan covers exactly one subsystem (the audio device-submission layer). Doesn't grow into video, doesn't touch UI, doesn't refactor unrelated code. Tight.

No issues found in self-review. Plan is ready.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-15-wasapi-direct-audio.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Each subagent gets the spec + plan + the single task it's responsible for; comes back with a build-green diff + RTC line. I verify each task's RTC before dispatching the next.

**2. Inline Execution** — Execute tasks in this session using `superpowers:executing-plans`, batch execution with checkpoints for review at task boundaries.

**Which approach?**
