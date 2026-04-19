#include "stream_prefetch.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
}

// STREAM_STALL_FIX Phase 4 — see stream_prefetch.h for the full design
// narrative. Implementation notes below focus on the invariants that keep
// us off Phase 1's two failure modes:
//
//   1. Producer observes AVERROR_EOF / avio_feof explicitly before flipping
//      state to EofReached. Transient zero-returns from avio_read_partial
//      during HTTP reconnect drain loop back with a 10 ms sleep instead of
//      poisoning the state. (Phase 1 attempt 1 bug: consumer mapped 0 →
//      AVERROR_EOF, which is not true for a live HTTP reconnect window.)
//
//   2. Consumer never blocks on avio_read_partial. It only waits on
//      m_data_cv up to m_consumer_wait_timeout_ms, then returns AVERROR(EAGAIN)
//      so the decode thread stays responsive. (Phase 1 attempt 2 bug: a
//      2 MiB blocking consumer read deadlocked cold-open waiting on bytes
//      the torrent hadn't fetched because libtorrent had no demand signal.)
//
//      Here the demand signal IS the producer thread's continuous pull,
//      independent of decoder pace.

StreamPrefetch::StreamPrefetch(AVIOContext* raw_avio,
                               std::size_t ring_capacity_bytes)
    : m_raw_avio(raw_avio),
      m_ring(ring_capacity_bytes),
      m_read_pos(0),
      m_write_pos(0),
      m_size(0),
      m_stream_pos(raw_avio ? avio_tell(raw_avio) : 0),
      m_source_size(raw_avio ? avio_size(raw_avio) : -1),
      m_state(State::Running),
      m_shutdown(false),
      m_seek_pending(false),
      m_seek_offset(0),
      m_seek_whence(0) {
    m_producer = std::thread(&StreamPrefetch::producer_loop, this);
}

StreamPrefetch::~StreamPrefetch() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown.store(true, std::memory_order_release);
    }
    m_space_cv.notify_all();
    m_data_cv.notify_all();
    if (m_producer.joinable()) {
        m_producer.join();
    }
    if (m_raw_avio) {
        avio_closep(&m_raw_avio);
    }
}

int StreamPrefetch::read_trampoline(void* opaque, uint8_t* buf, int size) {
    return static_cast<StreamPrefetch*>(opaque)->read(buf, size);
}

int64_t StreamPrefetch::seek_trampoline(void* opaque, int64_t offset, int whence) {
    return static_cast<StreamPrefetch*>(opaque)->seek(offset, whence);
}

int StreamPrefetch::read(uint8_t* buf, int size) {
    if (size <= 0) return 0;

    std::unique_lock<std::mutex> lock(m_mutex);

    // Block until data arrives, state changes (EOF/FatalError), or shutdown.
    // Unbounded wait: the producer thread owns the HTTP-source wait; if the
    // source is genuinely stalled, the decode thread has nothing else to do,
    // and the main-app 30 s first-frame watchdog handles user-facing UX.
    // Bounded wait_for(5 s) returning AVERROR(EAGAIN) breaks libavformat's
    // find_stream_info path — EAGAIN on read_packet is treated as fatal by
    // mkv demuxer, so a transient HTTP reconnect window (which is common on
    // our StreamHttpServer's range-chunked responses) would abort open.
    // Consumer-wait safety net is therefore driven by state flag, not clock.
    while (m_size == 0
           && m_state.load(std::memory_order_acquire) == State::Running
           && !m_shutdown.load(std::memory_order_acquire)) {
        m_data_cv.wait(lock);
    }

    if (m_size > 0) {
        // Copy up to min(size, m_size) into the caller's buffer. Handle ring
        // wrap: up to two memcpys (tail of ring + head after wraparound).
        const std::size_t want = static_cast<std::size_t>(size);
        std::size_t to_copy = std::min(want, m_size);
        std::size_t first_chunk = std::min(to_copy, m_ring.size() - m_read_pos);
        std::memcpy(buf, m_ring.data() + m_read_pos, first_chunk);
        std::size_t remainder = to_copy - first_chunk;
        if (remainder > 0) {
            std::memcpy(buf + first_chunk, m_ring.data(), remainder);
        }
        m_read_pos = (m_read_pos + to_copy) % m_ring.size();
        m_size -= to_copy;
        m_stream_pos += static_cast<int64_t>(to_copy);
        m_space_cv.notify_one();
        return static_cast<int>(to_copy);
    }

    if (m_shutdown.load(std::memory_order_acquire)) {
        return AVERROR_EXIT;
    }
    const State s = m_state.load(std::memory_order_acquire);
    if (s == State::EofReached) {
        return AVERROR_EOF;
    }
    // FatalError (or any unexpected terminal state) → signal I/O failure.
    return AVERROR(EIO);
}

