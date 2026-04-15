#pragma once

#include "core/TorrentIndexer.h"
#include <QSet>

class QNetworkAccessManager;
class QNetworkReply;

class NyaaIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit NyaaIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "nyaa"; }
    QString displayName() const override { return "Nyaa"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

private:
    void fetchPage(const QString& query, const QString& categoryId, int page, int limit);
    void onPageFetched(QNetworkReply* reply, const QString& query, const QString& categoryId,
                       int page, int limit);
    QList<TorrentResult> parseHtml(const QString& html);
    qint64 parseSize(const QString& text);

    QNetworkAccessManager* m_nam;
    QList<TorrentResult> m_accumulated;
    QSet<QString> m_seenHashes;
};
