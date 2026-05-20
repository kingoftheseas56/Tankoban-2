// tests/core/manga/fandom/test_infobox_extractor.cpp

#include <gtest/gtest.h>

#include "core/manga/fandom/extractors/InfoboxExtractor.h"
#include "core/manga/fandom/WikiManifest.h"

#include <QFile>
#include <QJsonDocument>

using namespace tankoban::manga::fandom;

namespace {

QString infoboxFixturePath(const QString& relPath)
{
    return QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath;
}

WikiManifest loadManifest(const QString& relPath)
{
    QFile f(infoboxFixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    return WikiManifest::fromJson(doc.object());
}

QString loadFixture(const QString& relPath)
{
    QFile f(infoboxFixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // anonymous

// ──────────────────────────────────────────────────────────────────────────
// Task 10 — Kingdom hierarchy pattern (InfoboxExtractor)
// ──────────────────────────────────────────────────────────────────────────

TEST(InfoboxExtractorTest, Kingdom_Vol73_HasCoverUrl)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/kingdom.json"));
    ASSERT_TRUE(m.isValid()) << "kingdom manifest failed to load";

    QString html = loadFixture(
        QStringLiteral("fandom/kingdom_vol-73_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty());

    FandomVolume v = InfoboxExtractor::extractSingle(html, 73, m);
    EXPECT_EQ(v.volumeNumber, 73);
    EXPECT_TRUE(v.coverUrlJapanese.startsWith(
        "https://static.wikia.nocookie.net/kingdom-anime/"))
        << "actual: " << v.coverUrlJapanese.toStdString();
}

TEST(InfoboxExtractorTest, Kingdom_Vol73_HasReleaseDateAndIsbn)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/kingdom.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/kingdom_vol-73_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 73, m);
    EXPECT_EQ(v.releaseDateJp, QDate(2024, 9, 19));
    EXPECT_EQ(v.isbnJp.toStdString(), "978-4-08-893381-8");
    // Kingdom doesn't surface EN ISBNs.
    EXPECT_TRUE(v.isbnEn.isEmpty());
}

TEST(InfoboxExtractorTest, Kingdom_Vol73_SynopsisIsEmptySlot)
{
    // Kingdom Vol.73's <h2>Synopsis</h2> header is immediately followed by
    // <h2>Chapters</h2> with no body content in between. Verify the
    // extractor distinguishes "slot exists but empty" (whitespace-only)
    // from "slot absent" (no Synopsis header at all).
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/kingdom.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/kingdom_vol-73_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 73, m);
    // Empty after tag-strip + trim.
    EXPECT_TRUE(v.synopsis.isEmpty())
        << "Vol.73 has an empty-body Synopsis section; got: "
        << v.synopsis.toStdString();
}

TEST(InfoboxExtractorTest, Kingdom_Vol73_StampsVolumeNumberFromCaller)
{
    // InfoboxExtractor doesn't parse the volume number from the page — the
    // caller stamps it from the URL it fetched (Vol.73 → 73).
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/kingdom.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/kingdom_vol-73_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 73, m);
    EXPECT_EQ(v.volumeNumber, 73);

    // Passing a different number should produce a volume with that number
    // — proves the caller's URL-derived value wins.
    FandomVolume v999 = InfoboxExtractor::extractSingle(html, 999, m);
    EXPECT_EQ(v999.volumeNumber, 999);
}

// ──────────────────────────────────────────────────────────────────────────
// Task 11 — JJK Volume 0 prequel handling (InfoboxExtractor JJK variant)
// ──────────────────────────────────────────────────────────────────────────

TEST(InfoboxExtractorTest, Jjk_Vol0_PreservesVolumeNumberZero)
{
    // Volume 0 is a canon prequel — extractor must not skip or shift it.
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/jujutsu-kaisen.json"));
    ASSERT_TRUE(m.isValid()) << "jjk manifest failed to load";

    QString html = loadFixture(
        QStringLiteral("fandom/jujutsu-kaisen_volume-0_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty());

    FandomVolume v = InfoboxExtractor::extractSingle(html, 0, m);
    EXPECT_EQ(v.volumeNumber, 0);
}

TEST(InfoboxExtractorTest, Jjk_Vol0_HasAllTitleFields)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/jujutsu-kaisen.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/jujutsu-kaisen_volume-0_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 0, m);
    EXPECT_EQ(v.titleEnglish.toStdString(), "Blinding Darkness");
    EXPECT_EQ(v.titleRomaji.toStdString(), "Mabushii Yami");
    EXPECT_FALSE(v.titleJapanese.isEmpty());
}

TEST(InfoboxExtractorTest, Jjk_Vol0_HasJpAndEnReleaseFields)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/jujutsu-kaisen.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/jujutsu-kaisen_volume-0_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 0, m);
    EXPECT_EQ(v.releaseDateJp, QDate(2018, 12, 4));
    EXPECT_EQ(v.releaseDateEn, QDate(2021, 1, 5));
    EXPECT_EQ(v.isbnJp.toStdString(), "9784088816722");
    EXPECT_EQ(v.isbnEn.toStdString(), "9781974720149");
}

TEST(InfoboxExtractorTest, Jjk_Vol0_HasCoverUrl)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/jujutsu-kaisen.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/jujutsu-kaisen_volume-0_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 0, m);
    EXPECT_TRUE(v.coverUrlJapanese.startsWith(
        "https://static.wikia.nocookie.net/jujutsu-kaisen/"))
        << "actual: " << v.coverUrlJapanese.toStdString();
}

TEST(InfoboxExtractorTest, Jjk_Vol1_HasOwnIdentity)
{
    // Quick smoke that vol 1 isn't mis-extracted as vol 0 — distinct title
    // + cover URL.
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/jujutsu-kaisen.json"));
    QString html = loadFixture(
        QStringLiteral("fandom/jujutsu-kaisen_volume-1_2026-05-19.html"));

    FandomVolume v = InfoboxExtractor::extractSingle(html, 1, m);
    EXPECT_EQ(v.volumeNumber, 1);
    EXPECT_EQ(v.titleEnglish.toStdString(), "Ryomen Sukuna");
    EXPECT_EQ(v.titleRomaji.toStdString(), "Ry\xc5\x8dmen Sukuna");
    EXPECT_TRUE(v.coverUrlJapanese.contains("Volume_1.png"))
        << "actual: " << v.coverUrlJapanese.toStdString();
}
