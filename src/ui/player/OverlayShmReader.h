#pragma once

// PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — subtitle overlay SHM reader.
//
// Opens the named SHM region created by the sidecar's OverlayShm. Reads
// the atomic frame_counter + valid flag + BGRA payload. FrameCanvas polls
// this on each render tick — when the counter advances, the BGRA bytes
// are uploaded to FrameCanvas's locally-owned D3D11 overlay texture via
// UpdateSubresource; otherwise the previous upload is reused.
//
// Layout matches native_sidecar/src/overlay_shm.h exactly.

#include <QString>
#include <atomic>
#include <cstdint>

class OverlayShmReader {
public:
    OverlayShmReader() = default;
    ~OverlayShmReader() { detach(); }

    bool attach(const QString& shmName, int width, int height);
    void detach();
    bool isAttached() const { return m_data != nullptr; }

    // Current overlay state. A returned frame with `counter != lastCounter`
    // indicates fresh content; caller should re-upload + cache the new
    // counter. `valid == false` means subs are not active this frame and
    // the caller should suppress the overlay draw pass.
    struct Frame {
        uint64_t counter = 0;
        bool     valid   = false;
        int      width   = 0;
        int      height  = 0;
        const uint8_t* bgra = nullptr;  // valid until next attach/detach
    };

    // Reads the header counter + valid flag + exposes the BGRA pointer.
    // Does NOT copy the bytes — caller reads the payload directly.
    Frame read();

private:
    // Header offsets (must match overlay_shm.h)
    static constexpr int HDR_COUNTER = 0;   // u64
    static constexpr int HDR_WIDTH   = 8;   // u32
    static constexpr int HDR_HEIGHT  = 12;  // u32
    static constexpr int HDR_VALID   = 16;  // u32
    static constexpr int HDR_SIZE    = 32;

    uint8_t* m_data   = nullptr;
    int      m_width  = 0;
    int      m_height = 0;

#ifdef Q_OS_WIN
    void* m_hMap = nullptr;
#endif
};
