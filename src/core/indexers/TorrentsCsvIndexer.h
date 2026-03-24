#pragma once

#include "core/TorrentIndexer.h"

class QNetworkAccessManager;
class QNetworkReply;

class TorrentsCsvIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit TorrentsCsvIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "torrentscsv"; }
    QString displayName() const override { return "Torrents-CSV"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam;
};
