// tests/ui/readers/test_comic_reader_pairing.cpp
//
// Pure-logic coverage for Tankoban-Max Build 17 double-page pairing parity.

#include <gtest/gtest.h>

#include "ui/readers/ComicReader.h"

namespace {

QVector<TwoPagePairingPage> pages(int count, std::initializer_list<int> spreads = {})
{
    QVector<TwoPagePairingPage> out(count);
    for (int index : spreads) {
        if (index >= 0 && index < out.size()) out[index].isSpread = true;
    }
    return out;
}

void forceOverride(QVector<TwoPagePairingPage>& pages, int index, bool spread)
{
    ASSERT_GE(index, 0);
    ASSERT_LT(index, pages.size());
    pages[index].hasSpreadOverride = true;
    pages[index].spreadOverride = spread;
}

} // namespace

TEST(ComicReaderPairing, AllNormalVolumePairsAfterCover)
{
    const auto pairs = buildTwoPagePairs(pages(6), false);

    ASSERT_EQ(pairs.size(), 4);
    EXPECT_TRUE(pairs[0].coverAlone);
    EXPECT_EQ(pairs[0].rightIndex, 0);

    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_EQ(pairs[1].leftIndex, 2);
    EXPECT_FALSE(pairs[1].isSpread);

    EXPECT_EQ(pairs[2].rightIndex, 3);
    EXPECT_EQ(pairs[2].leftIndex, 4);

    EXPECT_EQ(pairs[3].rightIndex, 5);
    EXPECT_EQ(pairs[3].leftIndex, -1);
    EXPECT_TRUE(pairs[3].unpairedSingle);
}

TEST(ComicReaderPairing, MidVolumeSpreadConsumesExtraSlotBeforeDownstreamPairing)
{
    const auto state = pages(7, {2});
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 6);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_TRUE(pairs[1].unpairedSingle);

    EXPECT_EQ(pairs[2].rightIndex, 2);
    EXPECT_TRUE(pairs[2].isSpread);

    EXPECT_EQ(pairs[3].rightIndex, 3);
    EXPECT_TRUE(pairs[3].unpairedSingle);
    EXPECT_EQ(pairs[4].rightIndex, 4);
    EXPECT_EQ(pairs[4].leftIndex, 5);
    EXPECT_EQ(twoPageExtraSlotsBefore(state, 5), 1);
    EXPECT_EQ(twoPageEffectiveIndex(state, 5), 6);
}

TEST(ComicReaderPairing, MultipleSpreadsEachConsumeOneExtraSlot)
{
    const auto state = pages(9, {2, 5});
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 8);
    EXPECT_TRUE(pairs[2].isSpread);
    EXPECT_EQ(pairs[2].rightIndex, 2);
    EXPECT_TRUE(pairs[5].isSpread);
    EXPECT_EQ(pairs[5].rightIndex, 5);
    EXPECT_EQ(twoPageExtraSlotsBefore(state, 7), 2);
    EXPECT_EQ(pairs[6].rightIndex, 6);
    EXPECT_TRUE(pairs[6].unpairedSingle);
    EXPECT_EQ(pairs[7].rightIndex, 7);
    EXPECT_EQ(pairs[7].leftIndex, 8);
}

TEST(ComicReaderPairing, CoverCanBeAStandaloneSpread)
{
    const auto pairs = buildTwoPagePairs(pages(4, {0}), false);

    ASSERT_GE(pairs.size(), 1);
    EXPECT_EQ(pairs[0].rightIndex, 0);
    EXPECT_TRUE(pairs[0].isSpread);
    EXPECT_FALSE(pairs[0].coverAlone);
}

TEST(ComicReaderPairing, LastPageSpreadStaysStandalone)
{
    const auto pairs = buildTwoPagePairs(pages(5, {4}), false);

    ASSERT_EQ(pairs.size(), 4);
    EXPECT_EQ(pairs[3].rightIndex, 4);
    EXPECT_TRUE(pairs[3].isSpread);
    EXPECT_EQ(pairs[3].leftIndex, -1);
}

TEST(ComicReaderPairing, CouplingNudgeFlipsNormalPairStarts)
{
    const auto pairs = buildTwoPagePairs(pages(5), true);

    ASSERT_EQ(pairs.size(), 4);
    EXPECT_TRUE(pairs[1].unpairedSingle);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_EQ(pairs[2].rightIndex, 2);
    EXPECT_EQ(pairs[2].leftIndex, 3);
}

