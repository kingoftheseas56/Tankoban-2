// src/core/manga/LocalMangaCatalogLoader.cpp
//
// Renamed + extended from src/core/manga/fandom/LocalFandomCatalogLoader.cpp
// (Phase B.2, COMICS_MANGAFIRE_PIVOT 2026-05-23).
// Deserializer extended to parse MangaFire's richer top-level schema fields
// and volume chapterStart/chapterEnd raw strings.

#include "LocalMangaCatalogLoader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace tankoban::manga {

namespace {

// "12.5" -> 12 ; "0.01" -> 0 ; "Extra 1" -> 0 ; "8" -> 8.
// Lenient by design: the MangaFire scraper preserves chapter ids verbatim
// (including half-chapters and bonus chapters), but MangaVolume uses int.
int parseChapterPrefix(const QString& raw) {
    bool ok = false;
    const int direct = raw.toInt(&ok);
    if (ok) return direct;

    // Try parsing as double first (handles "0.01", "5.5", "114.5")
    const double dval = raw.toDouble(&ok);
    if (ok) return static_cast<int>(dval);

    // Walk the leading digits manually for annotated ids like "Extra 1"
    QString prefix;
    for (QChar c : raw) {
        if (c.isDigit()) prefix.append(c);
        else break;
    }
    if (prefix.isEmpty()) return 0;
    return prefix.toInt();
}

} // namespace

std::optional<MangaCatalog> LocalMangaCatalogLoader::loadFromFile(const QString& filePath) {
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

    MangaCatalog cat;
    cat.seriesId      = seriesId;
    cat.seriesTitle   = root.value(QStringLiteral("seriesTitle")).toString();
    cat.anilistId     = root.value(QStringLiteral("anilistId")).toInt(0);
    cat.schemaVersion = kMangaCatalogSchemaVersion;

    // seriesTitleAlt — array of alternative titles
    const QJsonArray altArr = root.value(QStringLiteral("seriesTitleAlt")).toArray();
    for (const auto& v : altArr) {
        const QString alt = v.toString();
        if (!alt.isEmpty()) cat.seriesTitleAlt.append(alt);
    }

    // MangaFire-specific top-level fields
    cat.mangafireUrl  = root.value(QStringLiteral("mangafireUrl")).toString();
    cat.mangafireId   = root.value(QStringLiteral("mangafireId")).toString();
    cat.author        = root.value(QStringLiteral("author")).toString();
    cat.studio        = root.value(QStringLiteral("studio")).toString();

    const QJsonArray genresArr = root.value(QStringLiteral("genres")).toArray();
    for (const auto& g : genresArr) {
        const QString genre = g.toString();
        if (!genre.isEmpty()) cat.genres.append(genre);
    }

    cat.status             = root.value(QStringLiteral("status")).toString();
    cat.publishedYearStart = root.value(QStringLiteral("publishedYearStart")).toInt(0);
    // publishedYearEnd may be null in JSON (ongoing series) — toInt returns 0 on null
    cat.publishedYearEnd   = root.value(QStringLiteral("publishedYearEnd")).toInt(0);
    cat.mangazine          = root.value(QStringLiteral("mangazine")).toString();
    cat.malScoreRaw        = root.value(QStringLiteral("malScoreRaw")).toDouble(0.0);
    cat.seriesSynopsis     = root.value(QStringLiteral("synopsis")).toString();
    cat.source             = root.value(QStringLiteral("source")).toString();
    cat.notes              = root.value(QStringLiteral("notes")).toString();

    // scrapedAt -> fetchedAt (ISO 8601 UTC)
    const QString scrapedAt = root.value("scrapedAt").toString();
    if (!scrapedAt.isEmpty()) {
        cat.fetchedAt = QDateTime::fromString(scrapedAt, Qt::ISODate);
        if (!cat.fetchedAt.isValid()) {
            cat.fetchedAt = QDateTime();
        }
    }

    // Legacy fields left empty — not present in MangaFire JSON
    // cat.wikidataQid, cat.fandomWikiId, cat.fandomVolumePath stay default-empty

    cat.volumes.reserve(volumesArr.size());
    for (const auto& v : volumesArr) {
        const QJsonObject vo = v.toObject();
        MangaVolume vol;
        vol.volumeNumber     = vo.value("number").toInt();
        vol.titleEnglish     = vo.value("title").toString();
        vol.synopsis         = vo.value("synopsis").toString();
        // MangaFire's coverUrl maps to coverUrlJapanese; coverUrlEnglish stays empty
        vol.coverUrlJapanese = vo.value("coverUrl").toString();

        // chapterStart / chapterEnd: preserve raw string + parse to int
        const QString csRaw = vo.value("chapterStart").toString();
        const QString ceRaw = vo.value("chapterEnd").toString();
        vol.chapterStartRaw   = csRaw;
        vol.chapterEndRaw     = ceRaw;
        vol.chapterRangeStart = parseChapterPrefix(csRaw);
        vol.chapterRangeEnd   = parseChapterPrefix(ceRaw);

        cat.volumes.append(std::move(vol));
    }

    return cat;
}

QString LocalMangaCatalogLoader::canonicalDataDir() {
    // Dev-mode launch: Tankoban.exe lives at <repo-root>/out/Tankoban.exe;
    // the catalog lives at <repo-root>/data/mangafire_catalog/.
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir up(appDir);
    up.cdUp();
    const QString candidate = up.absoluteFilePath("data/mangafire_catalog");
    if (QFileInfo(candidate).isDir()) return candidate;

    // Fallback for tests or alternate launch paths: cwd-relative.
    return QDir::current().absoluteFilePath("data/mangafire_catalog");
}

} // namespace tankoban::manga
