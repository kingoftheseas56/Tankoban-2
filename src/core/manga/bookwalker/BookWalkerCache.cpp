#include "BookWalkerCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>

namespace tankoban::manga::bookwalker {

QString BookWalkerCache::cacheFilePath(int anilistId)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QStringLiteral("%1/cache/bookwalker_covers/%2.json").arg(root).arg(anilistId);
}

std::optional<BookWalkerCacheRecord> BookWalkerCache::load(int anilistId,
                                                          int currentCanonicalCount,
                                                          qint64 ttlSeconds)
{
    const QString path = cacheFilePath(anilistId);
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1) return std::nullopt;

    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::fromString(obj.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!rec.fetchedAt.isValid()) return std::nullopt;
    rec.canonicalCount = obj.value(QStringLiteral("canonicalCount")).toInt();
    rec.bookwalkerSeriesId = obj.value(QStringLiteral("bookwalkerSeriesId")).toString();

    const qint64 ageSeconds = rec.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    if (ageSeconds > ttlSeconds) return std::nullopt;

    if (currentCanonicalCount > 0 && rec.canonicalCount != currentCanonicalCount) {
        return std::nullopt;
    }

    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        const QJsonObject vo = v.toObject();
        BookWalkerCoverEntry e;
        e.volume = vo.value(QStringLiteral("vol")).toInt();
        e.url = vo.value(QStringLiteral("url")).toString();
        if (e.volume > 0 && !e.url.isEmpty()) rec.volumes.append(e);
    }
    return rec;
}

bool BookWalkerCache::store(int anilistId, const BookWalkerCacheRecord& record)
{
    const QString path = cacheFilePath(anilistId);
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    obj.insert(QStringLiteral("fetchedAt"),
               (record.fetchedAt.isValid() ? record.fetchedAt : QDateTime::currentDateTimeUtc())
                   .toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("canonicalCount"), record.canonicalCount);
    obj.insert(QStringLiteral("bookwalkerSeriesId"), record.bookwalkerSeriesId);

    QJsonArray arr;
    for (const auto& e : record.volumes) {
        QJsonObject vo;
        vo.insert(QStringLiteral("vol"), e.volume);
        vo.insert(QStringLiteral("url"), e.url);
        arr.append(vo);
    }
    obj.insert(QStringLiteral("volumes"), arr);

    QSaveFile sf(path);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return sf.commit();
}

// --- seriesKey-keyed API (WEEBCENTRAL_IDENTITY_PIVOT Tasks 6+7) ---

QString BookWalkerCache::cacheFilePathByKey(const QString& seriesKey)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString safe = seriesKey;
    safe.replace(QChar(':'), QChar('_'));
    safe.replace(QChar('/'), QChar('_'));
    safe.replace(QChar('\\'), QChar('_'));
    return QStringLiteral("%1/cache/bookwalker_covers/%2.json").arg(root, safe);
}

std::optional<BookWalkerCacheRecord> BookWalkerCache::loadByKey(const QString& seriesKey,
                                                                int currentCanonicalCount,
                                                                qint64 ttlSeconds)
{
    const QString path = cacheFilePathByKey(seriesKey);
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1) return std::nullopt;

    BookWalkerCacheRecord rec;
    rec.schemaVersion = 1;
    rec.fetchedAt = QDateTime::fromString(obj.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!rec.fetchedAt.isValid()) return std::nullopt;
    rec.canonicalCount = obj.value(QStringLiteral("canonicalCount")).toInt();
    rec.bookwalkerSeriesId = obj.value(QStringLiteral("bookwalkerSeriesId")).toString();

    const qint64 ageSeconds = rec.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    if (ageSeconds > ttlSeconds) return std::nullopt;

    if (currentCanonicalCount > 0 && rec.canonicalCount != currentCanonicalCount) {
        return std::nullopt;
    }

    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        const QJsonObject vo = v.toObject();
        BookWalkerCoverEntry e;
        e.volume = vo.value(QStringLiteral("vol")).toInt();
        e.url = vo.value(QStringLiteral("url")).toString();
        if (e.volume > 0 && !e.url.isEmpty()) rec.volumes.append(e);
    }
    return rec;
}

bool BookWalkerCache::storeByKey(const QString& seriesKey, const BookWalkerCacheRecord& record)
{
    const QString path = cacheFilePathByKey(seriesKey);
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    obj.insert(QStringLiteral("fetchedAt"),
               (record.fetchedAt.isValid() ? record.fetchedAt : QDateTime::currentDateTimeUtc())
                   .toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("canonicalCount"), record.canonicalCount);
    obj.insert(QStringLiteral("bookwalkerSeriesId"), record.bookwalkerSeriesId);

    QJsonArray arr;
    for (const auto& e : record.volumes) {
        QJsonObject vo;
        vo.insert(QStringLiteral("vol"), e.volume);
        vo.insert(QStringLiteral("url"), e.url);
        arr.append(vo);
    }
    obj.insert(QStringLiteral("volumes"), arr);

    QSaveFile sf(path);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return sf.commit();
}

} // namespace tankoban::manga::bookwalker
