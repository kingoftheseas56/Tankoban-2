// src/core/manga/fandom/LocalFandomCatalogLoader.cpp
#include "LocalFandomCatalogLoader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

namespace tankoban::manga::fandom {

namespace {

// "12.5" -> 12 ; "Extra 1" -> 0 ; "8" -> 8.
// Lenient by design: the Python scraper preserves chapter ids verbatim
// (including half-chapters and bonus chapters), but FandomVolume uses int.
int parseChapterPrefix(const QString& raw) {
    bool ok = false;
    const int direct = raw.toInt(&ok);
    if (ok) return direct;

    QString prefix;
    for (QChar c : raw) {
        if (c.isDigit()) prefix.append(c);
        else break;
    }
    if (prefix.isEmpty()) return 0;
    return prefix.toInt();
}

// "https://onepiece.fandom.com/wiki/Chapters_and_Volumes/Volumes"
//   -> subdomain "onepiece" ; path "/wiki/Chapters_and_Volumes/Volumes"
struct UrlSplit { QString subdomain; QString path; };

UrlSplit splitFandomUrl(const QString& raw) {
    UrlSplit out;
    const QUrl url(raw);
    if (!url.isValid()) return out;

    const QString host = url.host();
    const int dotPos = host.indexOf('.');
    if (dotPos > 0) out.subdomain = host.left(dotPos);

    out.path = url.path();
    return out;
}

} // namespace

std::optional<FandomCatalog> LocalFandomCatalogLoader::loadFromFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = doc.object();

    const QString seriesId = root.value("seriesId").toString();
    if (seriesId.isEmpty()) return std::nullopt;

    const QJsonArray volumesArr = root.value("volumes").toArray();
    if (volumesArr.isEmpty()) return std::nullopt;

    FandomCatalog cat;
    cat.seriesId    = seriesId;
    cat.seriesTitle = root.value(QStringLiteral("seriesTitle")).toString();
    cat.schemaVersion = kFandomCatalogSchemaVersion;

    const QString fandomUrl = root.value("fandomUrl").toString();
    const UrlSplit split = splitFandomUrl(fandomUrl);
    cat.fandomWikiId     = split.subdomain;
    cat.fandomVolumePath = split.path;

    const QString scrapedAt = root.value("scrapedAt").toString();
    if (!scrapedAt.isEmpty()) {
        cat.fetchedAt = QDateTime::fromString(scrapedAt, Qt::ISODate);
        if (!cat.fetchedAt.isValid()) {
            // Tolerate scrapedAt absence; ISO parse failure leaves it default.
            cat.fetchedAt = QDateTime();
        }
    }

    // wikidataQid stays empty -- local-loaded catalogs are not Wikidata-keyed.
    // seriesSynopsis stays empty -- the scraper's JSON has no series-level synopsis.

    cat.volumes.reserve(volumesArr.size());
    for (const auto& v : volumesArr) {
        const QJsonObject vo = v.toObject();
        FandomVolume vol;
        vol.volumeNumber     = vo.value("number").toInt();
        vol.titleEnglish     = vo.value("title").toString();
        vol.synopsis         = vo.value("synopsis").toString();
        vol.coverUrlJapanese = vo.value("coverUrl").toString();
        vol.chapterRangeStart = parseChapterPrefix(vo.value("chapterStart").toString());
        vol.chapterRangeEnd   = parseChapterPrefix(vo.value("chapterEnd").toString());
        cat.volumes.append(std::move(vol));
    }

    return cat;
}

QString LocalFandomCatalogLoader::canonicalDataDir() {
    // Dev-mode launch: Tankoban.exe lives at <repo-root>/out/Tankoban.exe;
    // the catalog lives at <repo-root>/data/fandom_catalog/.
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir up(appDir);
    up.cdUp();
    const QString candidate = up.absoluteFilePath("data/fandom_catalog");
    if (QFileInfo(candidate).isDir()) return candidate;

    // Fallback for tests or alternate launch paths: cwd-relative.
    return QDir::current().absoluteFilePath("data/fandom_catalog");
}

} // namespace tankoban::manga::fandom
