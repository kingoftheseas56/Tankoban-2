#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 - pure-logic pack-type classifier.
// Extends the Phase D EnrichedResult.completeSeries flag + detectedSeasons
// QSet into a 5-way enum that drives badges, filter chips, and the
// auto-fallback widening decision.

#include <QSet>
#include <QString>

namespace tankoban::stream::theatre {

enum class PackType {
    Unknown,         // could not classify; safe fallback (treated as Single)
    SingleEpisode,   // one episode: title has SxxExx and no range/multi/complete tokens
    MultiEpisode,    // episode range within one season: SxxExx-Exx or SxxExx.Exx
    SeasonPack,      // one complete season: Sxx tag + "complete"/"full"/"S0N.Full" hint
    MultiSeason,     // explicit range across seasons: Sxx-Sxx
    CompleteSeries   // literal "complete series" / "complete box set" / "complete collection"
};

struct PackClassification {
    PackType  type           = PackType::Unknown;
    QSet<int> detectedSeasons;     // populated from \bS\d{1,2}\b tokens; empty for Complete Series
    int       detectedEpisodeCount = 0;  // best-effort; 0 if not derivable from title
    bool      isCompleteSeries     = false;  // shortcut for type == CompleteSeries
};

// Classify a torrent title. Robust to noise (resolution, encoding, release-
// group tags). Returns Unknown only if absolutely no signal is present.
PackClassification classify(const QString& title);

// Human-readable label for badge rendering. ASCII only, no emoji per
// feedback_no_color_no_emoji.md.
QString labelForType(PackType type);

}  // namespace tankoban::stream::theatre
