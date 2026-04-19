// STREAM_STALL_FIX Phase 4 (tactic f rescoped) — dedicated prefetch thread
// + ring buffer wrapping a raw HTTP AVIOContext so the sidecar maintains
// continuous forward-read demand on StreamHttpServer regardless of decoder
// pacing. This is the mpv-style `stream-buffer-size=64MiB` semantic that
// libavformat's synchronous demuxer can't deliver on its own — see
// chat.md:2543-2552 for Phase 1's architectural root-cause finding.
//
// Design invariants (avoids Phase 1 attempt-1/attempt-2 failure modes):
//   - Consumer (demuxer thread via avio_alloc_context) NEVER sees transient
//     HTTP drain as EOF. `read()` returns 0 only when the producer thread
//     has explicitly observed source EOF (State::EofReached). Partial reads
//     are fine — avio_alloc_context re-invokes us when the buffer drains.
//   - Consumer does NOT block on the HTTP source. The producer thread owns
//     that wait. If the ring is empty, consumer bounded-waits on a condition
//     variable (default 5 s cap) and returns whatever arrives; if genuinely
//     nothing shows up it returns a negative error rather than deadlocking
//     the decode thread the way Phase 1 attempt 2 did on cold-open.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avio.h>
}

class StreamPrefetch {
public:
    enum class State {
        Running,       // producer thread actively reading from source
        EofReached,    // source hit EOF; consumer drains remaining ring bytes
        FatalError,    // producer hit non-EOF error; treat as unrecoverable
    };

    // Takes ownership of raw_avio. avio_closep(raw_avio) happens in dtor
    // (after the producer thread is joined), so callers MUST NOT free
    // raw_avio themselves. Caller passes in the already-opened source.
    //
    // ring_capacity_bytes: total ring size. 64 MiB default to mirror mpv.
    // Consumer read() blocks unbounded on an empty-ring + Running state
    // (wakes on data arrival / state change / shutdown) so that a
    // transient HTTP reconnect window never surfaces as AVERROR(EAGAIN)
    // to libavformat's find_stream_info — EAGAIN from read_packet is
    // treated as fatal by the mkv demuxer, aborting open mid-probe. The
    // producer thread is the one holding the HTTP-source wait; if the
    // source is genuinely stalled, the decode thread has nothing else to
    // do, and the main-app 30 s first-frame watchdog surfaces that to
    // the user.
    StreamPrefetch(AVIOContext* raw_avio,
                   std::size_t ring_capacity_bytes = 64 * 1024 * 1024);
    ~StreamPrefetch();

    StreamPrefetch(const StreamPrefetch&) = delete;
    StreamPrefetch& operator=(const StreamPrefetch&) = delete;

    // --- Trampolines for avio_alloc_context -------------------------------
    //
    // Pass `this` as the `opaque` pointer to avio_alloc_context, and these
    // trampolines as read_packet / seek. They just forward to the instance
    // methods below.
    static int     read_trampoline(void* opaque, uint8_t* buf, int size);
    static int64_t seek_trampoline(void* opaque, int64_t offset, int whence);

    // --- Consumer-facing ops (called on the demuxer thread) ---------------

    // Returns positive bytes copied, 0 on genuine EOF (state == EofReached
    // AND ring drained), AVERROR(EIO) on FatalError, AVERROR(EAGAIN) on
    // timeout while Running. Always returns at least 1 byte when data is
    // available in the ring; partial reads are allowed.
    int read(uint8_t* buf, int size);

    // Consumer-side seek. Flushes ring + reseeks the raw source +
    // producer resumes from new offset. AVSEEK_SIZE is delegated without
    // state change. Returns new stream position or negative error.
    int64_t seek(int64_t offset, int whence);

    // Observable state (atomic).
    State state() const { return m_state.load(std::memory_order_acquire); }

private:
    void producer_loop();

    // Ring accounting. All read/write/size access MUST hold m_mutex.
    AVIOContext*          m_raw_avio;
    std::vector<uint8_t>  m_ring;
    std::size_t           m_read_pos;
    std::size_t           m_write_pos;
    std::size_t           m_size;        // bytes currently in ring

    // Byte offset of m_read_pos in the underlying stream. Bumped on consume,
    // reset on seek. Needed so seek_trampoline returns a correct position.
    int64_t               m_stream_pos;

    // Cached at construction. avio_size / AVSEEK_SIZE queries read this
    // without touching m_raw_avio, which is only safe on the producer
    // thread once producer_loop is running. (AVIOContext is not thread-safe;
    // concurrent avio_size + avio_read_partial races corrupt http-protocol
    // state and surface as garbage negative return values from subsequent
    // reads.)
    int64_t               m_source_size;

    std::atomic<State>    m_state;
    std::atomic<bool>     m_shutdown;

    // Seek coordination: consumer sets m_seek_pending + params under mutex,
    // producer observes flag at top of each loop iteration and services it.
    bool                  m_seek_pending;
    int64_t               m_seek_offset;
    int                   m_seek_whence;

    std::mutex                m_mutex;
    std::condition_variable   m_data_cv;   // wake consumer when bytes arrive / state changes
    std::condition_variable   m_space_cv;  // wake producer when ring drains / shutdown

    std::thread               m_producer;
};
