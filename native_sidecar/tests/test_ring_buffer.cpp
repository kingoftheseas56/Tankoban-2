#include "ring_buffer.h"

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Test: header is written correctly by constructor
// ---------------------------------------------------------------------------

TEST(RingBuffer, HeaderInit) {
    const int slot_count = 4;
    const int slot_bytes = 100;
    size_t total = ring_buffer_size(slot_count, slot_bytes);

    std::vector<uint8_t> buf(total, 0xFF);  // fill with 0xFF to detect zeroing
    FrameRingWriter writer(buf.data(), slot_count, slot_bytes);

    // Read header fields at exact byte offsets (must match Python)
    auto read_u32 = [&](int offset) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, buf.data() + offset, 4);
        return v;
    };
    auto read_u64 = [&](int offset) -> uint64_t {
        uint64_t v;
        std::memcpy(&v, buf.data() + offset, 8);
        return v;
    };

    EXPECT_EQ(read_u32(0),  RING_MAGIC);    // offset 0: magic
    EXPECT_EQ(read_u32(4),  RING_VERSION);  // offset 4: version
    EXPECT_EQ(read_u32(8),  4u);            // offset 8: slot_count
    EXPECT_EQ(read_u32(12), 100u);          // offset 12: slot_bytes
    EXPECT_EQ(read_u32(16), 0u);            // offset 16: write_index
    EXPECT_EQ(read_u64(24), 0u);            // offset 24: latest_frame

    // All slot valid flags should be 0
    for (int i = 0; i < slot_count; ++i) {
        int mo = meta_offset(i);
        EXPECT_EQ(read_u32(mo + SM_VALID), 0u)
            << "slot " << i << " valid flag should be 0";
    }
}

// ---------------------------------------------------------------------------
// Test: write_frame produces correct layout
// ---------------------------------------------------------------------------

TEST(RingBuffer, WriteFrame) {
    const int slot_count = 2;
    const int w = 4, h = 2;
    const int stride = w * 4;  // BGRA
    const int slot_bytes = stride * h;  // 32 bytes
    size_t total = ring_buffer_size(slot_count, slot_bytes);

    std::vector<uint8_t> buf(total, 0);
    FrameRingWriter writer(buf.data(), slot_count, slot_bytes);

    // Create fake pixel data (8 BGRA pixels = 32 bytes)
    std::vector<uint8_t> pixels(slot_bytes);
    for (int i = 0; i < slot_bytes; ++i)
        pixels[i] = static_cast<uint8_t>(i & 0xFF);

    int64_t fid = writer.write_frame(
        pixels.data(), slot_bytes,
        w, h, stride,
        /*pts_us=*/123456789LL,
        PIXFMT_BGRA8,
        CS_BT709);

    EXPECT_EQ(fid, 1);

    auto read_u32 = [&](int offset) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, buf.data() + offset, 4);
        return v;
    };
    auto read_u64 = [&](int offset) -> uint64_t {
        uint64_t v;
        std::memcpy(&v, buf.data() + offset, 8);
        return v;
    };
    auto read_i64 = [&](int offset) -> int64_t {
        int64_t v;
        std::memcpy(&v, buf.data() + offset, 8);
        return v;
    };

    // Header should be updated
    EXPECT_EQ(read_u32(16), 1u);   // write_index advanced to 1
    EXPECT_EQ(read_u64(24), 1u);   // latest_frame = 1

    // Slot 0 metadata (at offset 64)
    int mo = meta_offset(0);
    EXPECT_EQ(read_u64(mo + SM_FRAME_ID),   1u);
    EXPECT_EQ(read_i64(mo + SM_PTS_US),     123456789LL);
    EXPECT_EQ(read_u32(mo + SM_WIDTH),      4u);
    EXPECT_EQ(read_u32(mo + SM_HEIGHT),     2u);
    EXPECT_EQ(read_u32(mo + SM_STRIDE),     16u);  // 4 * 4
    EXPECT_EQ(read_u32(mo + SM_PIXFMT),     PIXFMT_BGRA8);
    EXPECT_EQ(read_u32(mo + SM_COLORSPACE), CS_BT709);
    EXPECT_EQ(read_u32(mo + SM_VALID),      1u);

    // Pixel data should be at correct offset
    size_t doff = data_offset(0, slot_count, slot_bytes);
    EXPECT_EQ(std::memcmp(buf.data() + doff, pixels.data(), slot_bytes), 0);
}

// ---------------------------------------------------------------------------
// Test: write_frame wraps around slots
// ---------------------------------------------------------------------------

TEST(RingBuffer, SlotWrapAround) {
    const int slot_count = 2;
    const int slot_bytes = 16;
    size_t total = ring_buffer_size(slot_count, slot_bytes);

    std::vector<uint8_t> buf(total, 0);
    FrameRingWriter writer(buf.data(), slot_count, slot_bytes);

    std::vector<uint8_t> pixels(slot_bytes, 0xAA);

    auto read_u32 = [&](int offset) -> uint32_t {
        uint32_t v;
        std::memcpy(&v, buf.data() + offset, 4);
        return v;
    };

    // Write 3 frames into 2 slots — should wrap around
    writer.write_frame(pixels.data(), slot_bytes, 2, 2, 8, 100);
    EXPECT_EQ(read_u32(16), 1u);  // write_index = 1

    writer.write_frame(pixels.data(), slot_bytes, 2, 2, 8, 200);
    EXPECT_EQ(read_u32(16), 0u);  // write_index wraps to 0

    writer.write_frame(pixels.data(), slot_bytes, 2, 2, 8, 300);
    EXPECT_EQ(read_u32(16), 1u);  // write_index = 1 again

    // Slot 0 should have frame 3 (latest overwrite)
    int mo0 = meta_offset(0);
    auto read_u64 = [&](int offset) -> uint64_t {
        uint64_t v;
        std::memcpy(&v, buf.data() + offset, 8);
        return v;
    };
    EXPECT_EQ(read_u64(mo0 + SM_FRAME_ID), 3u);
    EXPECT_EQ(read_u32(mo0 + SM_VALID), 1u);
}

// ---------------------------------------------------------------------------
// Test: size calculation matches expected formula
// ---------------------------------------------------------------------------

TEST(RingBuffer, SizeCalculation) {
    // 4 slots, each 1920*1080*4 = 8294400 bytes
    int slot_count = 4;
    int slot_bytes = 1920 * 1080 * 4;
    size_t expected = 64 + 4 * 64 + 4 * static_cast<size_t>(slot_bytes);
    EXPECT_EQ(ring_buffer_size(slot_count, slot_bytes), expected);
}
