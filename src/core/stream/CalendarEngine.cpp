#include "CalendarEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSaveFile>

#include <algorithm>
#include <memory>

#include "StreamLibrary.h"
#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::MetaItemPreview;
using tankostream::addon::ResourceRequest;
using tankostream::addon::SeriesInfo;
using tankostream::addon::Video;

namespace tankostream::stream {

namespace {

bool sameRequest(const ResourceRequest& a, const ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

// Stremio's `released` fields come in two shapes in the wild: ISO 8601
// strings (most common, emitted by Cinemeta + IMDB-driven scrapers) and
// numeric epoch values (a handful of community addons). Epoch disambiguates
// s/ms by magnitude — seconds-since-epoch never crosses 1e12 in any future
// any user will live to see, so values >= 1e12 are treated as ms.
QDateTime parseFlexibleDate(const QJsonValue& raw)
{
    if (raw.isString()) {
        const QString s = raw.toString().trimmed();
        if (s.isEmpty()) return {};
        QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
        if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
        if (dt.isValid()) return dt.toUTC();
    }
    if (raw.isDouble()) {
        const qint64 n = static_cast<qint64>(raw.toDouble(0.0));
        if (n > 0) {
            return (n >= 1000000000000LL)
                ? QDateTime::fromMSecsSinceEpoch(n, Qt::UTC)
                : QDateTime::fromSecsSinceEpoch(n, Qt::UTC);
        }
    }
    return {};
}

// Build a fallback MetaItemPreview from the StreamLibraryEntry so that the
// CalendarItem is still renderable when an addon returns a bare meta
// response without a full preview block.
MetaItemPreview previewFromLibrary(const StreamLibraryEntry& entry)
{
    MetaItemPreview out;
    out.id          = entry.imdb.trimmed();
    out.type        = entry.type.trimmed();
    out.name        = entry.name.trimmed();
    out.poster      = QUrl(entry.poster.trimmed());
    out.description = entry.description.trimmed();
    out.releaseInfo = entry.year.trimmed();
    out.imdbRating  = entry.imdbRating.trimmed();
    return out;
}

// Merge an addon-returned MetaItemPreview on top of the library fallback —
// addon values win when non-empty, library values fill gaps.
MetaItemPreview parseMetaPreview(const QJsonObject&     metaObj,
                                 const MetaItemPreview& fallback)
{
    MetaItemPreview out = fallback;

    const QString id = metaObj.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty()) out.id = id;

    const QString type = metaObj.value(QStringLiteral("type")).toString().trimmed();
    if (!type.isEmpty()) out.type = type;

    const QString name = metaObj.value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty()) out.name = name;

    const QString poster = metaObj.value(QStringLiteral("poster")).toString().trimmed();
    if (!poster.isEmpty()) out.poster = QUrl(poster);

    const QString description = metaObj.value(QStringLiteral("description")).toString().trimmed();
    if (!description.isEmpty()) out.description = description;

    const QString releaseInfo = metaObj.value(QStringLiteral("releaseInfo")).toString().trimmed();
    if (!releaseInfo.isEmpty()) out.releaseInfo = releaseInfo;

    const QString rating = metaObj.value(QStringLiteral("imdbRating")).toString().trimmed();
    if (!rating.isEmpty()) out.imdbRating = rating;

