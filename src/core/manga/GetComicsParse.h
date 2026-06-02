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

// COLLECTED-EDITION matching (2026-06-02, Agent 1). GetComics indexes these
// series as coarse collected editions (Compendium / Omnibus / Collection /
// Complete / Deluxe), NOT the granular per-TPB volumes the RCO catalogue lists
// — verified live: "Invincible" carries only "Invincible Compendium Vol. 1-3",
// never the 25 individual TPBs. So the download unit is the collected edition,
// matched STRICTLY on series identity (Hemanth: miss rather than grab wrong).

// The series-identity tokens of a title: lowercased alnum tokens with edition
// noise (the/a/of/and), tier words (vol/compendium/omnibus/collection/...),
// 4-digit year tokens, and pure-number tokens removed. Two titles name the same
// series iff these token SETS are equal.
QStringList identityTokens(const QString& title);

// True iff candidateTitle is a COLLECTED EDITION of exactly seriesTitle:
//  (1) identityTokens(candidate) == identityTokens(series) — set equality, no
//      extra or missing series words. Rejects a different series sharing a word
//      ("Invincible Iron Man", "Invincible Universe", "The Invincible Red Sonja")
//      AND a same-series sub-named edition ("Spawn Origins Collection" has the
//      extra word "origins" → missed, the accepted strict trade).
//  (2) candidate carries a collected-edition marker — a tier keyword
//      (Compendium/Omnibus/Collection/Complete/Deluxe/Book/TPB/HC) — rejecting
//      single issues ("Spawn #375"). A bare number range is NOT a marker: a
//      publication-year span like "(2009-2013)" would false-positive an issue,
//      and every real collected edition carries a tier word anyway.
bool isCollectedEditionOf(const QString& seriesTitle, const QString& candidateTitle);

// The single best collected edition of seriesTitle among results, or an empty
// SearchResult (postUrl.isEmpty()) if none qualifies OR the top is an ambiguous
// tie. FAIL SAFE: when unsure, returns empty rather than a wrong post. Ranks
// qualifiers by collected-edition tier (Compendium > Omnibus > ... > TPB) with a
// year-match tie-break, so the most canonical collected form wins.
SearchResult pickBestCollectedEdition(const QString& seriesTitle, int year,
                                      const QList<SearchResult>& results);

} // namespace tankoban::manga::getcomics
