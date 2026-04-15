#pragma once

#include "core/TorrentIndexer.h"
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class EztvIndexer : public TorrentIndexer
{
    Q_OBJECT

public:
    explicit EztvIndexer(QNetworkAccessManager* nam, QObject* parent = nullptr);

    QString id() const override { return "eztv"; }
    QString displayName() const override { return "EZTV"; }
    void search(const QString& query, int limit = 30, const QString& categoryId = {}) override;

    IndexerHealth health() const override         { return m_health; }
    QDateTime     lastSuccess() const override    { return m_lastSuccess; }
    QString       lastError() const override      { return m_lastError; }
    qint64        lastResponseMs() const override { return m_lastResponseMs; }

    bool        requiresCredentials() const override { return true; }
    QStringList credentialKeys() const override      { return { QStringLiteral("cookie") }; }
    void        setCredential(const QString& key, const QString& value) override;
    QString     credential(const QString& key) const override;

private:
    void tryNextMirror();
    void onReplyFinished(QNetworkReply* reply);
    QList<TorrentResult> parseHtml(const QString& html, int limit);
    static QString normalizeSlug(const QString& query);
    static qint64 parseSize(const QString& text);

    QNetworkAccessManager* m_nam;
    QStringList m_mirrors;
    int m_mirrorIndex = 0;
    QString m_slug;
    int m_limit = 30;
};
