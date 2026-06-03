#include <gtest/gtest.h>
#include "core/stream/AutoSourcePicker.h"

using tankostream::stream::AutoSourcePicker;
using tankostream::stream::SourceCandidate;

static SourceCandidate c(const QString& title, int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
}

TEST(AutoSourcePicker, PicksHighestSeeded1080p) {
    QList<SourceCandidate> v {
        c("One Piece S01E01 [SubsPlease] 1080p", 1200, 1400000000LL, 3),
        c("One Piece S01E01 1080p WEB-DL",          300, 1500000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, ExcludesNon1080p) {
    QList<SourceCandidate> v {
        c("Show S01E01 720p",  900, 700000000LL, 2),
        c("Show S01E01 2160p", 800, 9000000000LL, 5),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesZeroSeeders) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 0, 1400000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesCamRips) {
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie 2024 1080p CAM"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.TELESYNC.1080p"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.HDCAM.1080p"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("Movie.2024.1080p.WEB-DL"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("GUTS.S01E01.1080p"));  // no false-positive on 'TS'

    QList<SourceCandidate> v { c("Movie 2024 1080p CAM", 500, 3000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 120).has_value());
}

TEST(AutoSourcePicker, WellSeededIgnoresSize) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 1200, 30000000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, LowSeedTailDropsReencode) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailDropsRemux) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 6, 13000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailKeepsSaneSized) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 1400000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, UnknownRuntimeSkipsSizeGuardrail) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 0);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, TieOnSeedersBreaksByReleaseType) {
    QList<SourceCandidate> v {
        c("Show S01E01 1080p WEBRip", 10, 1400000000LL, 3),
        c("Show S01E01 1080p BluRay", 10, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1);  // BluRay (higher sourceScore) wins the tie
}

TEST(AutoSourcePicker, NegativeSizeTreatedAsUnknown) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, -1LL, 3) };
    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());  // unknown size -> kept
}

TEST(AutoSourcePicker, EmptyListReturnsNone) {
    EXPECT_FALSE(AutoSourcePicker::pick({}, 24).has_value());
}

// ── Show-identity gate (DOWNLOAD BUG 2026-06-02) ────────────────────────────
// Clicking Download on One Piece downloaded 'Community' because the picker
// ranked by seeders/quality and never checked the result was the right show.
// pick(candidates, showTitle, runtime) rejects candidates whose release title
// lacks the show's significant tokens BEFORE ranking.

TEST(AutoSourcePicker, TitleGateSkipsWrongShowEvenIfBetterSeeded) {
    QList<SourceCandidate> v {
        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265-POIASD", 5000, 50000000000LL, 3),
        c("One Piece - 1164 [1080p].mkv",                         800,  1400000000LL,  3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1) << "must skip the better-seeded Community pack and pick One Piece";
}

TEST(AutoSourcePicker, TitleGateReturnsNoneWhenNoCandidateMatchesShow) {
    QList<SourceCandidate> v {
        c("Community.S01-S06.COMPLETE.1080p.BluRay.x265", 5000, 50000000000LL, 3),
        c("Friends.S01.1080p.BluRay.x265",                4000, 40000000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
}

TEST(AutoSourcePicker, TitleMatchesShowNormalizesSeparatorsAndTokens) {
    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("One.Piece.1164.1080p.WEB-DL"), QStringLiteral("One Piece")));
    EXPECT_TRUE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("[Erai-raws] One Piece - 1164 [1080p]"), QStringLiteral("One Piece")));
    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(
        QStringLiteral("Community.S01-S06.COMPLETE.1080p"), QStringLiteral("One Piece")));
    EXPECT_FALSE(AutoSourcePicker::titleMatchesShow(  // shares only the stop-ish 'one'
        QStringLiteral("One.Tree.Hill.S01.1080p"), QStringLiteral("One Piece")));
}

TEST(AutoSourcePicker, EmptyShowTitleSkipsGate) {
    QList<SourceCandidate> v { c("Anything 1080p", 1000, 1400000000LL, 3) };
    EXPECT_TRUE(AutoSourcePicker::pick(v, 24).has_value());            // int overload
    EXPECT_TRUE(AutoSourcePicker::pick(v, QString(), 24).has_value()); // empty gate
}

// ── matchText identity blob (DOWNLOAD BUG 2026-06-03) ───────────────────────
// NyaaSi / anime-shaped addons put a stats badge ("[2176 seeders | 1.34 GB |
// NyaaSi]") in the display title, so gating on `title` rejected EVERY real One Piece
// result -> "No 1080p source found" even though the right show's sources
// arrived. The gate now matches `matchText` (full raw identity blob) when set,
// falling back to `title` when empty.

static SourceCandidate cm(const QString& title, const QString& matchText,
                          int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.matchText = matchText;
    s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
}

TEST(AutoSourcePicker, MatchTextLetsBadgeTitledOnePiecePassGate) {
    // title is the useless stats badge; the real filename lives in matchText.
    QList<SourceCandidate> v {
        cm(QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]"),
           QStringLiteral("[2176 seeders | 1.34 GB | NyaaSi]\n[SubsPlease] One Piece - 1164 (1080p) [F9D2E8C1].mkv"),
           2176, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value()) << "matchText carries 'One Piece' -> must pass the gate";
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, MatchTextStillRejectsWrongShowBadge) {
    // Badge title AND a matchText for the wrong show -> still rejected (the gate
    // is not weakened, it just looks at the right text).
    QList<SourceCandidate> v {
        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
           QStringLiteral("[5000 seeders | 50 GB | NyaaSi]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.x265.mkv"),
           5000, 50000000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
}

TEST(AutoSourcePicker, MatchTextBadgePicksOnePieceOverBetterSeededWrongShow) {
    QList<SourceCandidate> v {
        cm(QStringLiteral("[5000 seeders | 50 GB | NyaaSi]"),
           QStringLiteral("[5000 seeders]\nCommunity.S01-S06.COMPLETE.1080p.BluRay.mkv"),
           5000, 50000000000LL, 3),
        cm(QStringLiteral("[800 seeders | 1.34 GB | NyaaSi]"),
           QStringLiteral("[800 seeders]\n[Erai-raws] One Piece - 1164 [1080p].mkv"),
           800, 1400000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1) << "must skip the better-seeded wrong-show badge and pick One Piece";
}

TEST(AutoSourcePicker, EmptyMatchTextFallsBackToTitleGate) {
    // matchText empty -> gate uses title (unchanged legacy behavior).
    QList<SourceCandidate> v {
        c(QStringLiteral("One Piece - 1164 [1080p].mkv"), 800, 1400000000LL, 3),
    };
    EXPECT_TRUE(AutoSourcePicker::pick(v, QStringLiteral("One Piece"), 24).has_value());
    QList<SourceCandidate> w {
        c(QStringLiteral("Community.S01.1080p.mkv"), 800, 1400000000LL, 3),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(w, QStringLiteral("One Piece"), 24).has_value());
}