TEST(ComicReaderPairing, ForceNormalOverrideBeatsAutoSpread)
{
    auto state = pages(5, {2});
    forceOverride(state, 2, false);

    EXPECT_FALSE(resolveTwoPageSpread(state, 2));

    const auto pairs = buildTwoPagePairs(state, false);
    ASSERT_EQ(pairs.size(), 3);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_EQ(pairs[1].leftIndex, 2);
}

TEST(ComicReaderPairing, ForceSpreadOverrideBeatsAutoNormal)
{
    auto state = pages(5);
    forceOverride(state, 2, true);

    EXPECT_TRUE(resolveTwoPageSpread(state, 2));

    const auto pairs = buildTwoPagePairs(state, false);
    ASSERT_EQ(pairs.size(), 5);
    EXPECT_EQ(pairs[2].rightIndex, 2);
    EXPECT_TRUE(pairs[2].isSpread);
}

// ── VOLUME_X_CHAPTER_PAIRING 2026-05-27 (Agent 1) ────────────────────────────
// Synthesized Volume X cbzs stitch uncollected chapters that were never laid
// out as a cohesive volume. Each chapter's first page must display alone
// (cover-style) and the pages after it must pair fresh, regardless of the
// previous chapter's page count.

namespace {
void markChapterStart(QVector<TwoPagePairingPage>& pages, int index)
{
    ASSERT_GE(index, 0);
    ASSERT_LT(index, pages.size());
    pages[index].isChapterStart = true;
}
} // namespace

TEST(ComicReaderPairing, ChapterStartShowsAloneAndContentPairsFresh)
{
    // 0=vol cover, 1-2=ch0 content (even), 3=ch1 cover, 4-5=ch1 content.
    auto state = pages(6);
    markChapterStart(state, 3);
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 4);
    EXPECT_TRUE(pairs[0].coverAlone);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_EQ(pairs[1].leftIndex, 2);

    EXPECT_EQ(pairs[2].rightIndex, 3);
    EXPECT_EQ(pairs[2].leftIndex, -1);
    EXPECT_TRUE(pairs[2].coverAlone);

    EXPECT_EQ(pairs[3].rightIndex, 4);
    EXPECT_EQ(pairs[3].leftIndex, 5);
}

TEST(ComicReaderPairing, OddPriorChapterTailFallsSingleBeforeChapterCover)
{
    // 0=vol cover, 1-3=ch0 content (odd), 4=ch1 cover, 5-6=ch1 content.
    auto state = pages(7);
    markChapterStart(state, 4);
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 5);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_EQ(pairs[1].leftIndex, 2);

    // ch0's odd tail page falls as a single — NOT paired into the ch1 cover.
    EXPECT_EQ(pairs[2].rightIndex, 3);
    EXPECT_TRUE(pairs[2].unpairedSingle);

    EXPECT_EQ(pairs[3].rightIndex, 4);
    EXPECT_TRUE(pairs[3].coverAlone);

    EXPECT_EQ(pairs[4].rightIndex, 5);
    EXPECT_EQ(pairs[4].leftIndex, 6);
}

TEST(ComicReaderPairing, ColorSpreadChapterCoverShowsAloneAndResetsParity)
{
    // 0=vol cover, 1-2=ch0, 3=ch1 cover that is itself a color spread, 4-5=ch1.
    auto state = pages(6, {3});
    markChapterStart(state, 3);
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 4);
    EXPECT_EQ(pairs[2].rightIndex, 3);
    EXPECT_TRUE(pairs[2].isSpread);
    EXPECT_EQ(pairs[2].leftIndex, -1);

    // Content after the spread cover still pairs fresh.
    EXPECT_EQ(pairs[3].rightIndex, 4);
    EXPECT_EQ(pairs[3].leftIndex, 5);
}

TEST(ComicReaderPairing, ChapterStartNeverPairedAsLeftPartner)
{
    // 0=vol cover, 1=ch0 single content page, 2=ch1 cover.
    auto state = pages(3);
    markChapterStart(state, 2);
    const auto pairs = buildTwoPagePairs(state, false);

    ASSERT_EQ(pairs.size(), 3);
    EXPECT_EQ(pairs[1].rightIndex, 1);
    EXPECT_TRUE(pairs[1].unpairedSingle);  // not paired into the ch1 cover

    EXPECT_EQ(pairs[2].rightIndex, 2);
    EXPECT_TRUE(pairs[2].coverAlone);
}