    out.released = parseFlexibleDate(metaObj.value(QStringLiteral("released")));
    return out;
}

QList<Video> parseVideos(const QJsonArray& videos)
{
    QList<Video> out;
    out.reserve(videos.size());

    for (const QJsonValue& v : videos) {
        const QJsonObject obj = v.toObject();

        Video video;
        video.id = obj.value(QStringLiteral("id")).toString().trimmed();

        // Stremio addons split across `title` (IMDB-style) and `name`
        // (Cinemeta-style). Take whichever is first non-empty.
        video.title = obj.value(QStringLiteral("title")).toString().trimmed();
        if (video.title.isEmpty())
            video.title = obj.value(QStringLiteral("name")).toString().trimmed();

        video.released  = parseFlexibleDate(obj.value(QStringLiteral("released")));
        video.overview  = obj.value(QStringLiteral("overview")).toString().trimmed();
        video.thumbnail = QUrl(obj.value(QStringLiteral("thumbnail")).toString().trimmed());

        const int season  = obj.value(QStringLiteral("season")).toInt(-1);
        const int episode = obj.value(QStringLiteral("episode")).toInt(-1);
        if (season >= 0 && episode >= 0) {
            video.seriesInfo = SeriesInfo{season, episode};
        }

        out.push_back(video);
    }
    return out;
}

QJsonObject previewToJson(const MetaItemPreview& preview)
{
    QJsonObject out;
    out[QStringLiteral("id")]          = preview.id;
    out[QStringLiteral("type")]        = preview.type;
    out[QStringLiteral("name")]        = preview.name;
    out[QStringLiteral("poster")]      = preview.poster.toString(QUrl::FullyEncoded);
    out[QStringLiteral("description")] = preview.description;
    out[QStringLiteral("releaseInfo")] = preview.releaseInfo;
    out[QStringLiteral("imdbRating")]  = preview.imdbRating;
    if (preview.released.isValid())
        out[QStringLiteral("released")] = preview.released.toUTC().toString(Qt::ISODate);
    return out;
}

QJsonObject videoToJson(const Video& video)
{
    QJsonObject out;
    out[QStringLiteral("id")]        = video.id;
    out[QStringLiteral("title")]     = video.title;
    out[QStringLiteral("overview")]  = video.overview;
    out[QStringLiteral("thumbnail")] = video.thumbnail.toString(QUrl::FullyEncoded);
    if (video.released.isValid())
        out[QStringLiteral("released")] = video.released.toUTC().toString(Qt::ISODate);
    if (video.seriesInfo.has_value()) {
        out[QStringLiteral("season")]  = video.seriesInfo->season;
        out[QStringLiteral("episode")] = video.seriesInfo->episode;
    }
    return out;
}

MetaItemPreview previewFromJson(const QJsonObject& obj)
{
    MetaItemPreview out;
    out.id          = obj.value(QStringLiteral("id")).toString();
    out.type        = obj.value(QStringLiteral("type")).toString();
    out.name        = obj.value(QStringLiteral("name")).toString();
    out.poster      = QUrl(obj.value(QStringLiteral("poster")).toString());
    out.description = obj.value(QStringLiteral("description")).toString();
    out.releaseInfo = obj.value(QStringLiteral("releaseInfo")).toString();
    out.imdbRating  = obj.value(QStringLiteral("imdbRating")).toString();
    out.released    = parseFlexibleDate(obj.value(QStringLiteral("released")));
    return out;
}

Video videoFromJson(const QJsonObject& obj)
{
    Video out;
    out.id        = obj.value(QStringLiteral("id")).toString();
    out.title     = obj.value(QStringLiteral("title")).toString();
    out.overview  = obj.value(QStringLiteral("overview")).toString();
    out.thumbnail = QUrl(obj.value(QStringLiteral("thumbnail")).toString());
    out.released  = parseFlexibleDate(obj.value(QStringLiteral("released")));

    const int season  = obj.value(QStringLiteral("season")).toInt(-1);
    const int episode = obj.value(QStringLiteral("episode")).toInt(-1);
    if (season >= 0 && episode >= 0) {
        out.seriesInfo = SeriesInfo{season, episode};
    }
    return out;
}

bool releasedWithinWindow(const QDateTime& released,
                          const QDateTime& nowUtc,
                          const QDateTime& untilUtc)
{
    if (!released.isValid()) return false;
    const QDateTime relUtc = released.toUTC();
    return relUtc > nowUtc && relUtc < untilUtc;
}

}

