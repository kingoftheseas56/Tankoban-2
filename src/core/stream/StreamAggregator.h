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

    void load(const StreamLoadRequest& request);

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B1) - season-pack indexer
    // fan-out, exposed for the new UnifiedPackSearchEngine (Task B2). Wraps
    // the existing per-indexer search pattern used by TorrentPackPicker
    // (PirateBay + 1337x + YTS + EZTV + ExtTorrents) and emits a single
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
    void searchPacks(const QString& imdbId,
                     const QString& showName,
                     int season);

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
    void finalizePackSearch(std::shared_ptr<PackSearchContext> ctx);

    tankostream::addon::AddonRegistry* m_registry = nullptr;

    StreamLoadRequest m_request;
    QMap<QString, PendingAddon> m_pendingByAddon;
    QHash<QString, QString> m_addonsById;
    QSet<QString> m_seenIdentityKeys;
    QList<tankostream::addon::Stream> m_streams;
    int m_pendingResponses = 0;

    // THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - lazily-constructed NAM shared
    // across pack-search calls (mirrors TorrentPackPicker's m_nam pattern).
    QNetworkAccessManager* m_packNam = nullptr;
};

}
