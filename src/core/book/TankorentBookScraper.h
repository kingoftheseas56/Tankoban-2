#pragma once

#include "BookScraper.h"

class TorrentClient;

// Forward-decl per Agent 4's HELP resolution 2026-05-21: `TankorentSearchService`
// is the headless dispatch surface Agent 4 will extract from
// `TankorentPage::dispatchIndexers()` as a Phase 5 follow-on commit (~150 LOC
// move + signal rewire). Until then, this class is registered + compiled but
// its `search()` path emits an empty result with a warning log. When the real
// service lands, the m_service injection point flips from nullptr to a live
// instance via TankoLibraryPage ctor wiring.
class TankorentSearchService;

// `BookScraper` impl that fans out queries to Tankorent's indexer stack filtered
// to the "Books" category. One `BookResult` per torrent hit, with format inferred
// from filename suffix (`.epub` / `.pdf` / `.mobi`).
//
// Tankorent ownership transferred to Agent 4 on 2026-05-20 (Agent 4B departure);
// the API surface this scraper consumes is Agent 4's `TankorentSearchService`
// signed off in HELP.md resolution 2026-05-21.
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

private:
    TankorentSearchService* m_service;
};
