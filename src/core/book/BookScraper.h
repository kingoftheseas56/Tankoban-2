#pragma once

#include "BookResult.h"

#include <QObject>
#include <QString>
#include <QList>

class QNetworkAccessManager;

// Shape-parallel to MangaScraper. Differences from manga-land:
//  - no fetchChapters / fetchPages (books are single-file)
//  - fetchDetail(md5OrId) returns a richer single BookResult via detailReady()
//  - resolveDownload(md5OrId) resolves one or more direct HTTP URLs via
//    downloadResolved() (plural — sources typically surface multiple mirrors;
//    BookDownloader does HEAD-probe failover across them). The download
//    itself is BookDownloader's job, not the scraper's.
//
// Some sources (Anna's Archive) require a JS-executing fetcher (QWebEngineView)
// rather than plain QNetworkRequest. Implementations may ignore the nam arg
// and roll their own fetch path; the base class keeps it as an optional
// convenience for implementations that don't need browser execution.
class BookScraper : public QObject
{
    Q_OBJECT

public:
    explicit BookScraper(QNetworkAccessManager* nam, QObject* parent = nullptr)
        : QObject(parent), m_nam(nam) {}

    virtual QString sourceId() const = 0;     // "annas-archive" | "libgen"
    virtual QString sourceName() const = 0;   // "Anna's Archive" | "LibGen"

    // Kick off an async search. Results arrive via searchFinished() or
    // errorOccurred(). limit is advisory — implementations may return fewer
    // or slightly more depending on source pagination.
    virtual void search(const QString& query, int limit = 30) = 0;

    // M2 stubs — scaffolded in M1 with TODO_M2 bodies so the interface compiles.
    virtual void fetchDetail(const QString& md5OrId) = 0;
    virtual void resolveDownload(const QString& md5OrId) = 0;

signals:
    void searchFinished(const QList<BookResult>& results);
    void detailReady(const BookResult& detail);
    // M2.3 — hoisted from AnnaArchiveScraper + LibGenScraper concrete classes
    // once both converged on the same shape (QStringList for mirror failover
    // + distinct failure signal so UI can differentiate timeout vs no-links).
    void downloadResolved(const QString& md5, const QStringList& mirrorUrls);
    void downloadFailed(const QString& md5, const QString& reason);
    void errorOccurred(const QString& message);

protected:
    QNetworkAccessManager* m_nam;
};
