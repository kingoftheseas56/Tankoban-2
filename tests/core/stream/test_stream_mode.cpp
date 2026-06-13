#include <gtest/gtest.h>
#include "core/stream/StreamMode.h"

TEST(StreamMode, AnimeFlagWinsForBothTypes) {
    EXPECT_EQ(classifyStreamMode(true,  "series"), StreamMode::Anime);
    EXPECT_EQ(classifyStreamMode(true,  "movie"),  StreamMode::Anime);  // anime film
}
TEST(StreamMode, NonAnimeSeriesIsTv)    { EXPECT_EQ(classifyStreamMode(false,"series"), StreamMode::TV); }
TEST(StreamMode, NonAnimeMovieIsMovies) {
    EXPECT_EQ(classifyStreamMode(false,"movie"), StreamMode::Movies);
    EXPECT_EQ(classifyStreamMode(false,""),      StreamMode::Movies);
}
TEST(StreamMode, AnimeTitleNormalizesCountryAndGenre) {
    EXPECT_TRUE (isAnimeTitle({"Animation","Action"}, "Japan"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "JP"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan, China"));
    EXPECT_TRUE (isAnimeTitle({"Animation"},          "Japan / USA"));
    EXPECT_FALSE(isAnimeTitle({"Animation"},          "United States"));
    EXPECT_FALSE(isAnimeTitle({"Drama"},              "Japan"));
    EXPECT_FALSE(isAnimeTitle({},                      "Japan"));
}
TEST(StreamMode, KeyAndFilenameRoundTrip) {
    EXPECT_EQ(streamModeKey(StreamMode::Anime),  QStringLiteral("anime"));
    EXPECT_EQ(streamModeFromKey("tv"),           StreamMode::TV);
    EXPECT_EQ(streamModeFromKey("bogus"),        StreamMode::Movies);
    EXPECT_EQ(streamLibraryFilename(StreamMode::Movies), QStringLiteral("movies_library.json"));
}
