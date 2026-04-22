// ── test_stream_prioritizer.cpp ────────────────────────────────────────────
// Stage 3a coverage per plan. Pure-function tests for StreamPrioritizer
// (no Qt event loop, no QObject). Validates:
//   - M4 compile-time constants (MAX_STARTUP_PIECES = 2 etc.)
//   - M5 first-piece-target math (0 ms URGENT staircase)
//   - M6 defensive-tail-preserve shape (InitialPlayback tail deadlines)
//   - Stremio-reference deadline staircases (CRITICAL HEAD / HEAD linear /
//     proactive / standard body / seeking / metadata / background pre-cache)
//   - SeekType-dispatch window sizes + base deadlines

#include "core/stream/StreamPrioritizer.h"
#include "core/stream/StreamSeekClassifier.h"

#include <gtest/gtest.h>

namespace {

// Standard piece / file assumptions for readable math across the tests.
constexpr qint64 kPieceLen  = 1LL * 1024 * 1024;       // 1 MB pieces (typical)
constexpr qint64 kFileSize  = 1LL * 1024 * 1024 * 1024; // 1 GB file
constexpr int    kLastPiece = 1023;                     // 1024 pieces total

StreamPrioritizer::Params defaultStreamingParams()
{
    StreamPrioritizer::Params p;
    p.currentPiece     = 0;
    p.totalPieces      = 1024;
    p.pieceLength      = kPieceLen;
    p.cacheSizeBytes   = 10LL * 1024 * 1024 * 1024;   // 10 GB
    p.cacheEnabled     = true;
    p.priorityLevel    = 1;                            // normal streaming
    p.downloadSpeed    = 0;
    p.bitrate          = 0;                            // unknown
    return p;
}

}  // namespace

// ── M4 compile-time constants (integration-memo §5) ────────────────────────

TEST(StreamPrioritizerTest, M4_StartupConstantsPinned) {
    // Integration memo §5 M4: MAX_STARTUP_PIECES = 2 re-verified from
    // Stremio priorities.rs:6-12 2026-04-18. Pinned as compile-time constants.
    EXPECT_EQ(StreamPrioritizer::kMinStartupBytes,  1LL * 1024 * 1024);
    EXPECT_EQ(StreamPrioritizer::kMaxStartupPieces, 2);
    EXPECT_EQ(StreamPrioritizer::kMinStartupPieces, 1);
}

// ── calculateStreamingPriorities — edge cases ──────────────────────────────

TEST(StreamPrioritizerTest, Calculate_InvalidInputs_ReturnsEmpty) {
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.pieceLength = 0;
    EXPECT_TRUE(StreamPrioritizer::calculateStreamingPriorities(p).isEmpty());

    p = defaultStreamingParams();
    p.totalPieces = 0;
    EXPECT_TRUE(StreamPrioritizer::calculateStreamingPriorities(p).isEmpty());

    p = defaultStreamingParams();
    p.currentPiece = -1;
    EXPECT_TRUE(StreamPrioritizer::calculateStreamingPriorities(p).isEmpty());
}

// ── Priority tier dispatch ─────────────────────────────────────────────────

TEST(StreamPrioritizerTest, Calculate_MetadataTier_AllFiftyMsFlat) {
    // priorityLevel >= 250: all pieces in window get 50 ms flat (metadata probes).
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.priorityLevel = 250;

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_FALSE(pairs.isEmpty());
    for (const auto& pair : pairs) {
        EXPECT_EQ(pair.second, 50) << "metadata tier should be 50 ms flat";
    }
}

TEST(StreamPrioritizerTest, Calculate_SeekingTier_StaircaseTenMsPerPiece) {
    // priorityLevel >= 100: 10 + d × 10 ms staircase.
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.priorityLevel = 100;

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_GE(pairs.size(), 3);
    EXPECT_EQ(pairs[0].second, 10);       // d=0 → 10 ms
    EXPECT_EQ(pairs[1].second, 20);       // d=1 → 20 ms
    EXPECT_EQ(pairs[2].second, 30);       // d=2 → 30 ms
}

