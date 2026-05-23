// src/core/manga/LocalMangaCatalogLoader.h
//
// Pure-function loader for pre-scraped MangaFire catalog JSON files produced
// by scripts/mangafire_scraper/mangafire_ingest.py (shipped Phase B.3).
//
// Files live at <repo-root>/data/mangafire_catalog/<seriesId>.json. The on-disk
// schema is the scraper's emit shape; this loader maps it to
// tankoban::manga::MangaCatalog so the rest of the pipeline
// (ComicsSeriesView::populateVolumeRowsFromCatalog et al) consumes a uniform
// type regardless of whether the catalog came from live HTTP or local JSON.
//
// Renamed from src/core/manga/fandom/LocalFandomCatalogLoader.h
// (Phase B.2, COMICS_MANGAFIRE_PIVOT 2026-05-23).
//
// Schema mapping (MangaFire JSON -> MangaCatalog):
//   seriesId               -> catalog.seriesId
//   seriesTitle            -> catalog.seriesTitle
//   seriesTitleAlt         -> catalog.seriesTitleAlt
//   anilistId              -> catalog.anilistId (0 when absent)
//   mangafireUrl           -> catalog.mangafireUrl
//   mangafireId            -> catalog.mangafireId
//   author                 -> catalog.author
//   studio                 -> catalog.studio
//   genres                 -> catalog.genres
//   status                 -> catalog.status
//   publishedYearStart     -> catalog.publishedYearStart
//   publishedYearEnd       -> catalog.publishedYearEnd (0 when null)
//   mangazine              -> catalog.mangazine
//   malScoreRaw            -> catalog.malScoreRaw
//   synopsis               -> catalog.seriesSynopsis
//   source                 -> catalog.source
//   notes                  -> catalog.notes
//   scrapedAt              -> catalog.fetchedAt (ISO 8601 UTC)
//   volumes[].number       -> vol.volumeNumber
//   volumes[].title        -> vol.titleEnglish
//   volumes[].synopsis     -> vol.synopsis
//   volumes[].coverUrl     -> vol.coverUrlJapanese (MangaFire CDN URL)
//   volumes[].chapterStart -> vol.chapterStartRaw (verbatim) + vol.chapterRangeStart (int, truncated)
//   volumes[].chapterEnd   -> vol.chapterEndRaw (verbatim) + vol.chapterRangeEnd (int, truncated)

#pragma once

#include "MangaCatalogTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga {

class LocalMangaCatalogLoader
{
public:
    // Load a single JSON file at the given absolute or relative path.
    // Returns nullopt when:
    //   - file missing / unreadable
    //   - JSON parse failure
    //   - top-level "seriesId" is missing or empty
    //   - top-level "volumes" array is missing or empty
    // String-int conversions for chapterStart / chapterEnd use the integer
    // prefix on decimal or annotated chapter ids ("12.5" -> 12; "0.01" -> 0;
    // "Extra 1" -> 0). Raw string is preserved in chapterStartRaw/chapterEndRaw.
    static std::optional<MangaCatalog> loadFromFile(const QString& filePath);

    // Canonical data directory: <repo-root>/data/mangafire_catalog/.
    // Repo root is derived from QCoreApplication::applicationDirPath() with
    // a fallback to the current working directory at startup.
    static QString canonicalDataDir();
};

} // namespace tankoban::manga
