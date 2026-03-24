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
    }

    // Called by video thread to get current audio position
    int64_t positionUs() const
    {
        if (!m_started.load())
            return -1;

        if (m_paused.load())
            return m_anchorPtsUs.load();

        int64_t elapsed = (nowNs() - m_anchorTimeNs.load()) / 1000;
        return m_anchorPtsUs.load() + elapsed;
    }

    bool hasStarted() const { return m_started.load(); }

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
};
