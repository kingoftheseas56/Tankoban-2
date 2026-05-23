// src/core/manga/fandom/LocalFandomCatalogLoader.h
//
// Pure-function loader for pre-scraped Fandom catalog JSON files produced
// by the Layer-1 Python scraper (scripts/fandom_scraper/discover_volume_urls.py
// + scripts/fandom_scraper/backfill_covers.py, shipped 2026-05-21).
//
// Files live at <repo-root>/data/fandom_catalog/<seriesId>.json. The on-disk
// schema is the scraper's emit shape; this loader maps it to the existing
// tankoban::manga::fandom::FandomCatalog struct so the rest of the pipeline
// (ComicsSeriesView::populateVolumeRowsFromFandom et al) consumes a uniform
// type regardless of whether the catalog came from live HTTP or local JSON.
//
// Schema mapping (Layer-1 JSON -> FandomCatalog):
//   seriesId               -> catalog.seriesId
//   anilistId              -> catalog.anilistId (0 when absent)
//   fandomUrl              -> split into catalog.fandomWikiId (subdomain) +
//                            catalog.fandomVolumePath (path with leading /)
//   scrapedAt              -> catalog.fetchedAt (ISO 8601 UTC)
//   volumes[].number       -> vol.volumeNumber
//   volumes[].title        -> vol.titleEnglish (scraper picks Viz English title
//                            when available; romaji fallback when not)
//   volumes[].synopsis     -> vol.synopsis
//   volumes[].coverUrl     -> vol.coverUrlJapanese (Fandom's CDN serves JP-edition
//                            covers; the field name is historical, not load-bearing)
//   volumes[].chapterStart -> vol.chapterRangeStart (int; "12.5" truncates to 12)
//   volumes[].chapterEnd   -> vol.chapterRangeEnd (int; same truncation rule)
//
// Fields the JSON doesn't carry (left empty on the FandomVolume):
//   titleJapanese, titleRomaji, releaseDateJp, releaseDateEn, isbnJp, isbnEn,
//   coverUrlEnglish, groupingLabel.
//
// Spec: docs/superpowers/plans/2026-05-21-fandom-catalog-local-loader-integration.md

#pragma once

#include "FandomTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga::fandom {

class LocalFandomCatalogLoader
{
public:
    // Load a single JSON file at the given absolute or relative path.
    // Returns nullopt when:
    //   - file missing / unreadable
    //   - JSON parse failure
    //   - top-level "seriesId" is missing or empty
    //   - top-level "volumes" array is missing or empty (a valid catalog
    //     must have at least one volume per FandomCatalog::isValid())
    // String-int conversions for chapterStart / chapterEnd use the integer
    // prefix on decimal or annotated chapter ids ("12.5" -> 12; "Extra 1" -> 0).
    static std::optional<FandomCatalog> loadFromFile(const QString& filePath);

    // Canonical data directory: <repo-root>/data/fandom_catalog/.
    // Repo root is derived from QCoreApplication::applicationDirPath() with
    // a fallback to the current working directory at startup (the dev-mode
    // launch path puts the binary one level below the repo root).
    static QString canonicalDataDir();
};

} // namespace tankoban::manga::fandom
