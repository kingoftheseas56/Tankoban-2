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

private:
    void onReplyFinished(QNetworkReply* reply, int limit);

    QNetworkAccessManager* m_nam;
};
