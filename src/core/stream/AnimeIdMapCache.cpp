#include "core/stream/AnimeIdMapCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

namespace tankostream::stream {

AnimeIdMapCache::AnimeIdMapCache(const QString& cacheDir) : m_cacheDir(cacheDir) {
    QMutexLocker locker(&m_mutex);
    loadFromDisk();
}

QString AnimeIdMapCache::filePath() const {
    return QDir(m_cacheDir).filePath(QStringLiteral("anime-id-map.json"));
}

void AnimeIdMapCache::loadFromDisk() {
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    m_map.loadFromJson(bytes);
}

std::optional<int> AnimeIdMapCache::kitsuIdForImdb(const QString& imdbId) const {
    QMutexLocker locker(&m_mutex);
    return m_map.kitsuIdForImdb(imdbId);
}

bool AnimeIdMapCache::isStale(qint64 maxAgeMs) const {
    const QFileInfo fi(filePath());
    if (!fi.exists()) {
        return true;
    }
    const qint64 ageMs = fi.lastModified().msecsTo(QDateTime::currentDateTime());
    return ageMs > maxAgeMs;
}

void AnimeIdMapCache::saveJson(const QByteArray& json) {
    QMutexLocker locker(&m_mutex);
    QDir().mkpath(m_cacheDir);
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(json);
        f.close();
    }
    m_map.loadFromJson(json);
}

int AnimeIdMapCache::size() const {
    QMutexLocker locker(&m_mutex);
    return m_map.size();
}

}  // namespace tankostream::stream
