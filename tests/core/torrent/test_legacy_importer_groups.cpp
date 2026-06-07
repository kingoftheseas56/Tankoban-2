// tankoban_tests — LegacyImporter::parseStreamGroups +
//                   LegacyImporter::parseStreamGroupItems tests (P1.3)
//
// Fixture-driven coverage of the legacy stream_bulk_groups.json parsers. Both
// parsers walk the SAME on-disk file; they're tested together to keep the
// fixture corpus in one place.
//
// Fixtures use the plan §1.3 "expected legacy shape" (array of groups with
// flat field names) since the parsers accept both that and the real
// TorrentClient.cpp write shape (object-keyed groups with nested sourceIds).
// The 9 cases exercise: full-field roundtrip, item-to-group attribution,
// multi-group flattening, empty/missing/malformed degenerate inputs, and the
// new packMode boolean.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase
// 1 Task 1.3 for the parser contract this exercises.

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
using tankoban::torrent::StreamGroupItemRow;
using tankoban::torrent::StreamGroupRow;

class LegacyImporterGroupsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    QString writeFixture(const QByteArray& contents,
                         const QString& name = QStringLiteral(
                             "stream_bulk_groups.json")) {
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

TEST_F(LegacyImporterGroupsTest, ParsesSingleGroupFields) {
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "stream:tt0903747:s01:1779097229693",
          "imdbId": "tt0903747",
          "season": 1,
          "label": "Breaking Bad — Season 1",
          "state": "downloading",
          "retryGeneration": 2,
          "stagingPath": "C:/Media/.tankoban-partial/stream-tt0903747-s01",
          "createdAt": "2026-05-01T10:00:00Z",
          "packMode": false,
          "items": []
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamGroups(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 1u);
    const StreamGroupRow& g = rows[0];
    EXPECT_EQ(g.groupId,
              QStringLiteral("stream:tt0903747:s01:1779097229693"));
    EXPECT_EQ(g.imdbId, QStringLiteral("tt0903747"));
    EXPECT_EQ(g.season, 1);
    EXPECT_EQ(g.label, QStringLiteral("Breaking Bad — Season 1"));
    EXPECT_EQ(g.state, QStringLiteral("downloading"));
    EXPECT_EQ(g.retryGeneration, 2);
    EXPECT_EQ(g.stagingPath,
              QStringLiteral(
                  "C:/Media/.tankoban-partial/stream-tt0903747-s01"));
    EXPECT_TRUE(g.createdAt.isValid());
    EXPECT_FALSE(g.packMode);
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, ParsesNestedItemsWithGroupIdAttribution) {
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "grp-multi-item",
          "imdbId": "tt1111111",
          "season": 1,
          "label": "Show — Season 1",
          "state": "downloading",
          "createdAt": "2026-05-01T10:00:00Z",
          "items": [
            { "itemId": "tt1111111:S01E01", "episode": 1,
              "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
              "state": "Downloading", "errorMessage": "", "fileIndex": 0 },
            { "itemId": "tt1111111:S01E02", "episode": 2,
              "infoHash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 },
            { "itemId": "tt1111111:S01E03", "episode": 3,
              "infoHash": "cccccccccccccccccccccccccccccccccccccccc",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 }
          ]
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    ASSERT_EQ(items.size(), 3u);
    for (const auto& it : items) {
        EXPECT_EQ(it.groupId, QStringLiteral("grp-multi-item"));
    }
    EXPECT_EQ(items[0].itemId, QStringLiteral("tt1111111:S01E01"));
    EXPECT_EQ(items[0].episode, 1);
    EXPECT_EQ(items[0].state, QStringLiteral("Downloading"));
    EXPECT_EQ(items[0].fileIndex, 0);
    EXPECT_EQ(items[1].itemId, QStringLiteral("tt1111111:S01E02"));
    EXPECT_EQ(items[1].episode, 2);
    EXPECT_EQ(items[1].fileIndex, -1);
    EXPECT_EQ(items[2].itemId, QStringLiteral("tt1111111:S01E03"));
    EXPECT_EQ(items[2].episode, 3);
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, ParsesFourDigitEpisodeFromItemKeyWhenEpisodeFieldMissing) {
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "stream:tt0388629:s01:long",
          "imdbId": "tt0388629",
          "season": 1,
          "items": [
            { "itemKey": "tt0388629:S01E1164",
              "itemState": "Downloading",
              "infoHash": "",
              "fileIndex": 42 }
          ]
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].itemId, QStringLiteral("tt0388629:S01E1164"));
    EXPECT_EQ(items[0].episode, 1164);
    EXPECT_EQ(items[0].state, QStringLiteral("Downloading"));
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, MultipleGroupsItemsFlattenedAcrossGroups) {
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "grp-A",
          "imdbId": "tt2222222",
          "season": 1,
          "items": [
            { "itemId": "A:E01", "episode": 1, "infoHash": "",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 },
            { "itemId": "A:E02", "episode": 2, "infoHash": "",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 }
          ]
        },
        {
          "groupId": "grp-B",
          "imdbId": "tt3333333",
          "season": 2,
          "items": [
            { "itemId": "B:E01", "episode": 1, "infoHash": "",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 },
            { "itemId": "B:E02", "episode": 2, "infoHash": "",
              "state": "Pending", "errorMessage": "", "fileIndex": -1 }
          ]
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    ASSERT_EQ(groups.size(), 2u);
    ASSERT_EQ(items.size(), 4u);

    // Each item carries its parent's groupId.
    int countA = 0, countB = 0;
    for (const auto& it : items) {
        if (it.groupId == QStringLiteral("grp-A")) ++countA;
        else if (it.groupId == QStringLiteral("grp-B")) ++countB;
    }
    EXPECT_EQ(countA, 2);
    EXPECT_EQ(countB, 2);
}

