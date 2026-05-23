// src/core/manga/LocalMangaCatalogIndex.h
//
// In-memory index of locally-scraped MangaFire catalogs keyed by AniList ID
// AND by series title (fallback for entry paths where AniList id is unset).
//
// Renamed from src/core/manga/fandom/LocalFandomCatalogIndex.h
// (Phase B.2, COMICS_MANGAFIRE_PIVOT 2026-05-23).
// Data directory changed from data/fandom_catalog/ to data/mangafire_catalog/.
//
// Scans <data-dir>/*.json at refresh() time, extracts seriesId + anilistId
// + seriesTitle from each file's root, and builds three maps:
//   anilistId        -> seriesId slug
//   normalizedTitle  -> seriesId slug
//   seriesId         -> absolute file path
//
// Consumed by ComicsPage to answer "does this AniList id (or series title)
// have a local catalog, and where's the file?"
//
// Title normalization: lowercase + non-alphanumeric runs collapsed to single
// hyphen + leading/trailing hyphens stripped. Examples:
//   "One Piece"                      -> "one-piece"
//   "Demon Slayer: Kimetsu no Yaiba" -> "demon-slayer-kimetsu-no-yaiba"

#pragma once

#include <QHash>
#include <QString>

namespace tankoban::manga {

class LocalMangaCatalogIndex
{
public:
    // dataDir defaults to LocalMangaCatalogLoader::canonicalDataDir() when
    // constructed via the no-arg path used in ComicsPage. Tests pass an
    // explicit temp dir.
    explicit LocalMangaCatalogIndex(const QString& dataDir = QString());

    // Scan dataDir for *.json; populate maps. Idempotent — safe to call
    // multiple times; each call rebuilds from scratch.
    void refresh();

    // Lookup. Returns empty string when no match.
    QString slugForAnilistId(int anilistId) const;

    // Title-based fallback. Normalizes the input title the same way the
    // refresh-time index does, then looks up by normalized form. Empty
    // string on no match OR when input title is empty.
    QString slugForSeriesTitle(const QString& title) const;

    QString filePathForSlug(const QString& seriesId) const;

    int size() const { return m_anilistToSlug.size(); }

    // Public helper exposed for tests + ComicsPage call sites.
    static QString normalizeTitle(const QString& raw);

private:
    QString                 m_dataDir;
    QHash<int, QString>     m_anilistToSlug;
    QHash<QString, QString> m_titleToSlug;   // normalized seriesTitle -> seriesId slug
    QHash<QString, QString> m_slugToPath;
};

} // namespace tankoban::manga
