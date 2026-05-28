#pragma once

#include <QObject>
#include <QString>
#include <QList>

#include "core/book/BookCatalogueResult.h"

class QNetworkAccessManager;
class QNetworkReply;

// One row of FictionDB's A–Z series directory (/series/author-series~<letter>.htm).
// Used to build the local series index (BOOKS_FICTIONDB_CATALOGUE §4.2).
struct SeriesIndexEntry {
    QString seriesId;    // FictionDB series slug, e.g. "dune-chronicles-frank-herbert~3735"
    QString seriesName;  // "Dune Chronicles"
    QString author;      // derived from the slug tail
    QString genre;       // optional; "" if absent
};

// Scrapes FictionDB (fictiondb.com) — the fiction-only catalogue source that
// replaced Open Library + Google Books (BOOKS_FICTIONDB_CATALOGUE, 2026-05-27).
//
// FictionDB structure (verified live 2026-05-27):
//   - Book page  /title/<slug>~<author>~<id>.htm  — og: meta tags (title/isbn/
//     image/description) + "Published":"YYYY-..." + a series link
//     ("Dune Chronicles - 1") that self-declares series membership + position.
//   - Series page /series/<slug>~<author>~<id>.htm — books listed in reading
//     order (document order = position).
//   - Search      /search/searchresults.htm?srchtxt=<q>&styp=5 — a flat
//     schema.org/Book <table>; rows carry itemprop url/name/author/genre but
//     NO series grouping (series tiles come from the Phase-2 Top-N resolution
//     pass that fetches the top result book pages + groups by their series link).
//
// All requests use a Chrome-UA QNetworkRequest (FictionDB is Cloudflare-proxied
// but passive at moderate rate). Parse functions are static + pure so they
// unit-test against frozen fixtures without a live network.
class FictionDbClient : public QObject
{
    Q_OBJECT
public:
    explicit FictionDbClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // bookId/seriesId are FictionDB slugs, e.g. "dune~frank-herbert~99723".
    void search(const QString& query);
    void fetchBook(const QString& bookId);
    void fetchSeries(const QString& seriesId);

    // Pure parsers — exposed for unit tests + the Phase-2 aggregator.
    static BookCatalogueResult parseBookPage(const QString& html, const QString& bookId);
    static QList<BookCatalogueResult> parseSeriesPage(const QString& html, const QString& seriesId);
    static QList<BookCatalogueResult> parseSearchPage(const QString& html);

    // Helper: extract a FictionDB slug ("dune~frank-herbert~99723") from a
    // relative/absolute /title/ or /series/ href. Empty if not a book/series link.
    static QString slugFromHref(const QString& href, const QString& kind /* "title"|"series" */);

    // Series-index directory (BOOKS_FICTIONDB_CATALOGUE §4.1) — for building the
    // local series index. Pure parser + async fetch of one author-series page.
    static QList<SeriesIndexEntry> parseSeriesIndexPage(const QString& html, bool* hasNextPage);
    void fetchSeriesIndexPage(const QString& letter, int page);

signals:
    void searchResults(const QString& query, const QList<BookCatalogueResult>& books);
    void searchFailed(const QString& query, const QString& error);
    void bookReady(const QString& bookId, const BookCatalogueResult& book);
    void bookFailed(const QString& bookId, const QString& error);
    void seriesReady(const QString& seriesId, const QString& seriesName,
                     const QList<BookCatalogueResult>& books);
    void seriesFailed(const QString& seriesId, const QString& error);
    void seriesIndexPageReady(const QString& letter, int page,
                              const QList<SeriesIndexEntry>& entries, bool hasNextPage);
    void seriesIndexPageFailed(const QString& letter, int page, const QString& error);

private:
    void onSearchReply();
    void onBookReply();
    void onSeriesReply();
    void onSeriesIndexReply();

    QNetworkAccessManager* m_nam = nullptr;
    static constexpr const char* kBase = "https://www.fictiondb.com";
};
