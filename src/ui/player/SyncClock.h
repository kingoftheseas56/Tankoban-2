#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

// Audio-master A/V sync clock.
// Audio thread calls update() after writing samples to the device.
// Video thread calls positionUs() to know where audio is, then syncs frames.
class SyncClock {
public:
    void start()
    {
        m_running.store(true);
    }

    void stop()
    {
        m_running.store(false);
    }

    bool isRunning() const { return m_running.load(); }

    // Called by audio thread after writing samples to device
    void update(int64_t ptsUs)
    {
        m_anchorPtsUs.store(ptsUs);
        m_anchorTimeNs.store(nowNs());
        m_started.store(true);
    }

    // Called by audio thread on seek
    void seekAnchor(int64_t ptsUs)
    {
        m_anchorPtsUs.store(ptsUs);
        m_anchorTimeNs.store(nowNs());
        // Batch 1.3 — drift history is meaningless across a seek; reset the
        // EMA and return velocity to nominal so the next sustained-lag
        // window has to accumulate fresh samples before re-dropping.
        m_latencyEmaMs.store(0.0);
        m_clockVelocity.store(1.0);
    }

    // Called by video thread to get current audio position
    int64_t positionUs() const
    {
        if (!m_started.load())
            return -1;

        if (m_paused.load())
            return m_anchorPtsUs.load();

        int64_t elapsed = (nowNs() - m_anchorTimeNs.load()) / 1000;
        // Batch 1.3 — apply clock velocity to interpolated elapsed time.
        // Velocity < 1.0 under sustained render lag pulls the reported
        // position back, giving future Phase 4 consumers (frame selection
        // via readBestForClock + audio-speed command to sidecar) a single
        // coherent signal. Cast to double for the multiply, back to int64.
        const double velocity = m_clockVelocity.load();
        if (velocity != 1.0) {
            elapsed = static_cast<int64_t>(
                static_cast<double>(elapsed) * velocity);
        }
        return m_anchorPtsUs.load() + elapsed;
    }

    bool hasStarted() const { return m_started.load(); }

    // Batch 1.1/1.3 (Phase 1) — Frame-latency feedback channel.
    // FrameCanvas calls reportFrameLatency after each Present with how far
    // past the expected vblank the frame actually landed. Internally this
    // feeds an exponential moving average of recent latency; getClockVelocity
    // exposes the derived clock-rate adjustment (1.0 ± 0.005 = ±0.5%).
    //
    // Today the only consumer of getClockVelocity is positionUs() itself
    // (which is dead-pathed — FrameCanvas uses readLatest, not
    // readBestForClock). Phase 4 lights this up: a new sidecar
    // sendSetAudioSpeed(velocity) command will drive the audio-resampler
    // drift correction (Kodi ActiveAE ±5% pattern), and FrameCanvas will
    // migrate from readLatest to readBestForClock(positionUs()).
    //
    // Scaling choice: 5ms sustained EMA latency → 0.5% slowdown floor.
    // Linear for simplicity; full gain tuning is a Phase 4 concern once
    // the loop is closed.
    void reportFrameLatency(double latencyMs)
    {
        m_lastFrameLatencyMs.store(latencyMs);

        // Discontinuity guard: pause/minimize/sleep can land a render tick
        // arbitrarily late vs the previous one, and treating that as drift
        // would poison the EMA for ~20 frames of recovery. Anything above
        // 500ms is clearly a gap event, not real drift — skip the EMA.
        if (latencyMs < 500.0) {
            constexpr double kAlpha = 0.05;  // ~20-sample timescale
            const double prev = m_latencyEmaMs.load();
            const double next = prev * (1.0 - kAlpha) + latencyMs * kAlpha;
            m_latencyEmaMs.store(next);
        }

        // Derive velocity from current EMA. Clamp adjustment to [0, 0.005]
        // so velocity ∈ [0.995, 1.000]. Lag only pulls velocity DOWN
        // (late frames = slow audio so video catches up); we don't
        // speed up if video is ever ahead of schedule, because our
        // latency signal is one-sided (clamp(intervalMs - expectedMs, 0)
        // in FrameCanvas — we never report negative latency).
        //
        // Batch 1.3 hotfix 2026-04-14: initial scaling (adj = ema / 1000)
        // bottomed out at the floor during normal Windows playback because
        // occasional missed vsyncs kept the EMA around 4-5ms steady — not
        // a real drift signal. Introduced a 5ms noise floor so "normal with
        // hiccups" sits at velocity 1.000, and gentled the slope so the
        // clamp is only reached at ~20ms sustained EMA (genuine drift).
        const double ema = m_latencyEmaMs.load();
        constexpr double kNoiseFloorMs = 5.0;
        double adj = (ema - kNoiseFloorMs) / 3000.0;
        if (adj > 0.005) adj = 0.005;
        if (adj < 0.0)   adj = 0.0;
        m_clockVelocity.store(1.0 - adj);
    }

    double lastFrameLatencyMs() const
    {
        return m_lastFrameLatencyMs.load();
    }

    // Batch 1.3 — derived clock rate adjustment. 1.0 = nominal;
    // [0.995, 1.000] range under sustained render lag. Read from any
    // thread (std::atomic<double>). Phase 4 consumer will forward this
    // to the sidecar as an audio-speed command.
    double getClockVelocity() const
    {
        return m_clockVelocity.load();
    }

    // Batch 1.3 — EMA exposed for Agent 6 verification / debug telemetry.
    // Not intended as a control signal (use getClockVelocity for that).
    double latencyEmaMs() const
    {
        return m_latencyEmaMs.load();
    }

    void setPaused(bool p)
    {
        if (p) {
            // Freeze at current interpolated position
            int64_t pos = positionUs();
            m_anchorPtsUs.store(pos);
            m_paused.store(true);
        } else {
            // Re-anchor wall clock
            m_anchorTimeNs.store(nowNs());
            m_paused.store(false);
        }
    }

private:
    static int64_t nowNs()
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    std::atomic<bool>    m_running{false};
    std::atomic<bool>    m_started{false};
    std::atomic<bool>    m_paused{false};
    std::atomic<int64_t> m_anchorPtsUs{0};
    std::atomic<int64_t> m_anchorTimeNs{0};
    std::atomic<double>  m_lastFrameLatencyMs{0.0};  // Batch 1.1
    std::atomic<double>  m_latencyEmaMs{0.0};        // Batch 1.3 — smoothed lag
    std::atomic<double>  m_clockVelocity{1.0};       // Batch 1.3 — [0.995, 1.000]
};
