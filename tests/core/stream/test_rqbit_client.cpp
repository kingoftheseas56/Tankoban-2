#include <gtest/gtest.h>
#include "core/stream/rqbit/RqbitClient.h"
#include <QJsonDocument>

using tankostream::rqbit::RqbitClient;
using tankostream::rqbit::RqbitStats;

static QJsonObject obj(const char* json) {
    return QJsonDocument::fromJson(json).object();
}

// Field names per docs/superpowers/specs/rqbit-api-contract.md §4 (captured from
// the real rqbit 8.1.1 GET /torrents/{id}/stats/v1 response on this machine).
TEST(RqbitClientTest, ParsesProgressAndStateFromStats) {
    const auto j = obj(R"({"state":"live","total_bytes":1000,"progress_bytes":250,"finished":false})");
    const RqbitStats s = RqbitClient::parseStats(j);
    EXPECT_EQ(s.state, QStringLiteral("live"));
    EXPECT_EQ(s.totalBytes, 1000);
    EXPECT_EQ(s.downloadedBytes, 250);
    EXPECT_NEAR(s.progressFraction(), 0.25, 1e-6);
    EXPECT_FALSE(s.finished);
}

TEST(RqbitClientTest, ProgressFractionZeroWhenTotalUnknown) {
    const auto j = obj(R"({"state":"initializing","total_bytes":0,"progress_bytes":0,"finished":false})");
    const RqbitStats s = RqbitClient::parseStats(j);
    EXPECT_DOUBLE_EQ(s.progressFraction(), 0.0);  // no divide-by-zero
}

// File objects per contract §3: details.files[] each has name + length.
TEST(RqbitClientTest, PicksLargestVideoFile) {
    const auto files = QJsonDocument::fromJson(
        R"([{"name":"readme.txt","length":10},
            {"name":"Show.S01E01.mkv","length":900},
            {"name":"sample.mkv","length":50}])").array();
    EXPECT_EQ(RqbitClient::pickPrimaryVideoFile(files), 1);
}

TEST(RqbitClientTest, IgnoresNonVideoEvenWhenLarger) {
    // The .srt/.jpg siblings in a real Sintel-style pack must never win over the .mp4.
    const auto files = QJsonDocument::fromJson(
        R"([{"name":"Sintel.en.srt","length":1514},
            {"name":"poster.jpg","length":46115},
            {"name":"Sintel.mp4","length":129241752}])").array();
    EXPECT_EQ(RqbitClient::pickPrimaryVideoFile(files), 2);
}

TEST(RqbitClientTest, PickReturnsMinusOneWhenNoVideo) {
    const auto files = QJsonDocument::fromJson(
        R"([{"name":"a.txt","length":10},{"name":"b.nfo","length":5}])").array();
    EXPECT_EQ(RqbitClient::pickPrimaryVideoFile(files), -1);
}
