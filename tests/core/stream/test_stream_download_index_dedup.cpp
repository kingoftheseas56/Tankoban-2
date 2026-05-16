#include <gtest/gtest.h>

#include "core/JsonStore.h"
#include "core/stream/StreamDownloadIndex.h"

#include <QTemporaryDir>

#include <memory>

namespace {

class StreamDownloadIndexDedupTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid());
        m_store = std::make_unique<JsonStore>(m_tempDir.path());
        m_index = std::make_unique<StreamDownloadIndex>(m_store.get());
    }

    QTemporaryDir m_tempDir;
    std::unique_ptr<JsonStore> m_store;
    std::unique_ptr<StreamDownloadIndex> m_index;
};

}  // namespace

TEST_F(StreamDownloadIndexDedupTest, HigherQualityEvictsLower)
{
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.720p.HDTV.mkv",
                             "tankorent:abc", 700'000'000);

    auto first = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.720p.HDTV.mkv", *first);

    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:def", 2'500'000'000);

    auto second = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.1080p.BluRay.mkv", *second);
}

TEST_F(StreamDownloadIndexDedupTest, LowerQualityDoesNotEvictHigher)
{
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:def", 2'500'000'000);

    m_index->registerEpisode("tt0141842", 6, 3,
                             "/x/Sopranos.S06E03.720p.HDTV.mkv",
                             "tankorent:abc", 700'000'000);

    auto kept = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ("/x/Sopranos.S06E03.1080p.BluRay.mkv", *kept);
}

TEST_F(StreamDownloadIndexDedupTest, EqualQualityKeepsFirst)
{
    m_index->registerEpisode("tt0141842", 6, 3,
                             "/a/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:a", 2'500'000'000);

    m_index->registerEpisode("tt0141842", 6, 3,
                             "/b/Sopranos.S06E03.1080p.BluRay.mkv",
                             "tankorent:b", 2'500'000'000);

    auto kept = m_index->filePathFor("tt0141842", 6, 3);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ("/a/Sopranos.S06E03.1080p.BluRay.mkv", *kept);
}

TEST_F(StreamDownloadIndexDedupTest, MovieRegistration)
{
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.1080p.BluRay.x264.mkv",
                           "tankorent:abc", 4'000'000'000);

    auto p = m_index->filePathForMovie("tt1375666");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ("/x/Inception.2010.1080p.BluRay.x264.mkv", *p);

    const auto entries = m_index->entriesForImdb("tt1375666");
    ASSERT_EQ(1, entries.size());
    EXPECT_EQ(QStringLiteral("movie"), entries.front().type);
}

TEST_F(StreamDownloadIndexDedupTest, MovieDedup_HigherQualityWins)
{
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.720p.WEBRip.mkv",
                           "tankorent:a", 1'500'000'000);
    m_index->registerMovie("tt1375666",
                           "/x/Inception.2010.2160p.BluRay.mkv",
                           "tankorent:b", 30'000'000'000);

    auto p = m_index->filePathForMovie("tt1375666");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ("/x/Inception.2010.2160p.BluRay.mkv", *p);
}
