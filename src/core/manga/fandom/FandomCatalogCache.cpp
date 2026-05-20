// src/core/manga/fandom/FandomCatalogCache.cpp

#include "FandomCatalogCache.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcFandomCatalogCache, "tankoban.manga.fandom.cache")

namespace tankoban::manga::fandom {

namespace {

QString cacheDirPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("cache/fandom_catalogs"));
}

const char* pageModelToString(PageModel m)
{
    switch (m) {
        case PageModel::Monolith:       return "monolith";
        case PageModel::Hierarchy:      return "hierarchy";
        case PageModel::ChapterBranded: return "chapter-branded";
    }
    return "monolith";
}

PageModel pageModelFromString(const QString& s)
{
    if (s == QStringLiteral("hierarchy"))       return PageModel::Hierarchy;
    if (s == QStringLiteral("chapter-branded")) return PageModel::ChapterBranded;
    return PageModel::Monolith;
}

QJsonObject referenceToJson(const FandomReference& r)
{
    QJsonObject o;
    o["subdomain"]      = r.subdomain;
    o["volumePagePath"] = r.volumePagePath;
    o["pageModel"]      = QString::fromLatin1(pageModelToString(r.pageModel));
    return o;
}

FandomReference referenceFromJson(const QJsonObject& o)
{
    FandomReference r;
    r.subdomain      = o.value(QStringLiteral("subdomain")).toString();
    r.volumePagePath = o.value(QStringLiteral("volumePagePath")).toString();
    r.pageModel      = pageModelFromString(o.value(QStringLiteral("pageModel")).toString());
    return r;
}

QJsonObject volumeToJson(const FandomVolume& v)
{
    QJsonObject o;
    o["volumeNumber"]      = v.volumeNumber;
    o["titleEnglish"]      = v.titleEnglish;
    o["titleJapanese"]     = v.titleJapanese;
    o["titleRomaji"]       = v.titleRomaji;
    o["chapterRangeStart"] = v.chapterRangeStart;
    o["chapterRangeEnd"]   = v.chapterRangeEnd;
    if (!v.chapterList.isEmpty()) {
        QJsonArray chList;
        for (const auto& s : v.chapterList)
            chList.append(s);
        o["chapterList"] = chList;
    }
    if (v.releaseDateJp.isValid())
        o["releaseDateJp"] = v.releaseDateJp.toString(Qt::ISODate);
    if (v.releaseDateEn.isValid())
        o["releaseDateEn"] = v.releaseDateEn.toString(Qt::ISODate);
    o["isbnJp"]            = v.isbnJp;
    o["isbnEn"]            = v.isbnEn;
    o["synopsis"]          = v.synopsis;
    o["coverUrlEnglish"]   = v.coverUrlEnglish;
    o["coverUrlJapanese"]  = v.coverUrlJapanese;
    o["groupingLabel"]     = v.groupingLabel;
    return o;
}

FandomVolume volumeFromJson(const QJsonObject& o)
{
    FandomVolume v;
    v.volumeNumber      = o.value(QStringLiteral("volumeNumber")).toInt();
    v.titleEnglish      = o.value(QStringLiteral("titleEnglish")).toString();
    v.titleJapanese     = o.value(QStringLiteral("titleJapanese")).toString();
    v.titleRomaji       = o.value(QStringLiteral("titleRomaji")).toString();
    v.chapterRangeStart = o.value(QStringLiteral("chapterRangeStart")).toInt();
    v.chapterRangeEnd   = o.value(QStringLiteral("chapterRangeEnd")).toInt();
    const QJsonArray chList = o.value(QStringLiteral("chapterList")).toArray();
    for (const auto& val : chList)
        v.chapterList.append(val.toString());
    const QString jpStr = o.value(QStringLiteral("releaseDateJp")).toString();
    if (!jpStr.isEmpty())
        v.releaseDateJp = QDate::fromString(jpStr, Qt::ISODate);
    const QString enStr = o.value(QStringLiteral("releaseDateEn")).toString();
    if (!enStr.isEmpty())
        v.releaseDateEn = QDate::fromString(enStr, Qt::ISODate);
    v.isbnJp            = o.value(QStringLiteral("isbnJp")).toString();
    v.isbnEn            = o.value(QStringLiteral("isbnEn")).toString();
    v.synopsis          = o.value(QStringLiteral("synopsis")).toString();
    v.coverUrlEnglish   = o.value(QStringLiteral("coverUrlEnglish")).toString();
    v.coverUrlJapanese  = o.value(QStringLiteral("coverUrlJapanese")).toString();
    v.groupingLabel     = o.value(QStringLiteral("groupingLabel")).toString();
    return v;
}

} // anonymous

