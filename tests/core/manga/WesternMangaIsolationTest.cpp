#include <gtest/gtest.h>
#include "core/manga/WesternIssueKey.h"

using tankoban::manga::isWesternIssueCbz;

// The manga Continue/Library strips EXCLUDE western issues; the western strips
// INCLUDE them. Both branch on this one predicate (isWesternIssueCbz), so its
// contract IS the isolation guard against the leak fixed this session
// (western issues showing up in the Manga Continue Reading strip).
TEST(WesternMangaIsolation, MangaVolumesNeverClassifiedWestern) {
    for (const char* manga : {"One Piece v114", "Death Note Volume 1",
                              "Volume X", "Berserk v40", "Naruto vol 72",
                              "20th Century Boys Volume 01"})
        EXPECT_FALSE(isWesternIssueCbz(manga)) << manga;
}

TEST(WesternMangaIsolation, WesternIssuesNeverClassifiedManga) {
    for (const char* w : {"Invincible #1", "Saga #54", "Watchmen #12",
                          "The Walking Dead #144"})
        EXPECT_TRUE(isWesternIssueCbz(w)) << w;
}
