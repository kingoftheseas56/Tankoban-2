// tests/core/manga/fandom/test_table_extractor_death_note_covers.cpp
//
// Regression test for Comics Series Page Polish Task 1.
//
// Per docs/superpowers/specs/2026-05-20-comics-series-page-polish-design.md
// § 3.5 (Per-volume thumbnails), every volume row in the Comics series view
// should surface a per-volume cover when the upstream Fandom catalog has one
// available. The existing test_table_extractor.cpp already asserts that Vol 1
// of Death Note carries both `coverUrlEnglish` + `coverUrlJapanese`; this file
// extends that assertion across the WHOLE series to catch regressions where a
// single-volume change to the extractor (or the upstream HTML drifting) silently
// strips cover URLs from later volumes.
//
// Tolerance is set to "at least 10 of 12 volumes have a non-empty
// coverUrlEnglish" — the manifest's editionFilters drop "Volume 13: How to Read"
// and "Short Stories" so the in-list size is 12; allowing 2 missing leaves
// room for the occasional Viz-edition-not-uploaded volume without false-positive
// regressions, while still flagging the broader "covers are gone" failure mode.

#include <gtest/gtest.h>

#include "core/manga/fandom/extractors/TableExtractor.h"
#include "core/manga/fandom/WikiManifest.h"

#include <QFile>
#include <QJsonDocument>

using namespace tankoban::manga::fandom;

namespace {

QString fixturePath(const QString& relPath)
{
    return QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath;
}

WikiManifest loadManifest(const QString& relPath)
{
    QFile f(fixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    return WikiManifest::fromJson(doc.object());
}

QString loadFixture(const QString& relPath)
{
    QFile f(fixturePath(relPath));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // anonymous namespace

TEST(TableExtractorDeathNoteCovers, AtLeast10Of12VolumesHaveEnglishCoverUrl)
{
    WikiManifest m = loadManifest(
        QStringLiteral("fandom/manifests/death-note.json"));
    ASSERT_TRUE(m.isValid()) << "manifest failed to load";

    QString html = loadFixture(QStringLiteral(
        "fandom/death-note_list-of-chapters_2026-05-19.html"));
    ASSERT_FALSE(html.isEmpty()) << "fixture empty or missing";

    QList<FandomVolume> vols = TableExtractor::extract(html, m);
    ASSERT_EQ(vols.size(), 12)
        << "Death Note expected to yield 12 main volumes after editionFilters; "
        << "got " << vols.size();

    int withEnglishCover = 0;
    for (const FandomVolume& v : vols) {
        if (!v.coverUrlEnglish.isEmpty())
            ++withEnglishCover;
    }

    EXPECT_GE(withEnglishCover, 10)
        << "Expected at least 10 of 12 Death Note volumes to surface a "
        << "non-empty coverUrlEnglish; only " << withEnglishCover
        << " did. Series Page Polish Task 1 regression: per-volume thumbnails "
        << "will be sparse if this drops below the tolerance.";

    // Diagnostic detail — when this test fails, dump which volume numbers
    // are missing the cover so Task 2.5 (extractor extension) knows where
    // to look.
    if (withEnglishCover < 10) {
        for (const FandomVolume& v : vols) {
            if (v.coverUrlEnglish.isEmpty()) {
                ADD_FAILURE() << "Volume " << v.volumeNumber
                              << " (\"" << v.titleEnglish.toStdString()
                              << "\") has no coverUrlEnglish";
            }
        }
    }
}
