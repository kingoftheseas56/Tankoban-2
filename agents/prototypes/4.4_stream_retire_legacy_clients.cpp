// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 4.4 (Multi-Source Stream Aggregation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:208
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:209
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:210
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:211
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:212
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:213
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:214
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.h:13
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.h:65
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:4
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:37
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:294
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.h:23
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamDetailView.cpp:58
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamSearchWidget.h:20
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/stream/StreamSearchWidget.cpp:42
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:40
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:51
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/StreamInfo.h:30
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/CMakeLists.txt:103
//   - C:/Users/Suprabha/Desktop/Tankoban 2/CMakeLists.txt:104
//   - C:/Users/Suprabha/Desktop/Tankoban 2/CMakeLists.txt:197
//   - C:/Users/Suprabha/Desktop/Tankoban 2/CMakeLists.txt:198
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.1_stream_aggregator.cpp:655
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.2_stream_picker_dialog.cpp:187
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/prototypes/4.3_stream_engine_direct_sources.cpp:348
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 4.4.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <optional>

namespace tankostream::addon {

struct ManifestResource {
    QString name;
    QStringList types;
    bool hasTypes = false;
};

struct AddonManifest {
    QString id;
    QString name;
    QStringList types;
    QList<ManifestResource> resources;
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

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;
    QUrl url;
    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;
    QString youtubeId;
};

struct StreamBehaviorHints {
    QString bingeGroup;
    QString filename;
    qint64 videoSize = 0;
    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;
    QVariantMap other;
};

struct Stream {
    StreamSource source;
    QString name;
    QString description;
    QUrl thumbnail;
    StreamBehaviorHints behaviorHints;
};

struct SeriesInfo {
    int season = 0;
    int episode = 0;
};

struct Video {
    QString id;
    QString title;
    std::optional<SeriesInfo> seriesInfo;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;
    QUrl poster;
    QString description;
    QString releaseInfo;
    QString imdbRating;
};

struct MetaItem {
    MetaItemPreview preview;
    QList<Video> videos;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    QList<AddonDescriptor> list() const;
    QList<AddonDescriptor> findByResourceType(const QString& resource,
                                              const QString& type) const;
};

class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);
};

} // namespace tankostream::addon

namespace tankostream::stream {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::MetaItemPreview;
using tankostream::addon::ResourceRequest;
using tankostream::addon::Stream;

// Keep the existing StreamDetailView table model unchanged in 4.4
// by preserving StreamEpisode shape while replacing data source.
struct StreamEpisode {
    int episode = 0;
    QString title;
};

namespace {

bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

bool hasResourceType(const AddonDescriptor& addon,
                     const QString& resource,
                     const QString& type)
{
    if (!addon.flags.enabled) {
        return false;
    }
    if (resource.compare(QStringLiteral("catalog"), Qt::CaseInsensitive) == 0) {
        // Catalog coverage is not represented in this trimmed prototype AddonManifest.
        // For this sketch, assume movie/series catalogs exist when type is listed.
        return addon.manifest.types.contains(type, Qt::CaseInsensitive);
    }

    for (const auto& r : addon.manifest.resources) {
        if (r.name.compare(resource, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!r.hasTypes) {
            return addon.manifest.types.contains(type, Qt::CaseInsensitive);
        }
        return r.types.contains(type, Qt::CaseInsensitive);
    }
    return false;
}

MetaItemPreview parseMetaPreview(const QJsonObject& obj, const QString& typeHint)
{
    MetaItemPreview out;
    out.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (out.id.isEmpty()) {
        out.id = obj.value(QStringLiteral("imdb_id")).toString().trimmed();
    }
    out.type = obj.value(QStringLiteral("type")).toString().trimmed();
    if (out.type.isEmpty()) {
        out.type = typeHint;
    }
    out.name = obj.value(QStringLiteral("name")).toString().trimmed();
    out.poster = QUrl(obj.value(QStringLiteral("poster")).toString().trimmed());
    out.description = obj.value(QStringLiteral("description")).toString().trimmed();
    out.releaseInfo = obj.value(QStringLiteral("releaseInfo")).toString().trimmed();
    out.imdbRating = obj.value(QStringLiteral("imdbRating")).toString().trimmed();
    return out;
}

QMap<int, QList<StreamEpisode>> parseSeriesEpisodes(const QJsonObject& payload)
{
    QMap<int, QList<StreamEpisode>> out;
    const QJsonArray videos =
        payload.value(QStringLiteral("meta")).toObject()
               .value(QStringLiteral("videos")).toArray();

    for (const QJsonValue& value : videos) {
        const QJsonObject videoObj = value.toObject();
        const int season = videoObj.value(QStringLiteral("season")).toInt(-1);
        const int episode = videoObj.value(QStringLiteral("episode")).toInt(0);
        if (season < 0 || episode < 1) {
            continue;
        }

        StreamEpisode item;
        item.episode = episode;
        item.title = videoObj.value(QStringLiteral("name")).toString().trimmed();
        if (item.title.isEmpty()) {
            item.title = videoObj.value(QStringLiteral("title")).toString().trimmed();
        }
        out[season].push_back(item);
    }

    for (auto it = out.begin(); it != out.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const StreamEpisode& a, const StreamEpisode& b) {
            return a.episode < b.episode;
        });
    }
    if (out.size() > 1) {
        out.remove(0);
    }
    return out;
}

} // namespace

