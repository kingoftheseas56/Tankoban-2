#include "MetaAggregator.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <memory>

#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestResource;
using tankostream::addon::MetaItemPreview;
using tankostream::addon::ResourceRequest;

namespace tankostream::stream {

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
        for (const ManifestCatalog& catalog : addon.manifest.catalogs) {
            if (catalog.type.compare(type, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    }

    for (const ManifestResource& r : addon.manifest.resources) {
        if (r.name.compare(resource, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (r.hasTypes) {
            return r.types.contains(type, Qt::CaseInsensitive);
        }
        return addon.manifest.types.contains(type, Qt::CaseInsensitive);
    }
    return false;
}

// Returns the first catalog id in `addon` for `type` that declares `search` in
// its extra prop list. Empty string if no search-capable catalog exists for
// that type. Matches stremio-core's catalog-driven search model — each addon
// nominates its own search-capable catalog via the manifest; the client MUST
// NOT hardcode a single catalog id.
QString searchCatalogIdForType(const AddonDescriptor& addon, const QString& type)
{
    for (const ManifestCatalog& catalog : addon.manifest.catalogs) {
        if (catalog.type.compare(type, Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const tankostream::addon::ManifestExtraProp& prop : catalog.extra) {
            if (prop.name.compare(QStringLiteral("search"), Qt::CaseInsensitive) == 0) {
                return catalog.id;
            }
        }
    }
    return {};
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
    if (out.releaseInfo.isEmpty()) {
        out.releaseInfo = obj.value(QStringLiteral("year")).toString().trimmed();
    }
    out.imdbRating = obj.value(QStringLiteral("imdbRating")).toString().trimmed();
    return out;
}

// Fuller MetaItemPreview parse that captures the Phase 3 fields (background,
// logo, runtime, genres, links) alongside the basics. Kept separate from
// parseMetaPreview() so search-result parsing (which runs per-tile and needs
// only the cheap fields) doesn't pay for fields it ignores.
MetaItemPreview parseFullMetaPreview(const QJsonObject& obj, const QString& typeHint)
{
    MetaItemPreview out = parseMetaPreview(obj, typeHint);
    out.background = QUrl(obj.value(QStringLiteral("background")).toString().trimmed());
    out.logo = QUrl(obj.value(QStringLiteral("logo")).toString().trimmed());
    out.runtime = obj.value(QStringLiteral("runtime")).toString().trimmed();

    for (const QJsonValue& g : obj.value(QStringLiteral("genres")).toArray()) {
        const QString genre = g.toString().trimmed();
        if (!genre.isEmpty()) out.genres.push_back(genre);
    }

    for (const QJsonValue& l : obj.value(QStringLiteral("links")).toArray()) {
        const QJsonObject lo = l.toObject();
        tankostream::addon::MetaLink link;
        link.name     = lo.value(QStringLiteral("name")).toString().trimmed();
        link.category = lo.value(QStringLiteral("category")).toString().trimmed();
        link.url      = QUrl(lo.value(QStringLiteral("url")).toString().trimmed());
        if (!link.name.isEmpty() || link.url.isValid()) {
            out.links.push_back(link);
        }
    }

    return out;
}

// Parses one MetaItem.videos[] row into a Video struct with the fields Phase 3
// wants (thumbnail + overview + released + season/episode) — minimal, sufficient
// for the detail-view episode-row thumbnails Phase 3 will render.
tankostream::addon::Video parseMetaVideo(const QJsonObject& obj)
{
    tankostream::addon::Video v;
    v.id = obj.value(QStringLiteral("id")).toString().trimmed();
    v.title = obj.value(QStringLiteral("title")).toString().trimmed();
    if (v.title.isEmpty()) {
        v.title = obj.value(QStringLiteral("name")).toString().trimmed();
    }
    v.overview = obj.value(QStringLiteral("overview")).toString().trimmed();
    v.thumbnail = QUrl(obj.value(QStringLiteral("thumbnail")).toString().trimmed());

    const QString releasedRaw =
        obj.value(QStringLiteral("released")).toString().trimmed();
    if (!releasedRaw.isEmpty()) {
        QDateTime dt = QDateTime::fromString(releasedRaw, Qt::ISODateWithMs);
        if (!dt.isValid()) dt = QDateTime::fromString(releasedRaw, Qt::ISODate);
        if (dt.isValid()) v.released = dt.toUTC();
    }

    const int season  = obj.value(QStringLiteral("season")).toInt(-1);
    const int episode = obj.value(QStringLiteral("episode")).toInt(-1);
    if (season >= 0 && episode >= 0) {
        v.seriesInfo = tankostream::addon::SeriesInfo{season, episode};
    }
    return v;
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
        // Phase 3 Batch 3.4 — overview + thumbnail for row rendering.
        item.overview = videoObj.value(QStringLiteral("overview")).toString().trimmed();
        item.thumbnail = QUrl(
            videoObj.value(QStringLiteral("thumbnail")).toString().trimmed());
        out[season].append(item);
    }

    for (auto it = out.begin(); it != out.end(); ++it) {
        std::sort(it->begin(), it->end(),
                  [](const StreamEpisode& a, const StreamEpisode& b) {
                      return a.episode < b.episode;
                  });
    }
    if (out.size() > 1) {
        out.remove(0);
    }
    return out;
}

}

MetaAggregator::MetaAggregator(AddonRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

void MetaAggregator::searchCatalog(const QString& query)
{
    resetSearch();
    const QString q = query.trimmed();
    if (q.isEmpty() || !m_registry) {
        emit catalogResults({});
        return;
    }

    QList<PendingSearch> queue;
    const QList<AddonDescriptor> addons = m_registry->list();
    for (const AddonDescriptor& addon : addons) {
        if (!addon.flags.enabled) {
            continue;
        }
        for (const QString& type :
             { QStringLiteral("movie"), QStringLiteral("series") }) {
            // Pick the specific catalog that declares `search` in its extra
            // prop list. If none exists, skip — this addon's catalogs are
            // browse-only for that type, not search-capable.
            const QString catalogId = searchCatalogIdForType(addon, type);
            if (catalogId.isEmpty()) {
                continue;
            }

            PendingSearch req;
            req.addonId   = addon.manifest.id;
            req.baseUrl   = addon.transportUrl;
            req.query     = q;
            req.type      = type;
            req.catalogId = catalogId;
            queue.append(req);
        }
    }

    if (queue.isEmpty()) {
        emit catalogResults({});
        return;
    }

    m_pendingSearch = 0;
    for (const PendingSearch& request : queue) {
        dispatchSearch(request);
    }
}

void MetaAggregator::fetchSeriesMeta(const QString& imdbId)
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

    QList<AddonDescriptor> candidates;
    for (const AddonDescriptor& addon : m_registry->list()) {
        if (hasResourceType(addon, QStringLiteral("meta"), QStringLiteral("series"))) {
            candidates.append(addon);
        }
    }
    if (candidates.isEmpty()) {
        emit seriesMetaError(imdbId,
                             QStringLiteral("No enabled addon supports series meta"));
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
    m_lastSeriesError.clear();

    for (const AddonDescriptor& addon : candidates) {
        dispatchSeriesMeta(addon.transportUrl, addon.manifest.id, imdbId);
    }
}

void MetaAggregator::dispatchSearch(const PendingSearch& pending)
{
    ResourceRequest request;
    request.resource = QStringLiteral("catalog");
    request.type = pending.type;
    // Use the per-addon matched catalog id instead of a hardcoded "top".
    // Different stremio addons nominate different catalogs for search.
    request.id = pending.catalogId;
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
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
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
                m_searchResults.append(item);
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
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();

            if (m_lastSearchError.isEmpty()) {
                m_lastSearchError = message;
            }
            finalizeSearch();
        });

    worker->fetchResource(pending.baseUrl, request);
}

void MetaAggregator::finalizeSearch()
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

void MetaAggregator::resetSearch()
{
    m_searchResults.clear();
    m_seenSearchIds.clear();
    m_lastSearchError.clear();
    m_pendingSearch = 0;
}

void MetaAggregator::dispatchSeriesMeta(const QUrl& baseUrl,
                                        const QString& addonId,
                                        const QString& imdbId)
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
        [this, addonId, request, imdbId, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QJsonObject& payload) {
            if (*handled || !sameRequest(request, incoming)) {
                return;
            }
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
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
        [this, addonId, request, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QString& message) {
            if (*handled || !sameRequest(request, incoming)) {
                return;
            }
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            finalizeSeries(addonId, message);
        });

    worker->fetchResource(baseUrl, request);
}

void MetaAggregator::finalizeSeries(const QString& addonId, const QString& error)
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

// ── MetaItem fetch (Phase 3 prep; kicked off by Phase 1 Batch 1.1) ──────────

void MetaAggregator::fetchMetaItem(const QString& imdbId, const QString& type)
{
    if (!m_registry
        || !imdbId.startsWith(QStringLiteral("tt"))
        || type.isEmpty()) {
        return;  // best-effort — silent on bad input
    }

    // Short-TTL cache: re-open within 60s serves synchronously without
    // a re-fetch. Phase 3 subscribers get the cached emit on each
    // showEntry rather than a first-visit miss.
    auto cacheIt = m_metaItemCache.find(imdbId);
    if (cacheIt != m_metaItemCache.end()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - cacheIt->first < kMetaItemCacheTtlMs) {
            emit metaItemReady(cacheIt->second);
            return;
        }
        m_metaItemCache.erase(cacheIt);
    }

