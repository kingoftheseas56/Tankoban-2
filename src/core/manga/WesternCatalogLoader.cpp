// src/core/manga/WesternCatalogLoader.cpp
//
// See WesternCatalogLoader.h for the schema-mapping contract. Mirrors the
// structure of LocalMangaCatalogLoader but parses the Western `editions` schema
// and emits a MangaCatalog whose "volumes" are collected editions.

#include "WesternCatalogLoader.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <vector>

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

    // ── schema-v3 GCD branch (COMICS_WESTERN_GCD 2026-06-05, Agent 1) ──────────
    // The GCD+Open-Library brain emits per-volume FORWARD editions, each with its
    // own ISBN / OL cover / year (vs the v2/rco shared-cover, position-ordinal,
    // reverse-ordered shape). Branch by source/schemaVersion; the v2/rco path
    // below stays intact for not-yet-regenerated series.
    const int schemaVer = obj.value(QStringLiteral("schemaVersion")).toInt();
    const bool isGcd = schemaVer >= 3
        || obj.value(QStringLiteral("source")).toString() == QLatin1String("gcd");
    if (isGcd) {
        // seriesCover is already an absolute OL URL (no host-relative rewrite).
        cat.seriesCover = obj.value(QStringLiteral("seriesCover")).toString().trimmed();
        cat.volumes.reserve(editionsArr.size());
        for (const auto& v : editionsArr) {
            const QJsonObject eo = v.toObject();
            MangaVolume vol;
            vol.volumeNumber  = eo.value(QStringLiteral("volumeNumber")).toInt();
            vol.titleEnglish  = eo.value(QStringLiteral("title")).toString();
            vol.isbnEn        = eo.value(QStringLiteral("isbn")).toString();
            vol.groupingLabel = tierLabel(eo.value(QStringLiteral("formatTier")).toInt(2));
            const int y = eo.value(QStringLiteral("year")).toInt();
            if (y > 0) vol.releaseDateEn = QDate(y, 1, 1);
            // Per-volume OL cover (may be empty -> VolumeTile renders a title-card).
            const QString cov = eo.value(QStringLiteral("coverUrl")).toString().trimmed();
            vol.coverUrlJapanese = cov;
            vol.coverUrlEdition  = cov;
            cat.volumes.append(std::move(vol));
        }
        return cat;
    }

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

    // ── reading-order normalisation (COMICS_WESTERN_ORDER 2026-06-06, Agent 1) ──
    // RCO lists editions newest-first and the harvester (WesternSeriesParse::
    // buildEditions) only tier-sorts them, so the raw `editions` array runs
    // TPB 25 -> TPB 1. Numbering the "Volume N" ordinal in record order made
    // Volume 1 = the FINALE (Hemanth flagged the Invincible page showing
    // "Volume 1 - TPB 25 The End of All Things Part Two"). Re-sort into forward
    // reading order by (formatTier, edition-number-in-label) before numbering:
    // the label carries the human edition number across all six RCO label shapes
    // ("TPB 25", "TPB Part 4", "Collection TPB 29", "Deluxe Edition 1 Part 2",
    // ...) so the FIRST integer in the label is the sort key. Number-less labels
    // (bare "TPB", "Compendium One") sort last within their tier and keep their
    // relative position via stable_sort. This is the single runtime load path for
    // both baked and live-searched catalogues, so it fixes every on-disk RCO file
    // at once with no data regen, and stays correct across future re-harvests.
    static const QRegularExpression kLeadingNum(QStringLiteral(R"(\d+)"));
    auto editionNumber = [](const QString& label) -> int {
        const auto m = kLeadingNum.match(label);
        return m.hasMatch() ? m.captured(0).toInt()
                            : std::numeric_limits<int>::max();
    };
    std::vector<QJsonObject> sortedEds;
    sortedEds.reserve(editionsArr.size());
    for (const auto& v : editionsArr) sortedEds.push_back(v.toObject());
    std::stable_sort(sortedEds.begin(), sortedEds.end(),
        [&](const QJsonObject& a, const QJsonObject& b) {
            const int ta = a.value(QStringLiteral("formatTier")).toInt(99);
            const int tb = b.value(QStringLiteral("formatTier")).toInt(99);
            if (ta != tb) return ta < tb;
            return editionNumber(a.value(QStringLiteral("label")).toString())
                 < editionNumber(b.value(QStringLiteral("label")).toString());
        });

    cat.volumes.reserve(sortedEds.size());
    int ordinal = 1;
    for (const auto& eo : sortedEds) {
        MangaVolume vol;
        // Forward display ordinal in reading order (see normalisation note above).
        // The human edition number still lives in the label ("TPB 1", "TPB 25").
        vol.volumeNumber     = ordinal++;
        vol.titleEnglish     = eo.value(QStringLiteral("label")).toString();
        vol.groupingLabel    = tierLabel(eo.value(QStringLiteral("formatTier")).toInt(99));
        vol.sourceHref       = eo.value(QStringLiteral("href")).toString();
        vol.coverUrlJapanese = seriesCover;  // shared hero cover (may be empty)
        // Per-edition cover (Task 5): harvester emits "cover" when the GetComics
        // post has its own cover image. Absolutise host-relative /... paths against
        // https://getcomics.org (same pattern as seriesCover above, different host).
        QString editionCover = eo.value(QStringLiteral("cover")).toString().trimmed();
        if (!editionCover.isEmpty() && editionCover.startsWith(QLatin1Char('/'))) {
            editionCover = QStringLiteral("https://getcomics.org") + editionCover;
        }
        vol.coverUrlEdition = editionCover;
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
