#include "core/stream/SourceRanker.h"
#include "core/TorrentResult.h"

#include <gtest/gtest.h>

#include <QSet>
#include <QString>

using tankoban::stream::SourceRanker;

namespace {
TorrentResult makeResult(const QString& title, int seeders, const QString& source = "piratebay") {
    TorrentResult r;
    r.title = title;
    r.seeders = seeders;
    r.sourceKey = source;
    return r;
}
}

TEST(SourceRankerTest, EmptyInputReturnsNullopt) {
    SourceRanker ranker({});
    EXPECT_FALSE(ranker.pickTop({}).has_value());
}

TEST(SourceRankerTest, SingleHighSeederResultPicked) {
    SourceRanker ranker({"NTb", "Joy"});
    auto r = makeResult("Community.S05.NTb", 500);
    auto picked = ranker.pickTop({r});
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->title, QString("Community.S05.NTb"));
}

TEST(SourceRankerTest, TrustedUploaderOutranksHigherSeederUntrusted) {
    SourceRanker ranker({"NTb"});
    auto untrusted = makeResult("Community.S05.UNKNOWN", 800);
    auto trusted = makeResult("Community.S05.NTb", 300);
    auto ranked = ranker.rank({untrusted, trusted});
    EXPECT_EQ(ranked.at(0).result.title, QString("Community.S05.NTb"));
}

TEST(SourceRankerTest, ZombieZeroSeederBelowThreshold) {
    SourceRanker ranker({"NTb"});
    auto zombie = makeResult("Community.S05.NTb", 0);
    EXPECT_FALSE(ranker.pickTop({zombie}).has_value());
}

TEST(SourceRankerTest, RankPreservesOriginalListImmutable) {
    SourceRanker ranker({});
    auto r1 = makeResult("a", 10);
    auto r2 = makeResult("b", 100);
    QList<TorrentResult> input = {r1, r2};
    auto ranked = ranker.rank(input);
    EXPECT_EQ(input.at(0).title, QString("a"));
    EXPECT_EQ(input.at(1).title, QString("b"));
    EXPECT_EQ(ranked.at(0).result.title, QString("b"));
}
