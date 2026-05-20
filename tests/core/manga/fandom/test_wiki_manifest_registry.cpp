// tests/core/manga/fandom/test_wiki_manifest_registry.cpp
//
// GoogleTest verification for WikiManifestRegistry (plan Task 3). Loads
// the test fixture manifests directory + verifies lookups by both
// seriesId (primary) and fandomWikiId (secondary index).

#include <gtest/gtest.h>

#include "core/manga/fandom/WikiManifestRegistry.h"

#include <QString>

using tankoban::manga::fandom::WikiManifest;
using tankoban::manga::fandom::WikiManifestRegistry;

namespace {

QString fixtureManifestsDir()
{
    return QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/fandom/manifests");
}

} // anonymous

TEST(WikiManifestRegistryTest, LoadsAtLeastOneManifestFromFixtureDir)
{
    WikiManifestRegistry reg;
    const int loaded = reg.loadFromDirectory(fixtureManifestsDir());
    EXPECT_GE(loaded, 1);
    EXPECT_GE(reg.count(), 1);
}

TEST(WikiManifestRegistryTest, FindBySeriesIdReturnsDeathNoteManifest)
{
    WikiManifestRegistry reg;
    reg.loadFromDirectory(fixtureManifestsDir());

    WikiManifest m = reg.find(QStringLiteral("death-note"));
    ASSERT_TRUE(m.isValid()) << "death-note manifest should be loaded from fixture dir";
    EXPECT_EQ(m.fandomWikiId.toStdString(), "deathnote");
    EXPECT_EQ(m.wikidataQid.toStdString(),  "Q14559");
}

TEST(WikiManifestRegistryTest, FindByFandomWikiIdResolvesToSeriesManifest)
{
    WikiManifestRegistry reg;
    reg.loadFromDirectory(fixtureManifestsDir());

    WikiManifest m = reg.findByFandomWikiId(QStringLiteral("deathnote"));
    ASSERT_TRUE(m.isValid());
    EXPECT_EQ(m.seriesId.toStdString(), "death-note");
}

TEST(WikiManifestRegistryTest, MissingSeriesIdReturnsInvalidManifest)
{
    WikiManifestRegistry reg;
    reg.loadFromDirectory(fixtureManifestsDir());

    WikiManifest m = reg.find(QStringLiteral("nonexistent-series-zzz"));
    EXPECT_FALSE(m.isValid());
}

TEST(WikiManifestRegistryTest, MissingFandomWikiIdReturnsInvalidManifest)
{
    WikiManifestRegistry reg;
    reg.loadFromDirectory(fixtureManifestsDir());

    WikiManifest m = reg.findByFandomWikiId(QStringLiteral("nonexistentwiki"));
    EXPECT_FALSE(m.isValid());
}

TEST(WikiManifestRegistryTest, LoadingNonExistentDirReturnsZero)
{
    WikiManifestRegistry reg;
    const int loaded = reg.loadFromDirectory(
        QStringLiteral("/some/path/that/should/never/exist/tankoban/zzz"));
    EXPECT_EQ(loaded, 0);
    EXPECT_EQ(reg.count(), 0);
}

TEST(WikiManifestRegistryTest, LoadingTwiceClearsPriorEntries)
{
    WikiManifestRegistry reg;
    reg.loadFromDirectory(fixtureManifestsDir());
    const int first = reg.count();
    EXPECT_GE(first, 1);

    // Reload from a known-empty dir should clear prior state.
    reg.loadFromDirectory(QStringLiteral("/some/empty/path/zzz"));
    EXPECT_EQ(reg.count(), 0);
    EXPECT_FALSE(reg.find(QStringLiteral("death-note")).isValid());
}
