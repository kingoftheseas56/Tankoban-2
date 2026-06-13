#include <gtest/gtest.h>
#include "core/manga/MangaDownloadIndex.h"

// Six-mode restructure Arc 1 (2026-06-07) — the comics split tags every
// download with its owning mode (Asian "manga" vs "western") so the two
// top-level modes can each show only their own downloads. The classifier is
// pure (static, no index state) and prefix-tolerant.

TEST(MangaDownloadIndexOrigin, ClassifiesAsianSourcesAsManga) {
    EXPECT_EQ(MangaDownloadIndex::originForSource("anilist"),           "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("mangafire_catalog"), "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("weebcentral"),       "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("tankoyomi_premium"), "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource("mangaupdates"),      "manga");
}

TEST(MangaDownloadIndexOrigin, ClassifiesWesternSourcesAsWestern) {
    EXPECT_EQ(MangaDownloadIndex::originForSource("getcomics"),     "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("readcomics"),    "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("readallcomics"), "western");
}

TEST(MangaDownloadIndexOrigin, IsCaseAndPrefixTolerant) {
    // Real sourceIds carry suffixes (e.g. "getcomics_v2", "readcomics_dl").
    // "readallcomics" must NOT be swallowed by the "readcomics" prefix —
    // they diverge at index 4 ('a' vs 'c'), so both stay western.
    EXPECT_EQ(MangaDownloadIndex::originForSource("GetComics"),    "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("getcomics_v2"), "western");
    EXPECT_EQ(MangaDownloadIndex::originForSource("readallcomics_issue"), "western");
}

TEST(MangaDownloadIndexOrigin, UnknownDefaultsToManga) {
    // Conservative: unknown sources stay in Manga (the historical home) so
    // nothing silently vanishes from both views.
    EXPECT_EQ(MangaDownloadIndex::originForSource("mystery_source"), "manga");
    EXPECT_EQ(MangaDownloadIndex::originForSource(""),               "manga");
}

// ── Task 2: origin-filtered accessor ────────────────────────────────────────

TEST(MangaDownloadIndexOrigin, EntriesForOriginPartitionsByMode) {
    MangaDownloadIndex idx(nullptr);
    idx.registerVolume("anilist",   "one-piece", 1, "C:/tmp/op1.cbz", 100, {"c1"});
    idx.registerVolume("getcomics", "invincible", 1, "C:/tmp/inv1.cbz", 100, {"i1"});

    const QList<MangaDownloadIndex::Entry> western = idx.entriesForOrigin("western");
    const QList<MangaDownloadIndex::Entry> manga   = idx.entriesForOrigin("manga");

    ASSERT_EQ(western.size(), 1);
    EXPECT_EQ(western.first().seriesId, "invincible");
    EXPECT_EQ(western.first().sourceId, "getcomics");

    ASSERT_EQ(manga.size(), 1);
    EXPECT_EQ(manga.first().seriesId, "one-piece");
    EXPECT_EQ(manga.first().sourceId, "anilist");
}

TEST(MangaDownloadIndexOrigin, EntriesForOriginEmptyWhenNoneMatch) {
    MangaDownloadIndex idx(nullptr);
    idx.registerVolume("anilist", "berserk", 1, "C:/tmp/b1.cbz", 100, {"c1"});

    EXPECT_TRUE(idx.entriesForOrigin("western").isEmpty());
    EXPECT_EQ(idx.entriesForOrigin("manga").size(), 1);
}