int64_t StreamPrefetch::seek(int64_t offset, int whence) {
    if (!m_raw_avio) return AVERROR(EIO);

    // AVSEEK_SIZE — size query; served from cached value so we don't touch
    // m_raw_avio concurrently with the producer's avio_read_partial (which
    // races on the non-thread-safe AVIOContext and surfaces as garbage
    // return values from subsequent producer reads).
    if ((whence & AVSEEK_SIZE) != 0) {
        return m_source_size;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_seek_offset  = offset;
    m_seek_whence  = whence;
    m_seek_pending = true;
    m_space_cv.notify_all();
    m_data_cv.notify_all();  // also wake producer if it was in data_cv.wait_for

    // Wait for producer to process the seek (ring reset + raw avio_seek).
    m_data_cv.wait(lock, [this] {
        return !m_seek_pending
            || m_shutdown.load(std::memory_order_acquire)
            || m_state.load(std::memory_order_acquire) == State::FatalError;
    });

    if (m_shutdown.load(std::memory_order_acquire)) return AVERROR(EIO);
    if (m_state.load(std::memory_order_acquire) == State::FatalError) return AVERROR(EIO);
    return m_stream_pos;
}

void StreamPrefetch::producer_loop() {
    std::fprintf(stderr, "[StreamPrefetch] producer thread started (ring=%zu bytes)\n",
                 m_ring.size());

    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_shutdown.load(std::memory_order_acquire)) {

        // Post-EOF idle: if we reached source EOF, keep the producer thread
        // ALIVE (don't break) — the demuxer commonly seeks backwards after
        // probing the MKV Cues at end-of-file, and that seek needs a live
        // producer to service. Wake on a new seek_pending or shutdown; any
        // successful seek resets state back to Running below.
        if (m_state.load(std::memory_order_acquire) == State::EofReached) {
            m_data_cv.wait(lock, [this] {
                return m_shutdown.load(std::memory_order_acquire)
                    || m_seek_pending;
            });
            if (m_shutdown.load(std::memory_order_acquire)) break;
            // seek_pending now true — fall through to seek-handling branch
        }
        // Seek always wins over reads: service the pending seek first so we
        // don't write bytes from the old position into the post-seek ring.
        if (m_seek_pending) {
            // Release the mutex during the potentially-blocking avio_seek
            // so consumer ops (which may also want the mutex to unblock a
            // shutdown) aren't starved.
            const int64_t want_off    = m_seek_offset;
            const int     want_whence = m_seek_whence;
            lock.unlock();
            const int64_t seeked = avio_seek(m_raw_avio, want_off, want_whence);
            lock.lock();
            if (seeked < 0) {
                m_state.store(State::FatalError, std::memory_order_release);
            } else {
                m_stream_pos = seeked;
                m_read_pos   = 0;
                m_write_pos  = 0;
                m_size       = 0;
                m_state.store(State::Running, std::memory_order_release);
            }
            m_seek_pending = false;
            m_data_cv.notify_all();
            m_space_cv.notify_all();
            if (m_state.load(std::memory_order_acquire) == State::FatalError) break;
            continue;
        }

        // If ring is full, wait for consumer to drain (or shutdown/seek).
        if (m_size >= m_ring.size()) {
            m_space_cv.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this] {
                    return m_shutdown.load(std::memory_order_acquire)
                        || m_seek_pending
                        || (m_size < m_ring.size());
                });
            continue;
        }

        // Compute the largest contiguous tail slot we can fill in one read.
        const std::size_t free_bytes = m_ring.size() - m_size;
        const std::size_t contig     = std::min(free_bytes, m_ring.size() - m_write_pos);
        uint8_t* dst = m_ring.data() + m_write_pos;

        // Release the mutex during the blocking read so:
        //   (1) consumer reads / seeks aren't starved by a long network wait
        //   (2) shutdown signals can flip m_shutdown without contending here
        // avio_read_partial is allowed to return fewer bytes than requested
        // (at least 1 on success), so latency per iteration is bounded by
        // the HTTP source's buffering cadence, not by `contig`.
        lock.unlock();
        const int n = avio_read_partial(m_raw_avio, dst, static_cast<int>(contig));
        lock.lock();

        // Seek arrived while we were reading — bytes we just read belong to
        // the old position. Discard them by NOT advancing write_pos / size.
        // The next iteration's seek-branch will reset the ring and raw
        // position, then reads resume from the new offset.
        if (m_seek_pending) continue;

        if (n < 0) {
            const bool is_eof = (n == AVERROR_EOF);
            m_state.store(is_eof ? State::EofReached : State::FatalError,
                          std::memory_order_release);
            m_data_cv.notify_all();
            // On EOF, DON'T break — loop back to the post-EOF wait at top so
            // consumer seeks back (common after MKV Cues probe at end-of-file)
            // get serviced. On FatalError, break — unrecoverable.
            if (is_eof) continue;
            break;
        }
        if (n == 0) {
            // Transient zero-return — could be EOF or a drain window during
            // HTTP reconnect. Distinguish via avio_feof: if EOF is true the
            // source really ended; otherwise back off briefly and retry.
            if (avio_feof(m_raw_avio)) {
                m_state.store(State::EofReached, std::memory_order_release);
                m_data_cv.notify_all();
                continue;  // loop to post-EOF wait (not break)
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.lock();
            continue;
        }

        m_write_pos = (m_write_pos + static_cast<std::size_t>(n)) % m_ring.size();
        m_size     += static_cast<std::size_t>(n);
        m_data_cv.notify_all();
    }

    // Last-chance wake so a consumer parked on m_data_cv sees shutdown.
    m_data_cv.notify_all();
}