CalendarEngine::CalendarEngine(AddonRegistry* registry,
                               StreamLibrary* library,
                               const QString& dataDir,
                               QObject*       parent)
    : QObject(parent)
    , m_registry(registry)
    , m_library(library)
    , m_dataDir(dataDir)
{
}

void CalendarEngine::loadUpcoming()
{
    // Bump generation FIRST so any still-pending worker from a prior call
    // observes the change and short-circuits in its callback before touching
    // state that resetTransientState is about to zero. Fix for Phase 6
    // REVIEW P1 #1.
    const quint64 gen = ++m_generation;
    resetTransientState();

    if (!m_registry || !m_library || m_dataDir.isEmpty()) {
        emit calendarReady({});
        emit calendarGroupedReady({});
        return;
    }

    m_nowUtc   = QDateTime::currentDateTimeUtc();
    m_untilUtc = m_nowUtc.addDays(kWindowDays);

    // Collect library series (imdb-backed, type == "series", dedupe).
    QList<StreamLibraryEntry> series;
    QSet<QString>             seenImdb;
    for (const StreamLibraryEntry& entry : m_library->getAll()) {
        const QString imdb = entry.imdb.trimmed();
        if (imdb.isEmpty() || !imdb.startsWith(QStringLiteral("tt")))
            continue;
        if (entry.type.compare(QStringLiteral("series"), Qt::CaseInsensitive) != 0)
            continue;
        if (seenImdb.contains(imdb))
            continue;
        seenImdb.insert(imdb);
        series.push_back(entry);
    }

    if (series.isEmpty()) {
        emit calendarReady({});
        emit calendarGroupedReady({});
        return;
    }

    // Parity with stremio-core calendar.rs: sort most-recently-added first,
    // then cap at CALENDAR_ITEMS_COUNT = 100. Entries pre-dating addedAt
    // tracking (addedAt == 0) fall to the bottom naturally. Fix for Phase 6
    // REVIEW P1 #2.
    std::sort(series.begin(), series.end(),
        [](const StreamLibraryEntry& a, const StreamLibraryEntry& b) {
            return a.addedAt > b.addedAt;
        });
    if (series.size() > kMaxSeries)
        series = series.mid(0, kMaxSeries);

    // Build fallback previews AFTER cap — no point carrying fallbacks for
    // series we won't fan out.
    for (const StreamLibraryEntry& s : series)
        m_libraryFallback.insert(s.imdb, previewFromLibrary(s));

    const QList<AddonDescriptor> addons =
        m_registry->findByResourceType(QStringLiteral("meta"),
                                        QStringLiteral("series"));
    if (addons.isEmpty()) {
        emit calendarError(QStringLiteral("No enabled addon supports series meta"));
        return;
    }

    // Probe each addon for batched-catalog support (calendarVideosIds /
    // lastVideosIds extra prop). Batched-supporting addons get one catalog
    // request for the full id set; the rest get per-series meta fan-out.
    struct BatchedPlan { QString catalogId; QString extraName; };
    QHash<QString, BatchedPlan> batchedAddons;
    QList<QString>              batchedAddonIds;
    for (const AddonDescriptor& addon : addons) {
        QString extraName;
        const QString catalogId = findBatchedSeriesCatalog(addon, &extraName);
        if (!catalogId.isEmpty()) {
            batchedAddons.insert(addon.manifest.id, {catalogId, extraName});
            batchedAddonIds.push_back(addon.manifest.id);
        }
    }
    std::sort(batchedAddonIds.begin(), batchedAddonIds.end());

    // Cache signature: sorted series IDs + sorted addon IDs + cap + batched
    // addon set. Any change to cap or which addons advertise batched support
    // invalidates the cache even if the TTL hasn't expired.
    QList<QString> seriesIds;
    seriesIds.reserve(series.size());
    for (const auto& s : series) seriesIds.push_back(s.imdb);
    m_cacheSignature = buildCacheSignature(seriesIds, addons, kMaxSeries, batchedAddonIds);

    if (tryServeFreshCache(gen))
        return;

    // Fan out: one batched catalog request per batched-supporting addon; one
    // per-(series × addon) meta request for the rest.
    for (const AddonDescriptor& addon : addons) {
        auto it = batchedAddons.constFind(addon.manifest.id);
        if (it != batchedAddons.constEnd()) {
            dispatchBatchedCatalogFetch(addon, it->catalogId, it->extraName,
                                         seriesIds, gen);
        } else {
            for (const StreamLibraryEntry& s : series) {
                dispatchMetaFetch(s.imdb, addon, gen);
            }
        }
    }

    if (m_pendingResponses == 0) {
        emit calendarReady({});
        emit calendarGroupedReady({});
    }
}

