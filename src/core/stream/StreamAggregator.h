#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QUrl>

#include <memory>

#include "addon/StreamInfo.h"
#include "core/TorrentResult.h"

class QNetworkAccessManager;

namespace tankostream::addon {
class AddonRegistry;
}

namespace tankostream::stream {

struct StreamLoadRequest {
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

class StreamAggregator : public QObject
{
    Q_OBJECT

public:
    explicit StreamAggregator(tankostream::addon::AddonRegistry* registry,
                              QObject* parent = nullptr);

    // DOWNLOAD BUG 2026-06-02 — load() now returns the monotonic generation
    // token it stamped (m_loadGeneration, bumped inside load() right after
    // reset()). Callers that arm a one-shot on the SHARED streamsReady signal
    // capture this token and gate their handler on currentLoadToken()==token,
    // so a late streamsReady from a SUPERSEDED load() (rapid re-clicks) cannot
    // deliver the wrong show's streams to the currently-connected one-shot.
    quint64 load(const StreamLoadRequest& request);

    // Generation of the most recent load() — see load() above. Used by
    // StreamPage's auto-download / play / prefetch one-shots to discard stale
    // responses correlated to an earlier request.
    quint64 currentLoadToken() const { return m_loadGeneration; }

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - season-pack indexer
    // fan-out, exposed for the new UnifiedPackSearchEngine (Task B2). Wraps
    // the existing per-indexer search pattern used by TorrentPackPicker
    // (Nyaa + PirateBay + 1337x + YTS + EZTV + ExtTorrents) and emits a single
    // packsAvailable signal once all indexers have replied or the per-call
    // timeout elapses. Pass season=0 for movies / whole-show "Complete"
    // probes; pass season>0 for "Show SNN" + "Show Season N" queries.
    // showName is taken from the addon-registry / Cinemeta context by the
    // caller (UnifiedPackSearchEngine in B2); imdbId is round-tripped on
    // the signal so callers can filter when multiple searches are in
    // flight. The host class is StreamAggregator (vs a new class) per the
    // THEATRE_DOWNLOAD_OVERHAUL plan; see the chat.md B1 RTC for the
    // host-class rationale (StreamAggregator owns the per-show source-fanout
    // surface; the new method is the TorrentResult-flavored sibling of the
    // existing Stream-flavored load()).
    // sourceFilter "all" -> dispatch all 6 indexers (default).
    // sourceFilter "<id>" -> dispatch only that indexer; valid ids match the
    // dispatch loop's QStringLiteral keys (nyaa, piratebay, 1337x, yts, eztv,
    // exttorrents). Unknown ids dispatch nothing (caller responsibility).
    void searchPacks(const QString& imdbId,
                     const QString& showName,
                     int season,
                     const QString& sourceFilter = QStringLiteral("all"),
                     bool anime = false);

signals:
    void streamsReady(const QList<tankostream::addon::Stream>& streams,
                      const QHash<QString, QString>& addonsById);
    void streamError(const QString& addonId, const QString& message);

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - one terminal emit per
    // searchPacks call, with the aggregated indexer results. Listeners
    // filter on imdbId+season to disambiguate overlapping searches.
    void packsAvailable(const QString& imdbId,
                        int season,
                        const QList<TorrentResult>& results);

private:
    struct PendingAddon {
        QString addonId;
        QString addonName;
        QUrl transportUrl;
        bool inFlight = false;
    };

    struct PackSearchContext;

    void dispatchRequests();
    void onAddonReady(const QString& addonId, const QJsonObject& payload);
    void onAddonFailed(const QString& addonId, const QString& message);
    void completeOne();
    void reset();
    // DOWNLOAD BUG 2026-06-03 (review fix) — the SINGLE terminal emit path for
    // streamsReady. Always queues the emit to the NEXT event-loop turn rather
    // than firing synchronously, and drops it if a newer load() has superseded
    // `generation`. Two reasons it must never fire synchronously inside load():
    //   (1) Callers arm their one-shot before load() returns and fill the
    //       correlation token FROM load()'s return value; a synchronous emit
    //       would fire while that token is still 0 and be wrongly discarded
    //       (the result for the CURRENT request lost, leaving the UI waiting).
    //   (2) dispatchRequests() runs inside load(), and AddonTransport can emit
    //       resourceFailed SYNCHRONOUSLY (invalid URL from a persisted addon) —
    //       so completeOne() could otherwise reach this emit before load()
    //       returns. Queuing makes the post-return ordering unconditional.
    // streams/addonsById are taken by value and captured into the queued lambda
    // so a subsequent reset()/load() can't mutate them out from under the emit.
    void emitStreamsReadyDeferred(quint64 generation,
                                  QList<tankostream::addon::Stream> streams,
                                  QHash<QString, QString> addonsById);
    void finalizePackSearch(std::shared_ptr<PackSearchContext> ctx);

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    StreamLoadRequest m_request;
    QMap<QString, PendingAddon> m_pendingByAddon;
    QHash<QString, QString> m_addonsById;
    QSet<QString> m_seenIdentityKeys;
    QList<tankostream::addon::Stream> m_streams;
    int m_pendingResponses = 0;

    // DOWNLOAD BUG 2026-06-02 — monotonic request token. Bumped on every
    // load() (right after reset()) and returned to the caller so a one-shot
    // streamsReady handler can correlate the emit back to ITS request and
    // ignore stale emits from a superseded load(). Defends against the rapid
    // re-click race where a late streamsReady from an earlier load() fires the
    // currently-armed one-shot carrying the wrong request's streams.
    quint64 m_loadGeneration = 0;

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - lazily-constructed NAM shared
    // across pack-search calls (mirrors TorrentPackPicker's m_nam pattern).
    QNetworkAccessManager* m_packNam = nullptr;

    // TANKORENT audit DEFECT 2 (2026-05-28) - monotonic pack-search epoch.
    // Bumped on every searchPacks(); finalizePackSearch suppresses any context
    // whose epoch is stale so a superseded fan-out (same imdbId/season, e.g.
    // a source-filter change) cannot bleed old results into the new search.
    quint64 m_packEpoch = 0;
};

}
