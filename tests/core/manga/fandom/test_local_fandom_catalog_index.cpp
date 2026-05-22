// tests/core/manga/fandom/test_local_fandom_catalog_index.cpp
#include <gtest/gtest.h>

#include "core/manga/fandom/LocalFandomCatalogIndex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

using namespace tankoban::manga::fandom;

namespace {

void writeStubCatalog(const QString& dir, const QString& filename,
                     const QString& seriesId, int anilistId) {
    QFile f(dir + "/" + filename);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QString(R"({"seriesId": "%1", "anilistId": %2, "fandomUrl": "https://x.fandom.com/wiki/Y", "volumes": [{"number": 1, "title": "T"}]})")
              .arg(seriesId).arg(anilistId);
    f.close();
}

// FANDOM_LOCAL_LOADER_INTEGRATION 2026-05-22 hotfix: catalogs may carry an
// optional top-level seriesTitle field used for the WeebCentral-identity-
// path fallback lookup. This helper writes the richer shape.
void writeStubCatalogWithTitle(const QString& dir, const QString& filename,
                               const QString& seriesId, int anilistId,
                               const QString& seriesTitle) {
    QFile f(dir + "/" + filename);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QString(R"({"seriesId": "%1", "seriesTitle": "%2", "anilistId": %3, "fandomUrl": "https://x.fandom.com/wiki/Y", "volumes": [{"number": 1, "title": "T"}]})")
              .arg(seriesId, seriesTitle).arg(anilistId);
    f.close();
}

} // namespace

TEST(LocalFandomCatalogIndex, EmptyDirectoryHasSizeZero) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();
    EXPECT_EQ(idx.size(), 0);
    EXPECT_TRUE(idx.slugForAnilistId(30013).isEmpty());
}

TEST(LocalFandomCatalogIndex, IndexesOnePieceByAnilistId) {
    QTemporaryDir tmp;
    writeStubCatalog(tmp.path(), "one-piece.json", "one-piece", 30013);

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();

    EXPECT_EQ(idx.size(), 1);
    EXPECT_EQ(idx.slugForAnilistId(30013), "one-piece");
    EXPECT_EQ(idx.filePathForSlug("one-piece"),
              QDir(tmp.path()).absoluteFilePath("one-piece.json"));
}

TEST(LocalFandomCatalogIndex, IndexesMultipleCatalogs) {
    QTemporaryDir tmp;
    writeStubCatalog(tmp.path(), "one-piece.json", "one-piece", 30013);
    writeStubCatalog(tmp.path(), "berserk.json",  "berserk",   30002);
    writeStubCatalog(tmp.path(), "death-note.json", "death-note", 30021);

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();

    EXPECT_EQ(idx.size(), 3);
    EXPECT_EQ(idx.slugForAnilistId(30013), "one-piece");
    EXPECT_EQ(idx.slugForAnilistId(30002), "berserk");
    EXPECT_EQ(idx.slugForAnilistId(30021), "death-note");
}

TEST(LocalFandomCatalogIndex, RefreshIsIdempotentAndPicksUpNewFiles) {
    QTemporaryDir tmp;
    writeStubCatalog(tmp.path(), "one-piece.json", "one-piece", 30013);

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();
    EXPECT_EQ(idx.size(), 1);

    // Add a new file post-construction; refresh picks it up.
    writeStubCatalog(tmp.path(), "berserk.json", "berserk", 30002);
    idx.refresh();
    EXPECT_EQ(idx.size(), 2);
    EXPECT_EQ(idx.slugForAnilistId(30002), "berserk");
}

TEST(LocalFandomCatalogIndex, SkipsMalformedFilesWithoutCrashing) {
    QTemporaryDir tmp;
    writeStubCatalog(tmp.path(), "one-piece.json", "one-piece", 30013);

    // Drop a non-JSON file in the same dir.
    QFile bad(tmp.path() + "/bad.json");
    bad.open(QIODevice::WriteOnly | QIODevice::Text);
    bad.write("this is not json");
    bad.close();

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();
    EXPECT_EQ(idx.size(), 1);  // bad.json silently skipped
    EXPECT_EQ(idx.slugForAnilistId(30013), "one-piece");
}

TEST(LocalFandomCatalogIndex, SkipsFilesMissingAnilistId) {
    QTemporaryDir tmp;
    // FANDOM_LOCAL_LOADER_INTEGRATION 2026-05-22 hotfix updated this test:
    // previously the index keyed solely on anilistId. The title-fallback
    // hotfix relaxed that — a catalog with seriesTitle but no anilistId is
    // STILL indexable (title-lookupable). Pure orphan (neither field) stays
    // skipped.
    QFile f(tmp.path() + "/orphan.json");
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(R"({"seriesId": "orphan", "fandomUrl": "https://x.fandom.com/wiki/Y"})");
    f.close();

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();
    EXPECT_EQ(idx.size(), 0);  // size() reflects anilistId map count; pure orphan absent there
    EXPECT_TRUE(idx.slugForAnilistId(0).isEmpty());
    EXPECT_TRUE(idx.slugForSeriesTitle(QString()).isEmpty());
}

// --- slugForSeriesTitle (title-fallback hotfix 2026-05-22) ---

TEST(LocalFandomCatalogIndex, SlugForSeriesTitleExactMatch) {
    QTemporaryDir tmp;
    writeStubCatalogWithTitle(tmp.path(), "one-piece.json",
                              "one-piece", 30013, "One Piece");

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();

    EXPECT_EQ(idx.slugForSeriesTitle("One Piece"), "one-piece");
}

TEST(LocalFandomCatalogIndex, SlugForSeriesTitleNormalizesCaseAndPunctuation) {
    QTemporaryDir tmp;
    writeStubCatalogWithTitle(tmp.path(), "demon-slayer.json",
                              "demon-slayer", 87216,
                              "Demon Slayer: Kimetsu no Yaiba");

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();

    // Title with colon + spaces normalizes to lowercase + hyphens; lookup
    // matches the same normalized form regardless of the input casing /
    // punctuation variant.
    EXPECT_EQ(idx.slugForSeriesTitle("Demon Slayer: Kimetsu no Yaiba"), "demon-slayer");
    EXPECT_EQ(idx.slugForSeriesTitle("demon slayer: kimetsu no yaiba"), "demon-slayer");
    EXPECT_EQ(idx.slugForSeriesTitle("DEMON SLAYER  KIMETSU NO YAIBA"), "demon-slayer");
    // Different normalized form -> no match.
    EXPECT_TRUE(idx.slugForSeriesTitle("Demon Slayer").isEmpty());
}

TEST(LocalFandomCatalogIndex, SlugForSeriesTitleReturnsEmptyOnNoMatch) {
    QTemporaryDir tmp;
    writeStubCatalogWithTitle(tmp.path(), "one-piece.json",
                              "one-piece", 30013, "One Piece");

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();

    EXPECT_TRUE(idx.slugForSeriesTitle("Unknown Series").isEmpty());
    EXPECT_TRUE(idx.slugForSeriesTitle("").isEmpty());
}

TEST(LocalFandomCatalogIndex, NormalizeTitleStaticHelper) {
    // Direct check of the public normalize helper, used at lookup time.
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle("One Piece"), "one-piece");
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle("ONE PIECE"), "one-piece");
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle("Demon Slayer: Kimetsu no Yaiba"),
              "demon-slayer-kimetsu-no-yaiba");
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle("  spaced  out  "), "spaced-out");
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle(""), "");
    EXPECT_EQ(LocalFandomCatalogIndex::normalizeTitle("---"), "");
}
