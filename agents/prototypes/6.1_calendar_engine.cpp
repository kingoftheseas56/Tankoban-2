// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 6.1 (Calendar backend)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:271
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:273
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:275
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:276
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:355
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9025
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:40
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:43
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:51
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:74
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:76
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:27
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:29
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.cpp:17
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.cpp:286
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.cpp:291
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.cpp:442
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamLibrary.h:11
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamLibrary.h:13
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamLibrary.h:33
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamLibrary.cpp:53
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CatalogAggregator.h:39
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CatalogAggregator.cpp:169
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CatalogAggregator.cpp:182
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CatalogAggregator.cpp:233
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/MetaAggregator.cpp:28
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/MetaAggregator.cpp:87
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/MetaAggregator.cpp:182
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.h:69
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:139
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:285
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:87
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:93
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:239
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:262
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/calendar.rs:288
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 6.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <memory>
#include <optional>

namespace tankostream::addon {

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct ManifestResource {
    QString name;
    QStringList types;
    bool hasTypes = false;
};

struct AddonManifest {
    QString id;
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

struct SeriesInfo {
    int season = 0;
    int episode = 0;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;
    QUrl poster;
    QString description;
    QString releaseInfo;
    QDateTime released;
    QString imdbRating;
};

struct Video {
    QString id;
    QString title;
    QDateTime released;
    QString overview;
    QUrl thumbnail;
    std::optional<SeriesInfo> seriesInfo;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

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

struct StreamLibraryEntry {
    QString imdb;
    QString type;
    QString name;
    QString year;
    QString poster;
    QString description;
    QString imdbRating;
    qint64 addedAt = 0;
};

class StreamLibrary : public QObject {
    Q_OBJECT
public:
    explicit StreamLibrary(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    QList<StreamLibraryEntry> getAll() const;
};

struct CalendarItem {
    tankostream::addon::MetaItemPreview meta;
    tankostream::addon::Video video;
};

enum class CalendarBucket {
    ThisWeek,
    NextWeek,
    Later,
};

struct CalendarDayGroup {
    QDate day;
    CalendarBucket bucket = CalendarBucket::Later;
    QList<CalendarItem> items;
};

namespace {

constexpr qint64 kCacheTtlMs = 12LL * 60LL * 60LL * 1000LL; // 12h
constexpr int kSchemaVersion = 1;

bool sameRequest(const tankostream::addon::ResourceRequest& a,
                 const tankostream::addon::ResourceRequest& b)
{
    return a.resource == b.resource
        && a.type == b.type
        && a.id == b.id
        && a.extra == b.extra;
}

QDateTime parseFlexibleDate(const QJsonValue& raw)
{
    if (raw.isString()) {
        const QString s = raw.toString().trimmed();
        QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
        if (!dt.isValid()) {
            dt = QDateTime::fromString(s, Qt::ISODate);
        }
        if (dt.isValid()) {
            return dt.toUTC();
        }
    }

    if (raw.isDouble()) {
        const qint64 n = static_cast<qint64>(raw.toDouble(0.0));
        if (n > 0) {
            // Heuristic: >= 1e12 looks like ms, else sec.
            return (n >= 1000000000000LL)
                ? QDateTime::fromMSecsSinceEpoch(n, Qt::UTC)
                : QDateTime::fromSecsSinceEpoch(n, Qt::UTC);
        }
    }

    return {};
}

tankostream::addon::MetaItemPreview parseMetaPreview(const QJsonObject& metaObj,
                                                     const StreamLibraryEntry& fallback)
{
    tankostream::addon::MetaItemPreview preview;
    preview.id = metaObj.value(QStringLiteral("id")).toString().trimmed();
    if (preview.id.isEmpty()) {
        preview.id = fallback.imdb.trimmed();
    }
    preview.type = metaObj.value(QStringLiteral("type")).toString().trimmed();
    if (preview.type.isEmpty()) {
        preview.type = fallback.type.trimmed();
    }
    preview.name = metaObj.value(QStringLiteral("name")).toString().trimmed();
    if (preview.name.isEmpty()) {
        preview.name = fallback.name.trimmed();
    }

    preview.poster = QUrl(metaObj.value(QStringLiteral("poster")).toString().trimmed());
    if (!preview.poster.isValid() || preview.poster.isEmpty()) {
        preview.poster = QUrl(fallback.poster.trimmed());
    }
    preview.description = metaObj.value(QStringLiteral("description")).toString().trimmed();
    if (preview.description.isEmpty()) {
        preview.description = fallback.description.trimmed();
    }
    preview.releaseInfo = metaObj.value(QStringLiteral("releaseInfo")).toString().trimmed();
    if (preview.releaseInfo.isEmpty()) {
        preview.releaseInfo = fallback.year.trimmed();
    }
    preview.imdbRating = metaObj.value(QStringLiteral("imdbRating")).toString().trimmed();
    if (preview.imdbRating.isEmpty()) {
        preview.imdbRating = fallback.imdbRating.trimmed();
    }
    preview.released = parseFlexibleDate(metaObj.value(QStringLiteral("released")));
    return preview;
}

QList<tankostream::addon::Video> parseVideos(const QJsonArray& videos)
{
    QList<tankostream::addon::Video> out;
    out.reserve(videos.size());

    for (const QJsonValue& v : videos) {
        const QJsonObject obj = v.toObject();

        tankostream::addon::Video video;
        video.id = obj.value(QStringLiteral("id")).toString().trimmed();
        video.title = obj.value(QStringLiteral("title")).toString().trimmed();
        if (video.title.isEmpty()) {
            video.title = obj.value(QStringLiteral("name")).toString().trimmed();
        }
        video.released = parseFlexibleDate(obj.value(QStringLiteral("released")));
        video.overview = obj.value(QStringLiteral("overview")).toString().trimmed();
        video.thumbnail = QUrl(obj.value(QStringLiteral("thumbnail")).toString().trimmed());

        const int season = obj.value(QStringLiteral("season")).toInt(-1);
        const int episode = obj.value(QStringLiteral("episode")).toInt(-1);
        if (season >= 0 && episode >= 0) {
            video.seriesInfo = tankostream::addon::SeriesInfo{season, episode};
        }

        out.push_back(video);
    }
    return out;
}

QString itemIdentityKey(const CalendarItem& item)
{
    const QString releasedIso =
        item.video.released.isValid() ? item.video.released.toUTC().toString(Qt::ISODate) : QString();
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

CalendarBucket classifyBucket(const QDate& date, const QDate& today)
{
    const QDate weekStart = today.addDays(1 - today.dayOfWeek());   // Monday
    const QDate nextWeekStart = weekStart.addDays(7);
    const QDate weekAfterStart = weekStart.addDays(14);

    if (date < nextWeekStart) {
        return CalendarBucket::ThisWeek;
    }
    if (date < weekAfterStart) {
        return CalendarBucket::NextWeek;
    }
    return CalendarBucket::Later;
}

QJsonObject previewToJson(const tankostream::addon::MetaItemPreview& preview)
{
    QJsonObject out;
    out[QStringLiteral("id")] = preview.id;
    out[QStringLiteral("type")] = preview.type;
    out[QStringLiteral("name")] = preview.name;
    out[QStringLiteral("poster")] = preview.poster.toString(QUrl::FullyEncoded);
    out[QStringLiteral("description")] = preview.description;
    out[QStringLiteral("releaseInfo")] = preview.releaseInfo;
    out[QStringLiteral("imdbRating")] = preview.imdbRating;
    if (preview.released.isValid()) {
        out[QStringLiteral("released")] = preview.released.toUTC().toString(Qt::ISODate);
    }
    return out;
}

QJsonObject videoToJson(const tankostream::addon::Video& video)
{
    QJsonObject out;
    out[QStringLiteral("id")] = video.id;
    out[QStringLiteral("title")] = video.title;
    out[QStringLiteral("overview")] = video.overview;
    out[QStringLiteral("thumbnail")] = video.thumbnail.toString(QUrl::FullyEncoded);
    if (video.released.isValid()) {
        out[QStringLiteral("released")] = video.released.toUTC().toString(Qt::ISODate);
    }
    if (video.seriesInfo.has_value()) {
        out[QStringLiteral("season")] = video.seriesInfo->season;
        out[QStringLiteral("episode")] = video.seriesInfo->episode;
    }
    return out;
}

tankostream::addon::MetaItemPreview previewFromJson(const QJsonObject& obj)
{
    tankostream::addon::MetaItemPreview out;
    out.id = obj.value(QStringLiteral("id")).toString();
    out.type = obj.value(QStringLiteral("type")).toString();
    out.name = obj.value(QStringLiteral("name")).toString();
    out.poster = QUrl(obj.value(QStringLiteral("poster")).toString());
    out.description = obj.value(QStringLiteral("description")).toString();
    out.releaseInfo = obj.value(QStringLiteral("releaseInfo")).toString();
    out.imdbRating = obj.value(QStringLiteral("imdbRating")).toString();
    out.released = parseFlexibleDate(obj.value(QStringLiteral("released")));
    return out;
}

tankostream::addon::Video videoFromJson(const QJsonObject& obj)
{
    tankostream::addon::Video out;
    out.id = obj.value(QStringLiteral("id")).toString();
    out.title = obj.value(QStringLiteral("title")).toString();
    out.overview = obj.value(QStringLiteral("overview")).toString();
    out.thumbnail = QUrl(obj.value(QStringLiteral("thumbnail")).toString());
    out.released = parseFlexibleDate(obj.value(QStringLiteral("released")));

    const int season = obj.value(QStringLiteral("season")).toInt(-1);
    const int episode = obj.value(QStringLiteral("episode")).toInt(-1);
    if (season >= 0 && episode >= 0) {
        out.seriesInfo = tankostream::addon::SeriesInfo{season, episode};
    }
    return out;
}

bool releasedWithinWindow(const QDateTime& released,
                          const QDateTime& nowUtc,
                          const QDateTime& untilUtc)
{
    if (!released.isValid()) {
        return false;
    }
    const QDateTime relUtc = released.toUTC();
    return relUtc > nowUtc && relUtc < untilUtc;
}

} // namespace

class CalendarEngine : public QObject
{
    Q_OBJECT

public:
    explicit CalendarEngine(tankostream::addon::AddonRegistry* registry,
                            StreamLibrary* library,
                            const QString& dataDir,
                            QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
        , m_library(library)
        , m_dataDir(dataDir)
    {
    }

    void loadUpcoming()
    {
        resetTransient();

        if (!m_registry || !m_library) {
            emit calendarReady({});
            emit calendarGroupedReady({});
            return;
        }

        m_nowUtc = QDateTime::currentDateTimeUtc();
        m_untilUtc = m_nowUtc.addDays(60);

        m_series = collectSeriesLibraryEntries();
        if (m_series.isEmpty()) {
            emit calendarReady({});
            emit calendarGroupedReady({});
            return;
        }

        const QList<tankostream::addon::AddonDescriptor> addons =
            m_registry->findByResourceType(QStringLiteral("meta"), QStringLiteral("series"));
        if (addons.isEmpty()) {
            emit calendarError(QStringLiteral("No enabled addon supports series meta"));
            emit calendarReady({});
            emit calendarGroupedReady({});
            return;
        }

        m_cacheSignature = buildCacheSignature(m_series, addons);
        if (tryServeFreshCache()) {
            return;
        }

        for (const StreamLibraryEntry& series : m_series) {
            for (const auto& addon : addons) {
                dispatchMetaFetch(series, addon);
            }
        }

        if (m_pendingResponses == 0) {
            emit calendarReady({});
            emit calendarGroupedReady({});
        }
    }

signals:
    // Spec signal for Batch 6.1:
    // CalendarItem = { MetaItemPreview, Video }.
    void calendarReady(const QList<CalendarItem>& items);

    // Helper signal for Batch 6.2 screen grouping (This Week / Next Week / Later).
    void calendarGroupedReady(const QList<CalendarDayGroup>& groups);

    void calendarError(const QString& message);
    void calendarAddonError(const QString& addonId, const QString& message);

private:
    struct PendingKey {
        QString addonId;
        QString seriesImdbId;
    };

    static QString cachePath(const QString& dataDir)
    {
        QDir dir(dataDir);
        dir.mkpath(QStringLiteral("."));
        return dir.filePath(QStringLiteral("stream_calendar_cache.json"));
    }

    static QString buildCacheSignature(const QList<StreamLibraryEntry>& series,
                                       const QList<tankostream::addon::AddonDescriptor>& addons)
    {
        QStringList seriesIds;
        seriesIds.reserve(series.size());
        for (const auto& s : series) {
            seriesIds.push_back(s.imdb);
        }
        std::sort(seriesIds.begin(), seriesIds.end());

        QStringList addonIds;
        addonIds.reserve(addons.size());
        for (const auto& a : addons) {
            addonIds.push_back(a.manifest.id);
        }
        std::sort(addonIds.begin(), addonIds.end());

        const QString payload = seriesIds.join(QLatin1Char(','))
            + QLatin1Char('|')
            + addonIds.join(QLatin1Char(','));
        return QString::fromUtf8(
            QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha1).toHex());
    }

