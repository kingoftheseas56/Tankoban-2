// tests/core/manga/fandom/test_table_extractor.cpp

#include <gtest/gtest.h>

#include "core/manga/fandom/extractors/TableExtractor.h"
#include "core/manga/fandom/WikiManifest.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>

using namespace tankoban::manga::fandom;

namespace {

QString fixturePath(const QString& relPath)
{
    return QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath;
}

WikiManifest loadManifest(const QString& relPath)
{
    QFile f(fixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    return WikiManifest::fromJson(doc.object());
}

QString loadFixture(const QString& relPath)
{
    QFile f(fixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // anonymous

TEST(TableExtractorTest, DeathNote_ExtractsExactly12MainVolumes)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    ASSERT_TRUE(m.isValid()) << "manifest failed to load";

    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty()) << "fixture empty or missing";

    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    EXPECT_EQ(vols.size(), 12)
        << "Death Note has 12 main volumes; 'Volume 13: How to Read' + "
        << "'Short Stories' filtered out via manifest editionFilters";
}

TEST(TableExtractorTest, DeathNote_Vol1_HasEnglishTitleAndJapanese)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.volumeNumber, 1);
    EXPECT_EQ(v1.titleEnglish.toStdString(), "Boredom");
    // JP kanji "退屈" + romaji "Taikutsu" — exact-string match validates the
    // id-suffix splitter and the UTF-8 round-trip.
    EXPECT_EQ(v1.titleJapanese.toStdString(), std::string("\xe9\x80\x80\xe5\xb1\x88"));
    EXPECT_EQ(v1.titleRomaji.toStdString(), "Taikutsu");
}

TEST(TableExtractorTest, DeathNote_VolumesAreInDocumentOrder)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_EQ(vols.size(), 12);

    for (int i = 0; i < vols.size(); ++i) {
        EXPECT_EQ(vols[i].volumeNumber, i + 1)
            << "volume " << (i + 1) << " out of order";
    }
}

TEST(TableExtractorTest, DeathNote_Vol1_HasReleaseDatesAndIsbns)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.releaseDateJp, QDate(2004, 4, 2));
    EXPECT_EQ(v1.releaseDateEn, QDate(2005, 10, 10));
    EXPECT_EQ(v1.isbnJp.toStdString(), "978-4-088-73621-1");
    EXPECT_EQ(v1.isbnEn.toStdString(), "978-1-421-50168-0");
}

TEST(TableExtractorTest, DeathNote_Vol1_HasBothCoverUrls)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_TRUE(v1.coverUrlEnglish.startsWith("https://static.wikia.nocookie.net/"))
        << "actual: " << v1.coverUrlEnglish.toStdString();
    EXPECT_TRUE(v1.coverUrlJapanese.startsWith("https://static.wikia.nocookie.net/"))
        << "actual: " << v1.coverUrlJapanese.toStdString();
    EXPECT_NE(v1.coverUrlEnglish, v1.coverUrlJapanese);
}

TEST(TableExtractorTest, DeathNote_FiltersOutHowToReadAndShortStories)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);

    for (const auto& v : vols) {
        EXPECT_NE(v.volumeNumber, 13) << "Volume 13 (How to Read) should be filtered";
        EXPECT_FALSE(v.titleEnglish.contains("How to Read"));
        EXPECT_FALSE(v.titleEnglish.contains("Short Stories"));
    }
}

TEST(TableExtractorTest, UnsupportedGroupingSemantics_ReturnsEmpty)
{
    WikiManifest m;
    m.seriesId          = QStringLiteral("test");
    m.fandomWikiId      = QStringLiteral("test");
    m.groupingSemantics = QStringLiteral("multi-era"); // Task 9 territory
    QList<FandomVolume> vols = TableExtractor::extract(
        QStringLiteral("<html></html>"), m);
    EXPECT_TRUE(vols.isEmpty());
}

// ──────────────────────────────────────────────────────────────────────────
// Task 8 — Berserk narrative-arcs pattern
// ──────────────────────────────────────────────────────────────────────────

TEST(TableExtractorTest, Berserk_ExtractsAtLeast42Volumes)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    ASSERT_TRUE(m.isValid()) << "berserk manifest failed to load";

    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty()) << "fixture empty or missing";

    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    EXPECT_GE(vols.size(), 42)
        << "Berserk had 42 published canonical volumes when Miura passed; "
        << "actual: " << vols.size();
}

TEST(TableExtractorTest, Berserk_Vol1_HasBlackSwordsmanArcGroupingLabel)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.volumeNumber, 1);
    EXPECT_EQ(v1.groupingLabel.toStdString(), "Black Swordsman Arc");
    // Manifest declares titles as absent — verify the extractor honors that.
    EXPECT_TRUE(v1.titleEnglish.isEmpty());
    EXPECT_TRUE(v1.titleJapanese.isEmpty());
}

