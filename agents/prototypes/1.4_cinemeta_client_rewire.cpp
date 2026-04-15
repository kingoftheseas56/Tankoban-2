// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.4 (Addon Protocol Foundation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:82
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CinemetaClient.h:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CinemetaClient.cpp:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamSearchWidget.cpp:42
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.cpp:58
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.2_addon_transport.cpp:99
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/1.3_addon_registry.cpp:105
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/request.rs:64
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 1.4.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Shared addon types (minimal prototype slice)
// -----------------------------------------------------------------

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct AddonManifest {
    QString id;
    QString name;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr) : QObject(parent) {}
    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(AddonTransport* transport, QObject* parent = nullptr)
        : QObject(parent)
    {
        Q_UNUSED(transport);
    }
    QList<AddonDescriptor> list() const;
};

} // namespace tankostream::addon

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ResourceRequest;

// -----------------------------------------------------------------
// Existing public data contract retained (unchanged).
// -----------------------------------------------------------------

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
    // Public signature unchanged from current src/.
    explicit CinemetaClient(QObject* parent = nullptr)
        : QObject(parent)
        , m_transport(new AddonTransport(this))
        , m_registry(new AddonRegistry(m_transport, this))
    {
    }

    // Public signature unchanged.
    void searchCatalog(const QString& query)
    {
        const QString q = query.trimmed();
        if (q.isEmpty()) {
            emit catalogResults({});
            return;
        }

        const QUrl cinemetaUrl = findCinemetaTransportUrl();
        if (!cinemetaUrl.isValid()) {
            emit catalogError("Cinemeta addon is not installed or disabled");
            return;
        }

        m_catalogAccumulator.clear();
        m_pendingCatalogRequests = 2;
        m_lastCatalogError.clear();

        fetchCatalogType(cinemetaUrl, q, "movie");
        fetchCatalogType(cinemetaUrl, q, "series");
    }

    // Public signature unchanged.
    void fetchSeriesMeta(const QString& imdbId)
    {
        if (!imdbId.startsWith("tt")) {
            emit seriesMetaError(imdbId, "Invalid IMDB ID");
            return;
        }

        // Existing cache behavior retained.
        auto cacheIt = m_metaCache.find(imdbId);
        if (cacheIt != m_metaCache.end()) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs - cacheIt->first < META_CACHE_TTL_MS) {
                emit seriesMetaReady(imdbId, cacheIt->second);
                return;
            }
            m_metaCache.erase(cacheIt);
        }

        const QUrl cinemetaUrl = findCinemetaTransportUrl();
        if (!cinemetaUrl.isValid()) {
            emit seriesMetaError(imdbId, "Cinemeta addon is not installed or disabled");
            return;
        }

        ResourceRequest request;
        request.resource = "meta";
        request.type = "series";
        request.id = imdbId;
        request.extra = {};

        // One-shot request routing through AddonTransport.
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *readyConn = connect(m_transport, &AddonTransport::resourceReady, this,
            [this, request, imdbId, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);

                QMap<int, QList<StreamEpisode>> seasons;
                if (!parseSeriesPayload(payload, seasons)) {
                    emit seriesMetaError(imdbId, "Invalid Cinemeta meta payload");
                    return;
                }

                // Keep current behavior: drop specials unless they are the only season.
                if (seasons.size() > 1)
                    seasons.remove(0);

                m_metaCache[imdbId] = qMakePair(QDateTime::currentMSecsSinceEpoch(), seasons);
                emit seriesMetaReady(imdbId, seasons);
            });

        *failConn = connect(m_transport, &AddonTransport::resourceFailed, this,
            [this, request, imdbId, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QString& message) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                emit seriesMetaError(imdbId, message);
            });

        m_transport->fetchResource(cinemetaUrl, request);
    }

signals:
    void catalogResults(const QList<CinemetaEntry>& results);
    void catalogError(const QString& message);
    void seriesMetaReady(const QString& imdbId,
                         const QMap<int, QList<StreamEpisode>>& seasons);
    void seriesMetaError(const QString& imdbId, const QString& message);