    if (m_metaItemInFlight.contains(imdbId)) {
        return;  // earlier call is already fanning out; let it land
    }

    // Pick the first enabled addon that declares meta/{type} support.
    // Cinemeta comes sorted first when multiple match so the same
    // canonical source that populates the series episode list also
    // fills the Phase 3 richer fields.
    QList<AddonDescriptor> candidates;
    for (const AddonDescriptor& addon : m_registry->list()) {
        if (hasResourceType(addon, QStringLiteral("meta"), type)) {
            candidates.append(addon);
        }
    }
    if (candidates.isEmpty()) {
        return;
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const AddonDescriptor& a, const AddonDescriptor& b) {
            const bool aCinemeta = a.manifest.id == QStringLiteral("com.linvo.cinemeta");
            const bool bCinemeta = b.manifest.id == QStringLiteral("com.linvo.cinemeta");
            return aCinemeta && !bCinemeta;
        });

    m_metaItemInFlight.insert(imdbId);
    dispatchMetaItemFetch(candidates.first().transportUrl, imdbId, type);
}

void MetaAggregator::dispatchMetaItemFetch(const QUrl& baseUrl,
                                            const QString& imdbId,
                                            const QString& type)
{
    ResourceRequest request;
    request.resource = QStringLiteral("meta");
    request.type     = type;
    request.id       = imdbId;

    auto* worker    = new AddonTransport(this);
    auto  handled   = std::make_shared<bool>(false);
    auto  readyConn = std::make_shared<QMetaObject::Connection>();
    auto  failConn  = std::make_shared<QMetaObject::Connection>();

    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
        [this, request, imdbId, type, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QJsonObject& payload) {
            if (*handled || !sameRequest(request, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            m_metaItemInFlight.remove(imdbId);

            const QJsonObject metaObj =
                payload.value(QStringLiteral("meta")).toObject();
            if (metaObj.isEmpty()) return;

            tankostream::addon::MetaItem item;
            item.preview = parseFullMetaPreview(metaObj, type);
            const QJsonArray videos =
                metaObj.value(QStringLiteral("videos")).toArray();
            item.videos.reserve(videos.size());
            for (const QJsonValue& v : videos) {
                item.videos.push_back(parseMetaVideo(v.toObject()));
            }

            m_metaItemCache[imdbId] =
                qMakePair(QDateTime::currentMSecsSinceEpoch(), item);
            emit metaItemReady(item);
        });

    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
        [this, request, imdbId, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QString& /*message*/) {
            if (*handled || !sameRequest(request, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            m_metaItemInFlight.remove(imdbId);
            // Silent — preview tile already painted; failure is a
            // Phase-3-richness miss, not a user-visible error.
        });

    worker->fetchResource(baseUrl, request);
}

// ── Stateless title → poster-candidate lookup (HELP.md Agent 5) ─────────────
//
// Per-call state in an anonymous-namespace struct, allocated as a shared_ptr
// captured by every dispatched worker's lambda. Reentrant: two concurrent
// callers never touch each other's state.

namespace {

struct TitleSearchState {
    QList<tankostream::addon::MetaItemPreview> results;
    QSet<QString>                              seenIds;
    QString                                    firstError;
    int                                        pending = 0;
    MetaAggregator::TitleSearchCallback        callback;
    bool                                       invoked = false;
};

// Fire the user callback exactly once (guards against double-fire if the
// pending counter hits zero in two threads of logic).
void maybeFinalize(const std::shared_ptr<TitleSearchState>& state)
{
    if (state->invoked) return;
    if (state->pending > 0) return;
    state->invoked = true;

    // Sort by name ascending — stable presentation for the picker.
    std::sort(state->results.begin(), state->results.end(),
        [](const tankostream::addon::MetaItemPreview& a,
           const tankostream::addon::MetaItemPreview& b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });

    // Error iff every addon failed AND results empty.
    const QString err = state->results.isEmpty() ? state->firstError : QString();
    state->callback(state->results, err);
}

}  // namespace

void MetaAggregator::searchByTitle(const QString& query,
                                    const QString& typeFilter,
                                    TitleSearchCallback callback)
{
    if (!callback) return;

    const QString q = query.trimmed();
    if (q.isEmpty() || !m_registry) {
        callback({}, QStringLiteral("Empty query"));
        return;
    }

    auto state = std::make_shared<TitleSearchState>();
    state->callback = std::move(callback);

    // Resolve (addon, type, catalogId) triples. If typeFilter is empty,
    // probe both movie and series; otherwise restrict to that single type.
    const QStringList probedTypes = typeFilter.isEmpty()
        ? QStringList{QStringLiteral("movie"), QStringLiteral("series")}
        : QStringList{typeFilter};

    struct Dispatch { QString addonId; QUrl baseUrl; QString type; QString catalogId; };
    QList<Dispatch> queue;

    for (const AddonDescriptor& addon : m_registry->list()) {
        if (!addon.flags.enabled) continue;
        for (const QString& type : probedTypes) {
            const QString catalogId = searchCatalogIdForType(addon, type);
            if (catalogId.isEmpty()) continue;
            queue.append({addon.manifest.id, addon.transportUrl, type, catalogId});
        }
    }

    if (queue.isEmpty()) {
        state->callback({}, QStringLiteral("No enabled addon supports catalog search"));
        return;
    }

    state->pending = queue.size();

    for (const Dispatch& d : queue) {
        ResourceRequest request;
        request.resource = QStringLiteral("catalog");
        request.type     = d.type;
        request.id       = d.catalogId;
        request.extra    = { qMakePair(QStringLiteral("search"), q) };

        auto* worker    = new AddonTransport(this);
        auto  handled   = std::make_shared<bool>(false);
        auto  readyConn = std::make_shared<QMetaObject::Connection>();
        auto  failConn  = std::make_shared<QMetaObject::Connection>();

        const QString typeForParse = d.type;

        *readyConn = connect(worker, &AddonTransport::resourceReady, this,
            [state, request, typeForParse, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QJsonObject& payload) {
                if (*handled || !sameRequest(request, incoming)) return;
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();

                const QJsonArray metas =
                    payload.value(QStringLiteral("metas")).toArray();
                for (const QJsonValue& v : metas) {
                    const MetaItemPreview item =
                        parseMetaPreview(v.toObject(), typeForParse);
                    if (item.id.isEmpty() || item.name.isEmpty()) continue;
                    if (state->seenIds.contains(item.id)) continue;
                    state->seenIds.insert(item.id);
                    state->results.append(item);
                }

                --state->pending;
                maybeFinalize(state);
            });

        *failConn = connect(worker, &AddonTransport::resourceFailed, this,
            [state, request, handled, readyConn, failConn, worker](
                const ResourceRequest& incoming, const QString& message) {
                if (*handled || !sameRequest(request, incoming)) return;
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();

                if (state->firstError.isEmpty()) {
                    state->firstError = message;
                }

                --state->pending;
                maybeFinalize(state);
            });

        worker->fetchResource(d.baseUrl, request);
    }
}

}
