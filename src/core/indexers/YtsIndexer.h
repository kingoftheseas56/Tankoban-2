#pragma once

#include "core/TorrentIndexer.h"

class QNetworkAccessManager;
class QNetworkReply;

class YtsIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit YtsIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "yts"; }
    QString displayName() const override { return "YTS"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

private:
    void onReplyFinished(QNetworkReply* reply, int limit);

    QNetworkAccessManager* m_nam;
};