    QList<StreamLibraryEntry> collectSeriesLibraryEntries() const
    {
        QList<StreamLibraryEntry> out;
        QSet<QString> seen;

        for (const StreamLibraryEntry& entry : m_library->getAll()) {
            if (entry.imdb.trimmed().isEmpty()) {
                continue;
            }
            if (!entry.imdb.startsWith(QStringLiteral("tt"))) {
                continue;
            }
            if (entry.type.compare(QStringLiteral("series"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (seen.contains(entry.imdb)) {
                continue;
            }
            seen.insert(entry.imdb);
            out.push_back(entry);
        }

        std::sort(out.begin(), out.end(), [](const StreamLibraryEntry& a, const StreamLibraryEntry& b) {
            return a.addedAt > b.addedAt;
        });
        return out;
    }

    bool tryServeFreshCache()
    {
        QFile file(cachePath(m_dataDir));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return false;
        }

        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("version")).toInt(0) != kSchemaVersion) {
            return false;
        }
        if (root.value(QStringLiteral("signature")).toString() != m_cacheSignature) {
            return false;
        }

        const qint64 generatedAtMs =
            static_cast<qint64>(root.value(QStringLiteral("generatedAtMs")).toDouble(0.0));
        if (generatedAtMs <= 0) {
            return false;
        }
        if (m_nowUtc.toMSecsSinceEpoch() - generatedAtMs > kCacheTtlMs) {
            return false;
        }

        QList<CalendarItem> items;
        const QJsonArray serializedItems = root.value(QStringLiteral("items")).toArray();
        items.reserve(serializedItems.size());
        for (const QJsonValue& value : serializedItems) {
            const QJsonObject row = value.toObject();
            CalendarItem item;
            item.meta = previewFromJson(row.value(QStringLiteral("meta")).toObject());
            item.video = videoFromJson(row.value(QStringLiteral("video")).toObject());
            if (item.meta.id.isEmpty() || !item.video.released.isValid()) {
                continue;
            }
            items.push_back(item);
        }

        emit calendarReady(items);
        emit calendarGroupedReady(buildDayGroups(items, m_nowUtc.date()));
        return true;
    }