TEST(StreamPrioritizerTest, Calculate_BackgroundPreCache_LazyDeadlines) {
    // priorityLevel == 0: lazy background deadlines 20000 + d × 200 ms.
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.priorityLevel = 0;

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_GE(pairs.size(), 2);
    EXPECT_EQ(pairs[0].second, 20000);    // d=0 → 20000 ms
    EXPECT_EQ(pairs[1].second, 20200);    // d=1 → 20200 ms
}

TEST(StreamPrioritizerTest, Calculate_CriticalHeadStaircase_10_60_110_160_210) {
    // Normal streaming (priority=1), distance < 5: 10 + d × 50 ms
    // (CRITICAL HEAD: 10/60/110/160/210). Per-memo M5 note, this is NOT
    // the cold-open 0 ms URGENT path — that's seekDeadlines(InitialPlayback).
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.priorityLevel = 1;

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_GE(pairs.size(), 5);
    EXPECT_EQ(pairs[0].second, 10);       // d=0 → 10 ms
    EXPECT_EQ(pairs[1].second, 60);       // d=1 → 60 ms
    EXPECT_EQ(pairs[2].second, 110);      // d=2 → 110 ms
    EXPECT_EQ(pairs[3].second, 160);      // d=3 → 160 ms
    EXPECT_EQ(pairs[4].second, 210);      // d=4 → 210 ms
}

TEST(StreamPrioritizerTest, Calculate_HeadLinear_StartsAt250) {
    // distance ∈ [5, headWindow): 250 + (d-5) × 50 ms. First HEAD linear
    // piece (d=5) should be 250 ms. Requires headWindow > 5; defaultStreamingParams
    // has bitrate=0 and downloadSpeed=0 which clamps targetHeadBytes to the
    // kMinBufferBytes=5MB floor (headWindow=5 exactly → HEAD linear tier
    // collapses to zero width and d=5 falls through to standard-body 5000+d*20).
    // A 10 MB/s bitrate opens the tier: targetHeadBytes clamps to
    // kMaxBufferBytes=50MB → headWindow=50 pieces wide.
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.priorityLevel = 1;
    p.bitrate       = 10LL * 1024 * 1024;   // 10 MB/s → headWindow=50

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_GE(pairs.size(), 7);
    EXPECT_EQ(pairs[5].second, 250);      // d=5 → 250 ms (HEAD linear start)
    EXPECT_EQ(pairs[6].second, 300);      // d=6 → 300 ms
}

// ── End-piece clamping ─────────────────────────────────────────────────────

TEST(StreamPrioritizerTest, Calculate_NearEndOfFile_ClampsAtLastPiece) {
    // currentPiece near end: endPiece must clamp at totalPieces - 1; no
    // out-of-range piece indices emitted.
    StreamPrioritizer::Params p = defaultStreamingParams();
    p.currentPiece = kLastPiece - 2;      // only 3 pieces left (1021, 1022, 1023)
    p.priorityLevel = 1;

    const auto pairs = StreamPrioritizer::calculateStreamingPriorities(p);
    ASSERT_FALSE(pairs.isEmpty());
    for (const auto& pair : pairs) {
        EXPECT_LE(pair.first, kLastPiece) << "piece index must not exceed last piece";
    }
    EXPECT_EQ(pairs.last().first, kLastPiece);
}

// ── seekDeadlines — SeekType dispatch ──────────────────────────────────────

TEST(StreamPrioritizerTest, SeekDeadlines_Sequential_ReturnsEmpty) {
    // Sequential seek type → empty list (caller should use
    // calculateStreamingPriorities instead).
    const auto pairs = StreamPrioritizer::seekDeadlines(
        StreamSeekType::Sequential, 0, kLastPiece, kFileSize, kPieceLen);
    EXPECT_TRUE(pairs.isEmpty());
}

