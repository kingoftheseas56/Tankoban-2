#include <gtest/gtest.h>
#include "core/manga/WesternIssueKey.h"

using tankoban::manga::isWesternIssueCbz;
using tankoban::manga::westernIssueNumber;

TEST(WesternIssueKey, MatchesIssueNamedCbz) {
    EXPECT_TRUE(isWesternIssueCbz("Invincible #1"));
    EXPECT_TRUE(isWesternIssueCbz("The Walking Dead #144"));
    EXPECT_TRUE(isWesternIssueCbz("Saga #0"));
}

TEST(WesternIssueKey, RejectsMangaVolumeNames) {
    EXPECT_FALSE(isWesternIssueCbz("One Piece v114"));
    EXPECT_FALSE(isWesternIssueCbz("Volume X"));
    EXPECT_FALSE(isWesternIssueCbz("Death Note Volume 1"));
    EXPECT_FALSE(isWesternIssueCbz("Chapter 5"));
}

TEST(WesternIssueKey, ParsesIssueNumber) {
    EXPECT_EQ(westernIssueNumber("Invincible #1"), 1);
    EXPECT_EQ(westernIssueNumber("The Walking Dead #144"), 144);
    EXPECT_EQ(westernIssueNumber("Saga #0"), 0);
    EXPECT_EQ(westernIssueNumber("One Piece v114"), -1); // not a western issue
}
