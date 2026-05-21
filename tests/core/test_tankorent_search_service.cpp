#include "core/TankorentSearchService.h"
#include "core/TorrentResult.h"
#include "MockTorrentIndexer.h"

#include <QSignalSpy>
#include <QList>
#include <QString>

#include <gtest/gtest.h>

// Test subclass that returns caller-provided mock indexers from
// buildIndexersFor, bypassing the QSettings + concrete-indexer path.
class TestableSearchService : public TankorentSearchService
{
public:
    TestableSearchService() : TankorentSearchService(nullptr) {}

    void setMockIndexers(const QList<TorrentIndexer*>& mocks) { m_mocks = mocks; }

protected:
    QList<TorrentIndexer*> buildIndexersFor(const QString& /*mediaType*/,
                                              const QString& /*sourceFilter*/) override
    {
        QList<TorrentIndexer*> snapshot = m_mocks;
        m_mocks.clear();  // one-shot; tests reset before each startSearch
        return snapshot;
    }

private:
    QList<TorrentIndexer*> m_mocks;
};

TEST(TankorentSearchServiceTest, EmptyFanOutReturnsEmptyHandle)
{
    TestableSearchService svc;
    svc.setMockIndexers({});

    const QString handle = svc.startSearch("books", "all", "test", 30);

    EXPECT_TRUE(handle.isEmpty());
    EXPECT_FALSE(svc.isActive(handle));
}

TEST(TankorentSearchServiceTest, SingleIndexerSuccessFiresResultsThenFinished)
{
    TestableSearchService svc;
    auto* mock = new MockTorrentIndexer("piratebay");
    svc.setMockIndexers({mock});

    QSignalSpy resultsSpy(&svc, &TankorentSearchService::resultsReady);
    QSignalSpy finishedSpy(&svc, &TankorentSearchService::searchFinished);

    const QString handle = svc.startSearch("books", "all", "test", 30);
    ASSERT_FALSE(handle.isEmpty());
    EXPECT_TRUE(mock->wasSearched());
    EXPECT_TRUE(svc.isActive(handle));

    TorrentResult r;
    r.title = "fake book";
    r.magnetUri = "magnet:?xt=urn:btih:abc";
    mock->triggerFinished({r});

    ASSERT_EQ(resultsSpy.count(), 1);
    EXPECT_EQ(resultsSpy.first().at(0).toString(), handle);
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_EQ(finishedSpy.first().at(0).toString(), handle);
    EXPECT_FALSE(svc.isActive(handle));
}

TEST(TankorentSearchServiceTest, MultiIndexerWaitsForAllSettle)
{
    TestableSearchService svc;
    auto* a = new MockTorrentIndexer("piratebay");
    auto* b = new MockTorrentIndexer("exttorrents");
    auto* c = new MockTorrentIndexer("torrentscsv");
    svc.setMockIndexers({a, b, c});

    QSignalSpy finishedSpy(&svc, &TankorentSearchService::searchFinished);

    const QString handle = svc.startSearch("books", "all", "test", 30);
    ASSERT_FALSE(handle.isEmpty());

    a->triggerFinished({});
    EXPECT_EQ(finishedSpy.count(), 0);
    EXPECT_TRUE(svc.isActive(handle));

    b->triggerFinished({});
    EXPECT_EQ(finishedSpy.count(), 0);

    c->triggerFinished({});
    EXPECT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(svc.isActive(handle));
}

TEST(TankorentSearchServiceTest, ErrorPathFiresIndexerErrorAndStillCountsTowardsFinished)
{
    TestableSearchService svc;
    auto* a = new MockTorrentIndexer("piratebay");
    auto* b = new MockTorrentIndexer("exttorrents");
    svc.setMockIndexers({a, b});

    QSignalSpy errorSpy(&svc, &TankorentSearchService::indexerError);
    QSignalSpy finishedSpy(&svc, &TankorentSearchService::searchFinished);

    const QString handle = svc.startSearch("books", "all", "test", 30);
    ASSERT_FALSE(handle.isEmpty());

    a->triggerError("network failure");
    ASSERT_EQ(errorSpy.count(), 1);
    EXPECT_EQ(errorSpy.first().at(0).toString(), handle);
    EXPECT_EQ(errorSpy.first().at(1).toString(), QString("piratebay"));
    EXPECT_EQ(errorSpy.first().at(2).toString(), QString("network failure"));
    EXPECT_EQ(finishedSpy.count(), 0);

    b->triggerFinished({});
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST(TankorentSearchServiceTest, CancelDropsHandleSilently)
{
    TestableSearchService svc;
    auto* a = new MockTorrentIndexer("piratebay");
    svc.setMockIndexers({a});

    QSignalSpy resultsSpy(&svc, &TankorentSearchService::resultsReady);
    QSignalSpy finishedSpy(&svc, &TankorentSearchService::searchFinished);

    const QString handle = svc.startSearch("books", "all", "test", 30);
    ASSERT_FALSE(handle.isEmpty());

    svc.cancelSearch(handle);
    EXPECT_FALSE(svc.isActive(handle));

    // Indexer triggers AFTER cancel must not produce signals.
    a->triggerFinished({});
    EXPECT_EQ(resultsSpy.count(), 0);
    EXPECT_EQ(finishedSpy.count(), 0);
}

TEST(TankorentSearchServiceTest, ConcurrentHandlesAreIsolated)
{
    TestableSearchService svc;
    auto* a = new MockTorrentIndexer("piratebay");
    svc.setMockIndexers({a});
    const QString handle1 = svc.startSearch("books", "all", "test1", 30);

    auto* b = new MockTorrentIndexer("exttorrents");
    svc.setMockIndexers({b});
    const QString handle2 = svc.startSearch("books", "all", "test2", 30);

    ASSERT_FALSE(handle1.isEmpty());
    ASSERT_FALSE(handle2.isEmpty());
    EXPECT_NE(handle1, handle2);

    QSignalSpy finishedSpy(&svc, &TankorentSearchService::searchFinished);

    a->triggerFinished({});
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_EQ(finishedSpy.first().at(0).toString(), handle1);
    EXPECT_FALSE(svc.isActive(handle1));
    EXPECT_TRUE(svc.isActive(handle2));

    b->triggerFinished({});
    ASSERT_EQ(finishedSpy.count(), 2);
    EXPECT_EQ(finishedSpy.last().at(0).toString(), handle2);
    EXPECT_FALSE(svc.isActive(handle2));
}
