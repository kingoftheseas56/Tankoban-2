// src/core/manga/fandom/LocalFandomCatalogIndex.h
//
// In-memory index of locally-scraped Fandom catalogs keyed by AniList ID
// AND by series title (fallback for entry paths where AniList id is unset,
// e.g. the WeebCentral search-result path that opens ComicsSeriesView with
// MangaResult identity instead of MediaPreview identity).
//
// Scans <data-dir>/*.json at refresh() time, extracts seriesId + anilistId
// + seriesTitle from each file's root, and builds three maps:
//   anilistId        -> seriesId slug
//   normalizedTitle  -> seriesId slug
//   seriesId         -> absolute file path
//
// Consumed by ComicsPage to answer "does this AniList id (or series title)
// have a local catalog, and where's the file?" before falling through to the
// live HTTP fallback chain.
//
// Title normalization: lowercase + non-alphanumeric runs collapsed to single
// hyphen + leading/trailing hyphens stripped. Examples:
//   "One Piece"                      -> "one-piece"
//   "Demon Slayer: Kimetsu no Yaiba" -> "demon-slayer-kimetsu-no-yaiba"
// Match works when the catalog JSON's seriesTitle normalizes to the same
// string as the input title.
//
// Spec: docs/superpowers/plans/2026-05-21-fandom-catalog-local-loader-integration.md

#pragma once

#include <QHash>
#include <QString>

namespace tankoban::manga::fandom {

class LocalFandomCatalogIndex
{
public:
    // dataDir defaults to LocalFandomCatalogLoader::canonicalDataDir() when
    // constructed via the no-arg path used in ComicsPage. Tests pass an
    // explicit temp dir.
    explicit LocalFandomCatalogIndex(const QString& dataDir = QString());

    // Scan dataDir for *.json; populate maps. Idempotent — safe to call
    // multiple times; each call rebuilds from scratch so files added or
    // removed since the last call are reflected.
    void refresh();

    // Lookup. Returns empty string when no match.
    QString slugForAnilistId(int anilistId) const;

    // Title-based fallback. Normalizes the input title the same way the
    // refresh-time index does, then looks up by normalized form. Empty
    // string on no match OR when input title is empty.
    QString slugForSeriesTitle(const QString& title) const;

    QString filePathForSlug(const QString& seriesId) const;

    int size() const { return m_anilistToSlug.size(); }

    // Public helper exposed for tests + ComicsPage call sites that want
    // to confirm what the normalized form of a candidate title looks like.
    static QString normalizeTitle(const QString& raw);

private:
    QString                 m_dataDir;
    QHash<int, QString>     m_anilistToSlug;
    QHash<QString, QString> m_titleToSlug;   // normalized seriesTitle -> seriesId slug
    QHash<QString, QString> m_slugToPath;
};

} // namespace tankoban::manga::fandom
