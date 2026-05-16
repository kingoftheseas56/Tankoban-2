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