private:
    static constexpr qint64 META_CACHE_TTL_MS = 24LL * 60 * 60 * 1000;
    static constexpr int MAX_RESULTS_PER_TYPE = 20;

    static bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
    {
        return a.resource == b.resource
            && a.type == b.type
            && a.id == b.id
            && a.extra == b.extra;
    }

    QUrl findCinemetaTransportUrl() const
    {
        // No hardcoded Cinemeta URL in this client. Registry is source of truth.
        const QList<AddonDescriptor> addons = m_registry->list();
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled)
                continue;
            if (addon.manifest.id == "com.linvo.cinemeta")
                return addon.transportUrl;
        }

        // Fallback for custom manifests carrying same name but different id.
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled)
                continue;
            if (addon.manifest.name.compare("Cinemeta", Qt::CaseInsensitive) == 0)
                return addon.transportUrl;
        }
        return {};
    }

    void fetchCatalogType(const QUrl& base, const QString& query, const QString& mediaType)
    {
        ResourceRequest request;
        request.resource = "catalog";
        request.type = mediaType;
        request.id = "top";
        request.extra = { qMakePair(QString("search"), query) };

        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *readyConn = connect(m_transport, &AddonTransport::resourceReady, this,
            [this, request, mediaType, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);

                QList<CinemetaEntry> parsed;
                if (!parseCatalogPayload(payload, mediaType, parsed))
                    m_lastCatalogError = "Invalid Cinemeta catalog payload";
                else
                    m_catalogAccumulator.append(parsed);

                finalizeCatalogRequest();
            });

        *failConn = connect(m_transport, &AddonTransport::resourceFailed, this,
            [this, request, handled, readyConn, failConn](
                const ResourceRequest& incoming,
                const QString& message) {
                if (*handled || !sameRequest(request, incoming))
                    return;
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);

                m_lastCatalogError = message;
                finalizeCatalogRequest();
            });

        m_transport->fetchResource(base, request);
    }

    void finalizeCatalogRequest()
    {
        --m_pendingCatalogRequests;
        if (m_pendingCatalogRequests > 0)
            return;

        if (m_catalogAccumulator.isEmpty())
            emit catalogError(m_lastCatalogError.isEmpty() ? "No results" : m_lastCatalogError);
        else
            emit catalogResults(m_catalogAccumulator);
    }

    static bool parseCatalogPayload(
        const QJsonObject& payload,
        const QString& mediaType,
        QList<CinemetaEntry>& out)
    {
        const QJsonArray metas = payload.value("metas").toArray();
        out.clear();
        int count = 0;

        for (const QJsonValue& value : metas) {
            if (count >= MAX_RESULTS_PER_TYPE)
                break;

            const QJsonObject obj = value.toObject();
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

            entry.year = obj.value("releaseInfo").toString().trimmed();
            if (entry.year.isEmpty())
                entry.year = obj.value("year").toString().trimmed();

            entry.poster = obj.value("poster").toString().trimmed();
            entry.description = obj.value("description").toString().trimmed();
            entry.imdbRating = obj.value("imdbRating").toString().trimmed();
            entry.runtime = obj.value("runtime").toString().trimmed();

            QStringList genres;
            const QJsonArray genreArray = obj.value("genre").toArray();
            for (int i = 0; i < std::min(3, genreArray.size()); ++i)
                genres.push_back(genreArray[i].toString().trimmed());
            entry.genre = genres.join(", ");

            out.push_back(entry);
            ++count;
        }
        return true;
    }

    static bool parseSeriesPayload(
        const QJsonObject& payload,
        QMap<int, QList<StreamEpisode>>& seasonsOut)
    {
        seasonsOut.clear();
        const QJsonArray videos = payload.value("meta").toObject().value("videos").toArray();
        for (const QJsonValue& value : videos) {
            const QJsonObject obj = value.toObject();
            const int season = obj.value("season").toInt(-1);
            const int episode = obj.value("episode").toInt(0);
            if (season < 0 || episode < 1)
                continue;

            StreamEpisode ep;
            ep.episode = episode;
            ep.title = obj.value("name").toString().trimmed();
            if (ep.title.isEmpty())
                ep.title = obj.value("title").toString().trimmed();

            seasonsOut[season].append(ep);
        }

        for (auto it = seasonsOut.begin(); it != seasonsOut.end(); ++it) {
            std::sort(it->begin(), it->end(), [](const StreamEpisode& a, const StreamEpisode& b) {
                return a.episode < b.episode;
            });
        }

        return true;
    }

    AddonTransport* m_transport = nullptr;
    AddonRegistry* m_registry = nullptr;

    int m_pendingCatalogRequests = 0;
    QString m_lastCatalogError;
    QList<CinemetaEntry> m_catalogAccumulator;

    // imdbId -> (timestampMs, parsed seasons)
    QHash<QString, QPair<qint64, QMap<int, QList<StreamEpisode>>>> m_metaCache;
};
