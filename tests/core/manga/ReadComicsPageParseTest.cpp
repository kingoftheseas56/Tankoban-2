// tests/core/manga/ReadComicsPageParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/ReadComicsPageParse.h"

using namespace tankoban::manga::readcomics;

// baeu() reproduces gallery-dl's readcomiconline descramble (rguard v1.5.8).
// Vector frozen from a live rcostation reader page (Invincible/TPB-1, captured
// 2026-06-03). raw token + its page replacements -> exact blogspot path. The
// query tail (rhlupa/rnvuka) rides through verbatim and is not asserted (it
// encodes the capturing client's IP/UA/timestamp, not part of the transform).
TEST(ReadComicsPageParse, BaeuDescramblesFrozenVector) {
    const QList<QPair<QString, QString>> repls = {
        {"Gs__iUlRK0_", "g"}, {"b", "pw_.g28x"}, {"h", "d2pr.x_27"}};
    const QString rawToken =
        "5DwBVtm19VrHulZb3MtWDB0UDJabmt0TGozn4etEalUYDxMQU14tazNpZTNuQUxrX19lQm1xVkYwZDl5"
        "aEhKeXVnQloxTzlLaHdvV1U1bUU2akdrdk92b2pZRGc5VHBKN0V6OW9sRmplQQvUaNzplaV===s1600"
        "?rhlupa=MjQwNToyMDE6YzAwZTpkMGYzOmZkZDA6NDkwNzo1MjJmOjk3MmEuNi8yLzIwMjYgMToxMDo0"
        "OSBQTS0wLXIwLXMtYzk4NA&rnvuka=TW96aWxsYS81LjAgKFdpbmRvd3MgTlQgMTAuMDsgV2luNjQ7IH"
        "g2NCkgQXBwbGVXZWJLaXQvNTM3LjM2IChLSFRNTCwgbGlrZSBHZWNrbykgQ2hyb21lLzEzNC4wLjAuMC"
        "BTYWZhcmkvNTM3LjM2";
    const QString url = baeu(applyReplacements(rawToken, repls), QString());
    EXPECT_TRUE(url.startsWith(
        "https://2.bp.blogspot.com/os-X0tP2ZnktLie3nALk__eBmqVF0d9yhHJyugBZ1O9KhwoWU5mE6jGkvOvojYDg9TpJ7Ez9olFj=s1600"))
        << "got: " << url.toStdString();
}

// parseReaderPages() extracts ordered page URLs from a reader HTML payload.
// Synthetic fixture uses already-`https` tokens so it exercises the extraction
// (root/var/replacements/split) without depending on the transpose offsets.
TEST(ReadComicsPageParse, ParseReaderPagesExtractsOrderedUrls) {
    const QString html = R"HTML(
        <script>
        var pth = '';
        var _ZZ = '';
        l = l.replace(/Gs__iUlRK0_/g, 'g');
        l = l.replace(/b/g, 'pw_.g28x');
        l = l.replace(/h/g, 'd2pr.x_27');
        return baeu(l, '');
        _ZZ = 'https://2.bp.blogspot.com/already-plain=s1600?rhlupa=x';
        _ZZ = 'https://2.bp.blogspot.com/second-plain=s1600?rhlupa=y';
        </script>
    )HTML";
    const auto pages = parseReaderPages(html);
    ASSERT_EQ(pages.size(), 2);
    EXPECT_EQ(pages[0].index, 0);
    EXPECT_TRUE(pages[0].imageUrl.startsWith("https://2.bp.blogspot.com/already-plain"));
    EXPECT_EQ(pages[1].index, 1);
    EXPECT_TRUE(pages[1].imageUrl.startsWith("https://2.bp.blogspot.com/second-plain"));
}

// Dead / obfuscation-changed page (no var/baeu markers) -> empty, never crashes.
TEST(ReadComicsPageParse, ParseReaderPagesFailSafeEmpty) {
    EXPECT_TRUE(parseReaderPages("<html>no reader here</html>").isEmpty());
}
