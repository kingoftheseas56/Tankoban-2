#include <gtest/gtest.h>
#include "core/manga/WeebCentralScraper.h"
#include <QString>

// Mirrors the live double_page_v2 markup: repeated <div x-show="page === N">
// blocks each wrapping one <img src=...>. Cover is alone (group 1, one img);
// later groups carry two halves.
static const char* kV2Html = R"HTML(
<section>
  <div x-show="page === 1" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-001.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 2" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-003.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 2" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-002.png" decoding="async" alt="" />
  </div>
  <div x-show="page === 3" class="max-w-full">
    <img src="https://cdn.example/One-Piece/0001-005.png" decoding="async" alt="" />
  </div>
</section>
)HTML";

TEST(WeebCentralPairedParse, GroupsCoverAloneAndPairsInOrder)
{
    const QList<PageInfo> pages =
        WeebCentralScraper::parsePagesPairedHtmlForTest(QString::fromUtf8(kV2Html));

    ASSERT_EQ(pages.size(), 4);

    // Group 1: cover, alone.
    EXPECT_EQ(pages[0].pageGroup, 1);
    EXPECT_TRUE(pages[0].imageUrl.endsWith("0001-001.png"));

    // Group 2: two halves, preserved in document (left-to-right visual) order.
    EXPECT_EQ(pages[1].pageGroup, 2);
    EXPECT_TRUE(pages[1].imageUrl.endsWith("0001-003.png"));
    EXPECT_EQ(pages[2].pageGroup, 2);
    EXPECT_TRUE(pages[2].imageUrl.endsWith("0001-002.png"));

    // Group 3: single (e.g. a natively-wide spread).
    EXPECT_EQ(pages[3].pageGroup, 3);
    EXPECT_TRUE(pages[3].imageUrl.endsWith("0001-005.png"));
}

TEST(WeebCentralPairedParse, SkipsBrokenImagePlaceholder)
{
    const QString html = QStringLiteral(
        "<div x-show=\"page === 1\"><img src=\"https://cdn/x/broken_image.jpg\"/></div>"
        "<div x-show=\"page === 1\"><img src=\"https://cdn/x/0001-001.png\"/></div>");
    const QList<PageInfo> pages =
        WeebCentralScraper::parsePagesPairedHtmlForTest(html);
    ASSERT_EQ(pages.size(), 1);
    EXPECT_TRUE(pages[0].imageUrl.endsWith("0001-001.png"));
    EXPECT_EQ(pages[0].pageGroup, 1);
}