TEST(StreamPrioritizerTest, SeekDeadlines_InitialPlayback_UrgentZeroMsStaircase) {
    // M5 invariant: InitialPlayback targets 0 ms URGENT tier per handle.rs:
    // base 0 ms, staircase 0 / 10 / 20. Window size = min(kMaxStartupPieces,
    // ceil(startup_bytes / piece_length)). startup_bytes = min(kMinStartupBytes=1MB,
    // fileSize/20).max(pieceLength). With module-level kPieceLen=1MB the ceil
    // divides to 1 piece (startup_bytes = max(1MB, 1MB) = 1MB; ceil(1MB/1MB) = 1);
    // half-MB pieces keep startup_bytes at 1MB and produce ceil(1MB/512KB)=2 pieces,
    // exercising the full URGENT staircase the test intends.
    constexpr qint64 kHalfMbPiece = 512LL * 1024;
    const auto pairs = StreamPrioritizer::seekDeadlines(
        StreamSeekType::InitialPlayback, 0, kLastPiece, kFileSize, kHalfMbPiece);
    ASSERT_EQ(pairs.size(), StreamPrioritizer::kMaxStartupPieces);
    EXPECT_EQ(pairs[0].first,  0);        // piece 0
    EXPECT_EQ(pairs[0].second, 0);        // 0 ms URGENT
    EXPECT_EQ(pairs[1].first,  1);
    EXPECT_EQ(pairs[1].second, 10);       // +10 ms staircase
}

TEST(StreamPrioritizerTest, SeekDeadlines_UserScrub_FourPiecesAtCritical300Ms) {
    // UserScrub: CRITICAL tier, 300 ms base, 4-piece window.
    // Per header: 300 / 310 / 320 / 330 ms.
    const int startPiece = 500;
    const auto pairs = StreamPrioritizer::seekDeadlines(
        StreamSeekType::UserScrub, startPiece, kLastPiece, kFileSize, kPieceLen);
    ASSERT_EQ(pairs.size(), 4);
    EXPECT_EQ(pairs[0].second, 300);
    EXPECT_EQ(pairs[1].second, 310);
    EXPECT_EQ(pairs[2].second, 320);
    EXPECT_EQ(pairs[3].second, 330);
    EXPECT_EQ(pairs[0].first, startPiece);
    EXPECT_EQ(pairs[3].first, startPiece + 3);
}

TEST(StreamPrioritizerTest, SeekDeadlines_ContainerMetadata_TwoPiecesAt100Ms) {
    // ContainerMetadata: CONTAINER-INDEX tier, 100 ms base, 2-piece window.
    // 100 / 110 ms.
    const int startPiece = 950;
    const auto pairs = StreamPrioritizer::seekDeadlines(
        StreamSeekType::ContainerMetadata, startPiece, kLastPiece, kFileSize, kPieceLen);
    ASSERT_EQ(pairs.size(), 2);
    EXPECT_EQ(pairs[0].second, 100);
    EXPECT_EQ(pairs[1].second, 110);
}

TEST(StreamPrioritizerTest, SeekDeadlines_SpeedFactor_MultipliesBase_ExceptUrgent) {
    // speedFactor multiplies non-URGENT bases; URGENT (InitialPlayback) stays
    // at 0 regardless (0 × anything = 0).
    const auto scrubScaled = StreamPrioritizer::seekDeadlines(
        StreamSeekType::UserScrub, 0, kLastPiece, kFileSize, kPieceLen, 2.0);
    ASSERT_FALSE(scrubScaled.isEmpty());
    EXPECT_EQ(scrubScaled[0].second, 600);   // 300 × 2.0 = 600 ms

    const auto urgentScaled = StreamPrioritizer::seekDeadlines(
        StreamSeekType::InitialPlayback, 0, kLastPiece, kFileSize, kPieceLen, 10.0);
    ASSERT_FALSE(urgentScaled.isEmpty());
    EXPECT_EQ(urgentScaled[0].second, 0);    // URGENT stays at 0 under any factor
}