    void saveCache(const QList<CalendarItem>& items) const
    {
        QJsonObject root;
        root[QStringLiteral("version")] = kSchemaVersion;
        root[QStringLiteral("generatedAtMs")] = m_nowUtc.toMSecsSinceEpoch();
        root[QStringLiteral("signature")] = m_cacheSignature;

        QJsonArray rows;
        rows.reserve(items.size());
        for (const CalendarItem& item : items) {
            QJsonObject row;
            row[QStringLiteral("meta")] = previewToJson(item.meta);
            row[QStringLiteral("video")] = videoToJson(item.video);
            rows.push_back(row);
        }
        root[QStringLiteral("items")] = rows;

        QSaveFile out(cachePath(m_dataDir));
        if (!out.open(QIODevice::WriteOnly)) {
            return;
        }
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.commit();
    }

    static QList<CalendarDayGroup> buildDayGroups(const QList<CalendarItem>& sortedItems,
                                                  const QDate& today)
    {
        QMap<QDate, QList<CalendarItem>> byDay;
        for (const CalendarItem& item : sortedItems) {
            const QDate d = item.video.released.toUTC().date();
            if (!d.isValid()) {
                continue;
            }
            byDay[d].push_back(item);
        }

        QList<CalendarDayGroup> out;
        out.reserve(byDay.size());
        for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
            CalendarDayGroup group;
            group.day = it.key();
            group.bucket = classifyBucket(group.day, today);
            group.items = it.value();
            out.push_back(group);
        }
        return out;
    }

