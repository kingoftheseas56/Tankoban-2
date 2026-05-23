// src/core/manga/MangaCatalogTypes.h
//
// Source-agnostic catalog types for the MangaFire-backed local catalog.
// Renamed from src/core/manga/fandom/FandomTypes.h (Phase B.2,
// COMICS_MANGAFIRE_PIVOT 2026-05-23). The structs are shaped right; only
// the source-implementation names changed.
//
// JSON files live at <repo-root>/data/mangafire_catalog/<seriesId>.json.
// The on-disk schema is the MangaFire scraper's emit shape; LocalMangaCatalogLoader
// maps it to these structs so all downstream consumers (ComicsSeriesView,
// ComicsCatalogScreen, ComicsPage) use a uniform type.
//
// Consumers:
//   - LocalMangaCatalogLoader PRODUCES MangaVolume + MangaCatalog values.
//   - ComicsSeriesView::populateVolumeRowsFromCatalog CONSUMES MangaCatalog.
//   - ComicsCatalogScreen CONSUMES MangaCatalog for tile population.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace tankoban::manga {

// One volume's worth of normalized catalog data. Empty strings/dates mean
// "field not present" — distinct from "field exists but is empty".
struct MangaVolume {
    int          volumeNumber     = 0;
    QString      titleEnglish;        // empty by default (MangaFire doesn't carry these)
    QString      titleJapanese;       // empty
    QString      titleRomaji;         // empty
    int          chapterRangeStart = 0;
    int          chapterRangeEnd   = 0;
    QStringList  chapterList;         // optional richer per-chapter strings if scraped
    QDate        releaseDateJp;
    QDate        releaseDateEn;
    QString      isbnJp;
    QString      isbnEn;
    QString      synopsis;            // empty if not present
    QString      coverUrlEnglish;     // empty (MangaFire doesn't separate editions)
    QString      coverUrlJapanese;    // MangaFire's coverUrl maps here
    QString      groupingLabel;       // optional arc/era label
    // MangaFire-specific raw chapter strings (e.g. "0.01", "5.5") preserved verbatim.
    QString      chapterStartRaw;
    QString      chapterEndRaw;
};

// Schema version stamp. Bumping this constant invalidates all on-disk caches.
constexpr int kMangaCatalogSchemaVersion = 1;

// One series's worth of normalized catalog data.
struct MangaCatalog {
    QString             seriesId;
    QString             seriesTitle;
    QStringList         seriesTitleAlt;         // alternative titles array
    int                 anilistId     = 0;
    QString             wikidataQid;            // empty (legacy, unused in MangaFire path)
    QString             fandomWikiId;           // empty (legacy)
    QString             fandomVolumePath;       // empty (legacy)
    QString             seriesSynopsis;
    QList<MangaVolume>  volumes;
    QDateTime           fetchedAt;
    int                 schemaVersion = kMangaCatalogSchemaVersion;
    // MangaFire-specific fields (additive):
    QString             mangafireUrl;
    QString             mangafireId;
    QString             author;
    QString             studio;
    QStringList         genres;
    QString             status;                 // "RELEASING" | "FINISHED" | etc.
    int                 publishedYearStart = 0; // 0 if unknown
    int                 publishedYearEnd   = 0; // 0 if unknown or ongoing
    QString             mangazine;
    double              malScoreRaw = 0.0;      // 0.0 if unknown
    QString             source;                 // e.g. "mangafire_ingest.py v1"
    QString             notes;
    bool isValid() const { return !seriesId.isEmpty() && !volumes.isEmpty(); }
};

} // namespace tankoban::manga
