#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Ring buffer constants — must match Python __init__.py exactly
// ---------------------------------------------------------------------------

static constexpr uint32_t RING_MAGIC      = 0x46524E47;  // 'FRNG'
static constexpr uint32_t RING_VERSION    = 1;

static constexpr int HEADER_SIZE          = 64;
static constexpr int SLOT_META_SIZE       = 64;

// Header field offsets
static constexpr int OFF_MAGIC            = 0;
static constexpr int OFF_VERSION          = 4;
static constexpr int OFF_SLOT_COUNT       = 8;
static constexpr int OFF_SLOT_BYTES       = 12;
static constexpr int OFF_WRITE_INDEX      = 16;
// 20: 4 bytes padding
static constexpr int OFF_LATEST_FRAME     = 24;  // uint64
static constexpr int OFF_CLOCK_US         = 32;  // int64: audio clock position (microseconds)
static constexpr int OFF_CONSUMER_FID     = 40;  // uint64: last frame_id displayed by consumer

// Per-slot metadata field offsets (relative to slot meta start)
static constexpr int SM_FRAME_ID          = 0;   // uint64
static constexpr int SM_PTS_US            = 8;   // int64
static constexpr int SM_WIDTH             = 16;  // uint32
static constexpr int SM_HEIGHT            = 20;  // uint32
static constexpr int SM_STRIDE            = 24;  // uint32
static constexpr int SM_PIXFMT            = 28;  // uint32
static constexpr int SM_COLORSPACE        = 32;  // uint32
static constexpr int SM_VALID             = 36;  // uint32

// Pixel format enum
static constexpr uint32_t PIXFMT_BGRA8    = 0;

// Colorspace enum
static constexpr uint32_t CS_BT709        = 0;
static constexpr uint32_t CS_BT601        = 1;

// ---------------------------------------------------------------------------
// Size helpers
// ---------------------------------------------------------------------------

inline size_t ring_buffer_size(int slot_count, int slot_bytes) {
    return static_cast<size_t>(HEADER_SIZE)
         + static_cast<size_t>(slot_count) * SLOT_META_SIZE
         + static_cast<size_t>(slot_count) * slot_bytes;
}

inline int meta_offset(int slot_index) {
    return HEADER_SIZE + slot_index * SLOT_META_SIZE;
}

inline size_t data_offset(int slot_index, int slot_count, int slot_bytes) {
    return static_cast<size_t>(HEADER_SIZE)
         + static_cast<size_t>(slot_count) * SLOT_META_SIZE
         + static_cast<size_t>(slot_index) * slot_bytes;
}

// ---------------------------------------------------------------------------
// FrameRingWriter — producer side (port of Python FrameRingWriter)
// ---------------------------------------------------------------------------

class FrameRingWriter {
public:
    FrameRingWriter(void* buf, int slot_count, int slot_bytes);

    // Write a BGRA frame into the next slot. Returns monotonic frame_id.
    int64_t write_frame(const uint8_t* data, int data_len,
                        int width, int height, int stride,
                        int64_t pts_us,
                        uint32_t pixel_format = PIXFMT_BGRA8,
                        uint32_t colorspace   = CS_BT709);

    // Write current audio clock position to SHM header (for display-side pacing).
    void write_clock_us(int64_t clock_us) {
        write_i64(OFF_CLOCK_US, clock_us);
    }

    // Read the last frame_id displayed by the consumer (for decode throttling).
    int64_t read_consumer_fid() {
        int64_t v;
        std::memcpy(&v, buf_ + OFF_CONSUMER_FID, 8);
        return v;
    }

private:
    uint8_t* buf_;
    int      slot_count_;
    int      slot_bytes_;
    int64_t  frame_counter_ = 0;

    // Low-level byte-offset writes (no struct padding issues)
    void write_u32(int offset, uint32_t v) {
        std::memcpy(buf_ + offset, &v, 4);
    }
    void write_u64(int offset, uint64_t v) {
        std::memcpy(buf_ + offset, &v, 8);
    }
    void write_i64(int offset, int64_t v) {
        std::memcpy(buf_ + offset, &v, 8);
    }
    uint32_t read_u32(int offset) {
        uint32_t v;
        std::memcpy(&v, buf_ + offset, 4);
        return v;
    }
};