class MetaAggregator : public QObject {
    Q_OBJECT

public:
    explicit MetaAggregator(AddonRegistry* registry, QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
    {
    }

    void searchCatalog(const QString& query)
    {
        resetSearch();
        const QString q = query.trimmed();
        if (q.isEmpty() || !m_registry) {
            emit catalogResults({});
            return;
        }

        const QList<AddonDescriptor> addons = m_registry->list();
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled) {
                continue;
            }
            for (const QString& type : {QStringLiteral("movie"), QStringLiteral("series")}) {
                if (!hasResourceType(addon, QStringLiteral("catalog"), type)) {
                    continue;
                }

                PendingSearch req;
                req.addonId = addon.manifest.id;
                req.baseUrl = addon.transportUrl;
                req.query = q;
                req.type = type;
                m_searchQueue.push_back(req);
            }
        }

        if (m_searchQueue.isEmpty()) {
            emit catalogResults({});
            return;
        }

        m_pendingSearch = 0;
        for (const PendingSearch& request : m_searchQueue) {
            dispatchSearch(request);
        }
    }

    void fetchSeriesMeta(const QString& imdbId)
    {
        if (!m_registry || !imdbId.startsWith(QStringLiteral("tt"))) {
            emit seriesMetaError(imdbId, QStringLiteral("Invalid IMDB ID"));
            return;
        }

        auto cacheIt = m_seriesCache.find(imdbId);
        if (cacheIt != m_seriesCache.end()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - cacheIt->first < kSeriesCacheTtlMs) {
                emit seriesMetaReady(imdbId, cacheIt->second);
                return;
            }
            m_seriesCache.erase(cacheIt);
        }

        // Keep behavior deterministic: prefer Cinemeta addon when available,
        // then fall back to any meta-capable addon.
        QList<AddonDescriptor> candidates;
        for (const AddonDescriptor& addon : m_registry->list()) {
            if (hasResourceType(addon, QStringLiteral("meta"), QStringLiteral("series"))) {
                candidates.push_back(addon);
            }
        }
        if (candidates.isEmpty()) {
            emit seriesMetaError(imdbId, QStringLiteral("No enabled addon supports series meta"));
            return;
        }

        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const AddonDescriptor& a, const AddonDescriptor& b) {
                             const bool aCinemeta = a.manifest.id == QStringLiteral("com.linvo.cinemeta");
                             const bool bCinemeta = b.manifest.id == QStringLiteral("com.linvo.cinemeta");
                             return aCinemeta && !bCinemeta;
                         });

        m_seriesPendingImdb = imdbId;
        m_seriesRemaining = candidates.size();
        m_seriesResolved = false;

        for (const AddonDescriptor& addon : candidates) {
            dispatchSeriesMeta(addon, imdbId);
        }
    }

signals:
    void catalogResults(const QList<MetaItemPreview>& results);
    void catalogError(const QString& message);
    void seriesMetaReady(const QString& imdbId,
                         const QMap<int, QList<StreamEpisode>>& seasons);
    void seriesMetaError(const QString& imdbId, const QString& message);

