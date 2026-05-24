// tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp
//
// COMICS_WC_VOLUME_WIRING Task 5 - pure-logic tests for the chapter-range
// filter. This file lands before MangaWeebCentralResolver.cpp, so Task 5 is
// the intentional RED link checkpoint.

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "core/manga/mangafire/MangaWeebCentralResolver.h"

using tankoban::manga::mangafire::MangaWeebCentralResolver;

TEST(MangaWeebCentralResolverFilter, FullCoverageSimple)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5" }, 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, SubsetReturnsFiltered)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5", "6", "7" }, 3, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, OutOfOrderInputSortedAscending)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "5", "3", "1", "4", "2" }, 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, PartialCoverageFlagged)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "5" }, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, NoOverlapEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "10", "11", "12" }, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, EmptyInputEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{}, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, AlphanumericChapterIdsParseNumericToken)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "chapter-1", "chapter-2", "chapter-3", "chapter-4", "chapter-5" },
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "chapter-1", "chapter-2", "chapter-3", "chapter-4", "chapter-5" }));
}

TEST(MangaWeebCentralResolverFilter, NonNumericChaptersIgnored)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "extra", "3", "side-story", "4", "5" },
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, SingleChapterRange)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "5" }, 5, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "5" }));
}

TEST(MangaWeebCentralResolverFilter, OutParamOptional)
{
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QStringList{ "1", "2", "3", "4", "5" }, 1, 5, nullptr);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}
