#include <gtest/gtest.h>

#include "core/stream/StreamPackParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

using tankostream::stream::StreamPackParser;
using tankostream::stream::ParsedPack;
using tankostream::stream::ParsedFile;

namespace {

QJsonObject makeFile(const QString& name, qint64 size, int index)
{
    QJsonObject f;
    f.insert(QStringLiteral("name"), name);
    f.insert(QStringLiteral("size"), QJsonValue::fromVariant(size));
    f.insert(QStringLiteral("index"), index);
    return f;
}

}  // namespace

TEST(StreamPackParserTest, SingleSeasonCleanSENaming)
{
    QJsonArray files;
    for (int ep = 1; ep <= 3; ++ep) {
        files.append(makeFile(
            QStringLiteral("Daredevil.S01E%1.1080p.WEB-DL.mkv")
                .arg(ep, 2, 10, QChar('0')),
            1500000000LL,
            ep - 1));
    }

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    EXPECT_EQ(pack.imdbId, QStringLiteral("tt18923754"));
    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].season, 1);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[2].season, 1);
    EXPECT_EQ(pack.episodes[2].episode, 3);
}

TEST(StreamPackParserTest, LongRunningAnimeEpisodeNumber)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("One.Piece.S01E1164.1080p.WEB-DL.mkv"),
                          1500000000LL,
                          0));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0388629"), 1);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    ASSERT_EQ(pack.episodes.size(), 1);
    EXPECT_EQ(pack.episodes[0].season, 1);
    EXPECT_EQ(pack.episodes[0].episode, 1164);
}

TEST(StreamPackParserTest, MultiSeasonProbe)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Sopranos.S01E01.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("Sopranos.S03E07.mkv"), 1500000000LL, 1));
    files.append(makeFile(QStringLiteral("Sopranos.S06E02.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0141842"), 0);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].season, 1);
    EXPECT_EQ(pack.episodes[1].season, 3);
    EXPECT_EQ(pack.episodes[2].season, 6);
}

TEST(StreamPackParserTest, MovieFallback)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Fight.Club.1080p.BluRay.mkv"),
                          5000000000LL, 0));
    files.append(makeFile(QStringLiteral("Fight.Club.nfo"), 2048LL, 1));
    files.append(makeFile(QStringLiteral("sample.mkv"), 30000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0137523"), 0);

    EXPECT_EQ(pack.type, QStringLiteral("movie"));
    EXPECT_EQ(pack.episodes.size(), 0);
    EXPECT_EQ(pack.movieFile.relName, QStringLiteral("Fight.Club.1080p.BluRay.mkv"));
    EXPECT_GT(pack.movieFile.sizeBytes, 4000000000LL);
}

TEST(StreamPackParserTest, EmptyFilesReturnsEmptyPack)
{
    QJsonArray files;  // empty

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt0000000"), 1);

    EXPECT_EQ(pack.episodes.size(), 0);
    EXPECT_EQ(pack.movieFile.relName.length(), 0);
}

TEST(StreamPackParserTest, UnparseableFilesSkippedSilently)
{
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Daredevil.S01E01.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("random.featurette.mkv"), 200000000LL, 1));
    files.append(makeFile(QStringLiteral("Daredevil.S01E03.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    EXPECT_EQ(pack.type, QStringLiteral("series"));
    ASSERT_EQ(pack.episodes.size(), 2);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[1].episode, 3);
}

TEST(StreamPackParserTest, EpisodesReturnedInEpisodeOrder)
{
    // Files arrive in NON-monotonic order; parser must sort by (season, episode).
    QJsonArray files;
    files.append(makeFile(QStringLiteral("Daredevil.S01E03.mkv"), 1500000000LL, 0));
    files.append(makeFile(QStringLiteral("Daredevil.S01E01.mkv"), 1500000000LL, 1));
    files.append(makeFile(QStringLiteral("Daredevil.S01E02.mkv"), 1500000000LL, 2));

    ParsedPack pack = StreamPackParser::parsePack(files, QStringLiteral("tt18923754"), 1);

    ASSERT_EQ(pack.episodes.size(), 3);
    EXPECT_EQ(pack.episodes[0].episode, 1);
    EXPECT_EQ(pack.episodes[1].episode, 2);
    EXPECT_EQ(pack.episodes[2].episode, 3);
}
