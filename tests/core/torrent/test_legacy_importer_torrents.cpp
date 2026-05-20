// tankoban_tests — LegacyImporter::parseTorrentsJson tests (P1.1)
//
// Fixture-driven coverage of the legacy torrents.json parser. No live
// filesystem state required — every fixture is written to a QTemporaryDir.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase
// 1 Task 1.1 for the parser contract this exercises.

#include <gtest/gtest.h>

#include "core/torrent/LegacyImporter.h"
#include "core/torrent/TorrentRow.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::LegacyImporter;
using tankoban::torrent::TorrentRow;
using tankoban::torrent::TorrentState;

class LegacyImporterTorrentsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    // Writes `contents` to <tmp>/<name> and returns the absolute path.
    QString writeFixture(const QByteArray& contents,
                         const QString& name = QStringLiteral("torrents.json")) {
        const QString path = m_tmpDir.path() + QLatin1Char('/') + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QString();
        }
        f.write(contents);
        f.close();
        return path;
    }

    QString missingPath() const {
        return m_tmpDir.path() + QStringLiteral("/does_not_exist.json");
    }

    QTemporaryDir m_tmpDir;
};

}  // namespace

TEST_F(LegacyImporterTorrentsTest, ParsesActiveBlockSingleRow) {
    const QByteArray fixture = R"({
      "active": {
        "abcdef1234567890abcdef1234567890abcdef12": {
          "state": "downloading",
          "name": "Test Movie",
          "addedAt": "2026-05-01T10:00:00Z",
          "category": "stream",
          "savePath": "C:/Media/Test",
          "contentLayout": "Original",
          "imdbId": "tt9999999",
          "season": 2,
          "streamGroupId": "grp-1",
          "sequential": true,
          "magnetUri": "magnet:?xt=urn:btih:abcdef1234567890abcdef1234567890abcdef12"
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 1u);
    const TorrentRow& r = rows[0];
    EXPECT_EQ(r.hash,
              QStringLiteral("abcdef1234567890abcdef1234567890abcdef12"));
    EXPECT_EQ(r.state, TorrentState::Active);
    EXPECT_EQ(r.name, QStringLiteral("Test Movie"));
    EXPECT_EQ(r.category, QStringLiteral("stream"));
    EXPECT_EQ(r.savePath, QStringLiteral("C:/Media/Test"));
    EXPECT_EQ(r.contentLayout, QStringLiteral("Original"));
    EXPECT_EQ(r.imdbId, QStringLiteral("tt9999999"));
    EXPECT_EQ(r.season, 2);
    EXPECT_EQ(r.streamGroupId, QStringLiteral("grp-1"));
    EXPECT_TRUE(r.sequential);
    EXPECT_FALSE(r.magnetUri.isEmpty());
    EXPECT_FALSE(r.legacyNoMagnet);
    EXPECT_TRUE(r.addedAt.isValid());
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, UnknownStateGetsDefaultAndWarning) {
    const QByteArray fixture = R"({
      "active": {
        "00112233445566778899aabbccddeeff00112233": {
          "state": "weird_state",
          "name": "Mystery",
          "addedAt": "2026-05-01T10:00:00Z"
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].state, TorrentState::Active);
    EXPECT_FALSE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, HashCanonicalizedLowercase) {
    const QByteArray fixture = R"({
      "active": {
        "ABCDEF1234567890ABCDEF1234567890ABCDEF12": {
          "state": "completed",
          "name": "Uppercased",
          "addedAt": "2026-05-01T10:00:00Z"
        }
      }
    })";

    LegacyImporter imp;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture));

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].hash,
              QStringLiteral("abcdef1234567890abcdef1234567890abcdef12"));
    EXPECT_EQ(rows[0].state, TorrentState::Completed);
}

TEST_F(LegacyImporterTorrentsTest, LegacyNoMagnetFlagSetWhenAbsent) {
    // Mirrors the verified live 2026-05-19 shape: no magnetUri field at all.
    const QByteArray fixture = R"({
      "active": {
        "1111111111111111111111111111111111111111": {
          "state": "paused",
          "name": "Legacy Row",
          "addedAt": "2026-05-01T10:00:00Z",
          "savePath": "C:/Media/Legacy"
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].state, TorrentState::Paused);
    EXPECT_TRUE(rows[0].magnetUri.isEmpty());
    EXPECT_TRUE(rows[0].legacyNoMagnet);
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, LegacyNoMagnetFlagFalseWhenPresent) {
    const QByteArray fixture = R"({
      "active": {
        "2222222222222222222222222222222222222222": {
          "state": "downloading",
          "name": "Has Magnet",
          "addedAt": "2026-05-01T10:00:00Z",
          "magnetUri": "magnet:?xt=urn:btih:2222222222222222222222222222222222222222"
        }
      }
    })";

    LegacyImporter imp;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture));

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].magnetUri.isEmpty());
    EXPECT_FALSE(rows[0].legacyNoMagnet);
}

TEST_F(LegacyImporterTorrentsTest, MissingFileReturnsEmptyVector) {
    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(missingPath(), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, MalformedJsonReturnsEmptyWithWarning) {
    const QByteArray fixture = "{ this is not valid json, ";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_FALSE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, AbsentActiveKeyReturnsEmptyWithWarning) {
    const QByteArray fixture = R"({
      "other": { "foo": "bar" }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_FALSE(warnings.isEmpty());
}

TEST_F(LegacyImporterTorrentsTest, MultipleRowsPreservedInOrderOfInsertion) {
    // QJsonObject orders keys lexicographically, so the parser walks the
    // active object in lexicographic-key order. Three rows seeded with
    // already-sorted keys → output preserves that order one-for-one.
    const QByteArray fixture = R"({
      "active": {
        "1111111111111111111111111111111111111111": {
          "state": "downloading",
          "name": "Row One",
          "addedAt": "2026-05-01T10:00:00Z"
        },
        "2222222222222222222222222222222222222222": {
          "state": "paused",
          "name": "Row Two",
          "addedAt": "2026-05-01T11:00:00Z"
        },
        "3333333333333333333333333333333333333333": {
          "state": "completed",
          "name": "Row Three",
          "addedAt": "2026-05-01T12:00:00Z"
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseTorrentsJson(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].name, QStringLiteral("Row One"));
    EXPECT_EQ(rows[0].state, TorrentState::Active);
    EXPECT_EQ(rows[1].name, QStringLiteral("Row Two"));
    EXPECT_EQ(rows[1].state, TorrentState::Paused);
    EXPECT_EQ(rows[2].name, QStringLiteral("Row Three"));
    EXPECT_EQ(rows[2].state, TorrentState::Completed);
    EXPECT_TRUE(warnings.isEmpty());
}
