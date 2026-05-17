#include "core/manga/mangaupdates/MangaUpdatesDisambiguator.h"
#include "core/manga/mangaupdates/MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::mangaupdates;
using tankoban::manga::anilist::MediaPreview;

namespace {

MangaUpdatesSearchHit hit(qint64 id, const QString& title, const QStringList& authors, int year)
{
    MangaUpdatesSearchHit h;
    h.seriesId = id;
    h.title = title;
    h.authors = authors;
    h.yearStarted = year;
    return h;
}

MediaPreview preview(const QString& title, int year)
{
    MediaPreview p;
    p.title = title;
    p.yearStarted = year;
    return p;
}

} // namespace

TEST(MangaUpdatesDisambiguatorTest, SingleExactTitleMatchWins)
{
    const QList<MangaUpdatesSearchHit> hits = {
        hit(4324727424, QStringLiteral("Kingdom"), {QStringLiteral("Hara, Yasuhisa")}, 2006),
    };
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(
        hits, preview(QStringLiteral("Kingdom"), 2006),
        QStringList{QStringLiteral("Hara, Yasuhisa")}), 4324727424);
}

TEST(MangaUpdatesDisambiguatorTest, MultiHitDisambiguatesByAuthorSurname)
{
    const QList<MangaUpdatesSearchHit> hits = {
        hit(99999, QStringLiteral("Kingdom"), {QStringLiteral("Other, Author")}, 2010),
        hit(4324727424, QStringLiteral("Kingdom"), {QStringLiteral("Hara, Yasuhisa")}, 2006),
    };
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(
        hits, preview(QStringLiteral("Kingdom"), 2006),
        QStringList{QStringLiteral("Yasuhisa Hara")}), 4324727424);
}

TEST(MangaUpdatesDisambiguatorTest, MultiHitDisambiguatesByYearWhenAuthorsTie)
{
    const QList<MangaUpdatesSearchHit> hits = {
        hit(11111, QStringLiteral("Berserk"), {}, 2018),
        hit(51239621230, QStringLiteral("Berserk"), {}, 1989),
    };
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(
        hits, preview(QStringLiteral("Berserk"), 1989), QStringList{}), 51239621230);
}

TEST(MangaUpdatesDisambiguatorTest, EmptyHitsReturnsZero)
{
    const QList<MangaUpdatesSearchHit> hits;
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(
        hits, preview(QStringLiteral("Unknown"), 2020), QStringList{}), 0);
}

TEST(MangaUpdatesDisambiguatorTest, NoTitleMatchReturnsZero)
{
    const QList<MangaUpdatesSearchHit> hits = {
        hit(11111, QStringLiteral("Kingdom Hearts III"), {}, 2019),
        hit(22222, QStringLiteral("A Kingdom of Quartz"), {}, 2015),
    };
    EXPECT_EQ(MangaUpdatesDisambiguator::bestMatch(
        hits, preview(QStringLiteral("Kingdom"), 2006), QStringList{}), 0);
}