TEST_F(LegacyImporterGroupsTest, EmptyGroupsArrayReturnsEmpty) {
    const QByteArray fixture = R"({ "groups": [] })";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    EXPECT_TRUE(groups.empty());
    EXPECT_TRUE(items.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, MissingGroupsKeyWarnsAndReturnsEmpty) {
    const QByteArray fixture = R"({ "other": { "foo": "bar" } })";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    EXPECT_TRUE(groups.empty());
    EXPECT_TRUE(items.empty());
    EXPECT_FALSE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, MissingFileReturnsEmpty) {
    LegacyImporter imp;
    QStringList warnings;
    const auto groups = imp.parseStreamGroups(missingPath(), &warnings);
    const auto items = imp.parseStreamGroupItems(missingPath(), &warnings);

    EXPECT_TRUE(groups.empty());
    EXPECT_TRUE(items.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, MalformedJsonWarnsAndReturnsEmpty) {
    const QByteArray fixture = "{ this is not valid json, ";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    EXPECT_TRUE(groups.empty());
    EXPECT_TRUE(items.empty());
    EXPECT_FALSE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, GroupWithoutItemsArrayReturnsEmptyItems) {
    // Group entry has every other field but omits the "items" array entirely.
    // parseStreamGroups still emits one StreamGroupRow; parseStreamGroupItems
    // emits zero items (no items key → skipped without warning).
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "grp-no-items",
          "imdbId": "tt4444444",
          "season": 1,
          "label": "Itemless Group",
          "state": "pending",
          "createdAt": "2026-05-01T10:00:00Z"
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);
    const auto items =
        imp.parseStreamGroupItems(writeFixture(fixture), &warnings);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].groupId, QStringLiteral("grp-no-items"));
    EXPECT_TRUE(items.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

TEST_F(LegacyImporterGroupsTest, PackModeFieldRoundTrip) {
    const QByteArray fixture = R"({
      "groups": [
        {
          "groupId": "grp-pack-true",
          "imdbId": "tt5555555",
          "season": 3,
          "label": "Pack-mode group",
          "state": "downloading",
          "createdAt": "2026-05-01T10:00:00Z",
          "packMode": true,
          "items": []
        },
        {
          "groupId": "grp-pack-false",
          "imdbId": "tt6666666",
          "season": 1,
          "label": "Non-pack group",
          "state": "downloading",
          "createdAt": "2026-05-01T10:00:00Z",
          "packMode": false,
          "items": []
        },
        {
          "groupId": "grp-pack-absent",
          "imdbId": "tt7777777",
          "season": 1,
          "label": "Absent packMode group",
          "state": "downloading",
          "createdAt": "2026-05-01T10:00:00Z",
          "items": []
        }
      ]
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto groups =
        imp.parseStreamGroups(writeFixture(fixture), &warnings);

    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].groupId, QStringLiteral("grp-pack-true"));
    EXPECT_TRUE(groups[0].packMode);
    EXPECT_EQ(groups[1].groupId, QStringLiteral("grp-pack-false"));
    EXPECT_FALSE(groups[1].packMode);
    // Absent key defaults to false — matches StreamGroupRow's default-init.
    EXPECT_EQ(groups[2].groupId, QStringLiteral("grp-pack-absent"));
    EXPECT_FALSE(groups[2].packMode);
    EXPECT_TRUE(warnings.isEmpty());
}
