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

#include <portaudio.h>

// Callback for audio events (audio_ready, eof, error).
// Called from the audio thread — must be thread-safe.
using AudioEventCb = std::function<void(const std::string& event,
                                        const std::string& detail)>;

class FilterGraph;

class AudioDecoder {
public:
    // prewarmed_stream: optional pre-opened PortAudio stream. When non-null,
    // AudioDecoder skips Pa_OpenStream / Pa_CloseStream (avoiding the 5+ second
    // Bluetooth cold-start on first file open) and resamples all source audio
    // to the prewarmed stream's fixed format (48kHz stereo).
    // prewarmed_latency: actual outputLatency reported by PortAudio for the
    // prewarmed stream — used to anchor the A/V sync clock correctly.
    // bind_generation: snapshot of audio_device_watcher::g_audio_reroute_generation
    // captured at the time `prewarmed_stream` was bound to its WASAPI default
    // device. The audio thread uses it as the baseline for hot-device-reroute
    // detection — if the live counter advances past `bind_generation`, the
    // stream is stale and gets rebuilt on the new default. Pass 0 (default)
    // when prewarmed_stream is null; the audio thread re-reads the live
    // counter on its own lazy-open path.
    AudioDecoder(AVSyncClock* clock, VolumeControl* volume, AudioEventCb on_event,
                 FilterGraph* audio_filter = nullptr,
                 PaStream* prewarmed_stream = nullptr,
                 double prewarmed_latency = 0.0,
                 uint32_t bind_generation = 0);
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
    // Main-app's SyncClock derives a clock velocity from per-frame
    // render latency; the main app forwards it via the new
    // `set_audio_speed` JSON command, which ultimately lands here.
    // The audio thread polls speed_ before each swr_convert and
    // applies swr_set_compensation on change. Range clamped to
    // [0.95, 1.05] to match Kodi ActiveAE m_maxspeedadjust. Atomic
    // because the caller runs on the main-app command thread while
    // the reader is our audio decode thread.
    void set_speed(double speed);

    // Batch 4.3 — Player Polish Phase 4 Dynamic Range Compression.
    // Simple soft-knee feed-forward compressor applied post-volume
    // in the audio thread: threshold -12 dB, ratio 3:1, attack 10 ms,
    // release 100 ms. "Loud movie at low volume keeps dialogue
    // audible" — compresses explosions / music peaks so mid-tone
    // dialogue stays present at conservative volumes. Off by default;
    // user toggles via EqualizerPopover's DRC checkbox. Atomic for
    // the same thread-crossing reason as speed_ above.
    void set_drc_enabled(bool on);

private:
    void audio_thread_func(std::string path, double start_seconds, int audio_stream_index);

    // AUDIO_HOT_DEVICE_REROUTE — open a fresh PortAudio stream on the
    // current WASAPI default and atomically swap into active_stream_.
    // Closes any decoder-owned previous stream (via *pa_stream_owned_inout);
    // never touches a prewarmed stream (main.cpp owns those). Audio thread
    // only — relies on stream_mutex_ to keep flush_queue/seek/stop coherent.
    // Returns false on Pa_OpenStream / Pa_StartStream failure; caller
    // emits AUDIO_DEVICE_LOST + bails to cleanup.
    bool rebuild_for_new_default(int sample_rate, int out_channels,
                                 double& actual_latency,
                                 PaStream** pa_stream_owned_inout,
                                 double elapsed_ms);

    AVSyncClock*   clock_;
    VolumeControl* volume_;
    AudioEventCb   on_event_;
    FilterGraph*   audio_filter_ = nullptr;
    PaStream*      prewarmed_stream_  = nullptr;
    double         prewarmed_latency_ = 0.0;
    // AUDIO_HOT_DEVICE_REROUTE — generation at which prewarmed_stream_ was
    // bound (passed in by main.cpp). The audio thread uses it as the
    // baseline for its first reroute-detection poll; on lazy-open, the
    // audio thread overrides observed_reroute_generation_ with the value
    // it read pre-Pa_OpenStream (Option-B race policy: a reroute that
    // races our open produces at most ONE spurious rebuild on first poll,
    // which is preferable to missing a real reroute).
    uint32_t       bind_generation_ = 0;
    // AUDIO_HOT_DEVICE_REROUTE — last observed value of
    // audio_device_watcher::g_audio_reroute_generation. Initialized at
    // audio_thread_func entry from bind_generation_; overridden after a
    // successful lazy-open with the value captured pre-Pa_OpenStream.
    // Audio thread only — never accessed from other threads.
    uint32_t       observed_reroute_generation_ = 0;

    std::thread         thread_;
    std::atomic<bool>   stop_flag_{false};
    std::atomic<bool>   running_{false};

    // Pause support via condition variable (no poll-based wait)
    std::mutex              pause_mutex_;
    std::condition_variable pause_cv_;
    std::atomic<bool>       paused_{false};

    // The current implementation has no separate software packet/sample
    // queue; pending post-stall audio lives in PortAudio's device buffer.
    // flush_queue() aborts/restarts this stream to drop already-accepted
    // samples before resume.
    std::mutex              stream_mutex_;
    PaStream*               active_stream_ = nullptr;

    // Seek request
    std::mutex          seek_mutex_;
    std::atomic<bool>   seek_pending_{false};
    double              seek_target_sec_ = 0.0;

    // Batch 4.1 — current requested playback speed from main-app's
    // SyncClock velocity signal. 1.0 = nominal; [0.95, 1.05] range;
    // read on the audio thread before each swr_convert.
    std::atomic<double> speed_{1.0};

    // Batch 4.3 — Dynamic Range Compression toggle. Audio thread
    // polls per-chunk; compressor state (envelope follower + attack/
    // release coefficients) lives as thread-local vars inside
    // audio_thread_func.
    std::atomic<bool> drc_enabled_{false};
};
