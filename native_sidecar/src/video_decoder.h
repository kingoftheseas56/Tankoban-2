#pragma once

#include "av_sync_clock.h"
#include "ring_buffer.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class FilterGraph;
class GpuRenderer;
class SubtitleRenderer;

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// Callback for decoder events (first_frame, eof, error).
// Called from the decode thread — must be thread-safe.
using DecoderEventCb = std::function<void(const std::string& event,
                                          const std::string& detail)>;

class VideoDecoder {
public:
    // ring_writer must outlive the decoder. clock may be null (video-only mode).
    // slot_bytes is the max pixel data per SHM slot (for overflow guard).
    VideoDecoder(FrameRingWriter* ring_writer, DecoderEventCb on_event,
                 AVSyncClock* clock = nullptr, int slot_bytes = 0,
                 SubtitleRenderer* sub_renderer = nullptr,
                 FilterGraph* video_filter = nullptr,
                 GpuRenderer* gpu_renderer = nullptr);
    ~VideoDecoder();

    // Start decode from `path` at `start_seconds`. Non-blocking.
    void start(const std::string& path, double start_seconds, int video_stream_index,
               const std::vector<int>& sub_stream_indices = {});

    // Set the active subtitle stream index (-1 = none, -2 = external sub).
    void set_active_sub_stream(int idx) { active_sub_stream_.store(idx); }

    // Disable hardware acceleration for this decoder instance.
    void set_hwaccel_disabled(bool disabled) { hwaccel_disabled_ = disabled; }

    // Query whether hardware acceleration ended up active for this decode session.
    bool hw_accel_active() const { return hw_accel_active_.load(std::memory_order_relaxed); }

    // Request stop and join decode thread (blocks up to ~5 s).
    void stop();

    // Is the decode thread running?
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Seek to a position (seconds). Thread-safe.
    void seek(double position_sec);

    // Pause/resume (holds video in place during A/V sync wait).
    void pause()  { paused_.store(true); }
    void resume() { paused_.store(false); }

    // Zero-copy short-circuit (Holy Grail). When true AND HW-decoded AND
    // no subtitle blending is required, skip hwframe_transfer + sws_scale +
    // SHM write. Producer per-frame cost drops from ~20ms to ~1ms.
    void set_zero_copy_active(bool v) { zero_copy_active_.store(v); }

    // Frame stepping: decode and display exactly one frame, then re-pause.
    void step_forward();
    void step_backward(double current_pos_sec);

private:
    void decode_thread_func(std::string path, double start_seconds, int video_stream_index);

    FrameRingWriter*    ring_writer_;
    DecoderEventCb      on_event_;
    AVSyncClock*        clock_ = nullptr;
    int                 slot_bytes_ = 0;
    SubtitleRenderer*   sub_renderer_ = nullptr;
    FilterGraph*        video_filter_ = nullptr;
    GpuRenderer*        gpu_renderer_ = nullptr;
    bool                hwaccel_disabled_ = false;
    std::atomic<bool>   hw_accel_active_{false};
    std::vector<int>    sub_stream_indices_;
    std::atomic<int>    active_sub_stream_{-1};

    std::thread         thread_;
    std::atomic<bool>   stop_flag_{false};
    std::atomic<bool>   running_{false};
    std::atomic<bool>   paused_{false};
    std::atomic<bool>   zero_copy_active_{false};

    // Seek request (pending)
    std::mutex          seek_mutex_;
    std::atomic<bool>   seek_pending_{false};
    double              seek_target_sec_ = 0.0;

    // Frame step (pending)
    std::atomic<bool>   step_pending_{false};
    std::atomic<bool>   step_back_pending_{false};
    double              step_back_target_sec_ = 0.0;
};
