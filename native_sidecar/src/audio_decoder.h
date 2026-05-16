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
