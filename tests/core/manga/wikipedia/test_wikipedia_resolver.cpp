// tests/core/manga/wikipedia/test_wikipedia_resolver.cpp

#include <gtest/gtest.h>

#include "core/manga/wikipedia/WikipediaParser.h"

#include <QFile>

using tankoban::manga::fandom::FandomVolume;

namespace {

QString loadWpFixture(const QString& relPath)
{
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // anonymous

// ──────────────────────────────────────────────────────────────────────────
// Task 15 — Wikipedia tier-2 fallback parser
// ──────────────────────────────────────────────────────────────────────────

TEST(WikipediaResolverTest, DeathNote_ParsesExactly12Volumes)
{
    QString html = loadWpFixture(
        QStringLiteral("wikipedia/list-of-death-note-chapters_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty()) << "fixture missing";

    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(html);
    EXPECT_EQ(vols.size(), 12)
        << "Death Note has 12 main volumes on Wikipedia; got " << vols.size();
}

TEST(WikipediaResolverTest, DeathNote_Vol1_HasAllTitleFields)
{
    QString html = loadWpFixture(
        QStringLiteral("wikipedia/list-of-death-note-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(html);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.volumeNumber, 1);
    EXPECT_EQ(v1.titleEnglish.toStdString(), "Boredom");
    EXPECT_EQ(v1.titleRomaji.toStdString(), "Taikutsu");
    // Kanji "退屈" preserved through UTF-8 round-trip.
    EXPECT_EQ(v1.titleJapanese.toStdString(), std::string("\xe9\x80\x80\xe5\xb1\x88"));
}

TEST(WikipediaResolverTest, DeathNote_Vol1_HasReleaseDatesAndIsbns)
{
    QString html = loadWpFixture(
        QStringLiteral("wikipedia/list-of-death-note-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(html);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.releaseDateJp, QDate(2004, 4, 2));
    EXPECT_EQ(v1.releaseDateEn, QDate(2005, 10, 10));
    EXPECT_EQ(v1.isbnJp.toStdString(),  "4-08-873621-4");
    EXPECT_EQ(v1.isbnEn.toStdString(),  "978-1-4215-0168-0");
}

TEST(WikipediaResolverTest, DeathNote_VolumesAreInDocumentOrder)
{
    QString html = loadWpFixture(
        QStringLiteral("wikipedia/list-of-death-note-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(html);
    ASSERT_EQ(vols.size(), 12);

    for (int i = 0; i < vols.size(); ++i) {
        EXPECT_EQ(vols[i].volumeNumber, i + 1)
            << "volume index " << i << " mismatch";
    }
}

TEST(WikipediaResolverTest, EmptyHtmlReturnsEmpty)
{
    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(QStringLiteral(""));
    EXPECT_TRUE(vols.isEmpty());
}

TEST(WikipediaResolverTest, NonsenseHtmlReturnsEmpty)
{
    QList<FandomVolume> vols = tankoban::manga::wikipedia::parseVolumeTable(
        QStringLiteral("<html><body>no volumes here</body></html>"));
    EXPECT_TRUE(vols.isEmpty());
}
