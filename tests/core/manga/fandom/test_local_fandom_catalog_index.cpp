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
    // Valid JSON but no anilistId — index has nothing to key on.
    QFile f(tmp.path() + "/orphan.json");
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(R"({"seriesId": "orphan", "fandomUrl": "https://x.fandom.com/wiki/Y"})");
    f.close();

    LocalFandomCatalogIndex idx(tmp.path());
    idx.refresh();
    EXPECT_EQ(idx.size(), 0);
}