private:
    struct PendingSearch {
        QString addonId;
        QUrl baseUrl;
        QString query;
        QString type;
    };

    void dispatchSearch(const PendingSearch& pending)
    {
        ResourceRequest request;
        request.resource = QStringLiteral("catalog");
        request.type = pending.type;
        request.id = QStringLiteral("top");
        request.extra = { qMakePair(QStringLiteral("search"), pending.query) };

        auto* worker = new AddonTransport(this);
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        ++m_pendingSearch;

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [this, request, type = pending.type, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming)) {
                    return;
                }
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                worker->deleteLater();

                const QJsonArray metas = payload.value(QStringLiteral("metas")).toArray();
                for (const QJsonValue& value : metas) {
                    const MetaItemPreview item = parseMetaPreview(value.toObject(), type);
                    if (item.id.isEmpty() || item.name.isEmpty()) {
                        continue;
                    }
                    if (m_seenSearchIds.contains(item.id)) {
                        continue;
                    }
                    m_seenSearchIds.insert(item.id);
                    m_searchResults.push_back(item);
                }
                finalizeSearch();
            });

        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
            [this, request, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QString& message) {
                if (*handled || !sameRequest(request, incoming)) {
                    return;
                }
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                worker->deleteLater();
                if (m_lastSearchError.isEmpty()) {
                    m_lastSearchError = message;
                }
                finalizeSearch();
            });

        worker->fetchResource(pending.baseUrl, request);
    }

    void finalizeSearch()
    {
        --m_pendingSearch;
        if (m_pendingSearch > 0) {
            return;
        }

        if (!m_searchResults.isEmpty()) {
            emit catalogResults(m_searchResults);
            return;
        }
        emit catalogError(m_lastSearchError.isEmpty()
                              ? QStringLiteral("No results")
                              : m_lastSearchError);
    }

    void resetSearch()
    {
        m_searchQueue.clear();
        m_searchResults.clear();
        m_seenSearchIds.clear();
        m_lastSearchError.clear();
        m_pendingSearch = 0;
    }

    void dispatchSeriesMeta(const AddonDescriptor& addon, const QString& imdbId)
    {
        ResourceRequest request;
        request.resource = QStringLiteral("meta");
        request.type = QStringLiteral("series");
        request.id = imdbId;

        auto* worker = new AddonTransport(this);
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [this, addonId = addon.manifest.id, request, imdbId, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming)) {
                    return;
                }
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                worker->deleteLater();

                const QMap<int, QList<StreamEpisode>> seasons = parseSeriesEpisodes(payload);
                if (!m_seriesResolved && !seasons.isEmpty()) {
                    m_seriesResolved = true;
                    m_seriesCache[imdbId] =
                        qMakePair(QDateTime::currentMSecsSinceEpoch(), seasons);
                    emit seriesMetaReady(imdbId, seasons);
                }
                finalizeSeries(addonId, QString());
            });

        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
            [this, addonId = addon.manifest.id, request, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QString& message) {
                if (*handled || !sameRequest(request, incoming)) {
                    return;
                }
                *handled = true;
                disconnect(*readyConn);
                disconnect(*failConn);
                worker->deleteLater();
                finalizeSeries(addonId, message);
            });

        worker->fetchResource(addon.transportUrl, request);
    }

    void finalizeSeries(const QString& addonId, const QString& error)
    {
        Q_UNUSED(addonId);
        if (!error.isEmpty() && m_lastSeriesError.isEmpty()) {
            m_lastSeriesError = error;
        }

        --m_seriesRemaining;
        if (m_seriesRemaining > 0 || m_seriesResolved) {
            return;
        }

        emit seriesMetaError(
            m_seriesPendingImdb,
            m_lastSeriesError.isEmpty() ? QStringLiteral("No episodes found")
                                        : m_lastSeriesError);
        m_lastSeriesError.clear();
        m_seriesPendingImdb.clear();
    }

    AddonRegistry* m_registry = nullptr;

    QList<PendingSearch> m_searchQueue;
    QList<MetaItemPreview> m_searchResults;
    QSet<QString> m_seenSearchIds;
    QString m_lastSearchError;
    int m_pendingSearch = 0;

    QString m_seriesPendingImdb;
    int m_seriesRemaining = 0;
    bool m_seriesResolved = false;
    QString m_lastSeriesError;
    QHash<QString, QPair<qint64, QMap<int, QList<StreamEpisode>>>> m_seriesCache;
    static constexpr qint64 kSeriesCacheTtlMs = 24LL * 60LL * 60LL * 1000LL;
};

