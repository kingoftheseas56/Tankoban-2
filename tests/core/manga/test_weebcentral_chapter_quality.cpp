// tests/core/manga/test_weebcentral_chapter_quality.cpp
#include <gtest/gtest.h>
#include "core/manga/WeebCentralScraper.h"
#include "core/manga/MangaResult.h"

TEST(WeebCentralChapterQuality, VioletTickMarksVolumeScannedGrayDoesNot)
{
    const QString html = R"HTML(
      <a href="/chapters/AAA"><span class="me-2"><svg class="w-4 h-4" stroke="#d8b4fe"></svg></span><span>Chapter 1133</span></a>
      <a href="/chapters/BBB"><span class="me-2"><svg class="w-4 h-4" stroke="#4C4D54"></svg></span><span>Chapter 1134</span></a>
    )HTML";
    const auto chapters = WeebCentralScraper::parseChaptersHtmlForTest(html, "weebcentral");
    ASSERT_EQ(chapters.size(), 2);
    // parseChaptersHtml sorts ascending by chapter number → [1133, 1134]
    EXPECT_DOUBLE_EQ(chapters[0].chapterNumber, 1133.0);
    EXPECT_TRUE(chapters[0].isVolumeScanned);
    EXPECT_DOUBLE_EQ(chapters[1].chapterNumber, 1134.0);
    EXPECT_FALSE(chapters[1].isVolumeScanned);
}
