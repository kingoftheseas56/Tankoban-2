#include <gtest/gtest.h>
#include "core/book/BookCatalogueResult.h"

TEST(BookCatalogueResultTest, DefaultConstructionLeavesEmptyFields) {
    BookCatalogueResult r;
    EXPECT_TRUE(r.catalogueId.isEmpty());
    EXPECT_TRUE(r.title.isEmpty());
    EXPECT_TRUE(r.author.isEmpty());
    EXPECT_EQ(r.isSeries, false);
    EXPECT_EQ(r.seriesPosition, 0);
}

TEST(BookCatalogueResultTest, SeriesShapeFieldsPopulate) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:OL14868682W");
    r.title = QStringLiteral("Stormlight Archive");
    r.author = QStringLiteral("Brandon Sanderson");
    r.isSeries = true;
    r.seriesName = QStringLiteral("Stormlight Archive");
    r.seriesTotal = 5;
    r.genres = QStringList{QStringLiteral("epic fantasy"), QStringLiteral("cosmere")};
    EXPECT_TRUE(r.isSeries);
    EXPECT_EQ(r.seriesTotal, 5);
    EXPECT_EQ(r.genres.size(), 2);
}

TEST(BookCatalogueResultTest, MoviesShapeFieldsPopulate) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:OL27448W");
    r.title = QStringLiteral("Project Hail Mary");
    r.author = QStringLiteral("Andy Weir");
    r.isbn = QStringLiteral("9780593135204");
    r.year = QStringLiteral("2021");
    r.publisher = QStringLiteral("Ballantine");
    r.pages = QStringLiteral("480");
    r.language = QStringLiteral("English");
    r.isSeries = false;
    EXPECT_FALSE(r.isSeries);
    EXPECT_EQ(r.year, QStringLiteral("2021"));
}
