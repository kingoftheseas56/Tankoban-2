#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFileInfo>

#include "core/manga/WesternCatalogLoader.h"

using tankoban::manga::WesternCatalogLoader;

TEST(WesternCatalogLoaderTest, ReadsEnrichmentFields)
{
    const QString fixtureDir = QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR);
    const QString path = fixtureDir + QStringLiteral("/western_catalogue/enriched.json");

    const auto cat = WesternCatalogLoader::loadFromFile(path);
    ASSERT_TRUE(cat.has_value());
    EXPECT_EQ(cat->seriesSynopsis, QStringLiteral("A teenage superhero grows up."));
    EXPECT_EQ(cat->author, QStringLiteral("Robert Kirkman, Cory Walker"));
    EXPECT_EQ(cat->studio, QStringLiteral("Image Comics"));   // 'studio' slot = publisher
    EXPECT_EQ(cat->genres.size(), 2);
    EXPECT_EQ(cat->publishedYearStart, 2003);
    EXPECT_EQ(cat->publishedYearEnd, 2018);
    EXPECT_EQ(cat->status, QStringLiteral("FINISHED"));
    EXPECT_EQ(cat->volumes.size(), 1);
}
