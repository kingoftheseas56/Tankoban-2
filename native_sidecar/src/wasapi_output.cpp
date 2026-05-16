#include "wasapi_output.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

// WASAPI_DIRECT_AUDIO Task 7 (2026-05-15): KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
// is declared extern in MinGW's ksmedia.h; its definition lives in ksuser.
// We link ksuser in CMakeLists.txt so the canonical symbol resolves at
// link time, removing the Task-2 local DEFINE_GUID workaround.

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
// Owner pointer is cleared via detach_owner() during destruction AFTER
// UnregisterEndpointNotificationCallback returns — that function is
// documented as synchronous, so no in-flight callback can race the
// detach.
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

double WasapiOutput::current_latency_sec() const { return latency_sec_.load(std::memory_order_acquire); }
bool WasapiOutput::is_active() const { return active_.load(); }

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
    double latency_sec_resolved = 0.0;
    if (SUCCEEDED(audio_client_->GetStreamLatency(&stream_latency_100ns))) {
        latency_sec_resolved = static_cast<double>(stream_latency_100ns) / 1e7;
    }
    // Some Windows audio drivers report 0 from GetStreamLatency (observed
    // on laptop internal-speaker driver during WASAPI_DIRECT_AUDIO Task 6
    // smoke). Fall back to a buffer-duration estimate so the AV-sync clock
    // gets a non-zero device-latency anchor.
    if (latency_sec_resolved <= 0.0 && buffer_frames_ > 0 && sample_rate > 0) {
        latency_sec_resolved = static_cast<double>(buffer_frames_) / sample_rate;
    }
    latency_sec_.store(latency_sec_resolved, std::memory_order_release);

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
