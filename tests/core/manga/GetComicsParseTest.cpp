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

// ── volume-aware matching (COMICS_WESTERN_GCD 2026-06-05) ────────────────────

TEST(GetComicsParse, TagSlug) {
    EXPECT_EQ(tagSlug("Saga"), "saga");
    EXPECT_EQ(tagSlug("The Wicked + The Divine"), "the-wicked-the-divine");
    EXPECT_EQ(tagSlug("Invincible"), "invincible");
}

// A standalone "<series> Vol. N" post is preferred over a range that covers N.
TEST(GetComicsParse, PickPostForVolumePrefersStandalone) {
    QList<SearchResult> r = {
        {"Saga Vol. 1 \xE2\x80\x93 10 + Book 1 \xE2\x80\x93 3 (TPB) (2012-2022)", "u-range"},
        {"Saga Vol. 12 (TPB) (2025)", "u-12"},
        {"Saga #72 (2025)", "u-iss"},
        {"Elektra Saga #1 (1984)", "u-noise"}};
    EXPECT_EQ(pickPostForVolume("Saga", 12, r).postUrl, "u-12");
}

// A gap volume (no standalone) falls back to the range post that covers it.
TEST(GetComicsParse, PickPostForVolumeFallsBackToRange) {
    QList<SearchResult> r = {
        {"Saga Vol. 1 \xE2\x80\x93 10 + Book 1 \xE2\x80\x93 3 (TPB) (2012-2022)", "u-range"},
        {"Saga Vol. 12 (TPB) (2025)", "u-12"}};
    EXPECT_EQ(pickPostForVolume("Saga", 3, r).postUrl, "u-range");
}

// No post covers the volume -> empty (caller treats as unavailable).
TEST(GetComicsParse, PickPostForVolumeNoneWhenUncovered) {
    QList<SearchResult> r = {{"Saga Vol. 12 (TPB) (2025)", "u-12"}};
    EXPECT_TRUE(pickPostForVolume("Saga", 3, r).postUrl.isEmpty());
}

// Wrong-series noise sharing the word "Saga" is rejected even if it has a volume.
TEST(GetComicsParse, PickPostForVolumeRejectsWrongSeries) {
    QList<SearchResult> r = {
        {"Spider-Man \xE2\x80\x93 Clone Saga Omnibus Vol. 1 (2016)", "u-clone"}};
    EXPECT_TRUE(pickPostForVolume("Saga", 1, r).postUrl.isEmpty());
}

// Standalone post (real shape, verified live 2026-06-05): no per-volume <li>
// list; the clean primary button is labelled "DOWNLOAD NOW" (not "Main Server").
// extractVolumeDownload falls back to pickBest over the whole post.
TEST(GetComicsParse, ExtractVolumeDownloadStandalone) {
    const QString html =
        R"HTML(<p><strong>Saga Vol. 12 (TPB) (2025)</strong></p>)HTML"
        R"HTML(<a href="https://getcomics.org/dls/AAA:sig==">DOWNLOAD NOW</a>)HTML"
        R"HTML(<a href="https://getcomics.org/dls/MEG:sig==">MEGA</a>)HTML";
    // DOWNLOAD NOW now classifies as main_server (top priority after magnet).
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 12).url,
              "https://getcomics.org/dls/AAA:sig==");
}

// Range/bundle post (real shape): each volume is its own <li>, label-first, with
// "Link 1"/"Main Server" the clean primary + mirror links. Pick the right <li>.
TEST(GetComicsParse, ExtractVolumeDownloadRangePicksRightSection) {
    const QString html =
        R"HTML(<ul>)HTML"
        R"HTML(<li>Saga Vol. 1 (2012) (841 MB) : <a href="https://getcomics.org/dls/V1:s=="><span>Link 1</span></a> | <a href="https://getcomics.org/dls/V1M:s=="><span>Mega</span></a></li>)HTML"
        R"HTML(<li>Saga Vol. 3 (2014) (324 MB) : <a href="https://getcomics.org/dls/V3:s=="><span>Main Server</span></a> | <a href="https://getcomics.org/dls/V3M:s=="><span>Mediafire</span></a></li>)HTML"
        R"HTML(<li>Saga Vol. 10 (2022) (420 MB) : <a href="https://getcomics.org/dls/V10:s=="><span>Link 1</span></a></li>)HTML"
        R"HTML(</ul>)HTML";
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 3).url, "https://getcomics.org/dls/V3:s==");
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 10).url, "https://getcomics.org/dls/V10:s==");
}

// Codex review fix: full-word "Volume N" (not just "Vol.") must match, in both
// post selection and per-<li> extraction.
TEST(GetComicsParse, VolumeFullWordMatches) {
    QList<SearchResult> r = {{"Saga Volume 3 (TPB) (2014)", "u-3"}};
    EXPECT_EQ(pickPostForVolume("Saga", 3, r).postUrl, "u-3");
    const QString html =
        R"HTML(<ul><li>Saga Volume 3 (2014) : <a href="https://getcomics.org/dls/V3:s=="><span>Main Server</span></a></li></ul>)HTML";
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 3).url, "https://getcomics.org/dls/V3:s==");
}

// Codex review fix: clean main-server link is preferred over a magnet (DoD is
// clean HTTP). pickBest alone would return the magnet first.
TEST(GetComicsParse, ExtractVolumeDownloadPrefersMainServer) {
    const QString html =
        R"HTML(<a href="magnet:?xt=urn:btih:ABC">Magnet</a>)HTML"
        R"HTML(<a href="https://getcomics.org/dls/MS:sig=="><span>Main Server</span></a>)HTML";
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 12).kind, "main_server");
    EXPECT_EQ(extractVolumeDownload(html, "Saga", 12).url, "https://getcomics.org/dls/MS:sig==");
}
