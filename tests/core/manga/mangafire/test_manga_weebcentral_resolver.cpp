// tests/core/manga/mangafire/test_manga_weebcentral_resolver.cpp
//
// COMICS_WC_VOLUME_WIRING - pure-logic tests for matching MangaFire
// volume chapter ranges to WeebCentral chapter refs.

#include <gtest/gtest.h>

#include <initializer_list>
#include <utility>

#include <QList>
#include <QString>
#include <QStringList>

#include "core/manga/mangafire/MangaWeebCentralResolver.h"

using tankoban::manga::mangafire::MangaWeebCentralResolver;

namespace {

using ChapterRef = MangaWeebCentralResolver::ChapterRef;

QList<ChapterRef> refs(std::initializer_list<std::pair<int, const char*>> items)
{
    QList<ChapterRef> out;
    out.reserve(static_cast<int>(items.size()));
    for (const auto& item : items) {
        out.append(ChapterRef{item.first, QString::fromUtf8(item.second)});
    }
    return out;
}

} // namespace

TEST(MangaWeebCentralResolverFilter, FullCoverageSimple)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}, {5, "5"}}), 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, SubsetReturnsFiltered)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}, {5, "5"}, {6, "6"}, {7, "7"}}),
        3, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, OutOfOrderInputSortedAscending)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{5, "5"}, {3, "3"}, {1, "1"}, {4, "4"}, {2, "2"}}), 1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "1", "2", "3", "4", "5" }));
}

TEST(MangaWeebCentralResolverFilter, PartialCoverageFlagged)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "1"}, {2, "2"}, {3, "3"}, {5, "5"}}), 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, NoOverlapEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{10, "10"}, {11, "11"}, {12, "12"}}), 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, EmptyInputEmptyResult)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        QList<ChapterRef>{}, 1, 5, &incomplete);
    EXPECT_TRUE(incomplete);
    EXPECT_TRUE(out.isEmpty());
}

TEST(MangaWeebCentralResolverFilter, OpaqueChapterIdsUseParsedChapterNumbers)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "01J76X-deathnote-a"}, {2, "01J76X-deathnote-b"},
              {3, "01J76X-deathnote-c"}, {4, "01J76X-deathnote-d"},
              {5, "01J76X-deathnote-e"}}),
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "01J76X-deathnote-a", "01J76X-deathnote-b",
                                 "01J76X-deathnote-c", "01J76X-deathnote-d",
                                 "01J76X-deathnote-e" }));
}

TEST(MangaWeebCentralResolverFilter, InvalidChapterNumbersIgnored)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "a"}, {2, "b"}, {0, "extra"}, {3, "c"}, {-1, "side"}, {4, "d"}, {5, "e"}}),
        1, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "a", "b", "c", "d", "e" }));
}

TEST(MangaWeebCentralResolverFilter, SingleChapterRange)
{
    bool incomplete = false;
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{5, "wc-opaque-5"}}), 5, 5, &incomplete);
    EXPECT_FALSE(incomplete);
    EXPECT_EQ(out, (QStringList{ "wc-opaque-5" }));
}

TEST(MangaWeebCentralResolverFilter, OutParamOptional)
{
    const auto out = MangaWeebCentralResolver::filterChaptersToRange(
        refs({{1, "a"}, {2, "b"}, {3, "c"}, {4, "d"}, {5, "e"}}), 1, 5, nullptr);
    EXPECT_EQ(out, (QStringList{ "a", "b", "c", "d", "e" }));
}
