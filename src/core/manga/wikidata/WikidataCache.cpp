// src/core/manga/wikidata/WikidataCache.cpp

#include "WikidataCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcWikidataCache, "tankoban.manga.wikidata.cache")

namespace tankoban::manga::wikidata {

namespace {

constexpr int kFileVersion = 1;

QString cacheDirPath()
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("cache"));
}

QJsonObject readWholeFile()
{
    QFile f(WikidataCache::cacheFilePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcWikidataCache) << "malformed cache file" << f.fileName()
                                    << ":" << err.errorString();
        return {};
    }
    return doc.object();
}

bool writeWholeFile(const QJsonObject& root)
{
    const QString dir = cacheDirPath();
    QDir d;
    if (!d.exists(dir) && !d.mkpath(dir)) {
        qCWarning(lcWikidataCache) << "cannot create cache dir" << dir;
        return false;
    }
    QSaveFile f(WikidataCache::cacheFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcWikidataCache) << "cannot open" << f.fileName() << "for write";
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qCWarning(lcWikidataCache) << "commit failed";
        return false;
    }
    return true;
}

} // anonymous

QString WikidataCache::cacheFilePath()
{
    return QDir(cacheDirPath()).filePath(QStringLiteral("wikidata_fandom_refs.json"));
}

std::optional<tankoban::manga::fandom::FandomReference>
WikidataCache::loadByQid(const QString& qid, qint64 ttlSeconds)
{
    const QJsonObject root = readWholeFile();
    if (root.isEmpty())
        return std::nullopt;

    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    if (!entries.contains(qid))
        return std::nullopt;

    const QJsonObject entry = entries.value(qid).toObject();
    const QString fetchedStr = entry.value(QStringLiteral("fetchedAt")).toString();
    if (!fetchedStr.isEmpty()) {
        const QDateTime fetched = QDateTime::fromString(fetchedStr, Qt::ISODateWithMs);
        if (fetched.isValid()) {
            const qint64 ageSec = fetched.secsTo(QDateTime::currentDateTimeUtc());
            if (ageSec > ttlSeconds) {
                qCInfo(lcWikidataCache) << "TTL expired for" << qid
                                         << "(age=" << ageSec << "s)";
                return std::nullopt;
            }
        }
    }

    tankoban::manga::fandom::FandomReference ref;
    ref.subdomain = entry.value(QStringLiteral("subdomain")).toString();
    // volumePagePath + pageModel intentionally left at defaults — WikidataClient
    // only resolves the subdomain; manifest layer populates the rest.
    if (ref.subdomain.isEmpty()) {
        qCInfo(lcWikidataCache) << "entry for" << qid
                                 << "has empty subdomain — treating as miss";
        return std::nullopt;
    }
    return ref;
}

bool WikidataCache::storeByQid(
    const QString& qid,
    const tankoban::manga::fandom::FandomReference& ref)
{
    QJsonObject root = readWholeFile();
    if (root.isEmpty()) {
        root[QStringLiteral("version")] = kFileVersion;
        root[QStringLiteral("entries")] = QJsonObject{};
    }

    QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    QJsonObject entry;
    entry[QStringLiteral("subdomain")] = ref.subdomain;
    entry[QStringLiteral("fetchedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entries[qid] = entry;
    root[QStringLiteral("entries")] = entries;

    if (!writeWholeFile(root))
        return false;

    qCInfo(lcWikidataCache) << "stored" << qid << "→" << ref.subdomain;
    return true;
}

} // namespace tankoban::manga::wikidata
