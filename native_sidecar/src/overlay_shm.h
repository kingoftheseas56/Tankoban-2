#pragma once
// PLAYER_PERF_FIX Phase 3 Batch 3.B Option B — SHM-routed subtitle overlay.
//
// Replaces the cross-process D3D11 shared texture approach (which hit the
// "no keyed mutex = implicit sync → main-app draw stalls" trap) with a
// plain named shared-memory region carrying the rendered overlay BGRA
// bytes. Main-app's FrameCanvas opens this SHM, memcpys into a LOCAL
// D3D11 texture (owned by its own device), and draws the overlay quad.
// No cross-process GPU sharing, no keyed-mutex lifecycle — the sync
// problem class goes away.
//
// Layout:
//   [0..8]   u64  frame_counter   (monotonic, bumped LAST after bgra write)
//   [8..12]  u32  width
//   [12..16] u32  height
//   [16..20] u32  valid            (1 = overlay has content, 0 = empty/no subs)
//   [20..24] u32  reserved
//   [24..32] padding to align BGRA region to 32 bytes
//   [32..N]  BGRA payload (width * height * 4 bytes)
//
// Writer protocol:
//   if overlay visible this frame:
//     memcpy BGRA into payload
//     write valid=1
//     frame_counter.fetch_add(1)       # atomic, last — publishes the frame
//   else:
//     write valid=0
//     frame_counter.fetch_add(1)
//
// Reader protocol:
//   counter = frame_counter.load()
//   if counter != last_seen:
//     read valid
//     if valid: memcpy BGRA from payload to local D3D11 texture
//     last_seen = counter
//
// Race window: reader reads counter=N, writer updates to N+1 during reader's
// memcpy → reader reads partial N/N+1 mix. Narrow window because writes
// happen at 24fps while reads at 60Hz, and the BGRA memcpy is fast on the
// reader side (~1-2ms on 1080p). Single-glitch-per-subtitle-change is the
// worst case; not visually problematic. Double-buffering added only if the
// race proves visible.

#include <atomic>
#include <cstdint>
#include <string>

#include "shm_helpers.h"

class OverlayShm {
public:
    OverlayShm();
    ~OverlayShm();

    // Create a named SHM region sized for an (width x height) BGRA overlay.
    // Name is generated via generate_shm_name() (same pattern as video SHM).
    bool create(int width, int height);

    // Tear down the mapping. Idempotent.
    void destroy();

    // Write `bgra` into the payload + bump frame_counter. `bgra` must point
    // to exactly width * height * 4 bytes. Thread-safe for a SINGLE writer
    // (the decode thread) — multiple writers would race on the counter.
    void write(const uint8_t* bgra);

    // Bump frame_counter with valid=0 — signals "no overlay this frame"
    // without writing any pixel data. Used when subs become inactive so
    // the reader clears its cached overlay upload.
    void write_empty();

    const std::string& name() const { return region_.name; }
    int  width()  const { return width_; }
    int  height() const { return height_; }
    bool ready()  const { return region_.ptr != nullptr; }

private:
    ShmRegion region_;
    int       width_  = 0;
    int       height_ = 0;
};

// Header + payload offsets (shared with main-app's reader).
inline constexpr int OVERLAY_HDR_COUNTER = 0;    // u64
inline constexpr int OVERLAY_HDR_WIDTH   = 8;    // u32
inline constexpr int OVERLAY_HDR_HEIGHT  = 12;   // u32
inline constexpr int OVERLAY_HDR_VALID   = 16;   // u32
inline constexpr int OVERLAY_HDR_SIZE    = 32;   // BGRA payload starts here
