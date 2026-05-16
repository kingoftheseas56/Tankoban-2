#include "core/stream/UnifiedPackSearchEngine.h"

#include "core/stream/StreamAggregator.h"
#include "core/stream/QualityScorer.h"
#include "core/stream/addon/StreamInfo.h"
#include "core/stream/addon/StreamSource.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QString>

namespace tankoban::stream::theatre {

namespace {

// Match the digits-before-word seeder count form ("245 seeders", "245 seeds",
// "245 Seeders" case-insensitive). Does NOT match the word-first form
// ("Seeders: 245") - Torrentio uses the emoji form (Pattern 2 below) for
// that shape. Verified against Torrentio sample descriptions 2026-05-16.
static const QRegularExpression kReSeedersWord(
    "(?i)\\b(\\d+)\\s*(?:seeders?|seeds?)\\b");

// Match peer-icon emoji (U+1F464 BUST IN SILHOUETTE / U+1F465 BUSTS IN
// SILHOUETTE) followed by digits. Torrentio embeds this glyph in description.
// Encoded via the \\x{...} escape so this source file stays ASCII-clean per
// brotherhood discipline.
static const QRegularExpression kReSeedersEmoji(
    "[\\x{1F464}\\x{1F465}]\\s*(\\d+)");

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
        connect(m_aggregator,
                &tankostream::stream::StreamAggregator::streamsReady, this,
                &UnifiedPackSearchEngine::onStremioStreamsReady);
        connect(m_aggregator,
                &tankostream::stream::StreamAggregator::packsAvailable, this,
                &UnifiedPackSearchEngine::onTankorentPacksAvailable);
    }
}

void UnifiedPackSearchEngine::search(const QString& imdbId,
                                     const QString& showName,
                                     int season) {
    // Reentrance discipline (per code-review C1, 2026-05-16): if a prior
    // search is in flight, force-emit its terminal searchComplete so the
    // prior consumer is not left waiting. The prior search's in-flight
    // responses will then be silently rejected by the imdbId+season guard
    // in onTankorentPacksAvailable (a stale Stremio response can still
    // slip through; a search-epoch token round-tripped via load() is the
    // proper Phase C-scope fix).
    if (m_pendingSourceCount > 0) {
        emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
    }

    m_pendingImdb        = imdbId;
    m_pendingShow        = showName;
    m_pendingSeason      = season;
    m_totalEmitted       = 0;
    m_pendingSourceCount = 2;  // Stremio + Tankorent

    if (!m_aggregator) {
        m_pendingSourceCount = 0;
        emit searchComplete(imdbId, season, 0);
        return;
    }

    // Tankorent indexer fan-out via B1's searchPacks. Terminal signal is
    // packsAvailable; one emit per call.
    m_aggregator->searchPacks(imdbId, showName, season);

    // Stremio addon fan-out via load(). Option A "episode-1 probe":
    // Stremio's stream resource is per-episode (no season-level endpoint),
    // but Torrentio-style addons attach season-pack streams to every
    // episode's pool so the episode-1 probe surfaces them. season == 0
    // -> movie request.
    tankostream::stream::StreamLoadRequest req;
    if (season <= 0) {
        req.type = QStringLiteral("movie");
        req.id   = imdbId;
    } else {
        req.type = QStringLiteral("series");
        req.id   = imdbId + QLatin1Char(':') + QString::number(season)
                          + QLatin1Char(':') + QString::number(1);
    }
    m_aggregator->load(req);
}

