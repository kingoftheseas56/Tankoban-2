#include <gtest/gtest.h>

#include "core/JsonStore.h"
#include "core/stream/UnifiedProgressStore.h"

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
