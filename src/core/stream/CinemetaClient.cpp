#include "CinemetaClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

static const QString CINEMETA_BASE = QStringLiteral("https://v3-cinemeta.strem.io");
static constexpr int REQUEST_TIMEOUT_MS = 8000;
static constexpr int MAX_RESULTS_PER_TYPE = 20;

CinemetaClient::CinemetaClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void CinemetaClient::searchCatalog(const QString& query)
{
    QString q = query.trimmed();
    if (q.isEmpty()) {
        emit catalogResults({});
        return;
    }

    m_catalogAccumulator.clear();
    m_pendingCatalogRequests = 2;

    fetchCatalogType(q, QStringLiteral("movie"));
    fetchCatalogType(q, QStringLiteral("series"));
}

void CinemetaClient::fetchCatalogType(const QString& query, const QString& mediaType)
{
    QString encoded = QUrl::toPercentEncoding(query);
    QUrl url(CINEMETA_BASE + "/catalog/" + mediaType + "/top/search=" + encoded + ".json");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"));
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(REQUEST_TIMEOUT_MS);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, mediaType]() {
        onCatalogReply(reply, mediaType);
    });
}

void CinemetaClient::onCatalogReply(QNetworkReply* reply, const QString& mediaType)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Partial failure is OK — continue with whatever the other type returns
        --m_pendingCatalogRequests;
        if (m_pendingCatalogRequests <= 0) {
            if (m_catalogAccumulator.isEmpty())
                emit catalogError(reply->errorString());
            else
                emit catalogResults(m_catalogAccumulator);
        }
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        --m_pendingCatalogRequests;
        if (m_pendingCatalogRequests <= 0) {
            if (m_catalogAccumulator.isEmpty())
                emit catalogError(QStringLiteral("JSON parse error: ") + err.errorString());
            else
                emit catalogResults(m_catalogAccumulator);
        }
        return;
    }

    QJsonArray metas = doc.object().value("metas").toArray();
    int count = 0;

    for (const auto& val : metas) {
        if (count >= MAX_RESULTS_PER_TYPE)
            break;

        QJsonObject obj = val.toObject();

        // IMDB ID: try "imdb_id", fallback to "id"
        QString imdb = obj.value("imdb_id").toString().trimmed();
        if (imdb.isEmpty())
            imdb = obj.value("id").toString().trimmed();
        if (!imdb.startsWith("tt"))
            continue;

        CinemetaEntry entry;
        entry.imdb = imdb;
        entry.type = mediaType;
        entry.name = obj.value("name").toString().trimmed();
        if (entry.name.isEmpty())
            continue;

        // Year: try "releaseInfo", fallback to "year"
        entry.year = obj.value("releaseInfo").toString().trimmed();
        if (entry.year.isEmpty())
            entry.year = obj.value("year").toString().trimmed();

        entry.poster = obj.value("poster").toString().trimmed();
        entry.description = obj.value("description").toString().trimmed();
        entry.imdbRating = obj.value("imdbRating").toString().trimmed();
        entry.runtime = obj.value("runtime").toString().trimmed();

        // Genre: array → take first 3, join with ", "
        QJsonArray genreArr = obj.value("genre").toArray();
        QStringList genres;
        for (int i = 0; i < qMin(3, (int)genreArr.size()); ++i)
            genres.append(genreArr[i].toString().trimmed());
        entry.genre = genres.join(", ");

        m_catalogAccumulator.append(entry);
        ++count;
    }

    --m_pendingCatalogRequests;
    if (m_pendingCatalogRequests <= 0)
        emit catalogResults(m_catalogAccumulator);
}

void CinemetaClient::fetchSeriesMeta(const QString& imdbId)
{
    if (!imdbId.startsWith("tt")) {
        emit seriesMetaError(imdbId, QStringLiteral("Invalid IMDB ID"));
        return;
    }

    // Check cache
    auto it = m_metaCache.find(imdbId);
    if (it != m_metaCache.end()) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - it->first < META_CACHE_TTL_MS) {
            emit seriesMetaReady(imdbId, it->second);
            return;
        }
        m_metaCache.erase(it);
    }

    QUrl url(CINEMETA_BASE + "/meta/series/" + imdbId + ".json");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"));
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(REQUEST_TIMEOUT_MS);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
        onSeriesMetaReply(reply, imdbId);
    });
}

void CinemetaClient::onSeriesMetaReply(QNetworkReply* reply, const QString& imdbId)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit seriesMetaError(imdbId, reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit seriesMetaError(imdbId, QStringLiteral("JSON parse error: ") + err.errorString());
        return;
    }

    QJsonArray videos = doc.object().value("meta").toObject().value("videos").toArray();
    QMap<int, QList<StreamEpisode>> seasons;

    for (const auto& val : videos) {
        QJsonObject obj = val.toObject();

        int season = obj.value("season").toInt(-1);
        int episode = obj.value("episode").toInt(0);
        if (season < 0 || episode < 1)
            continue;

        // Title: try "name", fallback to "title"
        QString title = obj.value("name").toString().trimmed();
        if (title.isEmpty())
            title = obj.value("title").toString().trimmed();

        StreamEpisode ep;
        ep.episode = episode;
        ep.title = title;
        seasons[season].append(ep);
    }

    // Sort episodes within each season
    for (auto it = seasons.begin(); it != seasons.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const StreamEpisode& a, const StreamEpisode& b) {
            return a.episode < b.episode;
        });
    }

    // Drop season 0 (specials) unless it's the only season
    if (seasons.size() > 1)
        seasons.remove(0);

    // Cache
    m_metaCache[imdbId] = qMakePair(QDateTime::currentMSecsSinceEpoch(), seasons);

    emit seriesMetaReady(imdbId, seasons);
}