void CalendarEngine::dispatchMetaFetch(const QString&         seriesImdbId,
                                       const AddonDescriptor& addon,
                                       quint64                gen)
{
    ResourceRequest req;
    req.resource = QStringLiteral("meta");
    req.type     = QStringLiteral("series");
    req.id       = seriesImdbId;

    auto* worker    = new AddonTransport(this);
    auto  handled   = std::make_shared<bool>(false);
    auto  readyConn = std::make_shared<QMetaObject::Connection>();
    auto  failConn  = std::make_shared<QMetaObject::Connection>();

    ++m_pendingResponses;

    const QString addonId = addon.manifest.id;
    const QString seriesId = seriesImdbId;

    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
        [this, req, addonId, seriesId, gen, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QJsonObject& payload) {
            if (*handled || !sameRequest(req, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            // Stale worker from a superseded loadUpcoming — don't mutate
            // current-generation state. Outer gate; onMetaReady gates again.
            if (!isCurrentGeneration(gen)) return;
            onMetaReady(gen, addonId, seriesId, payload);
        });

    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
        [this, req, addonId, gen, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QString& message) {
            if (*handled || !sameRequest(req, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            if (!isCurrentGeneration(gen)) return;
            onMetaFailed(gen, addonId, message);
        });

    worker->fetchResource(addon.transportUrl, req);
}

void CalendarEngine::dispatchBatchedCatalogFetch(const AddonDescriptor& addon,
                                                  const QString&         catalogId,
                                                  const QString&         extraPropName,
                                                  const QList<QString>&  imdbIds,
                                                  quint64                gen)
{
    ResourceRequest req;
    req.resource = QStringLiteral("catalog");
    req.type     = QStringLiteral("series");
    req.id       = catalogId;
    req.extra.push_back({extraPropName, imdbIds.join(QLatin1Char(','))});

    auto* worker    = new AddonTransport(this);
    auto  handled   = std::make_shared<bool>(false);
    auto  readyConn = std::make_shared<QMetaObject::Connection>();
    auto  failConn  = std::make_shared<QMetaObject::Connection>();

    ++m_pendingResponses;

    const QString addonId = addon.manifest.id;

    *readyConn = connect(worker, &AddonTransport::resourceReady, this,
        [this, req, addonId, gen, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QJsonObject& payload) {
            if (*handled || !sameRequest(req, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            if (!isCurrentGeneration(gen)) return;
            onBatchedCatalogReady(gen, addonId, payload);
        });

    *failConn = connect(worker, &AddonTransport::resourceFailed, this,
        [this, req, addonId, gen, handled, readyConn, failConn, worker](
            const ResourceRequest& incoming, const QString& message) {
            if (*handled || !sameRequest(req, incoming)) return;
            *handled = true;
            QObject::disconnect(*readyConn);
            QObject::disconnect(*failConn);
            worker->deleteLater();
            if (!isCurrentGeneration(gen)) return;
            onBatchedCatalogFailed(gen, addonId, message);
        });

    worker->fetchResource(addon.transportUrl, req);
}

// Shared body between onMetaReady and onBatchedCatalogReady. Reads one meta
// object (preview + videos), applies fallback merge, window filter, and dedup.
void CalendarEngine::ingestMetaObj(const QJsonObject& metaObj,
                                    const QString&     seriesHintId)
{
    if (metaObj.isEmpty()) return;

    const QString id = metaObj.value(QStringLiteral("id")).toString().trimmed();
    const QString fallbackKey = !id.isEmpty() ? id : seriesHintId;
    const MetaItemPreview fallback =
        m_libraryFallback.value(fallbackKey, MetaItemPreview{});
    const MetaItemPreview preview = parseMetaPreview(metaObj, fallback);
    const QList<Video>    videos  =
        parseVideos(metaObj.value(QStringLiteral("videos")).toArray());

    for (const Video& video : videos) {
        if (!releasedWithinWindow(video.released, m_nowUtc, m_untilUtc))
            continue;

        CalendarItem item;
        item.meta  = preview;
        item.video = video;

        const QString key = itemIdentityKey(item);
        if (m_seenKeys.contains(key))
            continue;
        m_seenKeys.insert(key);
        m_items.push_back(item);
    }
}

void CalendarEngine::onMetaReady(quint64            gen,
                                  const QString&      addonId,
                                  const QString&      seriesImdbId,
                                  const QJsonObject&  payload)
{
    Q_UNUSED(addonId);
    if (!isCurrentGeneration(gen)) return;

    ingestMetaObj(payload.value(QStringLiteral("meta")).toObject(), seriesImdbId);
    completeOne(gen);
}

void CalendarEngine::onMetaFailed(quint64 gen, const QString& addonId, const QString& message)
{
    if (!isCurrentGeneration(gen)) return;
    emit calendarAddonError(addonId, message);
    if (m_firstError.isEmpty())
        m_firstError = QStringLiteral("[%1] %2").arg(addonId, message);
    completeOne(gen);
}

void CalendarEngine::onBatchedCatalogReady(quint64            gen,
                                            const QString&     addonId,
                                            const QJsonObject& payload)
{
    Q_UNUSED(addonId);
    if (!isCurrentGeneration(gen)) return;

    // Stremio catalog response shape: { "metas": [ MetaItemPreview + videos[] ] }.
    // Addons that declare calendarVideosIds / lastVideosIds populate videos[]
    // on each preview with the upcoming episodes for that series id.
    const QJsonArray metas = payload.value(QStringLiteral("metas")).toArray();
    for (const QJsonValue& v : metas) {
        const QJsonObject metaObj = v.toObject();
        ingestMetaObj(metaObj, QString());
    }
    completeOne(gen);
}

void CalendarEngine::onBatchedCatalogFailed(quint64 gen, const QString& addonId, const QString& message)
{
    if (!isCurrentGeneration(gen)) return;
    emit calendarAddonError(addonId, message);
    if (m_firstError.isEmpty())
        m_firstError = QStringLiteral("[%1] %2").arg(addonId, message);
    completeOne(gen);
}

void CalendarEngine::completeOne(quint64 gen)
{
    // Defense-in-depth: the lambda + method-top gates should already have
    // short-circuited stale callers, but gate once more before decrementing
    // the counter. Per Agent 6's Phase 6 REVIEW nit — gen check at all
    // state-mutating / signal-emitting sites.
    if (!isCurrentGeneration(gen)) return;

    --m_pendingResponses;
    if (m_pendingResponses > 0) return;

    std::sort(m_items.begin(), m_items.end(),
        [](const CalendarItem& a, const CalendarItem& b) {
            const QDateTime ar = a.video.released.toUTC();
            const QDateTime br = b.video.released.toUTC();
            if (ar != br) return ar < br;
            if (a.meta.name != b.meta.name) return a.meta.name < b.meta.name;
            return a.video.title < b.video.title;
        });

    saveCache(m_items);

    // Phase 6 REVIEW Q5: on empty-items-with-errors, emit ONLY calendarError.
    // Prior shape emitted calendarError + calendarReady({}) + calendarGroupedReady({}),
    // and the screen's renderGroups -> setEmpty() clobbered the error message with
    // "No upcoming episodes...". Exclusive-or keeps the user-visible surface coherent.
    if (m_items.isEmpty() && !m_firstError.isEmpty()) {
        emit calendarError(m_firstError);
        return;
    }

    emit calendarReady(m_items);
    emit calendarGroupedReady(buildDayGroups(m_items, m_nowUtc.date()));
}

void CalendarEngine::resetTransientState()
{
    // NOTE: m_generation is NOT reset here — it's a monotonically increasing
    // counter owned by loadUpcoming(). Resetting it would defeat the whole
    // point of the P1 #1 fix.
    m_nowUtc           = {};
    m_untilUtc         = {};
    m_cacheSignature.clear();
    m_pendingResponses = 0;
    m_seenKeys.clear();
    m_items.clear();
    m_firstError.clear();
    m_libraryFallback.clear();
}

bool CalendarEngine::tryServeFreshCache(quint64 gen)
{
    // Called synchronously from loadUpcoming — gen is always current here by
    // construction. Keep the gate anyway per Agent 6's Phase 6 REVIEW nit, so
    // the invariant "no emission without current generation" holds locally at
    // every emission site, not just the async callback path.
    if (!isCurrentGeneration(gen)) return false;

    QFile file(cacheFilePath(m_dataDir));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(0) != kSchemaVersion)
        return false;
    if (root.value(QStringLiteral("signature")).toString() != m_cacheSignature)
        return false;

    const qint64 generatedAtMs =
        static_cast<qint64>(root.value(QStringLiteral("generatedAtMs")).toDouble(0.0));
    if (generatedAtMs <= 0)
        return false;
    if (m_nowUtc.toMSecsSinceEpoch() - generatedAtMs > kCacheTtlMs)
        return false;

    QList<CalendarItem> items;
    const QJsonArray rows = root.value(QStringLiteral("items")).toArray();
    items.reserve(rows.size());
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        CalendarItem item;
        item.meta  = previewFromJson(row.value(QStringLiteral("meta")).toObject());
        item.video = videoFromJson(row.value(QStringLiteral("video")).toObject());
        if (item.meta.id.isEmpty() || !item.video.released.isValid())
            continue;
        // Also re-apply the window filter — cache could hold an item whose
        // release slipped outside [now, now+60d] between writes (edge case
        // when the user reopens after a long gap shorter than TTL).
        if (!releasedWithinWindow(item.video.released, m_nowUtc, m_untilUtc))
            continue;
        items.push_back(item);
    }

    emit calendarReady(items);
    emit calendarGroupedReady(buildDayGroups(items, m_nowUtc.date()));
    return true;
}

void CalendarEngine::saveCache(const QList<CalendarItem>& items) const
{
    QJsonObject root;
    root[QStringLiteral("version")]        = kSchemaVersion;
    root[QStringLiteral("generatedAtMs")]  = m_nowUtc.toMSecsSinceEpoch();
    root[QStringLiteral("signature")]      = m_cacheSignature;

    QJsonArray rows;
    for (const CalendarItem& item : items) {
        QJsonObject row;
        row[QStringLiteral("meta")]  = previewToJson(item.meta);
        row[QStringLiteral("video")] = videoToJson(item.video);
        rows.push_back(row);
    }
    root[QStringLiteral("items")] = rows;

    QSaveFile out(cacheFilePath(m_dataDir));
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.commit();
}

QString CalendarEngine::cacheFilePath(const QString& dataDir)
{
    QDir dir(dataDir);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("stream_calendar_cache.json"));
}

QString CalendarEngine::buildCacheSignature(
    const QList<QString>&              seriesIds,
    const QList<AddonDescriptor>&      addons,
    int                                cap,
    const QList<QString>&              batchedAddonIds)
{
    QList<QString> sortedSeries = seriesIds;
    std::sort(sortedSeries.begin(), sortedSeries.end());

    QList<QString> addonIds;
    addonIds.reserve(addons.size());
    for (const auto& a : addons) addonIds.push_back(a.manifest.id);
    std::sort(addonIds.begin(), addonIds.end());

    // Caller is expected to pass batchedAddonIds already sorted, but sort
    // defensively so a different caller can't produce a signature drift.
    QList<QString> sortedBatched = batchedAddonIds;
    std::sort(sortedBatched.begin(), sortedBatched.end());

    const QString payload = sortedSeries.join(QLatin1Char(','))
                          + QLatin1Char('|')
                          + addonIds.join(QLatin1Char(','))
                          + QLatin1Char('|')
                          + QString::number(cap)
                          + QLatin1Char('|')
                          + sortedBatched.join(QLatin1Char(','));
    return QString::fromUtf8(
        QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString CalendarEngine::findBatchedSeriesCatalog(const AddonDescriptor& addon,
                                                  QString*               extraPropName)
{
    // Stremio-core's calendar.rs batches via ExtraType::Ids through the
    // calendarVideosIds extra prop. A small number of community addons accept
    // lastVideosIds with similar semantics; we treat both as equivalent.
    static const QStringList kBatchedExtras = {
        QStringLiteral("calendarVideosIds"),
        QStringLiteral("lastVideosIds"),
    };

    for (const auto& catalog : addon.manifest.catalogs) {
        if (catalog.type.compare(QStringLiteral("series"), Qt::CaseInsensitive) != 0)
            continue;
        for (const auto& prop : catalog.extra) {
            for (const QString& name : kBatchedExtras) {
                if (prop.name.compare(name, Qt::CaseInsensitive) == 0) {
                    if (extraPropName) *extraPropName = prop.name;
                    return catalog.id;
                }
            }
        }
    }
    if (extraPropName) extraPropName->clear();
    return {};
}

CalendarBucket CalendarEngine::classifyBucket(const QDate& day, const QDate& today)
{
    // Week starts Monday (Qt's dayOfWeek is 1..7, Monday=1).
    const QDate weekStart       = today.addDays(1 - today.dayOfWeek());
    const QDate nextWeekStart   = weekStart.addDays(7);
    const QDate weekAfterStart  = weekStart.addDays(14);

    if (day < nextWeekStart)    return CalendarBucket::ThisWeek;
    if (day < weekAfterStart)   return CalendarBucket::NextWeek;
    return CalendarBucket::Later;
}

QList<CalendarDayGroup> CalendarEngine::buildDayGroups(
    const QList<CalendarItem>& sortedItems,
    const QDate&               today)
{
    QMap<QDate, QList<CalendarItem>> byDay;
    for (const CalendarItem& item : sortedItems) {
        const QDate d = item.video.released.toUTC().date();
        if (!d.isValid()) continue;
        byDay[d].push_back(item);
    }

    QList<CalendarDayGroup> out;
    out.reserve(byDay.size());
    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
        CalendarDayGroup group;
        group.day    = it.key();
        group.bucket = classifyBucket(group.day, today);
        group.items  = it.value();
        out.push_back(group);
    }
    return out;
}

QString CalendarEngine::itemIdentityKey(const CalendarItem& item)
{
    const QString releasedIso = item.video.released.isValid()
        ? item.video.released.toUTC().toString(Qt::ISODate)
        : QString();
    const QString season = item.video.seriesInfo.has_value()
        ? QString::number(item.video.seriesInfo->season)
        : QStringLiteral("-");
    const QString episode = item.video.seriesInfo.has_value()
        ? QString::number(item.video.seriesInfo->episode)
        : QStringLiteral("-");
    return item.meta.id + QLatin1Char('|')
         + item.video.id + QLatin1Char('|')
         + season + QLatin1Char('|')
         + episode + QLatin1Char('|')
         + releasedIso;
}

}
