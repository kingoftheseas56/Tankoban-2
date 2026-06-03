#pragma once

#include "MangaUpdatesTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::mangaupdates {

class MangaUpdatesClient : public QObject
{
    Q_OBJECT
public:
    explicit MangaUpdatesClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaUpdatesClient() override;

    void searchByTitle(const QString& query, int requestId);
    void seriesById(qint64 seriesId, int requestId);

signals:
    void searchSucceeded(int requestId, const QList<tankoban::manga::mangaupdates::MangaUpdatesSearchHit>& hits);
    void searchFailed(int requestId, const QString& reason);

    void seriesSucceeded(int requestId, const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info);
    void seriesFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onSeriesReplyFinished();

private:
    // Non-blocking 1-req/sec throttle — see scheduleThrottled in the .cpp.
    // Defers each request to its next free slot via a single-shot QTimer
    // instead of sleeping the GUI thread (the old QThread::msleep froze the UI).
    void scheduleThrottled(std::function<void()> dispatch);

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_nextAllowedMs = 0;  // earliest ms-since-epoch the next request may dispatch
};

} // namespace tankoban::manga::mangaupdates
