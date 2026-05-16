#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B2) - orchestrates the unified
// pack fan-out. Queries StreamAggregator::load() (Stremio addon stream-list,
// filtering for Magnet-kind streams - this is the Option A "episode-1 probe"
// per the B2 brief; whole-season probing is not supported by the Stremio
// addon protocol, but Torrentio-style addons attach season packs to per-
// episode pools so the episode-1 probe surfaces them) AND
// StreamAggregator::searchPacks() (Tankorent indexer fan-out from B1) in
// parallel. Normalizes the union into a single QList<EnrichedPack> tagged
// with PackSource (Stremio | Tankorent) + PackClassification + score.
//
// Plan deviation from the original B2 spec: B1 found that StreamAggregator
// did not have a pre-existing season-pack fan-out from the Stremio addon
// side - load() returns Stream rows (HTTP + Magnet + YouTube) not a curated
// pack list. The host-class design (Option 3) treats both Stremio and
// Tankorent symmetrically: each side is a real fan-out hosted on
// StreamAggregator; this engine subscribes to both and merges results.
// The plan's launchTankorentSearches() private helper is dropped; B1's
// searchPacks() already does that work.

#include "core/TorrentResult.h"
#include "core/stream/PackClassifier.h"

#include <QList>
#include <QObject>
#include <QString>

namespace tankostream::addon {
struct Stream;
}

namespace tankostream::stream {
class StreamAggregator;
}

namespace tankoban::stream::theatre {

enum class PackSource {
    Stremio,
    Tankorent
};

struct EnrichedPack {
    TorrentResult       raw;
    PackSource          source = PackSource::Tankorent;
    QString             sourceLabel;     // "Stremio (<addon>)" or per-indexer name
    PackClassification  classification;
    double              combinedScore = 0.0;
};

class UnifiedPackSearchEngine : public QObject {
    Q_OBJECT
public:
    explicit UnifiedPackSearchEngine(tankostream::stream::StreamAggregator* aggregator,
                                     QObject* parent = nullptr);

    // Fire a unified search. Cancels any prior in-flight search by emitting
    // its terminal searchComplete (with the in-flight totalPacks count) so
    // consumers are never left waiting. Stale callbacks from the prior
    // search are filtered out by imdbId+season guards (Tankorent path) or
    // silently ignored (Stremio path - see header preamble for limitation).
    // Consumers should not call search() concurrently from multiple threads;
    // the engine is single-threaded GUI-only.
    //
    // Results stream via packResults (can fire multiple times - one per
    // source as each responds). Terminal signal searchComplete fires after
    // all sources respond / time out.
    //
    // season == 0 -> whole-show / movie probe (Complete Series packs +
    // movie packs). For Stremio, season == 0 issues a movie-type load();
    // season > 0 issues a series-type load() with episode=1 ("Option A"
    // per the B2 brief).
    void search(const QString& imdbId, const QString& showName, int season);

private slots:
    void onStremioStreamsReady(const QList<tankostream::addon::Stream>& streams,
                               const QHash<QString, QString>& addonsById);
    void onTankorentPacksAvailable(const QString& imdbId, int season,
                                   const QList<TorrentResult>& results);

signals:
    void packResults(const QString& imdbId, int season,
                     const QList<EnrichedPack>& results);
    void searchComplete(const QString& imdbId, int season, int totalPacks);

private:
    void normalizeAndEmit(const QList<TorrentResult>& rawResults,
                          PackSource source,
                          const QString& defaultSourceLabel);

    // Parse seeder counts that Stremio addons (Torrentio in particular)
    // embed in the free-text description field. Supports both the
    // "245 seeders"/"245 seeds" word forms AND the peer-icon emoji prefix
    // (U+1F464 BUST IN SILHOUETTE / U+1F465 BUSTS IN SILHOUETTE) followed
    // by digits. Returns 0 if no signal found.
    static int seedersFromDescription(const QString& description);

    // Convert a magnet-kind Stream into a TorrentResult. Returns an empty
    // TorrentResult (magnetUri + infoHash both empty) for non-magnet
    // streams - the F1 defensive filter in normalizeAndEmit drops these.
    static TorrentResult streamToTorrentResult(const tankostream::addon::Stream& s,
                                               const QHash<QString, QString>& addonsById);

    tankostream::stream::StreamAggregator* m_aggregator = nullptr;
    QString           m_pendingImdb;
    QString           m_pendingShow;
    int               m_pendingSeason      = 0;
    int               m_pendingSourceCount = 0;
    int               m_totalEmitted       = 0;
};

}  // namespace tankoban::stream::theatre
