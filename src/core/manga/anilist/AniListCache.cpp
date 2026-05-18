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

QString AniListCache::mangaUpdatesSidecarFilePath(int anilistId) const
{
    return m_cacheDir + QStringLiteral("/mangaupdates_%1.json").arg(anilistId);
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

    const auto sidecars = dir.entryList(QStringList{ QStringLiteral("mangaupdates_*.json") },
                                        QDir::Files);
    for (const auto& filename : sidecars) {
        const QString idText = filename.mid(13, filename.size() - 13 - 5);
        bool ok = false;
        const int anilistId = idText.toInt(&ok);
        if (!ok || anilistId <= 0) continue;

        QFile f(dir.absoluteFilePath(filename));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const auto o = doc.object();
        tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo info;
        info.seriesId = static_cast<qint64>(
            o.value(QStringLiteral("seriesId")).toVariant().toLongLong());
        info.title = o.value(QStringLiteral("title")).toString();
        info.rawStatus = o.value(QStringLiteral("rawStatus")).toString();
        info.volumeCount = o.value(QStringLiteral("volumeCount")).toInt();
        info.latestChapter = o.value(QStringLiteral("latestChapter")).toInt();
        info.completed = o.value(QStringLiteral("completed")).toBool();
        info.description = o.value(QStringLiteral("description")).toString();
        info.imageUrl = o.value(QStringLiteral("imageUrl")).toString();
        info.lastUpdated = QDateTime::fromString(
            o.value(QStringLiteral("lastUpdated")).toString(), Qt::ISODate);
        info.fetchedAtMs = static_cast<qint64>(
            o.value(QStringLiteral("fetchedAtMs")).toVariant().toLongLong());
        m_mangaUpdatesByAnilistId.insert(anilistId, info);
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

std::optional<tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo>
AniListCache::getMangaUpdatesSidecar(int anilistId) const
{
    QMutexLocker lk(&m_mutex);
    const auto it = m_mangaUpdatesByAnilistId.constFind(anilistId);
    if (it == m_mangaUpdatesByAnilistId.constEnd()) return std::nullopt;
    return it.value();
}

void AniListCache::putMangaUpdatesSidecar(
    int anilistId,
    const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info)
{
    if (anilistId <= 0) return;
    {
        QMutexLocker lk(&m_mutex);
        m_mangaUpdatesByAnilistId.insert(anilistId, info);
    }

    QJsonObject o;
    o.insert(QStringLiteral("seriesId"), QString::number(info.seriesId));
    o.insert(QStringLiteral("title"), info.title);
    o.insert(QStringLiteral("rawStatus"), info.rawStatus);
    o.insert(QStringLiteral("volumeCount"), info.volumeCount);
    o.insert(QStringLiteral("latestChapter"), info.latestChapter);
    o.insert(QStringLiteral("completed"), info.completed);
    o.insert(QStringLiteral("description"), info.description);
    o.insert(QStringLiteral("imageUrl"), info.imageUrl);
    o.insert(QStringLiteral("lastUpdated"), info.lastUpdated.toString(Qt::ISODate));
    o.insert(QStringLiteral("fetchedAtMs"), QString::number(info.fetchedAtMs));

    QSaveFile f(mangaUpdatesSidecarFilePath(anilistId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
    emit cacheChanged(anilistId);
}

QString AniListCache::japaneseTitleFor(int anilistId) const
{
    // Scan alternateTitles for the first entry containing CJK Unified
    // Ideographs (U+4E00..U+9FFF), Hiragana (U+3040..U+309F), or
    // Katakana (U+30A0..U+30FF). The native (Japanese) title is stored
    // in alternateTitles by AniListClient::collectAlternateTitles and is
    // typically the third entry (after english and romaji), but we detect
    // by script rather than by position to be robust.
    QMutexLocker lk(&m_mutex);
    const auto it = m_byId.constFind(anilistId);
    if (it == m_byId.constEnd()) return QString();
    for (const QString& alt : it->preview.alternateTitles) {
        for (const QChar& ch : alt) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) ||  // CJK Unified Ideographs
                (u >= 0x3040 && u <= 0x309F) ||  // Hiragana
                (u >= 0x30A0 && u <= 0x30FF)) {  // Katakana
                return alt;
            }
        }
    }
    // Fall-through: no CJK/Hiragana/Katakana found. Some manga have latin-script
    // official Japanese titles (Death Note, One Piece, Bleach, etc. — Shueisha
    // properties that publish under the english title verbatim in Japan). Return
    // the first non-empty alternateTitle so BookWalker search has something to
    // query with rather than short-circuiting to unresolved("no-japanese-title").
    for (const QString& alt : it->preview.alternateTitles) {
        if (!alt.isEmpty()) return alt;
    }
    return QString();
}

} // namespace tankoban::manga::anilist
