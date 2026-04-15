#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

#include "addon/MetaItem.h"

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct StreamEpisode {
    int episode = 0;
    QString title;
    // Phase 3 Batch 3.4 — per-episode overview + thumbnail sourced from
    // MetaItem.videos[] by parseSeriesEpisodes. StreamDetailView renders
    // these as the second line + left-side image in each episode row.
    // Either may be empty when the addon's meta response omits them.
    QString overview;
    QUrl    thumbnail;
};

class MetaAggregator : public QObject
{
    Q_OBJECT

public:
    explicit MetaAggregator(tankostream::addon::AddonRegistry* registry,
                            QObject* parent = nullptr);

    void searchCatalog(const QString& query);
    void fetchSeriesMeta(const QString& imdbId);

    // Fetches the full MetaItem (preview + videos[] + trailer streams + links)
    // for any (imdbId, type) — used by StreamDetailView to light up richer
    // header + episode fields (cast, genres, runtime, episode thumbnails).
    // Best-effort async; emits metaItemReady on success, silent on failure
    // (the tile's own MetaItemPreview stays the fallback surface).
    // Type in {"movie", "series"}.
    void fetchMetaItem(const QString& imdbId, const QString& type);

    // Stateless title → poster-candidate lookup (HELP.md 2026-04-15 for
    // Agent 5 / Videos-mode folder-poster fetch). Reentrant — unlike
    // `searchCatalog`, each call allocates its own internal state, so
    // concurrent callers don't collide on `m_pendingSearch` /
    // `m_searchResults`.
    //
    // `typeFilter` restricts the query to a specific addon catalog type:
    //   "movie"  — only movie catalogs
    //   "series" — only series catalogs (Videos-mode TV-show folders)
    //   ""       — both types (unfiltered)
    //
    // `callback(results, error)` fires once after every eligible addon has
    // responded (success or timeout). `results` is deduped by
    // MetaItemPreview::id and sorted by name ascending. `error` is empty
    // on partial success; non-empty iff every addon failed AND results is
    // empty. QPointer-guard `this` in the callback if the MetaAggregator
    // outlives your target widget.
    using TitleSearchCallback = std::function<void(
        const QList<tankostream::addon::MetaItemPreview>& results,
        const QString&                                    error)>;
    void searchByTitle(const QString& query,
                       const QString& typeFilter,
                       TitleSearchCallback callback);

signals:
    void catalogResults(const QList<tankostream::addon::MetaItemPreview>& results);
    void catalogError(const QString& message);
    void seriesMetaReady(const QString& imdbId,
                         const QMap<int, QList<StreamEpisode>>& seasons);
    void seriesMetaError(const QString& imdbId, const QString& message);
    // Phase 3 will consume this in StreamDetailView to render hero image,
    // cast/director, runtime/genres, episode thumbnails. Phase 1 kicks it off
    // so by the time Phase 3 lands, the fetch has already populated the cache.
    void metaItemReady(const tankostream::addon::MetaItem& item);

private:
    struct PendingSearch {
        QString addonId;
        QUrl baseUrl;
        QString query;
        QString type;
        QString catalogId;   // matched catalog id within the addon (not hardcoded)
    };

    void dispatchSearch(const PendingSearch& pending);
    void finalizeSearch();
    void resetSearch();

    void dispatchSeriesMeta(const QUrl& baseUrl, const QString& addonId,
                            const QString& imdbId);
    void finalizeSeries(const QString& addonId, const QString& error);

    void dispatchMetaItemFetch(const QUrl& baseUrl,
                               const QString& imdbId,
                               const QString& type);

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    QList<tankostream::addon::MetaItemPreview> m_searchResults;
    QSet<QString> m_seenSearchIds;
    QString m_lastSearchError;
    int m_pendingSearch = 0;

    QString m_seriesPendingImdb;
    int m_seriesRemaining = 0;
    bool m_seriesResolved = false;
    QString m_lastSeriesError;
    QHash<QString, QPair<qint64, QMap<int, QList<StreamEpisode>>>> m_seriesCache;

    // fetchMetaItem cache keyed by imdbId. Short TTL — detail view reopens
    // are hot and the payload is small; a minute is enough to coalesce
    // repeat-open events without staleness.
    QHash<QString, QPair<qint64, tankostream::addon::MetaItem>> m_metaItemCache;
    QSet<QString> m_metaItemInFlight;

    static constexpr qint64 kSeriesCacheTtlMs = 24LL * 60LL * 60LL * 1000LL;
    static constexpr qint64 kMetaItemCacheTtlMs = 60LL * 1000LL;
};

}
