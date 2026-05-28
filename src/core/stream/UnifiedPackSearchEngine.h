#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 (Task B2) - orchestrates the pack
// fan-out by subscribing to StreamAggregator::packsAvailable (Tankorent
// indexer fan-out from B1).
//
// chip-simplification 2026-05-17: Stremio addon fan-out stripped from this
// engine. The "Unified" name is retained to minimize blast radius but the
// engine is now effectively single-source (Tankorent only). Stremio addon
// results live exclusively in the Sources sidebar - duplicating them in the
// pack panel was redundant. The PackSource enum keeps the Stremio variant
// because the public EnrichedPack struct still carries it for forward-
// compatibility (and to avoid an ABI ripple); only Tankorent values are
// emitted today. Rename to TankorentPackSearchEngine deferred as a polish
// pass.

#include "core/TorrentResult.h"
#include "core/stream/PackClassifier.h"

#include <QList>
#include <QObject>
#include <QString>

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

    // Fire a search. Cancels any prior in-flight search by emitting its
    // terminal searchComplete (with the in-flight totalPacks count) so
    // consumers are never left waiting. Stale callbacks from the prior
    // search are filtered out by imdbId+season guards in
    // onTankorentPacksAvailable. Consumers should not call search()
    // concurrently from multiple threads; the engine is single-threaded
    // GUI-only.
    //
    // Results stream via packResults (currently fires once per search since
    // Stremio fan-out was stripped 2026-05-17; the multi-emit shape is
    // preserved for forward-compatibility). Terminal signal searchComplete
    // fires after the Tankorent side responds / times out.
    //
    // season == 0 -> whole-show / movie probe (Complete Series packs +
    // movie packs).
    // sourceFilter forwarded to StreamAggregator::searchPacks - "all" =
    // fan out to every Tankorent indexer (default); "<id>" = single
    // indexer. See StreamAggregator::searchPacks for valid id keys.
    // THEATRE_ANIME_CATALOG — anime=true routes to StreamAggregator's broad
    // batch-query strategy + wider per-indexer cap (anime is torrented in big
    // multi-episode batches, not "Season N"). Defaults false (unchanged path).
    void search(const QString& imdbId, const QString& showName, int season,
                const QString& sourceFilter = QStringLiteral("all"),
                bool anime = false);

private slots:
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

    tankostream::stream::StreamAggregator* m_aggregator = nullptr;
    QString           m_pendingImdb;
    QString           m_pendingShow;
    int               m_pendingSeason      = 0;
    QString           m_pendingSource      = QStringLiteral("all");
    int               m_pendingSourceCount = 0;
    int               m_totalEmitted       = 0;
};

}  // namespace tankoban::stream::theatre
