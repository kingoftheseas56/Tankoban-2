#include <gtest/gtest.h>
#include <QJsonObject>
#include <QJsonDocument>
#include "core/book/CatalogueRecord.h"

TEST(CatalogueRecordTest, DefaultsAreSafe) {
    CatalogueRecord r;
    EXPECT_TRUE(r.catalogueId.isEmpty());
    EXPECT_TRUE(r.filePath.isEmpty());
    EXPECT_DOUBLE_EQ(r.readProgress, 0.0);
    EXPECT_EQ(r.seriesPosition, 0);
}

TEST(CatalogueRecordTest, RoundTripsThroughJson) {
    CatalogueRecord r;
    r.catalogueId = QStringLiteral("openlib:OL27448W");
    r.isbn = QStringLiteral("9780593135204");
    r.md5 = QStringLiteral("aabbccdd11223344");
    r.title = QStringLiteral("Project Hail Mary");
    r.author = QStringLiteral("Andy Weir");
    r.publisher = QStringLiteral("Ballantine");
    r.year = QStringLiteral("2021");
    r.language = QStringLiteral("English");
    r.description = QStringLiteral("Ryland Grace is the sole survivor on a desperate, last-chance mission.");
    r.genres = QStringList{QStringLiteral("hard sci-fi"), QStringLiteral("first contact")};
    r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/12345-L.jpg");
    r.filePath = QStringLiteral("Project Hail Mary.epub");
    r.format = QStringLiteral("epub");
    r.fileSize = QStringLiteral("4.2 MB");
    r.addedAt = 1716100000;
    r.readProgress = 0.42;
    r.lastReadAt = 1716200000;
    r.lastReadCfi = QStringLiteral("epubcfi(/6/8!/4/2/12)");

    QJsonObject json = r.toJson();
    CatalogueRecord back = CatalogueRecord::fromJson(json);

    EXPECT_EQ(back.catalogueId, r.catalogueId);
    EXPECT_EQ(back.isbn, r.isbn);
    EXPECT_EQ(back.md5, r.md5);
    EXPECT_EQ(back.title, r.title);
    EXPECT_EQ(back.author, r.author);
    EXPECT_EQ(back.description, r.description);
    EXPECT_EQ(back.genres, r.genres);
    EXPECT_EQ(back.filePath, r.filePath);
    EXPECT_EQ(back.format, r.format);
    EXPECT_EQ(back.addedAt, r.addedAt);
    EXPECT_DOUBLE_EQ(back.readProgress, r.readProgress);
    EXPECT_EQ(back.lastReadCfi, r.lastReadCfi);
}

TEST(CatalogueRecordTest, SeriesFieldsRoundTrip) {
    CatalogueRecord r;
    r.catalogueId = QStringLiteral("openlib:OL27448W:3");
    r.seriesId = QStringLiteral("openlib:OL14868682W");
    r.seriesName = QStringLiteral("Stormlight Archive");
    r.seriesPosition = 3;
    r.seriesTotal = 5;

    QJsonObject json = r.toJson();
    CatalogueRecord back = CatalogueRecord::fromJson(json);

    EXPECT_EQ(back.seriesId, r.seriesId);
    EXPECT_EQ(back.seriesName, r.seriesName);
    EXPECT_EQ(back.seriesPosition, 3);
    EXPECT_EQ(back.seriesTotal, 5);
}