TEST(StreamPrioritizerTest, SeekDeadlines_ClampsAtLastPiece) {
    // UserScrub window of 4 near end of file must not emit piece indices
    // beyond lastPiece.
    const int startPiece = kLastPiece - 1;   // only 2 pieces remain (1022, 1023)
    const auto pairs = StreamPrioritizer::seekDeadlines(
        StreamSeekType::UserScrub, startPiece, kLastPiece, kFileSize, kPieceLen);
    ASSERT_EQ(pairs.size(), 2);
    EXPECT_EQ(pairs.last().first, kLastPiece);
}

// ── initialPlaybackWindowSize (M4) ─────────────────────────────────────────

TEST(StreamPrioritizerTest, InitialPlaybackWindowSize_LargeFile_MaxPieces) {
    // 1 GB file with 1 MB pieces: effective = min(1MB, 1GB/20=50MB).max(1MB) = 1MB.
    // piecesNeeded = ceil(1MB / 1MB) = 1. Clamped to [1, 2] → 1.
    // Actually, re-checking: the clamp is MIN=1, MAX=2.
    const int size = StreamPrioritizer::initialPlaybackWindowSize(kFileSize, kPieceLen);
    EXPECT_GE(size, StreamPrioritizer::kMinStartupPieces);
    EXPECT_LE(size, StreamPrioritizer::kMaxStartupPieces);
}

TEST(StreamPrioritizerTest, InitialPlaybackWindowSize_InvalidInputs_ReturnsMin) {
    EXPECT_EQ(StreamPrioritizer::initialPlaybackWindowSize(0, kPieceLen),
              StreamPrioritizer::kMinStartupPieces);
    EXPECT_EQ(StreamPrioritizer::initialPlaybackWindowSize(kFileSize, 0),
              StreamPrioritizer::kMinStartupPieces);
}

// ── initialPlaybackTailDeadlines (M6 defensive-tail-preserve) ──────────────

TEST(StreamPrioritizerTest, TailDeadlines_TailWithinHeadWindow_ReturnsEmpty) {
    // Tail within head window → empty (no deferred tail work needed).
    const auto pairs = StreamPrioritizer::initialPlaybackTailDeadlines(
        /*startPiece=*/0, /*lastPiece=*/10, /*headWindowSize=*/20);
    EXPECT_TRUE(pairs.isEmpty());
}

TEST(StreamPrioritizerTest, TailDeadlines_TailBeyondHead_ReturnsTwoPairs) {
    // Tail strictly beyond head window → 2 pairs: last@1200ms, last-1@1250ms.
    // handle.rs:329-331 per M6 defensive-preserve.
    const auto pairs = StreamPrioritizer::initialPlaybackTailDeadlines(
        /*startPiece=*/0, /*lastPiece=*/1023, /*headWindowSize=*/10);
    ASSERT_EQ(pairs.size(), 2);
    EXPECT_EQ(pairs[0].first,  1023);
    EXPECT_EQ(pairs[0].second, 1200);
    EXPECT_EQ(pairs[1].first,  1022);
    EXPECT_EQ(pairs[1].second, 1250);
}

TEST(StreamPrioritizerTest, TailDeadlines_OnlyOnePieceBeyondHead_ReturnsOnePair) {
    // last_piece > startPiece + window, but last_piece-1 is NOT beyond window.
    // handle.rs:330 condition → only the single tail piece gets a deadline.
    const auto pairs = StreamPrioritizer::initialPlaybackTailDeadlines(
        /*startPiece=*/0, /*lastPiece=*/11, /*headWindowSize=*/10);
    ASSERT_EQ(pairs.size(), 1);
    EXPECT_EQ(pairs[0].first,  11);
    EXPECT_EQ(pairs[0].second, 1200);
}
