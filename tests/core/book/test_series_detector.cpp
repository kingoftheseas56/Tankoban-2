#include <gtest/gtest.h>
#include "core/book/SeriesDetector.h"
#include "core/book/BookCatalogueResult.h"

namespace {
BookCatalogueResult mk(const QString& title, const QString& author,
                      const QString& year = QString()) {
    BookCatalogueResult r;
    r.catalogueId = QStringLiteral("openlib:/works/OL_%1_W").arg(title);
    r.workId = QStringLiteral("/works/OL_%1_W").arg(title);
    r.title = title;
    r.author = author;
    r.year = year;
    return r;
}
} // namespace

TEST(SeriesDetectorTest, SingleStandaloneNotSeries) {
    QList<BookCatalogueResult> input{
        mk("Project Hail Mary", "Andy Weir", "2021"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.standalones.size(), 1);
    EXPECT_EQ(out.standalones[0].title, QStringLiteral("Project Hail Mary"));
    EXPECT_FALSE(out.standalones[0].isSeries);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
}

TEST(SeriesDetectorTest, MultipleBooksSameAuthorNoSeriesPatternNotGrouped) {
    QList<BookCatalogueResult> input{
        mk("The Martian", "Andy Weir", "2014"),
        mk("Project Hail Mary", "Andy Weir", "2021"),
        mk("Artemis", "Andy Weir", "2017"),
    };
    auto out = SeriesDetector::detect(input);
    EXPECT_EQ(out.standalones.size(), 3);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
}

TEST(SeriesDetectorTest, CommonSeriesNamePrefixGroupsAsSeries) {
    QList<BookCatalogueResult> input{
        mk("The Stormlight Archive #1: The Way of Kings", "Brandon Sanderson", "2010"),
        mk("The Stormlight Archive #2: Words of Radiance", "Brandon Sanderson", "2014"),
        mk("The Stormlight Archive #3: Oathbringer", "Brandon Sanderson", "2017"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    const auto& g = out.seriesGroups.first();
    EXPECT_EQ(g.seriesName, QStringLiteral("The Stormlight Archive"));
    EXPECT_EQ(g.books.size(), 3);
    EXPECT_EQ(g.books[0].seriesPosition, 1);
    EXPECT_EQ(g.books[1].seriesPosition, 2);
    EXPECT_EQ(g.books[2].seriesPosition, 3);
}

TEST(SeriesDetectorTest, ColonStyleSeriesNameGrouped) {
    QList<BookCatalogueResult> input{
        mk("Mistborn: The Final Empire", "Brandon Sanderson", "2006"),
        mk("Mistborn: The Well of Ascension", "Brandon Sanderson", "2007"),
        mk("Mistborn: The Hero of Ages", "Brandon Sanderson", "2008"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Mistborn"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 3);
}

TEST(SeriesDetectorTest, BookNumberPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Wheel of Time, Book 1: The Eye of the World", "Robert Jordan", "1990"),
        mk("Wheel of Time, Book 2: The Great Hunt", "Robert Jordan", "1990"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Wheel of Time"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
    EXPECT_EQ(out.seriesGroups.first().books[0].seriesPosition, 1);
    EXPECT_EQ(out.seriesGroups.first().books[1].seriesPosition, 2);
}

TEST(SeriesDetectorTest, RomanNumeralPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Dune I", "Frank Herbert", "1965"),
        mk("Dune II: Dune Messiah", "Frank Herbert", "1969"),
        mk("Dune III: Children of Dune", "Frank Herbert", "1976"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Dune"));
    EXPECT_EQ(out.seriesGroups.first().books[0].seriesPosition, 1);
    EXPECT_EQ(out.seriesGroups.first().books[1].seriesPosition, 2);
    EXPECT_EQ(out.seriesGroups.first().books[2].seriesPosition, 3);
}

TEST(SeriesDetectorTest, ParensPositionPatternGrouped) {
    QList<BookCatalogueResult> input{
        mk("Discworld (1): The Colour of Magic", "Terry Pratchett", "1983"),
        mk("Discworld (2): The Light Fantastic", "Terry Pratchett", "1986"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Discworld"));
}

TEST(SeriesDetectorTest, OpenLibrarySeriesFieldUsedWhenPresent) {
    auto a = mk("The Way of Kings", "Brandon Sanderson", "2010");
    auto b = mk("Words of Radiance", "Brandon Sanderson", "2014");
    a.seriesName = QStringLiteral("The Stormlight Archive");
    b.seriesName = QStringLiteral("The Stormlight Archive");
    QList<BookCatalogueResult> input{a, b};
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName,
              QStringLiteral("The Stormlight Archive"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
}

TEST(SeriesDetectorTest, DifferentAuthorsSameTitlePatternNotGrouped) {
    // If two books have the same title-suffix pattern but different authors,
    // do not group — series are author-bound.
    QList<BookCatalogueResult> input{
        mk("Foo Book 1", "Author A", "2020"),
        mk("Foo Book 2", "Author B", "2021"),
    };
    auto out = SeriesDetector::detect(input);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
    EXPECT_EQ(out.standalones.size(), 2);
}

TEST(SeriesDetectorTest, SingleBookWithSeriesFieldStaysStandalone) {
    // A single book with a series field hint but no sibling is not a series
    // (movie-shape fallback per spec — safer than wrong-grouping).
    auto a = mk("The Way of Kings", "Brandon Sanderson", "2010");
    a.seriesName = QStringLiteral("The Stormlight Archive");
    QList<BookCatalogueResult> input{a};
    auto out = SeriesDetector::detect(input);
    EXPECT_TRUE(out.seriesGroups.isEmpty());
    ASSERT_EQ(out.standalones.size(), 1);
    EXPECT_FALSE(out.standalones[0].isSeries);
}

TEST(SeriesDetectorTest, MixedSeriesAndStandaloneInOneAuthorGroup) {
    QList<BookCatalogueResult> input{
        mk("Mistborn: The Final Empire", "Brandon Sanderson", "2006"),
        mk("Mistborn: The Well of Ascension", "Brandon Sanderson", "2007"),
        mk("Elantris", "Brandon Sanderson", "2005"),
        mk("Warbreaker", "Brandon Sanderson", "2009"),
    };
    auto out = SeriesDetector::detect(input);
    ASSERT_EQ(out.seriesGroups.size(), 1);
    EXPECT_EQ(out.seriesGroups.first().seriesName, QStringLiteral("Mistborn"));
    EXPECT_EQ(out.seriesGroups.first().books.size(), 2);
    EXPECT_EQ(out.standalones.size(), 2);
    QStringList standaloneTitles;
    for (const auto& s : out.standalones) standaloneTitles << s.title;
    EXPECT_TRUE(standaloneTitles.contains(QStringLiteral("Elantris")));
    EXPECT_TRUE(standaloneTitles.contains(QStringLiteral("Warbreaker")));
}