// -----------------------------------------------------------------
// Integration sketch: StreamPage rewired to StreamAggregator + MetaAggregator
// -----------------------------------------------------------------

struct StreamLoadRequest {
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

class StreamAggregator : public QObject {
    Q_OBJECT
public:
    explicit StreamAggregator(AddonRegistry* registry, QObject* parent = nullptr)
        : QObject(parent)
    {
        Q_UNUSED(registry);
    }
    void load(const StreamLoadRequest& request) { Q_UNUSED(request); }
signals:
    void streamsReady(const QList<Stream>& streams,
                      const QHash<QString, QString>& addonsById);
    void streamError(const QString& addonId, const QString& message);
};

struct StreamPickerChoice {
    Stream stream;
    QString sourceKind;
    QString addonId;
    QString addonName;
};

class StreamPickerDialog : public QDialog {
    Q_OBJECT
public:
    StreamPickerDialog(const QList<Stream>& streams,
                       const QHash<QString, QString>& addonsById,
                       QWidget* parent = nullptr)
        : QDialog(parent)
    {
        Q_UNUSED(streams);
        Q_UNUSED(addonsById);
    }
    bool hasSelection() const { return false; }
    StreamPickerChoice selectedChoice() const { return {}; }
};

class StreamPlayerController : public QObject {
    Q_OBJECT
public:
    explicit StreamPlayerController(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    void startStream(const QString& imdbId,
                     const QString& mediaType,
                     int season,
                     int episode,
                     const Stream& selectedStream)
    {
        Q_UNUSED(imdbId);
        Q_UNUSED(mediaType);
        Q_UNUSED(season);
        Q_UNUSED(episode);
        Q_UNUSED(selectedStream);
    }
};

class StreamPageBatch44Sketch : public QObject {
    Q_OBJECT
public:
    explicit StreamPageBatch44Sketch(AddonRegistry* addonRegistry,
                                     StreamPlayerController* playerController,
                                     QObject* parent = nullptr)
        : QObject(parent)
        , m_addonRegistry(addonRegistry)
        , m_metaAggregator(new MetaAggregator(addonRegistry, this))
        , m_streamAggregator(new StreamAggregator(addonRegistry, this))
        , m_playerController(playerController)
    {
    }

    void onPlayRequested(const QString& imdbId, const QString& mediaType, int season, int episode)
    {
        StreamLoadRequest request;
        request.type = (mediaType == QStringLiteral("movie"))
            ? QStringLiteral("movie")
            : QStringLiteral("series");
        request.id = (mediaType == QStringLiteral("movie"))
            ? imdbId
            : imdbId + QLatin1Char(':') + QString::number(season)
                     + QLatin1Char(':') + QString::number(episode);

        // Batch 4.4 replacement for TorrentioClient::fetchStreams:
        // StreamPage now subscribes to StreamAggregator output.
        auto* connected = new bool(false);
        if (!*connected) {
            *connected = true;
            connect(m_streamAggregator, &StreamAggregator::streamsReady, this,
                [this, imdbId, mediaType, season, episode](
                    const QList<Stream>& streams,
                    const QHash<QString, QString>& addonsById) {
                    if (streams.isEmpty()) {
                        emit uiMessage(QStringLiteral("No sources found for this title"));
                        return;
                    }

                    StreamPickerDialog picker(streams, addonsById);
                    if (picker.exec() != QDialog::Accepted || !picker.hasSelection()) {
                        return;
                    }

                    const StreamPickerChoice selected = picker.selectedChoice();
                    persistChoice(selected);

                    // Batch 4.3 handoff signature (selected Stream, not magnet tuple).
                    m_playerController->startStream(
                        imdbId, mediaType, season, episode, selected.stream);
                });

            connect(m_streamAggregator, &StreamAggregator::streamError, this,
                [this](const QString& addonId, const QString& message) {
                    const QString msg = addonId.isEmpty()
                        ? message
                        : QStringLiteral("[%1] %2").arg(addonId, message);
                    emit uiMessage(QStringLiteral("Failed to fetch sources: ") + msg);
                });
        }

        m_streamAggregator->load(request);
    }

