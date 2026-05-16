// src/core/manga/anilist/AniListCache.cpp
#include "AniListCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>

#include <algorithm>

namespace tankoban::manga::anilist {

namespace {

QJsonObject mediaPreviewToJson(const MediaPreview& p)
{
    QJsonObject o;
    o["anilistId"]       = p.anilistId;
    o["title"]           = p.title;
    QJsonArray alts;
    for (const auto& s : p.alternateTitles) alts.append(s);
    o["alternateTitles"] = alts;
    o["coverThumbUrl"]   = p.coverThumbUrl;
    o["coverFullUrl"]    = p.coverFullUrl;
    o["bannerUrl"]       = p.bannerUrl;
    o["format"]          = p.format;
    o["status"]          = p.status;
    o["yearStarted"]     = p.yearStarted;
    QJsonArray g;
    for (const auto& s : p.genres) g.append(s);
    o["genres"]          = g;
    o["description"]     = p.description;
    return o;
}

MediaPreview mediaPreviewFromJson(const QJsonObject& o)
{
    MediaPreview p;
    p.anilistId     = o.value("anilistId").toInt();
    p.title         = o.value("title").toString();
    for (const auto& v : o.value("alternateTitles").toArray()) p.alternateTitles.append(v.toString());
    p.coverThumbUrl = o.value("coverThumbUrl").toString();
    p.coverFullUrl  = o.value("coverFullUrl").toString();
    p.bannerUrl     = o.value("bannerUrl").toString();
    p.format        = o.value("format").toString();
    p.status        = o.value("status").toString();
    p.yearStarted   = o.value("yearStarted").toInt();
    for (const auto& v : o.value("genres").toArray()) p.genres.append(v.toString());
    p.description   = o.value("description").toString();
    return p;
}

QJsonObject mediaDetailToJson(const MediaDetail& d)
{
    QJsonObject o;
    o["preview"]       = mediaPreviewToJson(d.preview);
    o["totalChapters"] = d.totalChapters;
    o["totalVolumes"]  = d.totalVolumes;
    QJsonArray chapters;
    for (const auto& c : d.chapters) {
        QJsonObject co;
        co["number"]      = c.number;
        co["title"]       = c.title;
        co["boundVolume"] = c.boundVolume;
        chapters.append(co);
    }
    o["chapters"]      = chapters;
    QJsonArray volArt;
    for (const auto& a : d.volumeArt) {
        QJsonObject ao;
        ao["thumbnailUrl"] = a.thumbnailUrl;
        ao["fullUrl"]      = a.fullUrl;
        volArt.append(ao);
    }
    o["volumeArt"]     = volArt;
    o["fetchedAtMs"]   = static_cast<qint64>(d.fetchedAtMs);
    return o;
}

MediaDetail mediaDetailFromJson(const QJsonObject& o)
{
    MediaDetail d;
    d.preview       = mediaPreviewFromJson(o.value("preview").toObject());
    d.totalChapters = o.value("totalChapters").toInt();
    d.totalVolumes  = o.value("totalVolumes").toInt();
    for (const auto& v : o.value("chapters").toArray()) {
        const QJsonObject co = v.toObject();
        AniListChapter c;
        c.number      = co.value("number").toString();
        c.title       = co.value("title").toString();
        c.boundVolume = co.value("boundVolume").toInt(-1);
        d.chapters.append(c);
    }
    for (const auto& v : o.value("volumeArt").toArray()) {
        const QJsonObject ao = v.toObject();
        AniListVolumeArt a;
        a.thumbnailUrl = ao.value("thumbnailUrl").toString();
        a.fullUrl      = ao.value("fullUrl").toString();
        d.volumeArt.append(a);
    }
    d.fetchedAtMs   = static_cast<qint64>(o.value("fetchedAtMs").toVariant().toLongLong());
    return d;
}

} // anonymous namespace

AniListCache::AniListCache(const QString& cacheDir, QObject* parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);
    loadFromDisk();
}

AniListCache::~AniListCache() = default;

