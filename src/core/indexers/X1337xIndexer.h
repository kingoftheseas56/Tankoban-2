#pragma once

#include "core/TorrentIndexer.h"

class QNetworkAccessManager;
class QNetworkReply;

class X1337xIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit X1337xIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "1337x"; }
    QString displayName() const override { return "1337x"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

private:
    struct ListRow {
        QString title;
        QString detailPath;
        QString magnetUri;
        qint64  sizeBytes = 0;
        int     seeders = 0;
        int     leechers = 0;
        QString categoryId;
    };

    void onListPageFetched(QNetworkReply* reply, int limit);
    QList<ListRow> parseListPage(const QString& html);
    void fetchDetailPages(const QList<ListRow>& rows, int limit);
    void onDetailFetched(QNetworkReply* reply, int index);
    void checkComplete();
    static qint64 parseSize(const QString& text);

    QNetworkAccessManager* m_nam;
    QList<ListRow> m_rows;
    int m_pendingDetails = 0;
    int m_limit = 30;
};
