#pragma once

#include "core/TorrentIndexer.h"

class QNetworkAccessManager;
class QNetworkReply;

class PirateBayIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit PirateBayIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "piratebay"; }
    QString displayName() const override { return "PirateBay"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

private:
    void onReplyFinished(QNetworkReply* reply);
    static QString categoryLabel(const QString& catId);

    QNetworkAccessManager* m_nam;
};