TEST(TableExtractorTest, Berserk_Vol1_HasReleaseDatesAndIsbns)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    // Berserk uses DD Month YYYY date format.
    EXPECT_EQ(v1.releaseDateJp, QDate(1990, 11, 26));
    EXPECT_EQ(v1.releaseDateEn, QDate(2003, 10, 22));
    EXPECT_EQ(v1.isbnJp.toStdString(), "9784592135746");
    EXPECT_EQ(v1.isbnEn.toStdString(), "9781593070205");
}

TEST(TableExtractorTest, Berserk_Vol1_HasCoverUrl)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    EXPECT_TRUE(vols[0].coverUrlJapanese.startsWith(
        "https://static.wikia.nocookie.net/berserk/"))
        << "actual: " << vols[0].coverUrlJapanese.toStdString();
}

TEST(TableExtractorTest, Berserk_VolumesAreInDocumentOrder)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 2);

    for (int i = 0; i < vols.size(); ++i) {
        EXPECT_EQ(vols[i].volumeNumber, i + 1)
            << "volume index " << i << " mismatch";
    }
}

TEST(TableExtractorTest, Berserk_Vol3_HasMultipleArcsJoined)
{
    // Vol 3 sits at the Black Swordsman / Golden Age boundary — its Arc(s)
    // cell carries both. Verify the joined groupingLabel.
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/berserk.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/berserk_releases-manga_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 3);

    const QString& label = vols[2].groupingLabel;
    EXPECT_TRUE(label.contains("Black Swordsman", Qt::CaseInsensitive))
        << "actual: " << label.toStdString();
    EXPECT_TRUE(label.contains("Golden Age", Qt::CaseInsensitive))
        << "actual: " << label.toStdString();
}

// ──────────────────────────────────────────────────────────────────────────
// Task 7 — One Piece mathematical-buckets pattern
// ──────────────────────────────────────────────────────────────────────────

TEST(TableExtractorTest, OnePiece_ExtractsAtLeast100Volumes)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    ASSERT_TRUE(m.isValid()) << "one-piece manifest failed to load";

    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty()) << "fixture empty or missing";

    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    EXPECT_GE(vols.size(), 100)
        << "One Piece has well over 100 published volumes; got "
        << vols.size();
}

TEST(TableExtractorTest, OnePiece_Vol1_HasJapanAndUsTitles)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.volumeNumber, 1);
    EXPECT_EQ(v1.titleEnglish.toStdString(), "Romance Dawn");
    // JP title is "ROMANCE DAWN—冒険の夜明け—" — assert prefix + non-empty
    // (full Unicode equality is exercised by the Death Note test).
    EXPECT_TRUE(v1.titleJapanese.startsWith(QStringLiteral("ROMANCE DAWN")))
        << "actual: " << v1.titleJapanese.toStdString();
}

TEST(TableExtractorTest, OnePiece_Vol1_HasReleaseDatesAndIsbns)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_EQ(v1.releaseDateJp, QDate(1997, 12, 24));
    EXPECT_EQ(v1.releaseDateEn, QDate(2003, 6, 30));
    EXPECT_EQ(v1.isbnJp.toStdString(), "978-4-08-872509-3");
    EXPECT_EQ(v1.isbnEn.toStdString(), "978-1-56931-901-7");
}

TEST(TableExtractorTest, OnePiece_Vol1_HasJapanCoverUrl)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 1);

    const FandomVolume& v1 = vols[0];
    EXPECT_TRUE(v1.coverUrlJapanese.startsWith(
        "https://static.wikia.nocookie.net/onepiece/"))
        << "actual: " << v1.coverUrlJapanese.toStdString();
}

TEST(TableExtractorTest, OnePiece_VolumesAreInDocumentOrder)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_GE(vols.size(), 2);

    for (int i = 0; i < vols.size(); ++i) {
        EXPECT_EQ(vols[i].volumeNumber, i + 1)
            << "volume index " << i << " mismatch";
    }
}

TEST(TableExtractorTest, OnePiece_SpecialVolumesSectionFiltered)
{
    // The fixture contains a "Special Volumes" h2 section after the canon
    // 1..N range. With the edition filter, that section must be trimmed
    // pre-extraction; we verify no extracted volume's English title contains
    // typical Special-Volumes markers.
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/one-piece.json"));
    QString html = loadFixture(QStringLiteral(
        "fandom/one-piece_chapters-and-volumes-volumes_2026-05-19.html"));
    QList<FandomVolume> vols = TableExtractor::extract(html, m);

    for (const auto& v : vols) {
        EXPECT_FALSE(v.titleEnglish.contains("Special", Qt::CaseInsensitive))
            << "vol " << v.volumeNumber << " leaked: " << v.titleEnglish.toStdString();
        EXPECT_FALSE(v.titleEnglish.contains("Stampede", Qt::CaseInsensitive));
    }
}
