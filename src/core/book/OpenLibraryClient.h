#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "BookCatalogueResult.h"

class QNetworkAccessManager;
class QNetworkReply;

// HTTP client + JSON parser for Open Library (https://openlibrary.org).
// Primary catalogue source for BOOKS_STREMIO_PIVOT — no API key, richer
// series metadata, author endpoint powers the "Other books by author" scroller.
//
// API endpoints (subset used by v1):
//   - GET /search.json?q=<query>             -> book search
//   - GET /authors/<OLnA>/works.json         -> author's works (for scroller)
//   - GET /works/<OLnW>.json                 -> work detail (synopsis if not on search)
//
// Parsers are static + pure (no network), tested against frozen fixtures.
// Network-fetching is a thin wrapper around QNetworkAccessManager.
class OpenLibraryClient : public QObject
{
    Q_OBJECT

public:
    explicit OpenLibraryClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // ── Pure parsers (frozen-fixture testable) ────────────────────────────
    static QList<BookCatalogueResult> parseSearchResponse(const QByteArray& json);
    static QList<BookCatalogueResult> parseAuthorWorksResponse(const QByteArray& json,
                                                               const QString& authorName);

    // Detail-page synopsis is not in the search response. fetchWorkDetail
    // pulls /works/<OLnW>.json and parses its description field; this is the
    // pure version that operates on the response body.
    static QString parseWorkDescription(const QByteArray& json);

    // ── Network methods (signal-based) ────────────────────────────────────
    void search(const QString& query);          // fires searchResults / searchFailed
    void fetchAuthorWorks(const QString& authorKey, const QString& authorName);
    void fetchWorkDetail(const QString& workKey);

signals:
    void searchResults(const QList<BookCatalogueResult>& results);
    void searchFailed(const QString& error);
    void authorWorksResults(const QString& authorKey,
                            const QList<BookCatalogueResult>& results);
    void authorWorksFailed(const QString& authorKey, const QString& error);
    void workDetailReady(const QString& workKey, const QString& description);
    void workDetailFailed(const QString& workKey, const QString& error);

private:
    void onSearchReply();
    void onAuthorWorksReply();
    void onWorkDetailReply();

    QNetworkAccessManager* m_nam;
};
