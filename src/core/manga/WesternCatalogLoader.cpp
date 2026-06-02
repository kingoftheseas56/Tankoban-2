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

MangaCatalog WesternCatalogLoader::loadFromJsonObject(const QJsonObject& obj)
{
    const QString seriesId = obj.value(QStringLiteral("seriesId")).toString();
    if (seriesId.isEmpty()) return {};

    // NOTE: empty `editions` is VALID, not a reject. A live-searched series whose
    // RCO page lists only single issues (the marquee titles — Saga/Hellboy/etc.,
    // whose collected editions live under sibling slugs) yields zero collected
    // editions, but still has a synopsis/cover worth showing. Rejecting here made
    // the pick handler silently swallow it and hang on "Loading" (2026-06-02).
    // The series page renders with an empty-editions state instead. (spec §8.)
    const QJsonArray editionsArr = obj.value(QStringLiteral("editions")).toArray();

    MangaCatalog cat;
    cat.seriesId       = seriesId;
    cat.seriesTitle    = obj.value(QStringLiteral("seriesTitle")).toString();
    cat.source         = obj.value(QStringLiteral("source")).toString();
    cat.seriesSynopsis = obj.value(QStringLiteral("synopsis")).toString();
    cat.status         = obj.value(QStringLiteral("status")).toString();
    cat.author         = obj.value(QStringLiteral("author")).toString();
    cat.studio         = obj.value(QStringLiteral("publisher")).toString();  // publisher reuses 'studio' slot
    cat.publishedYearStart = obj.value(QStringLiteral("yearStart")).toInt();
    cat.publishedYearEnd   = obj.value(QStringLiteral("yearEnd")).toInt();
    const QJsonArray genresArr = obj.value(QStringLiteral("genres")).toArray();
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
    QString seriesCover = obj.value(QStringLiteral("seriesCover")).toString().trimmed();
    if (!seriesCover.isEmpty() && seriesCover.startsWith(QLatin1Char('/'))) {
        seriesCover = QStringLiteral("https://rcostation.xyz") + seriesCover;
    }
    // Stash the (absolutised) series-level cover so it survives an empty editions
    // list — an editionless live series has no volume to carry it onto, and the
    // hero band + grid tile read this field directly. (2026-06-02.)
    cat.seriesCover = seriesCover;

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

std::optional<MangaCatalog> WesternCatalogLoader::loadFromFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo("WesternCatalogLoader::loadFromFile: open failed for %s", qUtf8Printable(filePath));
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    MangaCatalog cat = loadFromJsonObject(doc.object());
    if (cat.seriesId.isEmpty()) return std::nullopt;
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
