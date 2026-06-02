// tests/core/manga/GetComicsParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/GetComicsParse.h"

using namespace tankoban::manga::getcomics;

TEST(GetComicsParse, ExtractDownloadsFiltersAndKinds) {
    const QString html = R"HTML(
        <a href="magnet:?xt=urn:btih:ABC">MAGNET Link</a>
        <a href="https://getcomics.org/dls/tok1">Main Server</a>
        <a href="https://getcomics.org/dls/tok2">Pixeldrain</a>
        <a href="https://craveu.example/ad">Hot Singles</a>
        <a href="https://getcomics.org/dls/tok3">MEGA</a>
    )HTML";
    const auto dls = extractDownloads(html);
    ASSERT_EQ(dls.size(), 4);                 // ad link dropped
    EXPECT_EQ(dls[0].kind, "magnet");
    EXPECT_EQ(dls[1].kind, "main_server");
    EXPECT_EQ(dls[2].kind, "pixeldrain");
    EXPECT_EQ(dls[3].kind, "mega");
}

TEST(GetComicsParse, PickBestPriority) {
    QList<DownloadLink> links = {
        {"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"magnet", "u-mag"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(links).kind, "magnet");
    QList<DownloadLink> noMagnet = {{"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(noMagnet).kind, "main_server");
    EXPECT_TRUE(pickBest({}).url.isEmpty());
}

TEST(GetComicsParse, ParsePostCover) {
    const QString html = R"HTML(<meta property="og:image" content="https://getcomics.org/x/Invincible-Compendium-1.jpg" />)HTML";
    EXPECT_EQ(parsePostCover(html), "https://getcomics.org/x/Invincible-Compendium-1.jpg");
    EXPECT_EQ(parsePostCover("<html>no og</html>"), "");
}

// parseSearchResults decodes the HTML entities GetComics emits in titles
// (&#8211; en-dash, &#038;/&amp; ampersand, &#8217; apostrophe) so both display
// and the identity matcher see real text.
TEST(GetComicsParse, ParseSearchResultsDecodesEntities) {
    const QString html = R"HTML(
        <h1 class="post-title"><a href="https://getcomics.org/p/inv">Invincible Compendium Vol. 1 &#8211; 3 (2013-2019)</a></h1>
        <h1 class="post-title"><a href="https://getcomics.org/p/alien">Alien by Shalvey &#038; Broccardo Vol. 2</a></h1>
    )HTML";
    const auto r = parseSearchResults(html);
    ASSERT_EQ(r.size(), 2);
    EXPECT_EQ(r[0].title, QString::fromUtf8("Invincible Compendium Vol. 1 \xE2\x80\x93 3 (2013-2019)"));
    EXPECT_EQ(r[1].title, QString::fromUtf8("Alien by Shalvey & Broccardo Vol. 2"));
}

// identityTokens drops edition noise, tier words, years, and pure numbers so two
// titles naming the same series compare equal.
TEST(GetComicsParse, IdentityTokensStripsNoiseTierYearNumber) {
    const auto t = identityTokens("Invincible Compendium Vol. 1 \xE2\x80\x93 3 (2013-2019)");
    ASSERT_EQ(t.size(), 1);
    EXPECT_EQ(t[0], "invincible");
    // Deluxe / Edition / Book / numbers all stripped.
    const auto u = identityTokens("Sweet Tooth \xE2\x80\x93 The Deluxe Edition Book 1 \xE2\x80\x93 3 (2015-2016)");
    EXPECT_EQ(u, (QStringList{"sweet", "tooth"}));
}

// ACCEPT: a collected edition of exactly this series.
TEST(GetComicsParse, IsCollectedEditionAcceptsExactSeries) {
    EXPECT_TRUE(isCollectedEditionOf("Invincible", "Invincible Compendium Vol. 1 \xE2\x80\x93 3 (2013-2019)"));
    EXPECT_TRUE(isCollectedEditionOf("Watchmen", "Watchmen (Collection) (1986-2019)"));
    EXPECT_TRUE(isCollectedEditionOf("Watchmen", "Watchmen (TPB) (1995)"));
    EXPECT_TRUE(isCollectedEditionOf("Preacher", "Preacher \xE2\x80\x93 Book 1 \xE2\x80\x93 6 (Complete) (2009-2014)"));
    EXPECT_TRUE(isCollectedEditionOf("Sweet Tooth", "Sweet Tooth Vol. 1 \xE2\x80\x93 6 (TPB) (2010-2013)"));
    EXPECT_TRUE(isCollectedEditionOf("Descender", "Descender Compendium (2024)"));
    // The Wicked + The Divine: "the" is noise on both sides.
    EXPECT_TRUE(isCollectedEditionOf("The Wicked The Divine", "The Wicked + The Divine Compendium (2020)"));
}

// REJECT: a DIFFERENT series sharing a word (the false-positives Codex hammered).
TEST(GetComicsParse, IsCollectedEditionRejectsDifferentSeries) {
    EXPECT_FALSE(isCollectedEditionOf("Invincible", "Invincible Iron Man Omnibus Vol. 1 (2023)"));
    EXPECT_FALSE(isCollectedEditionOf("Invincible", "Invincible Universe Compendium Vol. 1 (TPB) (2023)"));
    EXPECT_FALSE(isCollectedEditionOf("Invincible", "The Invincible Red Sonja Vol. 1 (TPB) (2023)"));
    EXPECT_FALSE(isCollectedEditionOf("Invincible", "Marvel Masterworks \xE2\x80\x93 The Invincible Iron Man Vol. 1 \xE2\x80\x93 18"));
}

// REJECT: same series but a sub-named edition with an extra word — the accepted
// STRICT trade (Spawn "Origins" Collection misses rather than risk a wrong grab).
TEST(GetComicsParse, IsCollectedEditionRejectsSubNamedExtraWord) {
    EXPECT_FALSE(isCollectedEditionOf("Spawn", "Spawn Origins Collection Vol. 1 \xE2\x80\x93 31 (2010-2025)"));
}

// REJECT: a single issue of the right series — identity matches but there is no
// collected-edition marker (no tier word, no multi-volume range).
TEST(GetComicsParse, IsCollectedEditionRejectsSingleIssue) {
    EXPECT_FALSE(isCollectedEditionOf("Spawn", "Spawn #375 (2026)"));
    EXPECT_FALSE(isCollectedEditionOf("Spawn", "King Spawn #55 (2026)"));  // also extra word "king"
}

// The full live "Invincible Vol 1" result set: noise rejected, the compendium
// is the unique collected edition picked.
TEST(GetComicsParse, PickBestCollectedEditionRealNoisySet) {
    QList<SearchResult> results = {
        {"Invincible Iron Man Omnibus Vol. 1 (2023)", "u-iron"},
        {"Invincible Universe \xE2\x80\x93 Battle Beast Vol. 1 (TPB) (2025)", "u-bb"},
        {"Marvel Masterworks \xE2\x80\x93 The Invincible Iron Man Vol. 1 \xE2\x80\x93 18 (2003-2025)", "u-mm"},
        {"Invincible Compendium Vol. 1 \xE2\x80\x93 3 (2013-2019)", "u-comp"},
        {"Invincible Universe Compendium Vol. 1 (TPB) (2023)", "u-univ"},
        {"The Invincible Red Sonja Vol. 1 (TPB) (2023)", "u-sonja"}};
    EXPECT_EQ(pickBestCollectedEdition("Invincible", 0, results).postUrl, "u-comp");
}

// Prefers the most canonical collected tier: Compendium over a raw issue-run.
TEST(GetComicsParse, PickBestCollectedEditionPrefersCanonicalTier) {
    QList<SearchResult> results = {
        {"Descender #1 \xE2\x80\x93 32 + TPBs (2015-2018)", "u-run"},
        {"Descender Compendium (2024)", "u-comp"}};
    EXPECT_EQ(pickBestCollectedEdition("Descender", 0, results).postUrl, "u-comp");
}

// No qualifying collected edition -> empty (fail safe).
TEST(GetComicsParse, PickBestCollectedEditionFailSafeNoMatch) {
    QList<SearchResult> junk = {{"Batman Year One", "u1"}, {"Spawn #375", "u2"}};
    EXPECT_TRUE(pickBestCollectedEdition("Invincible", 0, junk).postUrl.isEmpty());
}

// Ambiguous tie (two qualifiers, identical rank) -> empty (fail safe).
TEST(GetComicsParse, PickBestCollectedEditionTieReturnsEmpty) {
    QList<SearchResult> tie = {
        {"Invincible Compendium Vol. 1", "u1"},
        {"Invincible Compendium Vol. 2", "u2"}};   // same tier, no year -> equal rank
    EXPECT_TRUE(pickBestCollectedEdition("Invincible", 0, tie).postUrl.isEmpty());
}