    MetaAggregator* metaAggregator() const { return m_metaAggregator; }

signals:
    void uiMessage(const QString& message);

private:
    void persistChoice(const StreamPickerChoice& selected)
    {
        // Mirrors Batch 4.2 persistence contract.
        QJsonObject choice;
        choice.insert(QStringLiteral("sourceKind"), selected.sourceKind);
        choice.insert(QStringLiteral("addonId"), selected.addonId);
        choice.insert(QStringLiteral("addonName"), selected.addonName);
        choice.insert(QStringLiteral("infoHash"), selected.stream.source.infoHash);
        choice.insert(QStringLiteral("fileIndex"), selected.stream.source.fileIndex);
        choice.insert(QStringLiteral("fileNameHint"), selected.stream.source.fileNameHint);
        choice.insert(QStringLiteral("directUrl"), selected.stream.source.url.toString());
        choice.insert(QStringLiteral("youtubeId"), selected.stream.source.youtubeId);
        // StreamChoices::saveChoice(epKey, choice) call remains in StreamPage.
    }

    AddonRegistry* m_addonRegistry = nullptr;
    MetaAggregator* m_metaAggregator = nullptr;
    StreamAggregator* m_streamAggregator = nullptr;
    StreamPlayerController* m_playerController = nullptr;
};

// -----------------------------------------------------------------
// StreamDetailView and StreamSearchWidget constructor rewires
// -----------------------------------------------------------------
//
// 1) StreamDetailView:
//    Old:
//      StreamDetailView(CoreBridge*, CinemetaClient*, TorrentioClient*, StreamLibrary*, QWidget*)
//    New:
//      StreamDetailView(CoreBridge*, MetaAggregator*, StreamLibrary*, QWidget*)
//
//    Behavior:
//      - Movie path stays unchanged (Play Movie button emits playRequested).
//      - Series path calls m_meta->fetchSeriesMeta(imdbId).
//      - Connect to MetaAggregator::seriesMetaReady / seriesMetaError.
//      - Keep m_seasons type as QMap<int, QList<StreamEpisode>> so UI table code is stable.
//
// 2) StreamSearchWidget:
//    Old:
//      StreamSearchWidget(CinemetaClient*, StreamLibrary*, QWidget*)
//      slots use QList<CinemetaEntry>.
//    New:
//      StreamSearchWidget(MetaAggregator*, StreamLibrary*, QWidget*)
//      slots use QList<MetaItemPreview>.
//
//    Mapping for add/remove library card:
//      imdb       <- item.id
//      type       <- item.type
//      name       <- item.name
//      year       <- item.releaseInfo (or parsed year if desired)
//      poster     <- item.poster.toString()
//      description<- item.description
//      imdbRating <- item.imdbRating
//
// -----------------------------------------------------------------
// Batch 4.4 retirement checklist (source + build graph)
// -----------------------------------------------------------------
//
// A) Delete legacy files:
//      src/core/stream/CinemetaClient.h
//      src/core/stream/CinemetaClient.cpp
//      src/core/stream/TorrentioClient.h
//      src/core/stream/TorrentioClient.cpp
//
// B) Remove old structs/usages:
//      CinemetaEntry
//      TorrentioStream
//
// C) CMakeLists cleanup:
//      - Remove src/core/stream/CinemetaClient.cpp/.h
//      - Remove src/core/stream/TorrentioClient.cpp/.h
//      - If Batch 4.2 rename landed, replace TorrentPickerDialog with StreamPickerDialog.
//      - Add new MetaAggregator.cpp/.h if implemented as real src file.
//
// D) End-state grep guard:
//      grep -i "cinemeta\\|torrentio" src/
//    should only match default-seeded URLs in AddonRegistry.cpp.
//

} // namespace tankostream::stream
