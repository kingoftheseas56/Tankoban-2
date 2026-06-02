// src/core/manga/GetComicsParse.h
#pragma once

#include <QString>
#include <QList>

// Pure parse/match helpers for GetComics (getcomics.org). C++ port of
// scripts/comics_catalogue/getcomics_resolve.py (extract_downloads + pick_best)
// plus the search-result match scorer that file did NOT have. No network, no UI
// — unit-tested in tankoban_tests. The live HTTP lives in GetComicsResolver.
namespace tankoban::manga::getcomics {

struct DownloadLink {
    QString kind;   // "magnet" | "main_server" | "pixeldrain" | "mediafire" | "mega"
    QString url;
};

struct SearchResult {
    QString title;     // candidate post title
    QString postUrl;   // absolute getcomics.org post URL
};

// Every real download anchor on a post, as {kind,url}. Ad links + non-download
// anchors dropped (kept only if magnet: scheme OR contains "getcomics.org/dls/").
QList<DownloadLink> extractDownloads(const QString& postHtml);

// Best link by priority magnet > main_server > pixeldrain > mediafire > mega.
// Returns an empty (url.isEmpty()) DownloadLink if none.
DownloadLink pickBest(const QList<DownloadLink>& links);

// The post's per-edition cover from <meta property="og:image" ...>, or "".
QString parsePostCover(const QString& postHtml);

// Parse a getcomics.org/?s= results page into {title, postUrl}, first-seen order.
QList<SearchResult> parseSearchResults(const QString& searchHtml);

// Confidence score (>=0) for a candidate post title vs the wanted edition.
// Higher = better. 0 means no meaningful overlap.
int scoreMatch(const QString& editionTitle, int year,
               const QString& tierLabel, const QString& candidateTitle);

// The best result whose score clears the confidence floor, or an empty
// SearchResult (postUrl.isEmpty()) if none is confident enough. FAIL SAFE:
// when unsure, returns empty rather than a wrong post.
SearchResult pickBestMatch(const QString& editionTitle, int year,
                           const QString& tierLabel,
                           const QList<SearchResult>& results);

} // namespace tankoban::manga::getcomics
