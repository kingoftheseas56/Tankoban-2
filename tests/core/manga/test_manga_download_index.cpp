#include <gtest/gtest.h>
#include "core/manga/MangaDownloadIndex.h"

TEST(MangaDownloadIndex, EvictByVolumeRemovesOnlyThatVolume) {
    MangaDownloadIndex idx(nullptr);
    idx.registerVolume("weebcentral", "one-piece", 1,
                       "C:/tmp/v1.cbz", 100, {"c1", "c2"});
    idx.registerVolume("weebcentral", "one-piece", 2,
                       "C:/tmp/v2.cbz", 100, {"c3", "c4"});

    idx.evictByVolume("weebcentral", "one-piece", 1);

    EXPECT_FALSE(idx.entryForSeriesAndVolume("weebcentral", "one-piece", 1).has_value());
    EXPECT_TRUE(idx.entryForSeriesAndVolume("weebcentral", "one-piece", 2).has_value());
}

TEST(MangaDownloadIndex, EvictByVolumeNoOpWhenVolumeNotFound) {
    MangaDownloadIndex idx(nullptr);
    idx.registerVolume("weebcentral", "one-piece", 1,
                       "C:/tmp/v1.cbz", 100, {"c1", "c2"});

    idx.evictByVolume("weebcentral", "one-piece", 99);

    EXPECT_TRUE(idx.entryForSeriesAndVolume("weebcentral", "one-piece", 1).has_value());
    EXPECT_TRUE(idx.hasAnyForSeries("weebcentral", "one-piece"));
}

TEST(MangaDownloadIndex, EvictByVolumeClearsSeriesHasAnyWhenLastEntry) {
    MangaDownloadIndex idx(nullptr);
    idx.registerVolume("weebcentral", "naruto", 1,
                       "C:/tmp/n1.cbz", 100, {"c1"});

    idx.evictByVolume("weebcentral", "naruto", 1);

    EXPECT_FALSE(idx.hasAnyForSeries("weebcentral", "naruto"));
}
