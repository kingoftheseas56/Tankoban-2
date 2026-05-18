#include "core/manga/bookwalker/VolumeCoverAlignment.h"

#include <gtest/gtest.h>
#include <QString>
#include <QList>

using tankoban::manga::bookwalker::VolumeCoverAlignment;

namespace {

QList<QString> mkUrls(int n) {
    QList<QString> out;
    for (int i = 1; i <= n; ++i) {
        out.append(QStringLiteral("https://rimg.bookwalker.jp/v%1.jpg").arg(i, 7, 10, QChar('0')));
    }
    return out;
}

} // namespace

TEST(VolumeCoverAlignment, ExactCountAlignsOneToOne) {
    auto urls = mkUrls(43);
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 43);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[43], urls[42]);
}

TEST(VolumeCoverAlignment, OverflowDropsTail) {
    auto urls = mkUrls(60); // 43 regular + 17 omnibus/special
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 43);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[43], urls[42]);
    EXPECT_FALSE(m.contains(44));
}

TEST(VolumeCoverAlignment, ShortfallMapsWhatWeHave) {
    auto urls = mkUrls(30); // BookWalker missing some
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/43);
    ASSERT_EQ(m.size(), 30);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[30], urls[29]);
    EXPECT_FALSE(m.contains(31));
}

TEST(VolumeCoverAlignment, ZeroCanonicalReturnsAllAsIs) {
    // Degraded path: MangaUpdates count unavailable, use BookWalker raw count.
    auto urls = mkUrls(5);
    auto m = VolumeCoverAlignment::align(urls, /*canonicalCount=*/0);
    ASSERT_EQ(m.size(), 5);
    EXPECT_EQ(m[1], urls[0]);
    EXPECT_EQ(m[5], urls[4]);
}

TEST(VolumeCoverAlignment, EmptyInputReturnsEmpty) {
    auto m = VolumeCoverAlignment::align(QList<QString>{}, 10);
    EXPECT_TRUE(m.isEmpty());
}
