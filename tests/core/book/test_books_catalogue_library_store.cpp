#include <gtest/gtest.h>
#include <QTemporaryDir>
#include "core/book/BooksCatalogueLibraryStore.h"
#include "core/book/CatalogueRecord.h"

namespace {
CatalogueRecord makeMovieRecord(const QString& id, const QString& title) {
    CatalogueRecord r;
    r.catalogueId = id;
    r.title = title;
    r.author = QStringLiteral("Test Author");
    r.filePath = title + QStringLiteral(".epub");
    r.format = QStringLiteral("epub");
    r.addedAt = 1716100000;
    return r;
}

CatalogueRecord makeSeriesBookRecord(const QString& id, const QString& seriesId,
                                     const QString& seriesName,
                                     int pos, const QString& title) {
    CatalogueRecord r;
    r.catalogueId = id;
    r.title = title;
    r.author = QStringLiteral("Test Author");
    r.seriesId = seriesId;
    r.seriesName = seriesName;
    r.seriesPosition = pos;
    r.seriesTotal = 5;
    r.filePath = title + QStringLiteral(".epub");
    r.format = QStringLiteral("epub");
    r.addedAt = 1716100000 + pos;
    return r;
}
} // namespace

TEST(BooksCatalogueLibraryStoreTest, EmptyStoreReportsEmpty) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    EXPECT_EQ(store.all().size(), 0);
    EXPECT_FALSE(store.hasRecord(QStringLiteral("openlib:nonexistent")));
}

TEST(BooksCatalogueLibraryStoreTest, RegisterAndLookupRoundTrips) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    auto r = makeMovieRecord(QStringLiteral("openlib:OL27448W"),
                             QStringLiteral("Project Hail Mary"));
    store.upsertRecord(r);
    EXPECT_TRUE(store.hasRecord(QStringLiteral("openlib:OL27448W")));
    auto opt = store.recordFor(QStringLiteral("openlib:OL27448W"));
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->title, QStringLiteral("Project Hail Mary"));
}

TEST(BooksCatalogueLibraryStoreTest, EvictRemovesRecord) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    store.upsertRecord(makeMovieRecord(QStringLiteral("a"), QStringLiteral("A")));
    store.upsertRecord(makeMovieRecord(QStringLiteral("b"), QStringLiteral("B")));
    EXPECT_EQ(store.all().size(), 2);
    store.evictByCatalogueId(QStringLiteral("a"));
    EXPECT_EQ(store.all().size(), 1);
    EXPECT_FALSE(store.hasRecord(QStringLiteral("a")));
    EXPECT_TRUE(store.hasRecord(QStringLiteral("b")));
}

TEST(BooksCatalogueLibraryStoreTest, BySeriesAggregation) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    const QString sid = QStringLiteral("openlib:OL14868682W");
    store.upsertRecord(makeSeriesBookRecord(QStringLiteral("k1"), sid,
                                            QStringLiteral("Stormlight Archive"), 1,
                                            QStringLiteral("The Way of Kings")));
    store.upsertRecord(makeSeriesBookRecord(QStringLiteral("k2"), sid,
                                            QStringLiteral("Stormlight Archive"), 2,
                                            QStringLiteral("Words of Radiance")));
    store.upsertRecord(makeMovieRecord(QStringLiteral("phm"),
                                       QStringLiteral("Project Hail Mary")));
    auto ids = store.catalogueIdsForSeries(sid);
    EXPECT_EQ(ids.size(), 2);
    EXPECT_TRUE(ids.contains(QStringLiteral("k1")));
    EXPECT_TRUE(ids.contains(QStringLiteral("k2")));
    auto allSeries = store.allSeriesIds();
    EXPECT_EQ(allSeries.size(), 1);
    EXPECT_TRUE(allSeries.contains(sid));
}

TEST(BooksCatalogueLibraryStoreTest, FilePathReverseLookup) {
    QTemporaryDir tmp;
    BooksCatalogueLibraryStore store(tmp.path());
    auto r = makeMovieRecord(QStringLiteral("openlib:OL27448W"),
                             QStringLiteral("Project Hail Mary"));
    store.upsertRecord(r);
    auto opt = store.catalogueIdForFile(QStringLiteral("Project Hail Mary.epub"));
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, QStringLiteral("openlib:OL27448W"));
    EXPECT_FALSE(store.catalogueIdForFile(QStringLiteral("nonexistent.epub")).has_value());
}

TEST(BooksCatalogueLibraryStoreTest, PersistAndReload) {
    QTemporaryDir tmp;
    {
        BooksCatalogueLibraryStore store(tmp.path());
        store.upsertRecord(makeMovieRecord(QStringLiteral("phm"),
                                           QStringLiteral("Project Hail Mary")));
        store.save();
    }
    {
        BooksCatalogueLibraryStore store(tmp.path());
        store.load();
        EXPECT_EQ(store.all().size(), 1);
        EXPECT_TRUE(store.hasRecord(QStringLiteral("phm")));
    }
}
