#include <gtest/gtest.h>
#include "core/book/CatalogueDeduper.h"
#include "core/book/BookCatalogueResult.h"

namespace {
BookCatalogueResult mk(const QString& source, const QString& id,
                      const QString& title, const QString& author,
                      const QString& isbn = QString()) {
    BookCatalogueResult r;
    r.catalogueId = source + QStringLiteral(":") + id;
    r.title = title;
    r.author = author;
    r.isbn = isbn;
    return r;
}
} // namespace

TEST(CatalogueDeduperTest, EmptyInputReturnsEmpty) {
    QList<BookCatalogueResult> openlib, googlebooks;
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 0);
}

TEST(CatalogueDeduperTest, NonOverlappingResultsAllPreserved) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Klara and the Sun", "Kazuo Ishiguro", "9780593318171"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 2);
}

TEST(CatalogueDeduperTest, ExactIsbnMatchDedupes) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    // OpenLibrary wins as primary source.
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, IsbnSubsetMatchDedupes) {
    // OpenLib has multi-ISBN ",9780593135204,0593135202"; Google has just 9780593135204.
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir",
           "9780593135204,0593135202"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir", "9780593135204"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, FuzzyTitleAuthorMatchDedupesWhenNoIsbn) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "OL27448W", "Project Hail Mary", "Andy Weir"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "xyz", "Project Hail Mary", "Andy Weir"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
}

TEST(CatalogueDeduperTest, FuzzyTitleNormalizesCaseAndPunctuation) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "a", "The Way of Kings", "Brandon Sanderson"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "b", "the way of kings.", "BRANDON SANDERSON"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 1);
}

TEST(CatalogueDeduperTest, DifferentAuthorSameTitleNotDeduped) {
    QList<BookCatalogueResult> openlib{
        mk("openlib", "a", "Foundation", "Isaac Asimov"),
    };
    QList<BookCatalogueResult> googlebooks{
        mk("googlebooks", "b", "Foundation", "Frank Herbert"),
    };
    auto out = CatalogueDeduper::merge(openlib, googlebooks);
    EXPECT_EQ(out.size(), 2);
}

TEST(CatalogueDeduperTest, MergePreservesOpenLibraryDescriptionAndCover) {
    // If OpenLibrary lacks description and Google has it, the merge should
    // copy missing fields from the Google record onto the OpenLibrary winner.
    BookCatalogueResult openlib_book;
    openlib_book.catalogueId = QStringLiteral("openlib:OL27448W");
    openlib_book.title = QStringLiteral("Project Hail Mary");
    openlib_book.author = QStringLiteral("Andy Weir");
    openlib_book.isbn = QStringLiteral("9780593135204");
    openlib_book.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/123-L.jpg");
    // No description on the OpenLibrary side.

    BookCatalogueResult google_book;
    google_book.catalogueId = QStringLiteral("googlebooks:xyz");
    google_book.title = QStringLiteral("Project Hail Mary");
    google_book.author = QStringLiteral("Andy Weir");
    google_book.isbn = QStringLiteral("9780593135204");
    google_book.description = QStringLiteral("Ryland Grace, sole survivor...");

    auto out = CatalogueDeduper::merge({openlib_book}, {google_book});
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out[0].catalogueId.startsWith(QStringLiteral("openlib:")));
    EXPECT_EQ(out[0].description, QStringLiteral("Ryland Grace, sole survivor..."));
    EXPECT_TRUE(out[0].coverUrl.startsWith(QStringLiteral("https://covers.openlibrary.org")));
}
