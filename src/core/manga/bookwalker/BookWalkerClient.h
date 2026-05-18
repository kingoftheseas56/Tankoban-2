#pragma once

#include "BookWalkerTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::bookwalker {

// Async HTTP client for bookwalker.jp series-search and series-page parsing.
// Pattern mirrors MangaUpdatesClient: caller-allocated NAM, requestId correlation,
// signal/slot completion, soft throttling.
class BookWalkerClient : public QObject
{
    Q_OBJECT
public:
    explicit BookWalkerClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~BookWalkerClient() override;

    // GET bookwalker.jp/search/?word=<japaneseTitle>, extract all search hits.
    void searchSeries(const QString& japaneseTitle, int requestId);

    // GET bookwalker.jp/series/<seriesId>/list/, extract ordered cover URLs.
    void fetchSeriesCovers(const QString& bookwalkerSeriesId, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void searchFailed(int requestId, const QString& reason);

    void coversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void coversFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onCoversReplyFinished();

private:
    void throttleIfNeeded();

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_lastRequestMs = 0;
};

} // namespace tankoban::manga::bookwalker
