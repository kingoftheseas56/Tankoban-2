// ── test_stream_seek_classifier.cpp ────────────────────────────────────────
// Stage 3a coverage per plan. Pure-function tests for StreamSeekClassifier
// (no Qt event loop, no QObject). Validates the M5 / M6 invariants the
// Congress 6 integration memo §5 pinned + the Stremio-reference semantics
// the header docstring documents.

#include "core/stream/StreamSeekClassifier.h"

#include <gtest/gtest.h>

// ── containerMetadataStart ─────────────────────────────────────────────────

TEST(StreamSeekClassifierTest, ContainerMetadataStart_LargeFile_PicksNinetyFivePercent) {
    // 1 GB file: 95 % = 973 MB, minus 10 MB = 1014 MB. min() picks 973 MB.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    const qint64 expected = (oneGb / 100) * 95;
    EXPECT_EQ(StreamSeekClassifier::containerMetadataStart(oneGb), expected);
}

TEST(StreamSeekClassifierTest, ContainerMetadataStart_MidFile_PicksTenMbSubtracted) {
    // 100 MB file: 95 % = 95 MB; minus 10 MB = 90 MB. min() picks 90 MB.
    const qint64 oneHundredMb = 100LL * 1024 * 1024;
    const qint64 expected = oneHundredMb - (10LL * 1024 * 1024);
    EXPECT_EQ(StreamSeekClassifier::containerMetadataStart(oneHundredMb), expected);
}

TEST(StreamSeekClassifierTest, ContainerMetadataStart_SmallFile_ClampsAtZero) {
    // File < 10 MB: saturating_sub clamps at 0. `.min(file_size * 95 / 100)`
    // then picks 0 (the smaller value).
    const qint64 fiveMb = 5LL * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::containerMetadataStart(fiveMb), 0);
}

TEST(StreamSeekClassifierTest, ContainerMetadataStart_ZeroOrNegative_ReturnsZero) {
    EXPECT_EQ(StreamSeekClassifier::containerMetadataStart(0), 0);
    EXPECT_EQ(StreamSeekClassifier::containerMetadataStart(-1), 0);
}

// ── classifySeek ───────────────────────────────────────────────────────────

TEST(StreamSeekClassifierTest, ClassifySeek_FirstAtZero_IsInitialPlayback) {
    // M5 invariant: cold-open first read maps to InitialPlayback so the caller
    // can drive Stremio's URGENT tier on the first piece.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::classifySeek(0, oneGb, true),
              StreamSeekType::InitialPlayback);
}

TEST(StreamSeekClassifierTest, ClassifySeek_FirstAtNonZero_FallsThrough) {
    // `isFirstClassification=true` alone is not sufficient — offset must also
    // be zero. Non-zero first call falls through to the offset-based path.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    const qint64 midOffset = 500LL * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::classifySeek(midOffset, oneGb, true),
              StreamSeekType::UserScrub);
}

TEST(StreamSeekClassifierTest, ClassifySeek_NotFirstAtZero_IsUserScrub) {
    // Subsequent classification at offset 0 (when not first) falls to the
    // tail check, fails, then returns UserScrub. Caller (Session) resolves
    // Sequential vs UserScrub via last-offset comparison.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::classifySeek(0, oneGb, false),
              StreamSeekType::UserScrub);
}

TEST(StreamSeekClassifierTest, ClassifySeek_InContainerTail_IsContainerMetadata) {
    // Offset within the last 10 MB of a large file (or last 5 %) classifies
    // as ContainerMetadata. 1 GB file → threshold at 973 MB. Offset 990 MB
    // is above threshold → ContainerMetadata.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    const qint64 tailOffset = 990LL * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::classifySeek(tailOffset, oneGb, false),
              StreamSeekType::ContainerMetadata);
}

TEST(StreamSeekClassifierTest, ClassifySeek_MidFile_IsUserScrub) {
    // Random mid-file seek (not first, not in tail region). Returns
    // UserScrub per header contract: caller (Session) distinguishes
    // Sequential vs UserScrub itself with last-offset cache.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    const qint64 midOffset = 500LL * 1024 * 1024;
    EXPECT_EQ(StreamSeekClassifier::classifySeek(midOffset, oneGb, false),
              StreamSeekType::UserScrub);
}

TEST(StreamSeekClassifierTest, ClassifySeek_ZeroFileSize_IsSequential) {
    // Defensive edge case: fileSize <= 0 short-circuits to Sequential before
    // the threshold check (can't compute a sensible container region).
    EXPECT_EQ(StreamSeekClassifier::classifySeek(0, 0, false),
              StreamSeekType::Sequential);
    EXPECT_EQ(StreamSeekClassifier::classifySeek(100, -1, false),
              StreamSeekType::Sequential);
}

TEST(StreamSeekClassifierTest, ClassifySeek_ExactlyAtThreshold_IsContainerMetadata) {
    // Sentinel: offset exactly at the threshold should be ContainerMetadata
    // (the `>=` comparison). Protects against off-by-one during future edits.
    const qint64 oneGb = 1LL * 1024 * 1024 * 1024;
    const qint64 threshold = StreamSeekClassifier::containerMetadataStart(oneGb);
    EXPECT_EQ(StreamSeekClassifier::classifySeek(threshold, oneGb, false),
              StreamSeekType::ContainerMetadata);
    EXPECT_EQ(StreamSeekClassifier::classifySeek(threshold - 1, oneGb, false),
              StreamSeekType::UserScrub);
}
