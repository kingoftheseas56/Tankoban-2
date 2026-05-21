#pragma once

#include "BookScraper.h"
#include "core/TorrentResult.h"  // needed for QList<TorrentResult> slot signature

#include <QHash>
#include <QString>

class TorrentClient;
class TankorentSearchService;

// `BookScraper` impl that fans out queries to Tankorent's indexer stack filtered
// to the "Books" category. One `BookResult` per torrent hit, with format inferred
// from filename suffix (`.epub` / `.pdf` / `.mobi` / `.azw3` / `.djvu` / `.cbz`).
//
// Tankorent ownership transferred to Agent 4 on 2026-05-20 (Agent 4B departure);
// the API surface this scraper consumes is Agent 4's `TankorentSearchService`
// signed off in HELP.md resolution 2026-05-21 + shipped at b94e47f.
//
// Single-flight per scraper instance: starting a second search() while the first
// is in flight cancels the first via TankorentSearchService::cancelSearch(handle)
// and overwrites m_currentHandle. The picker UI in Phase 8 won't normally do
// this — but the scraper is honest about which handle it owns at any moment.
class TankorentBookScraper : public BookScraper
{
    Q_OBJECT
public:
    explicit TankorentBookScraper(TankorentSearchService* service = nullptr,
                                  QObject* parent = nullptr);

    QString sourceId()   const override { return QStringLiteral("tankorent"); }
    QString sourceName() const override { return QStringLiteral("Tankorent (torrents)"); }

    void search(const QString& query, int limit = 30) override;
    void fetchDetail(const QString& torrentId) override;
    void resolveDownload(const QString& torrentId) override;

private slots:
    void onServiceResultsReady(const QString& handle,
                               const QList<TorrentResult>& results);
    void onServiceIndexerError(const QString& handle,
                               const QString& indexerId,
                               const QString& error);
    void onServiceSearchFinished(const QString& handle);

private:
    // Cached row payload by magnetUri — populated during search() result mapping
    // so resolveDownload(magnetUri) can emit downloadResolved synchronously
    // without re-querying the service. Cleared on each new search().
    struct CachedRow {
        QString magnetUri;
        QString detailsUrl;
    };

    TankorentSearchService* m_service;
    QString                 m_currentHandle;
    int                     m_currentLimit = 30;
    QList<BookResult>       m_accumulator;
    QHash<QString, CachedRow> m_magnetByRowId;  // BookResult.sourceId -> row
};
