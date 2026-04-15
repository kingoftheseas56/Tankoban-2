#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

// Wall-clock-anchored A/V sync master clock driven by audio playback.
//
// The AudioDecoder calls update() when it outputs audio frames to the device.
// The first call anchors the clock; from that moment position_us() advances
// continuously by real wall time rather than waiting for the next audio PTS.
//
// Direct port of Python AVSyncClock from __init__.py lines 568-712.
class AVSyncClock {
public:
    AVSyncClock();

    // -- producer API (AudioDecoder calls these) --

    // Report the latest audio PTS (microseconds).
    // First call anchors the wall clock. Later calls only re-anchor when
    // audio jumps forward by more than SEEK_FORWARD_US (unexpected gap).
    // Backward diffs are ignored (normal decode lag).
    void update(int64_t position_us);

    void set_paused(bool paused);
    void set_rate(double rate);
    void reset();

    // Set PortAudio output latency so the clock accounts for the delay
    // between writing audio and it reaching the speakers.
    void set_output_latency(double seconds);

    // Set additional user-configurable audio delay in milliseconds.
    // Positive values delay audio (compensate for hidden Bluetooth/HDMI latency
    // that PortAudio's outputLatency doesn't account for).
    // Negative values pull audio earlier (rare).
    // Applied as an offset in position_us() — takes effect immediately,
    // no re-anchor needed.
    void set_extra_latency_ms(int ms);

    // Force-reset the clock anchor after an explicit seek.
    void seek_anchor(int64_t position_us);

    // -- consumer API (VideoDecoder / time_update reads these) --

    int64_t position_us() const;
    bool    started() const;
    bool    paused() const;
    double  rate() const;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;

    static constexpr int64_t SEEK_FORWARD_US = 200000;  // 200ms

    mutable std::mutex lock_;
    bool      paused_   = false;
    double    rate_     = 1.0;
    int64_t   anchor_pts_us_ = 0;
    TimePoint anchor_time_;
    bool      started_  = false;
    int64_t   output_latency_us_ = 0;
    std::atomic<int64_t> extra_latency_us_{0};  // user-configurable audio delay
};