    void dispatchMetaFetch(const StreamLibraryEntry& librarySeries,
                           const tankostream::addon::AddonDescriptor& addon)
    {
        tankostream::addon::ResourceRequest req;
        req.resource = QStringLiteral("meta");
        req.type = QStringLiteral("series");
        req.id = librarySeries.imdb;

        auto* worker = new tankostream::addon::AddonTransport(this);
        auto handled = std::make_shared<bool>(false);
        auto readyConn = std::make_shared<QMetaObject::Connection>();
        auto failConn = std::make_shared<QMetaObject::Connection>();
        ++m_pendingResponses;

        const QString addonId = addon.manifest.id;
        const StreamLibraryEntry series = librarySeries;

        *readyConn = connect(worker, &tankostream::addon::AddonTransport::resourceReady, this,
            [this, req, addonId, series, handled, readyConn, failConn, worker](
                const tankostream::addon::ResourceRequest& incoming,
                const QJsonObject& payload) {
                if (*handled || !sameRequest(req, incoming)) {
                    return;
                }
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();
                onMetaReady(addonId, series, payload);
            });

        *failConn = connect(worker, &tankostream::addon::AddonTransport::resourceFailed, this,
            [this, req, addonId, handled, readyConn, failConn, worker](
                const tankostream::addon::ResourceRequest& incoming,
                const QString& message) {
                if (*handled || !sameRequest(req, incoming)) {
                    return;
                }
                *handled = true;
                QObject::disconnect(*readyConn);
                QObject::disconnect(*failConn);
                worker->deleteLater();
                onMetaFailed(addonId, message);
            });

        worker->fetchResource(addon.transportUrl, req);
    }

