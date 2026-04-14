#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct CinemetaEntry {
    QString imdb;
    QString type;           // "movie" or "series"
    QString name;
    QString year;
    QString poster;         // URL
    QString description;
    QString imdbRating;
    QString genre;          // comma-separated, max 3
    QString runtime;
};

struct StreamEpisode {
    int episode = 0;
    QString title;
};

class CinemetaClient : public QObject
{
    Q_OBJECT

public:
    explicit CinemetaClient(QObject* parent = nullptr);

    void searchCatalog(const QString& query);
    void fetchSeriesMeta(const QString& imdbId);

signals:
    void catalogResults(const QList<CinemetaEntry>& results);
    void catalogError(const QString& message);
    void seriesMetaReady(const QString& imdbId,
                         const QMap<int, QList<StreamEpisode>>& seasons);
    void seriesMetaError(const QString& imdbId, const QString& message);

private:
    void fetchCatalogType(const QString& query, const QString& mediaType);
    void onCatalogReply(QNetworkReply* reply, const QString& mediaType);
    void onSeriesMetaReply(QNetworkReply* reply, const QString& imdbId);

    QNetworkAccessManager* m_nam;

    int m_pendingCatalogRequests = 0;
    QList<CinemetaEntry> m_catalogAccumulator;

    // imdbId → (timestampMs, seasons)
    QHash<QString, QPair<qint64, QMap<int, QList<StreamEpisode>>>> m_metaCache;
    static constexpr qint64 META_CACHE_TTL_MS = 24LL * 60 * 60 * 1000;
};
