#include "ring_buffer.h"

#include <algorithm>
#include <cstdio>

FrameRingWriter::FrameRingWriter(void* buf, int slot_count, int slot_bytes)
    : buf_(static_cast<uint8_t*>(buf))
    , slot_count_(slot_count)
    , slot_bytes_(slot_bytes)
{
    // Write header
    write_u32(OFF_MAGIC,       RING_MAGIC);
    write_u32(OFF_VERSION,     RING_VERSION);
    write_u32(OFF_SLOT_COUNT,  static_cast<uint32_t>(slot_count));
    write_u32(OFF_SLOT_BYTES,  static_cast<uint32_t>(slot_bytes));
    write_u32(OFF_WRITE_INDEX, 0);
    write_u64(OFF_LATEST_FRAME, 0);

    // Zero all slot metadata valid flags
    for (int i = 0; i < slot_count; ++i) {
        int mo = meta_offset(i);
        write_u32(mo + SM_VALID, 0);
    }
}

int64_t FrameRingWriter::write_frame(
    const uint8_t* data, int data_len,
    int width, int height, int stride,
    int64_t pts_us,
    uint32_t pixel_format,
    uint32_t colorspace)
{
    uint32_t slot_idx = read_u32(OFF_WRITE_INDEX);
    ++frame_counter_;
    int64_t fid = frame_counter_;

    int mo = meta_offset(static_cast<int>(slot_idx));
    size_t doff = data_offset(static_cast<int>(slot_idx), slot_count_, slot_bytes_);

    // Step 1: invalidate slot
    write_u32(mo + SM_VALID, 0);

    // Step 2: copy pixel data
    int nbytes = std::min(data_len, slot_bytes_);
    std::memcpy(buf_ + doff, data, static_cast<size_t>(nbytes));

    // Step 3: write metadata
    write_u64(mo + SM_FRAME_ID,   static_cast<uint64_t>(fid));
    write_i64(mo + SM_PTS_US,     pts_us);
    write_u32(mo + SM_WIDTH,      static_cast<uint32_t>(width));
    write_u32(mo + SM_HEIGHT,     static_cast<uint32_t>(height));
    write_u32(mo + SM_STRIDE,     static_cast<uint32_t>(stride));
    write_u32(mo + SM_PIXFMT,     pixel_format);
    write_u32(mo + SM_COLORSPACE, colorspace);

    // Step 4: memory fence (publish barrier)
    std::atomic_thread_fence(std::memory_order_release);

    // Step 5: mark valid
    write_u32(mo + SM_VALID, 1);

    // Step 6: advance write_index and update latest_frame
    uint32_t next_idx = (slot_idx + 1) % static_cast<uint32_t>(slot_count_);
    write_u32(OFF_WRITE_INDEX, next_idx);
    write_u64(OFF_LATEST_FRAME, static_cast<uint64_t>(fid));

    return fid;
}
