// ── test_stream_piece_waiter.cpp ───────────────────────────────────────────
// Option A coverage per plan: null-engine short-circuit + timeout + destructor
// + cancellation semantics. Proves the harness wires up without pulling
// libtorrent headers into the test translation unit.
//
// Notification-path coverage (onPieceFinished slot → QWaitCondition::wakeAll)
// is deferred to Stage 3 — needs either a no-libtorrent TorrentEngine branch
// or a PieceSignalSource interface extraction. Not justified by one test site.
//
// Bootstrap: custom main() instantiates QCoreApplication before
// InitGoogleTest because StreamPieceWaiter::StreamPieceWaiter calls
// QCoreApplication::applicationDirPath() during construction (see
// StreamPieceWaiter.cpp line ~38 telemetry cache path).

#include "core/stream/StreamPieceWaiter.h"

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>

TEST(StreamPieceWaiterTest, NullEngineAwaitReturnsFalseImmediately) {
    StreamPieceWaiter w(nullptr);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(w.awaitRange("deadbeef", 0, 0, 1024, 5000, cancelled));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    // StreamPieceWaiter.cpp:109 short-circuit (`!m_engine` branch) returns
    // false immediately — we must not block for anything close to the 5000 ms
    // timeout we passed.
    EXPECT_LT(elapsed_ms, 100);
}

TEST(StreamPieceWaiterTest, NonPositiveTimeoutReturnsFalse) {
    StreamPieceWaiter w(nullptr);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    EXPECT_FALSE(w.awaitRange("deadbeef", 0, 0, 1024, 0, cancelled));
    EXPECT_FALSE(w.awaitRange("deadbeef", 0, 0, 1024, -1, cancelled));
}

TEST(StreamPieceWaiterTest, DestructorDoesNotHangWithNoWaiters) {
    // Exercises dtor path with empty m_waiters. Sentinel for the destructor
    // never blocking when no in-flight waits exist.
    {
        StreamPieceWaiter w(nullptr);
        (void)w;
    }
    SUCCEED();
}

TEST(StreamPieceWaiterTest, CancelFlagRespectedEvenWithNullEngine) {
    // Regression sentinel: even with the short-circuit at line 109, calling
    // awaitRange with a pre-set cancellation flag must not produce true.
    // Documents current behavior so Stage 3 (notification-path refactor)
    // doesn't accidentally invert this under a null engine.
    StreamPieceWaiter w(nullptr);
    auto cancelled = std::make_shared<std::atomic<bool>>(true);
    EXPECT_FALSE(w.awaitRange("deadbeef", 0, 0, 1024, 100, cancelled));
}

int main(int argc, char** argv) {
    // QCoreApplication must outlive StreamPieceWaiter instances constructed
    // in test bodies; ctor path reaches QCoreApplication::applicationDirPath()
    // via telemetry-path-cache init even when TANKOBAN_STREAM_TELEMETRY=0.
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
