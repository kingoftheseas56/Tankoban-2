// src/core/manga/WesternCatalogLoader.cpp
//
// See WesternCatalogLoader.h for the schema-mapping contract. Mirrors the
// structure of LocalMangaCatalogLoader but parses the Western `editions` schema
// and emits a MangaCatalog whose "volumes" are collected editions.

#include "WesternCatalogLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tankoban::manga {

namespace {

// RCO edition_classify tiers -> human format label for groupingLabel.
// Mirrors the tier ranks in scripts/comics_catalogue/edition_classify.py.
// Tier 99 (single issue / unknown) is excluded by the harvester before emit,
// so it should not appear here; map it to an empty label defensively.
QString tierLabel(int tier) {
    switch (tier) {
        case 0: return QStringLiteral("Compendium");
        case 1: return QStringLiteral("Omnibus");
        case 2: return QStringLiteral("TPB");
        case 3: return QStringLiteral("Deluxe");
        case 4: return QStringLiteral("Vol");
        default: return QString();
    }
}

} // namespace

std::optional<MangaCatalog> WesternCatalogLoader::loadFromFile(const QString& filePath) {
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

    const QString seriesId = root.value(QStringLiteral("seriesId")).toString();
    if (seriesId.isEmpty()) return std::nullopt;

    const QJsonArray editionsArr = root.value(QStringLiteral("editions")).toArray();
    if (editionsArr.isEmpty()) return std::nullopt;

    MangaCatalog cat;
    cat.seriesId       = seriesId;
    cat.seriesTitle    = root.value(QStringLiteral("seriesTitle")).toString();
    cat.source         = root.value(QStringLiteral("source")).toString();
    cat.seriesSynopsis = root.value(QStringLiteral("synopsis")).toString();
    cat.status         = root.value(QStringLiteral("status")).toString();
    cat.author         = root.value(QStringLiteral("author")).toString();
    cat.studio         = root.value(QStringLiteral("publisher")).toString();  // publisher reuses 'studio' slot
    cat.publishedYearStart = root.value(QStringLiteral("yearStart")).toInt();
    cat.publishedYearEnd   = root.value(QStringLiteral("yearEnd")).toInt();
    const QJsonArray genresArr = root.value(QStringLiteral("genres")).toArray();
    cat.genres.clear();
    for (const auto& g : genresArr) {
        const QString s = g.toString().trimmed();
        if (!s.isEmpty()) cat.genres.append(s);
    }
    cat.schemaVersion  = kMangaCatalogSchemaVersion;

    // Cover-tolerant: one shared series hero cover painted on every edition tile
    // (per-edition covers are a follow-on). Absent/empty => VolumeTile renders a
    // text tile. The harvester emits seriesCover in a parallel change; we accept
    // either an absolute URL (pass through) or a host-relative /Uploads/... path
    // (absolutise against the RCO host so VolumeTile can fetch it).
    QString seriesCover = root.value(QStringLiteral("seriesCover")).toString().trimmed();
    if (!seriesCover.isEmpty() && seriesCover.startsWith(QLatin1Char('/'))) {
        seriesCover = QStringLiteral("https://rcostation.xyz") + seriesCover;
    }

    cat.volumes.reserve(editionsArr.size());
    int ordinal = 1;
    for (const auto& v : editionsArr) {
        const QJsonObject eo = v.toObject();
        MangaVolume vol;
        // Stable display ordinal in record order; the harvester is the source of
        // truth for edition ordering (tier-first). The human edition number lives
        // in the label ("TPB 25", "Compendium One").
        vol.volumeNumber     = ordinal++;
        vol.titleEnglish     = eo.value(QStringLiteral("label")).toString();
        vol.groupingLabel    = tierLabel(eo.value(QStringLiteral("formatTier")).toInt(99));
        vol.sourceHref       = eo.value(QStringLiteral("href")).toString();
        vol.coverUrlJapanese = seriesCover;  // shared hero cover (may be empty)
        cat.volumes.append(std::move(vol));
    }

    return cat;
}

QString WesternCatalogLoader::canonicalDataDir() {
    // Dev-mode launch: Tankoban.exe lives at <repo-root>/out/Tankoban.exe;
    // the catalogue lives at <repo-root>/data/western_catalogue/. Same
    // derivation as LocalMangaCatalogLoader::canonicalDataDir().
    const QString appDir = QCoreApplication::applicationDirPath();
    QDir up(appDir);
    up.cdUp();
    const QString candidate = up.absoluteFilePath("data/western_catalogue");
    if (QFileInfo(appDir).fileName().startsWith(
            QStringLiteral("out"), Qt::CaseInsensitive)) {
        return candidate;
    }
    if (QFileInfo(candidate).isDir()) return candidate;

    // Fallback for tests or alternate launch paths: cwd-relative.
    return QDir::current().absoluteFilePath("data/western_catalogue");
}

} // namespace tankoban::manga
