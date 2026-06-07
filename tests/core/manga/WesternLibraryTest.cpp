#include <gtest/gtest.h>
#include "core/manga/WesternLibrary.h"

using tankoban::manga::WesternLibrary;
using tankoban::manga::WesternLibraryRecord;

static WesternLibraryRecord rec(const QString& id, const QString& title) {
    WesternLibraryRecord r;
    r.seriesId = id; r.title = title;
    r.coverUrl = "http://x/" + id + ".jpg"; r.addedAt = 1000;
    return r;
}

TEST(WesternLibrary, AddThenContainsAndGet) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    EXPECT_TRUE(lib.contains("invincible"));
    EXPECT_FALSE(lib.contains("saga"));
    ASSERT_TRUE(lib.get("invincible").has_value());
    EXPECT_EQ(lib.get("invincible")->title.toStdString(), "Invincible");
}

TEST(WesternLibrary, AddOrUpdateIsIdempotentBySeriesId) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    lib.addOrUpdate(rec("invincible", "Invincible (Image)"));
    EXPECT_EQ(lib.all().size(), 1);
    EXPECT_EQ(lib.get("invincible")->title.toStdString(), "Invincible (Image)");
}

TEST(WesternLibrary, RemoveDropsOnlyThatSeries) {
    WesternLibrary lib(nullptr);
    lib.addOrUpdate(rec("invincible", "Invincible"));
    lib.addOrUpdate(rec("saga", "Saga"));
    lib.remove("invincible");
    EXPECT_FALSE(lib.contains("invincible"));
    EXPECT_TRUE(lib.contains("saga"));
    EXPECT_EQ(lib.all().size(), 1);
}

TEST(WesternLibrary, RecordJsonRoundTrip) {
    const auto r = rec("invincible", "Invincible");
    const auto back = WesternLibraryRecord::fromJson(r.toJson());
    EXPECT_EQ(back.seriesId.toStdString(), "invincible");
    EXPECT_EQ(back.title.toStdString(), "Invincible");
    EXPECT_EQ(back.coverUrl.toStdString(), "http://x/invincible.jpg");
    EXPECT_EQ(back.addedAt, 1000);
}