void UnifiedPackSearchEngine::onStremioStreamsReady(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById) {
    // No imdbId echo on streamsReady - StreamAggregator does not round-trip
    // request context with this signal. The aggregator serializes one load()
    // at a time (each load() reset()s prior state), so this callback is
    // assumed to correspond to the most-recent search() unless source-count
    // has already drained. If search() never fired (m_pendingSourceCount==0)
    // we silently drop - this protects against load() callers outside of
    // this engine triggering us. Future work: round-trip a search-id token.
    if (m_pendingSourceCount <= 0) {
        return;
    }

    QList<TorrentResult> converted;
    converted.reserve(streams.size());
    for (const auto& s : streams) {
        // streamToTorrentResult returns an empty TorrentResult for non-magnet
        // streams; the authoritative F1 defensive filter inside
        // normalizeAndEmit drops those naturally (per reviewer M1 the early
        // skip here was redundant and has been removed).
        converted.append(streamToTorrentResult(s, addonsById));
    }
    normalizeAndEmit(converted, PackSource::Stremio, QStringLiteral("Stremio"));
    if (--m_pendingSourceCount == 0) {
        emit searchComplete(m_pendingImdb, m_pendingSeason, m_totalEmitted);
    }
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
        // matters for the Tankorent path (scrapers may populate one without
        // the other); the Stremio path collapses to !infoHash.isEmpty() via
        // the toMagnetUri() invariant in StreamSource::toMagnetUri.
        if (raw.magnetUri.isEmpty() && raw.infoHash.isEmpty()) {
            continue;
        }

        EnrichedPack p;
        p.raw    = raw;
        p.source = source;
        if (source == PackSource::Tankorent && !raw.sourceName.isEmpty()) {
            p.sourceLabel = raw.sourceName;
        } else if (source == PackSource::Stremio && !raw.sourceName.isEmpty()) {
            // streamToTorrentResult fills sourceName with the addon's name
            // (resolved via addonsById lookup) when available.
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

int UnifiedPackSearchEngine::seedersFromDescription(const QString& description) {
    if (description.isEmpty()) {
        return 0;
    }
    auto m = kReSeedersWord.match(description);
    if (m.hasMatch()) {
        return m.captured(1).toInt();
    }
    m = kReSeedersEmoji.match(description);
    if (m.hasMatch()) {
        return m.captured(1).toInt();
    }
    return 0;
}

TorrentResult UnifiedPackSearchEngine::streamToTorrentResult(
    const tankostream::addon::Stream& s,
    const QHash<QString, QString>& addonsById) {
    TorrentResult r;
    if (s.source.kind != tankostream::addon::StreamSource::Kind::Magnet) {
        return r;  // not a torrent - empty result; F1 filter drops it
    }

    // Title priority: behaviorHints.filename (canonical release name) ->
    // stream.name (addon-supplied human label) -> stream.description.
    if (!s.behaviorHints.filename.isEmpty()) {
        r.title = s.behaviorHints.filename;
    } else if (!s.name.isEmpty()) {
        r.title = s.name;
    } else {
        r.title = s.description;
    }

    r.magnetUri  = s.source.toMagnetUri();
    r.infoHash   = s.source.infoHash;
    r.sizeBytes  = s.behaviorHints.videoSize;
    r.seeders    = seedersFromDescription(s.description);
    r.leechers   = 0;

    // Prefer the actual addon name (resolved via originAddonId stamped by
    // StreamAggregator::onAddonReady into behaviorHints.other) over the
    // generic "Stremio" label. Falls back to "Stremio" if not resolvable.
    QString addonName;
    const auto originIt = s.behaviorHints.other.constFind(
        QStringLiteral("originAddonId"));
    if (originIt != s.behaviorHints.other.constEnd()) {
        const QString originId = originIt.value().toString();
        const auto nameIt = addonsById.constFind(originId);
        if (nameIt != addonsById.constEnd() && !nameIt.value().isEmpty()) {
            addonName = nameIt.value();
        }
    }
    if (addonName.isEmpty()) {
        const auto nameIt = s.behaviorHints.other.constFind(
            QStringLiteral("originAddonName"));
        if (nameIt != s.behaviorHints.other.constEnd()) {
            addonName = nameIt.value().toString();
        }
    }
    r.sourceName = addonName.isEmpty()
                       ? QStringLiteral("Stremio")
                       : QStringLiteral("Stremio (") + addonName +
                             QLatin1Char(')');
    r.sourceKey  = QStringLiteral("stremio");
    return r;
}

}  // namespace tankoban::stream::theatre
