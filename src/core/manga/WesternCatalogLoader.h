// src/core/manga/WesternCatalogLoader.h
//
// Pure-function loader for Western-comics catalogue JSON produced by
// scripts/comics_catalogue/harvest.py (COMICS_WESTERN_CATALOGUE 2026-05-31,
// Agent 2 under Agent 1's domain ownership).
//
// Files live at <repo-root>/data/western_catalogue/<seriesId>.json. The on-disk
// schema differs from the manga/MangaFire schema (it carries `editions`, not
// `volumes`), so it gets its OWN loader — deliberately separate from
// LocalMangaCatalogLoader (one loader per on-disk schema; the manga loader stays
// untouched). It emits the SAME tankoban::manga::MangaCatalog, so every
// downstream consumer (ComicsSeriesView::populateVolumeRowsFromCatalog) renders
// Western series through the identical tile path — a collected edition simply
// IS a "volume".
//
// Schema mapping (Western JSON -> MangaCatalog):
//   seriesId              -> catalog.seriesId
//   seriesTitle           -> catalog.seriesTitle
//   source                -> catalog.source            ("rco")
//   seriesCover           -> EVERY vol.coverUrlJapanese (one shared hero cover;
//                            per-edition covers are a clean follow-on). Absent or
//                            empty => text tile. Cover-tolerant by design, so the
//                            harvester's parallel seriesCover addition flows in
//                            with no further code change here.
//   editions[].label      -> vol.titleEnglish
//   editions[].formatTier -> vol.groupingLabel via tier-name map
//                            (0 Compendium / 1 Omnibus / 2 TPB / 3 Deluxe / 4 Vol)
//   (position in record)  -> vol.volumeNumber (stable ordinal; the harvester is
//                            the source of truth for edition ordering)
//   editions[].href       -> vol.sourceHref (RCO item path; GetComics download
//                            path is Task 8)

#pragma once

#include "MangaCatalogTypes.h"

#include <QString>
#include <optional>

namespace tankoban::manga {

class WesternCatalogLoader
{
public:
    // Load a single Western base-record JSON. Returns nullopt when:
    //   - file missing / unreadable
    //   - JSON parse failure
    //   - top-level "seriesId" is missing or empty
    //   - top-level "editions" array is missing or empty
    static std::optional<MangaCatalog> loadFromFile(const QString& filePath);

    // Canonical data directory: <repo-root>/data/western_catalogue/.
    // Same repo-root derivation as LocalMangaCatalogLoader::canonicalDataDir().
    static QString canonicalDataDir();
};

} // namespace tankoban::manga