QString AniListCache::seriesFilePath(int anilistId) const
{
    return m_cacheDir + QStringLiteral("/series_%1.json").arg(anilistId);
}

QString AniListCache::bookmarksFilePath() const
{
    return m_cacheDir + QStringLiteral("/_bookmarks.json");
}

QString AniListCache::indexFilePath() const
{
    return m_cacheDir + QStringLiteral("/_index.json");
}

void AniListCache::loadFromDisk()
{
    // Constructor-only call site in v1; the lock is held for symmetry with
    // any future re-load helper that runs concurrently with reader threads.
    QMutexLocker lk(&m_mutex);

    // Bookmarks first (small file).
    QFile bf(bookmarksFilePath());
    if (bf.exists() && bf.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(bf.readAll()).array();
        for (const auto& v : arr) m_bookmarks.insert(v.toInt());
    }

    // Series detail files. One JSON file per series.
    QDir dir(m_cacheDir);
    const auto entries = dir.entryList(QStringList{ QStringLiteral("series_*.json") },
                                       QDir::Files);
    for (const auto& filename : entries) {
        QFile f(dir.absoluteFilePath(filename));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) continue;
        const MediaDetail d = mediaDetailFromJson(doc.object());
        if (d.preview.anilistId > 0) {
            m_byId.insert(d.preview.anilistId, d);
        }
    }
}

void AniListCache::saveSeriesToDisk(const MediaDetail& d) const
{
    QSaveFile f(seriesFilePath(d.preview.anilistId));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(mediaDetailToJson(d)).toJson(QJsonDocument::Indented));
    f.commit();
}

void AniListCache::saveBookmarksToDisk() const
{
    // Sort for deterministic on-disk ordering. QSet<int> iteration order is
    // unspecified; sorting avoids per-session diff noise in _bookmarks.json.
    QList<int> sortedIds(m_bookmarks.begin(), m_bookmarks.end());
    std::sort(sortedIds.begin(), sortedIds.end());
    QJsonArray arr;
    for (int id : sortedIds) arr.append(id);
    QSaveFile f(bookmarksFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

std::optional<MediaDetail> AniListCache::get(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(anilistId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

bool AniListCache::isBookmarked(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    return m_bookmarks.contains(anilistId);
}

QSet<int> AniListCache::bookmarkedIds() const
{
    QMutexLocker lk(&m_mutex);
    return m_bookmarks;
}

QList<MediaPreview> AniListCache::bookmarkedPreviews() const
{
    QMutexLocker lk(&m_mutex);
    QList<MediaPreview> out;
    for (int id : m_bookmarks) {
        const auto it = m_byId.constFind(id);
        if (it != m_byId.constEnd()) out.append(it.value().preview);
    }
    return out;
}

void AniListCache::put(const MediaDetail& detail)
{
    if (detail.preview.anilistId <= 0) return;
    MediaDetail toSave;
    {
        QMutexLocker lk(&m_mutex);
        m_byId.insert(detail.preview.anilistId, detail);
        toSave = detail;
    }
    saveSeriesToDisk(toSave);
    emit cacheChanged(detail.preview.anilistId);
}

void AniListCache::addBookmark(int anilistId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_bookmarks.contains(anilistId)) {
            m_bookmarks.insert(anilistId);
            changed = true;
        }
    }
    if (changed) {
        QMutexLocker lk(&m_mutex);
        saveBookmarksToDisk();
    }
    if (changed) emit bookmarksChanged();
}

void AniListCache::removeBookmark(int anilistId)
{
    bool changed = false;
    {
        QMutexLocker lk(&m_mutex);
        if (m_bookmarks.remove(anilistId)) changed = true;
    }
    if (changed) {
        QMutexLocker lk(&m_mutex);
        saveBookmarksToDisk();
    }
    if (changed) emit bookmarksChanged();
}

bool AniListCache::isFresh(int anilistId, qint64 maxAgeMs) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(anilistId);
    if (it == m_byId.constEnd()) return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - it->fetchedAtMs) < maxAgeMs;
}

} // namespace tankoban::manga::anilist
