// src/core/manga/fandom/LocalFandomCatalogIndex.h
//
// In-memory index of locally-scraped Fandom catalogs keyed by AniList ID.
// Scans <data-dir>/*.json at refresh() time, extracts seriesId + anilistId
// from each file's root, and builds two maps:
//   anilistId  -> seriesId slug
//   seriesId   -> absolute file path
//
// Consumed by ComicsPage to answer "does this AniList id have a local catalog,
// and where's the file?" before falling through to the live HTTP fallback chain.
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
    QString filePathForSlug(const QString& seriesId) const;

    int size() const { return m_anilistToSlug.size(); }

private:
    QString                 m_dataDir;
    QHash<int, QString>     m_anilistToSlug;
    QHash<QString, QString> m_slugToPath;
};

} // namespace tankoban::manga::fandom
