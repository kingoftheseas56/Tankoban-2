#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include "addon/Descriptor.h"
#include "addon/MetaItem.h"

class QJsonObject;

// StreamLibrary lives in the global namespace (see StreamLibrary.h). Declare it
// before the nested namespace so member `StreamLibrary* m_library` below resolves
// to the real class, not a phantom tankostream::stream::StreamLibrary.
class StreamLibrary;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

// Batch 6.1 (Tankostream Phase 6) — Calendar backend.
//
// For every series in the user's StreamLibrary, fans out a `meta` resource
// request to every enabled meta-capable addon, collects `MetaItem.videos[]`
// entries whose `released` falls within now..now+60d, dedupes across addon
// responses, and emits a sorted list of upcoming episodes.
//
// Cache: {dataDir}/stream_calendar_cache.json with 12h TTL plus a signature
// over the (library series-ids + addon-ids) set, so adding/removing a
// series or swapping a meta addon invalidates stale entries immediately.
struct CalendarItem
{
    tankostream::addon::MetaItemPreview meta;
    tankostream::addon::Video           video;
};

enum class CalendarBucket {
    ThisWeek,
    NextWeek,
    Later,
};

struct CalendarDayGroup
{
    QDate                 day;
    CalendarBucket        bucket = CalendarBucket::Later;
    QList<CalendarItem>   items;
};

class CalendarEngine : public QObject
{
    Q_OBJECT

public:
    explicit CalendarEngine(tankostream::addon::AddonRegistry* registry,
                            StreamLibrary*                     library,
                            const QString&                     dataDir,
                            QObject*                           parent = nullptr);

    // Fans out one `meta`/series request per (library-series × meta-capable
    // addon) pair. Fresh cache hit emits synchronously from within this call;
    // otherwise emits after all pending requests complete. No-op guards if
    // registry, library, or dataDir are unset.
    void loadUpcoming();

signals:
    // Spec signal per STREAM_PARITY_TODO.md:276 — flat, time-sorted list.
    void calendarReady(const QList<CalendarItem>& items);

    // Pre-grouped convenience for Batch 6.2 (This Week / Next Week / Later).
    // Derivable from calendarReady, but computing it in one place avoids the
    // 6.2 screen re-running the same QDate math on every refresh.
    void calendarGroupedReady(const QList<CalendarDayGroup>& groups);

    // Emitted when no data was produced AND at least one addon error was
    // collected — gives the 6.2 screen something to surface instead of an
    // empty grid. Partial success never fires this.
    void calendarError(const QString& message);

    // Per-addon failure ping for fine-grained UI toasting (parity with
    // SubtitlesAggregator::subtitlesError).
    void calendarAddonError(const QString& addonId, const QString& message);

private:
    void  dispatchMetaFetch(const QString&                                         seriesImdbId,
                            const tankostream::addon::AddonDescriptor&             addon,
                            quint64                                                gen);
    void  dispatchBatchedCatalogFetch(const tankostream::addon::AddonDescriptor&   addon,
                                      const QString&                               catalogId,
                                      const QString&                               extraPropName,
                                      const QList<QString>&                        imdbIds,
                                      quint64                                      gen);
    void  onMetaReady(quint64            gen,
                      const QString&      addonId,
                      const QString&      seriesImdbId,
                      const QJsonObject&  payload);
    void  onMetaFailed(quint64 gen, const QString& addonId, const QString& message);
    void  onBatchedCatalogReady(quint64            gen,
                                const QString&     addonId,
                                const QJsonObject& payload);
    void  onBatchedCatalogFailed(quint64 gen, const QString& addonId, const QString& message);
    void  ingestMetaObj(const QJsonObject& metaObj, const QString& seriesHintId);
    void  completeOne(quint64 gen);
    void  resetTransientState();
    bool  isCurrentGeneration(quint64 gen) const { return gen == m_generation; }

    bool  tryServeFreshCache(quint64 gen);
    void  saveCache(const QList<CalendarItem>& items) const;

    // Returns the catalog id whose extra-prop list declares one of the batched
    // video-id extras this engine understands ("calendarVideosIds" or
    // "lastVideosIds"). Writes the matched extra prop name into *extraPropName
    // when non-null. Empty string if addon doesn't declare the extra.
    static QString findBatchedSeriesCatalog(const tankostream::addon::AddonDescriptor& addon,
                                            QString* extraPropName);

    static QString buildCacheSignature(
        const QList<QString>& seriesIds,
        const QList<tankostream::addon::AddonDescriptor>& addons,
        int                    cap,
        const QList<QString>&  batchedAddonIds);
    static QString cacheFilePath(const QString& dataDir);
    static CalendarBucket classifyBucket(const QDate& day, const QDate& today);
    static QList<CalendarDayGroup> buildDayGroups(const QList<CalendarItem>& sortedItems,
                                                  const QDate&               today);
    static QString itemIdentityKey(const CalendarItem& item);

    tankostream::addon::AddonRegistry* m_registry = nullptr;
    StreamLibrary*                     m_library  = nullptr;
    QString                            m_dataDir;

    // Per-load generation counter. Each loadUpcoming call increments it; every
    // dispatched worker captures it at dispatch time; every mutation + emission
    // site gates on `gen == m_generation` so a stale in-flight worker from a
    // prior call cannot corrupt the counter or produce a premature emission.
    // Fix for Phase 6 REVIEW P1 #1 (re-entrancy hazard).
    quint64                            m_generation = 0;

    // Per-load transient state.
    QDateTime                          m_nowUtc;
    QDateTime                          m_untilUtc;
    QString                            m_cacheSignature;
    int                                m_pendingResponses = 0;
    QSet<QString>                      m_seenKeys;
    QList<CalendarItem>                m_items;
    QString                            m_firstError;

    // Fallback preview fields pulled from StreamLibraryEntry when the addon
    // meta response is thin. Stored by imdbId so the late-arriving response
    // from a second addon for the same series can still resolve fallbacks.
    QHash<QString, tankostream::addon::MetaItemPreview> m_libraryFallback;

    static constexpr qint64 kCacheTtlMs    = 12LL * 60LL * 60LL * 1000LL; // 12h
    static constexpr int    kSchemaVersion = 2;                            // bumped for cap+batched salt
    static constexpr int    kWindowDays    = 60;
    // Parity with stremio-core calendar.rs CALENDAR_ITEMS_COUNT = 100.
    // Fix for Phase 6 REVIEW P1 #2 (unbounded fan-out).
    static constexpr int    kMaxSeries     = 100;
};

}