QJsonObject FandomCatalogCache::toJson(const FandomCatalog& c)
{
    QJsonObject o;
    o["schemaVersion"]    = c.schemaVersion;
    o["seriesId"]         = c.seriesId;
    o["wikidataQid"]      = c.wikidataQid;
    o["fandomWikiId"]     = c.fandomWikiId;
    o["fandomVolumePath"] = c.fandomVolumePath;
    o["seriesSynopsis"]   = c.seriesSynopsis;
    if (c.fetchedAt.isValid())
        o["fetchedAt"] = c.fetchedAt.toString(Qt::ISODateWithMs);

    QJsonArray volsArr;
    for (const auto& v : c.volumes)
        volsArr.append(volumeToJson(v));
    o["volumes"] = volsArr;
    return o;
}

FandomCatalog FandomCatalogCache::fromJson(const QJsonObject& obj)
{
    FandomCatalog c;
    c.schemaVersion    = obj.value(QStringLiteral("schemaVersion"))
                            .toInt(kFandomCatalogSchemaVersion);
    c.seriesId         = obj.value(QStringLiteral("seriesId")).toString();
    c.wikidataQid      = obj.value(QStringLiteral("wikidataQid")).toString();
    c.fandomWikiId     = obj.value(QStringLiteral("fandomWikiId")).toString();
    c.fandomVolumePath = obj.value(QStringLiteral("fandomVolumePath")).toString();
    c.seriesSynopsis   = obj.value(QStringLiteral("seriesSynopsis")).toString();
    const QString fetched = obj.value(QStringLiteral("fetchedAt")).toString();
    if (!fetched.isEmpty())
        c.fetchedAt = QDateTime::fromString(fetched, Qt::ISODateWithMs);

    const QJsonArray volsArr = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& val : volsArr)
        c.volumes.append(volumeFromJson(val.toObject()));
    return c;
}

QString FandomCatalogCache::cacheFilePath(const QString& qid)
{
    return QDir(cacheDirPath()).filePath(qid + QStringLiteral(".json"));
}

std::optional<FandomCatalog> FandomCatalogCache::loadByQid(
    const QString& qid,
    qint64 ttlSeconds)
{
    QFile f(cacheFilePath(qid));
    if (!f.exists())
        return std::nullopt;
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFandomCatalogCache) << "cannot read" << f.fileName();
        return std::nullopt;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcFandomCatalogCache) << "malformed cache JSON for" << qid
                                         << ":" << err.errorString();
        return std::nullopt;
    }

    FandomCatalog c = fromJson(doc.object());

    if (c.schemaVersion != kFandomCatalogSchemaVersion) {
        qCInfo(lcFandomCatalogCache) << "schema mismatch for" << qid
                                      << "(cached=" << c.schemaVersion
                                      << "current=" << kFandomCatalogSchemaVersion
                                      << ") — treating as miss";
        return std::nullopt;
    }

    if (c.fetchedAt.isValid()) {
        const qint64 ageSec = c.fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
        if (ageSec > ttlSeconds) {
            qCInfo(lcFandomCatalogCache) << "TTL expired for" << qid
                                          << "(age=" << ageSec << "s)";
            return std::nullopt;
        }
    }

    return c;
}

bool FandomCatalogCache::storeByQid(const QString& qid,
                                    const FandomCatalog& catalog)
{
    const QString dirPath = cacheDirPath();
    QDir dir;
    if (!dir.exists(dirPath) && !dir.mkpath(dirPath)) {
        qCWarning(lcFandomCatalogCache) << "cannot create cache dir" << dirPath;
        return false;
    }

    FandomCatalog stamped = catalog;
    if (!stamped.fetchedAt.isValid())
        stamped.fetchedAt = QDateTime::currentDateTimeUtc();
    if (stamped.schemaVersion == 0)
        stamped.schemaVersion = kFandomCatalogSchemaVersion;

    QSaveFile f(cacheFilePath(qid));
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcFandomCatalogCache) << "cannot open for write:" << f.fileName();
        return false;
    }
    const QJsonDocument doc(toJson(stamped));
    f.write(doc.toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qCWarning(lcFandomCatalogCache) << "commit failed for" << qid;
        return false;
    }

    qCInfo(lcFandomCatalogCache) << "stored" << qid
                                  << "with" << stamped.volumes.size() << "volumes";
    return true;
}

} // namespace tankoban::manga::fandom
