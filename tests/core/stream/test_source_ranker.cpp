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
