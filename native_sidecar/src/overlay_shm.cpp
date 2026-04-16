#include "overlay_shm.h"

#include <atomic>
#include <cstdio>
#include <cstring>

OverlayShm::OverlayShm() = default;

OverlayShm::~OverlayShm() {
    destroy();
}

bool OverlayShm::create(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (region_.ptr) return true;  // already created

    const size_t payload_bytes = static_cast<size_t>(width) * height * 4;
    const size_t total_bytes   = OVERLAY_HDR_SIZE + payload_bytes;

    region_ = create_shm(generate_shm_name(), total_bytes);
    if (!region_.ptr) {
        std::fprintf(stderr, "OverlayShm: create_shm failed for %dx%d (%zu bytes)\n",
                     width, height, total_bytes);
        return false;
    }

    // Seed header — create_shm already zeros the region.
    auto* buf = static_cast<uint8_t*>(region_.ptr);
    uint32_t w = static_cast<uint32_t>(width);
    uint32_t h = static_cast<uint32_t>(height);
    std::memcpy(buf + OVERLAY_HDR_WIDTH,  &w, 4);
    std::memcpy(buf + OVERLAY_HDR_HEIGHT, &h, 4);

    width_  = width;
    height_ = height;

    std::fprintf(stderr, "OverlayShm: created %dx%d name=%s total=%zu bytes\n",
                 width, height, region_.name.c_str(), total_bytes);
    return true;
}

void OverlayShm::destroy() {
    cleanup_shm(region_);
    width_  = 0;
    height_ = 0;
}

void OverlayShm::write(const uint8_t* bgra) {
    if (!region_.ptr || !bgra) return;

    auto* buf = static_cast<uint8_t*>(region_.ptr);
    const size_t payload_bytes = static_cast<size_t>(width_) * height_ * 4;

    // Write payload first, header-valid flag second, counter last.
    // The counter bump is the "publish" — consumers read counter first,
    // then payload, so the reader seeing counter=N+1 implies payload is
    // settled. Narrow race only during the payload memcpy itself
    // (consumer might read partial data if the memcpy hasn't finished
    // before the consumer's own memcpy starts — 24fps vs 60Hz gives a
    // small probability; acceptable for 3.B).
    std::memcpy(buf + OVERLAY_HDR_SIZE, bgra, payload_bytes);

    uint32_t valid = 1;
    std::memcpy(buf + OVERLAY_HDR_VALID, &valid, 4);

    // Atomic counter bump — the "publish" step. Uses std::atomic_ref on
    // the raw buffer's counter u64. C++17-safe: atomic_ref isn't in
    // pre-C++20 stdlibs, so fall back to a memory-order-release
    // std::atomic_store on a reinterpret_cast<std::atomic<uint64_t>*>.
    // On x86/x64, aligned 8-byte stores are naturally atomic; the
    // atomic_store call enforces release semantics so prior payload
    // writes are visible before the counter update.
    auto* counter = reinterpret_cast<std::atomic<uint64_t>*>(buf + OVERLAY_HDR_COUNTER);
    counter->fetch_add(1, std::memory_order_release);
}

void OverlayShm::write_empty() {
    if (!region_.ptr) return;

    auto* buf = static_cast<uint8_t*>(region_.ptr);
    uint32_t valid = 0;
    std::memcpy(buf + OVERLAY_HDR_VALID, &valid, 4);

    auto* counter = reinterpret_cast<std::atomic<uint64_t>*>(buf + OVERLAY_HDR_COUNTER);
    counter->fetch_add(1, std::memory_order_release);
}
