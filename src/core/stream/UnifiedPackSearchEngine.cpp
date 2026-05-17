#include "core/stream/UnifiedPackSearchEngine.h"

// THEATRE_DOWNLOAD_OVERHAUL chip-simplification 2026-05-17 - Stremio fan-out
// stripped. The "Unified" name is now slightly misleading - this engine is
// effectively single-source (Tankorent indexers only). Rationale: the
// Stremio addon results already live in the Sources sidebar (Torrentio
// path); duplicating them inside the pack panel was redundant + confusing.
// The Layers-3 pack panel now serves exclusively as the Tankorent custom-
// scraper viewer (broader result set than Torrentio per Hemanth's curator
// flow). Class name retained to minimize blast radius; rename to
// TankorentPackSearchEngine deferred as a separate polish pass.

#include "core/stream/StreamAggregator.h"
#include "core/stream/QualityScorer.h"

#include <QFileInfo>
#include <QString>

namespace tankoban::stream::theatre {

namespace {

// Scoring weights - mirrors the StreamDownloadIndex highest-quality-wins
// dedup weights (0.6 quality / 0.4 health). Picker will sort on
// combinedScore descending.
constexpr double kScoreQualityWeight = 0.6;
constexpr double kScoreHealthWeight  = 0.4;

}  // namespace

UnifiedPackSearchEngine::UnifiedPackSearchEngine(
    tankostream::stream::StreamAggregator* aggregator,
    QObject* parent)
    : QObject(parent), m_aggregator(aggregator) {
    if (m_aggregator) {
        // Tankorent-only path post chip-simplification 2026-05-17. The
        // streamsReady (Stremio) subscription was removed; addons are now
        // surfaced exclusively through the Sources sidebar.
        connect(m_aggregator,
                &tankostream::stream::StreamAggregator::packsAvailable, this,
                &UnifiedPackSearchEngine::onTankorentPacksAvailable);
    }
}

void UnifiedPackSearchEngine::search(const QString& imdbId,
                                     const QString& showName,
                                     int season,
                                     const QString& sourceFilter) {
    // Reentrance discipline (per code-review C1, 2026-05-16): if a prior
    // search is in flight, force-emit its terminal searchComplete so the
    // prior consumer is not left waiting. The prior search's in-flight
    // responses will then be silently rejected by the imdbId+season guard
    // in onTankorentPacksAvailable.
    if (m_pendingSourceCount > 0) {
        emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
    }

    m_pendingImdb        = imdbId;
    m_pendingShow        = showName;
    m_pendingSeason      = season;
    m_pendingSource      = sourceFilter;
    m_totalEmitted       = 0;
    m_pendingSourceCount = 1;  // Tankorent only (Stremio fan-out stripped 2026-05-17)

    if (!m_aggregator) {
        m_pendingSourceCount = 0;
        emit searchComplete(imdbId, season, 0);
        return;
    }

    // Tankorent indexer fan-out via B1's searchPacks. Terminal signal is
    // packsAvailable; one emit per call.
    m_aggregator->searchPacks(imdbId, showName, season, sourceFilter);
}

void UnifiedPackSearchEngine::onTankorentPacksAvailable(
    const QString& imdbId, int season, const QList<TorrentResult>& results) {
    // packsAvailable round-trips imdbId+season - guard against stale
    // callbacks from prior in-flight searches that landed after search()
    // was re-fired.
    if (imdbId != m_pendingImdb || season != m_pendingSeason) {
        return;
    }
    normalizeAndEmit(results, PackSource::Tankorent,
                     QStringLiteral("Tankorent"));
    if (--m_pendingSourceCount == 0) {
        emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
    }
}

void UnifiedPackSearchEngine::normalizeAndEmit(
    const QList<TorrentResult>& rawResults, PackSource source,
    const QString& defaultSourceLabel) {
    QList<EnrichedPack> enriched;
    enriched.reserve(rawResults.size());
    for (const auto& raw : rawResults) {
        // F1 defensive filter (TANKORENT_STREAM_INTEGRATION smoke 2026-05-15):
        // drop empty-magnet+empty-infoHash placeholder rows. The && shape
        // matters for the Tankorent path - scrapers may populate one without
        // the other.
        if (raw.magnetUri.isEmpty() && raw.infoHash.isEmpty()) {
            continue;
        }

        EnrichedPack p;
        p.raw    = raw;
        p.source = source;
        if (!raw.sourceName.isEmpty()) {
            p.sourceLabel = raw.sourceName;
        } else {
            p.sourceLabel = defaultSourceLabel;
        }
        p.classification = classify(raw.title);

        const QString basename = QFileInfo(raw.title).fileName();
        const int qScore = tankostream::stream::QualityScorer::qualityScore(basename);
        const int hScore = tankostream::stream::QualityScorer::healthScore(raw.seeders);
        p.combinedScore  = tankostream::stream::QualityScorer::combinedScore(
            qScore, hScore, kScoreQualityWeight, kScoreHealthWeight);

        enriched.append(p);
        ++m_totalEmitted;
    }
    emit packResults(m_pendingImdb, m_pendingSeason, enriched);
}

}  // namespace tankoban::stream::theatre