    void onMetaReady(const QString& addonId,
                     const StreamLibraryEntry& librarySeries,
                     const QJsonObject& payload)
    {
        const QJsonObject metaObj = payload.value(QStringLiteral("meta")).toObject();
        if (metaObj.isEmpty()) {
            completeOne();
            return;
        }

        tankostream::addon::MetaItemPreview preview = parseMetaPreview(metaObj, librarySeries);
        QList<tankostream::addon::Video> videos =
            parseVideos(metaObj.value(QStringLiteral("videos")).toArray());

        for (const auto& video : videos) {
            if (!releasedWithinWindow(video.released, m_nowUtc, m_untilUtc)) {
                continue;
            }
            CalendarItem item;
            item.meta = preview;
            item.video = video;

            const QString key = itemIdentityKey(item);
            if (m_seenKeys.contains(key)) {
                continue;
            }
            m_seenKeys.insert(key);
            m_items.push_back(item);
        }

        Q_UNUSED(addonId);
        completeOne();
    }

    void onMetaFailed(const QString& addonId, const QString& message)
    {
        emit calendarAddonError(addonId, message);
        if (m_firstError.isEmpty()) {
            m_firstError = QStringLiteral("[%1] %2").arg(addonId, message);
        }
        completeOne();
    }

    void completeOne()
    {
        --m_pendingResponses;
        if (m_pendingResponses > 0) {
            return;
        }

        std::sort(m_items.begin(), m_items.end(), [](const CalendarItem& a, const CalendarItem& b) {
            const QDateTime ar = a.video.released.toUTC();
            const QDateTime br = b.video.released.toUTC();
            if (ar != br) {
                return ar < br;
            }
            if (a.meta.name != b.meta.name) {
                return a.meta.name < b.meta.name;
            }
            return a.video.title < b.video.title;
        });

        saveCache(m_items);

        if (m_items.isEmpty() && !m_firstError.isEmpty()) {
            emit calendarError(m_firstError);
        }
        emit calendarReady(m_items);
        emit calendarGroupedReady(buildDayGroups(m_items, m_nowUtc.date()));
    }

    void resetTransient()
    {
        m_series.clear();
        m_cacheSignature.clear();
        m_seenKeys.clear();
        m_items.clear();
        m_pendingResponses = 0;
        m_firstError.clear();
        m_nowUtc = {};
        m_untilUtc = {};
    }

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    StreamLibrary* m_library = nullptr;
    QString m_dataDir;

    QList<StreamLibraryEntry> m_series;
    QString m_cacheSignature;
    QSet<QString> m_seenKeys;
    QList<CalendarItem> m_items;
    int m_pendingResponses = 0;
    QString m_firstError;
    QDateTime m_nowUtc;
    QDateTime m_untilUtc;
};

} // namespace tankostream::stream

// -----------------------------------------------------------------
// StreamPage integration sketch (Batch 6.1 handoff target for 6.2)
// -----------------------------------------------------------------
//
// New includes:
//   #include "core/stream/CalendarEngine.h"
//
// New members in StreamPage.h:
//   tankostream::stream::CalendarEngine* m_calendarEngine = nullptr;
//
// Constructor wiring in StreamPage.cpp (near MetaAggregator construction):
//   m_calendarEngine = new tankostream::stream::CalendarEngine(
//       m_addonRegistry, m_library, m_bridge->dataDir(), this);
//
// 6.2 uses this engine:
//   connect(m_calendarEngine, &CalendarEngine::calendarReady, m_calendarScreen,
//           &CalendarScreen::setItems);
//   connect(m_calendarEngine, &CalendarEngine::calendarGroupedReady, m_calendarScreen,
//           &CalendarScreen::setGroupedItems);
//
// Trigger load on calendar entry:
//   m_calendarEngine->loadUpcoming();
//
// Notes:
// - Keeps fan-out pattern close to CatalogAggregator/MetaAggregator:
//   per-request AddonTransport worker, sameRequest guard, partial failure tolerance.
// - Cache file path matches TODO requirement exactly:
//   {AppData}/stream_calendar_cache.json with 12h TTL.

