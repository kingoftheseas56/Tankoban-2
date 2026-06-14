#include <gtest/gtest.h>

#include "core/JsonStore.h"
#include "core/stream/UnifiedProgressStore.h"

#include <QJsonDocument>
#include <QTemporaryDir>

#include <memory>

class UnifiedProgressStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid());
        m_store = std::make_unique<JsonStore>(m_tempDir.path());
        m_progress = std::make_unique<UnifiedProgressStore>(m_store.get());
    }

    QTemporaryDir m_tempDir;
    std::unique_ptr<JsonStore> m_store;
    std::unique_ptr<UnifiedProgressStore> m_progress;
};

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_SetAndResume)
{
    m_progress->setProgress("tt0141842", 6, 3, 1234.5, 3600.0);
    EXPECT_DOUBLE_EQ(1234.5, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_UnsetReturnsZero)
{
    EXPECT_DOUBLE_EQ(0.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_Overwrite)
{
    m_progress->setProgress("tt0141842", 6, 3, 100.0, 3600.0);
    m_progress->setProgress("tt0141842", 6, 3, 250.0, 3600.0);
    EXPECT_DOUBLE_EQ(250.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, EpisodeKeyed_ScrubBackLowers)
{
    m_progress->setProgress("tt0141842", 6, 3, 1500.0, 3600.0);
    m_progress->setProgress("tt0141842", 6, 3, 30.0, 3600.0);
    EXPECT_DOUBLE_EQ(30.0, m_progress->resumePositionFor("tt0141842", 6, 3));
}

TEST_F(UnifiedProgressStoreTest, PathKeyed_SetAndResume)
{
    m_progress->setProgressByPath("D:\\Sports\\Game.mkv", 500.0, 7200.0);
    EXPECT_DOUBLE_EQ(500.0, m_progress->resumePositionForPath("D:\\Sports\\Game.mkv"));
}

TEST_F(UnifiedProgressStoreTest, PathKeyed_UnsetReturnsZero)
{
    EXPECT_DOUBLE_EQ(0.0, m_progress->resumePositionForPath("D:\\Nope\\X.mkv"));
}

TEST_F(UnifiedProgressStoreTest, EpisodeAndPath_NoCollision)
{
    m_progress->setProgress("tt0141842", 6, 3, 100.0, 3600.0);
    m_progress->setProgressByPath("D:\\Sports\\Game.mkv", 500.0, 7200.0);
    EXPECT_DOUBLE_EQ(100.0, m_progress->resumePositionFor("tt0141842", 6, 3));
    EXPECT_DOUBLE_EQ(500.0, m_progress->resumePositionForPath("D:\\Sports\\Game.mkv"));
}

// ── SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 5 — per-domain progress ─────
// The continue-watching key namespace is parameterized by a domain prefix so
// each video mode (anime/tv/movies) builds + parses its own "<prefix>:..." keys
// off the SAME UnifiedProgressStore. Default prefix "stream" stays byte-
// identical to the legacy single-domain behavior. These tests pin both halves
// (build via allEpisodePayloadsForStreamDomain(prefix), parse via the new
// static parseDomainKey) so build+parse can never drift apart.

TEST_F(UnifiedProgressStoreTest, DomainKey_BuildUnderAnimePrefix)
{
    m_progress->setProgress("tt0903747", 5, 14, 600.0, 2700.0);  // Breaking-Bad-shaped id, anime domain
    const QJsonObject dom = m_progress->allEpisodePayloadsForStreamDomain("anime");
    EXPECT_TRUE(dom.contains("anime:tt0903747:s5:e14"))
        << "expected an anime-prefixed episode key; keys were: "
        << QJsonDocument(dom).toJson(QJsonDocument::Compact).toStdString();
    EXPECT_FALSE(dom.contains("stream:tt0903747:s5:e14"));
}

TEST_F(UnifiedProgressStoreTest, DomainKey_DefaultPrefixIsStreamByteIdentical)
{
    m_progress->setProgress("tt0903747", 5, 14, 600.0, 2700.0);
    // Default arg AND explicit "stream" must both produce the legacy key form.
    const QJsonObject viaDefault = m_progress->allEpisodePayloadsForStreamDomain();
    const QJsonObject viaStream  = m_progress->allEpisodePayloadsForStreamDomain("stream");
    EXPECT_TRUE(viaDefault.contains("stream:tt0903747:s5:e14"));
    EXPECT_EQ(viaDefault, viaStream);
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_AnimeRoundTrip)
{
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_TRUE(UnifiedProgressStore::parseDomainKey(
        "anime:tt0903747:s5:e14", "anime", imdb, season, episode));
    EXPECT_EQ("tt0903747", imdb);
    EXPECT_EQ(5, season);
    EXPECT_EQ(14, episode);
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_TvRoundTrip)
{
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_TRUE(UnifiedProgressStore::parseDomainKey(
        "tv:tt0944947:s8:e6", "tv", imdb, season, episode));
    EXPECT_EQ("tt0944947", imdb);
    EXPECT_EQ(8, season);
    EXPECT_EQ(6, episode);
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_MoviesImdbOnly)
{
    // Movies have no season/episode — the 2-part imdb-only form yields s/e == 0.
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_TRUE(UnifiedProgressStore::parseDomainKey(
        "movies:tt1375666", "movies", imdb, season, episode));
    EXPECT_EQ("tt1375666", imdb);
    EXPECT_EQ(0, season);
    EXPECT_EQ(0, episode);
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_LegacyStreamDefault)
{
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_TRUE(UnifiedProgressStore::parseDomainKey(
        "stream:tt0141842:s6:e3", "stream", imdb, season, episode));
    EXPECT_EQ("tt0141842", imdb);
    EXPECT_EQ(6, season);
    EXPECT_EQ(3, episode);
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_RejectsWrongPrefix)
{
    // A key built for one domain must NOT parse under another domain's prefix
    // (else anime/tv/movies Continue strips would bleed into each other).
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_FALSE(UnifiedProgressStore::parseDomainKey(
        "stream:tt0903747:s5:e14", "anime", imdb, season, episode));
    EXPECT_FALSE(UnifiedProgressStore::parseDomainKey(
        "anime:tt0903747:s5:e14", "tv", imdb, season, episode));
}

TEST_F(UnifiedProgressStoreTest, ParseDomainKey_RoundTripsBuilderOutput)
{
    // Build+parse must agree: feed parseDomainKey exactly what the builder emits.
    m_progress->setProgress("tt0903747", 5, 14, 600.0, 2700.0);
    const QJsonObject dom = m_progress->allEpisodePayloadsForStreamDomain("anime");
    ASSERT_FALSE(dom.isEmpty());
    const QString builtKey = dom.begin().key();
    QString imdb;
    int season = -1;
    int episode = -1;
    EXPECT_TRUE(UnifiedProgressStore::parseDomainKey(builtKey, "anime", imdb, season, episode));
    EXPECT_EQ("tt0903747", imdb);
    EXPECT_EQ(5, season);
    EXPECT_EQ(14, episode);
}
