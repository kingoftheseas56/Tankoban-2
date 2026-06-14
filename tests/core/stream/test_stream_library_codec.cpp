#include <gtest/gtest.h>
#include <QJsonObject>
#include "core/stream/StreamLibraryCodec.h"

TEST(StreamLibraryCodec, RoundTripsNewFields) {
    StreamLibraryEntry e; e.imdb="tt9335498"; e.type="series"; e.name="Demon Slayer";
    e.animeFlag=true; e.kitsuId=41370;
    const StreamLibraryEntry back = streamLibraryEntryFromJson(streamLibraryEntryToJson(e));
    EXPECT_TRUE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, 41370);
    EXPECT_EQ(back.type, QStringLiteral("series"));
}
TEST(StreamLibraryCodec, LegacyRowsDefaultNonAnime) {
    QJsonObject legacy; legacy["imdb"]="tt0111161"; legacy["type"]="movie"; legacy["name"]="X";
    const StreamLibraryEntry back = streamLibraryEntryFromJson(legacy);
    EXPECT_FALSE(back.animeFlag);
    EXPECT_EQ(back.kitsuId, -1);
}
