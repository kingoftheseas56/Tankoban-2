#pragma once

#include <QSharedMemory>
#include <QImage>
#include <cstdint>
#include <cstring>

// Reads BGRA frames from the sidecar's shared memory ring buffer.
// Layout matches native_sidecar/src/ring_buffer.h exactly.
class ShmFrameReader {
public:
    ShmFrameReader() = default;
    ~ShmFrameReader() { detach(); }

    bool attach(const QString& shmName, int slotCount, int slotBytes);
    void detach();
    bool isAttached() const { return m_data != nullptr; }

    struct Frame {
        uint64_t frameId = 0;
        int64_t  ptsUs   = 0;
        int      width   = 0;
        int      height  = 0;
        int      stride  = 0;
        const uint8_t* pixels = nullptr;
        bool     valid   = false;
    };

    // Read the latest frame (newest valid frame in the ring).
    Frame readLatest();

    // Read the audio clock position from the SHM header.
    int64_t readClockUs() const;

    // Write back the last frame_id we displayed (feedback for decode throttling).
    void writeConsumerFid(uint64_t fid);

private:
    // Ring buffer layout constants (must match ring_buffer.h)
    static constexpr int HEADER_SIZE    = 64;
    static constexpr int SLOT_META_SIZE = 64;

    static constexpr int OFF_LATEST_FRAME = 24;
    static constexpr int OFF_CLOCK_US     = 32;
    static constexpr int OFF_CONSUMER_FID = 40;

    static constexpr int SM_FRAME_ID  = 0;
    static constexpr int SM_PTS_US    = 8;
    static constexpr int SM_WIDTH     = 16;
    static constexpr int SM_HEIGHT    = 20;
    static constexpr int SM_STRIDE    = 24;
    static constexpr int SM_VALID     = 36;

    uint8_t* m_data       = nullptr;
    int      m_slotCount  = 0;
    int      m_slotBytes  = 0;
    uint64_t m_lastFrameId = 0;

#ifdef Q_OS_WIN
    void*    m_hMapFile   = nullptr;
#else
    QSharedMemory m_shm;
#endif
};
