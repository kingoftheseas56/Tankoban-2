#include "av_sync_clock.h"

AVSyncClock::AVSyncClock()
    : anchor_time_(Clock::now())
{}

void AVSyncClock::update(int64_t position_us) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!started_) {
        anchor_pts_us_ = position_us;
        // Compensate for output latency: audio written now won't be audible
        // for output_latency_us_ microseconds. Setting anchor_time_ into the
        // future makes position_us() return this PTS only when the audio
        // actually reaches the speakers.
        anchor_time_ = Clock::now() + std::chrono::microseconds(output_latency_us_);
        started_ = true;
        return;
    }

    // Detect only unexpected *forward* jumps (e.g. a gap in the stream).
    int64_t current_us;
    if (!paused_) {
        auto elapsed = std::chrono::duration<double>(Clock::now() - anchor_time_).count();
        current_us = anchor_pts_us_ + static_cast<int64_t>(elapsed * rate_ * 1000000.0);
    } else {
        current_us = anchor_pts_us_;
    }

    int64_t diff = position_us - current_us;
    if (diff > SEEK_FORWARD_US) {
        // Audio jumped unexpectedly far forward — re-anchor.
        anchor_pts_us_ = position_us;
        anchor_time_ = Clock::now();
    }
}

void AVSyncClock::set_paused(bool paused) {
    std::lock_guard<std::mutex> guard(lock_);
    if (paused == paused_) return;

    if (paused && started_) {
        // Freeze at current interpolated position.
        auto elapsed = std::chrono::duration<double>(Clock::now() - anchor_time_).count();
        anchor_pts_us_ += static_cast<int64_t>(elapsed * rate_ * 1000000.0);
        anchor_time_ = Clock::now();
    } else if (!paused && started_) {
        // Resume: re-anchor time to now.
        anchor_time_ = Clock::now();
    }
    paused_ = paused;
}

void AVSyncClock::set_rate(double rate) {
    std::lock_guard<std::mutex> guard(lock_);
    if (started_ && !paused_) {
        auto elapsed = std::chrono::duration<double>(Clock::now() - anchor_time_).count();
        anchor_pts_us_ += static_cast<int64_t>(elapsed * rate_ * 1000000.0);
        anchor_time_ = Clock::now();
    }
    rate_ = rate;
}

void AVSyncClock::set_output_latency(double seconds) {
    std::lock_guard<std::mutex> guard(lock_);
    output_latency_us_ = static_cast<int64_t>(seconds * 1000000.0);
}

void AVSyncClock::set_extra_latency_ms(int ms) {
    extra_latency_us_.store(static_cast<int64_t>(ms) * 1000, std::memory_order_relaxed);
}

void AVSyncClock::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    anchor_pts_us_ = 0;
    anchor_time_ = Clock::now();
    paused_ = false;
    rate_ = 1.0;
    started_ = false;
}

void AVSyncClock::seek_anchor(int64_t position_us) {
    std::lock_guard<std::mutex> guard(lock_);
    anchor_pts_us_ = position_us;
    anchor_time_ = Clock::now() + std::chrono::microseconds(output_latency_us_);
    started_ = true;
}

int64_t AVSyncClock::position_us() const {
    int64_t extra = extra_latency_us_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> guard(lock_);
    // Subtract extra_latency: pretend the clock is "behind" by this amount,
    // so video paces to a later wall-clock time and waits for slow audio
    // (Bluetooth/HDMI hidden latency).
    if (!started_ || paused_) return anchor_pts_us_ - extra;
    auto elapsed = std::chrono::duration<double>(Clock::now() - anchor_time_).count();
    return anchor_pts_us_ + static_cast<int64_t>(elapsed * rate_ * 1000000.0) - extra;
}

bool AVSyncClock::started() const {
    std::lock_guard<std::mutex> guard(lock_);
    return started_;
}

bool AVSyncClock::paused() const {
    std::lock_guard<std::mutex> guard(lock_);
    return paused_;
}

double AVSyncClock::rate() const {
    std::lock_guard<std::mutex> guard(lock_);
    return rate_;
}
