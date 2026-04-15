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

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

    // Manual-paste fallback for when the harvester fails repeatedly — user
    // supplies a cf_clearance value via the Sources panel. Stored at the
    // same QSettings path the harvester writes to, so reader-side logic
    // treats auto-harvested and manually-pasted cookies uniformly.
    bool        requiresCredentials() const override { return true; }
    QStringList credentialKeys() const override      { return { QStringLiteral("cf_clearance") }; }
    void        setCredential(const QString& key, const QString& value) override;
    QString     credential(const QString& key) const override;

private slots:
    void onCookieHarvested(const QString& indexerId,
                           const QString& cfClearance,
                           const QString& userAgent);
    void onHarvestFailed(const QString& indexerId, const QString& reason);

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

    void performSearch(const QString& query, int limit, const QString& categoryId);
    bool haveValidClearance() const;
    void invalidateClearance();
    void kickOffHarvest();

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

    // Pending-search state — used when the harvester has to run before
    // we can issue the real HTTP request.
    QString m_pendingQuery;
    QString m_pendingCategoryId;
    int     m_pendingLimit = 0;
    bool    m_pendingSearch = false;
    bool    m_retryAttempted = false;  // guards 503 retry-once
};
