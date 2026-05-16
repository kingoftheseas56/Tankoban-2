#include "core/stream/TitleMetadataEstimator.h"

#include "core/stream/PackClassifier.h"

#include <QRegularExpression>
#include <algorithm>

namespace tankoban::stream::theatre {

namespace {

constexpr int kDefaultEpisodesPerSeason = 10;

// Extract explicit episode count from title patterns like "13 Episodes",
// "13 Eps", "10ep", "complete (10 eps)".
static const QRegularExpression kReEpisodeCount(
    "(?i)\\b(\\d{1,3})[\\s._-]*(?:eps?|episodes?)\\b");

// Episode-range tag SxxEyy-Ezz / SxxEyy.Ezz - captures season + lo + hi.
static const QRegularExpression kReEpisodeRange(
    "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})[\\s._-]*[-\\s.][\\s._-]*E(\\d{1,3})\\b");

// Single SxxEyy tag - captures season + episode.
static const QRegularExpression kReSingleEpisode(
    "(?i)\\bS(\\d{1,2})[\\s._-]*E(\\d{1,3})\\b");

int episodeCountFromTitle(const QString& title) {
    auto m = kReEpisodeCount.match(title);
    if (m.hasMatch())
        return m.captured(1).toInt();
    return 0;
}

}  // namespace

ScopeEstimate estimate(const QString& title) {
    ScopeEstimate out;
    if (title.isEmpty())
        return out;

    const auto classification = classify(title);
    out.isCompleteSeries = classification.isCompleteSeries;

    // Sort detected seasons ascending.
    out.detectedSeasons = classification.detectedSeasons.values();
    std::sort(out.detectedSeasons.begin(), out.detectedSeasons.end());

    // For Complete Series with no embedded count, leave episodes empty -
    // the panel renders only per-season headers until real metadata arrives.
    if (out.isCompleteSeries && out.detectedSeasons.isEmpty())
        return out;

    // For multi-episode patterns with an explicit range like SxxEyy-Ezz,
    // populate episodes for that exact span.
    if (auto m = kReEpisodeRange.match(title); m.hasMatch()) {
        const int season = m.captured(1).toInt();
        const int lo     = m.captured(2).toInt();
        const int hi     = m.captured(3).toInt();
        if (season > 0 && lo > 0 && hi >= lo) {
            out.hasExplicitEpisodeCount = true;
            for (int ep = lo; ep <= hi; ++ep) {
                EpisodeEstimate e;
                e.season  = season;
                e.episode = ep;
                out.episodes.append(e);
            }
            return out;
        }
    }

    // Single episode tag - one tile.
    if (auto m = kReSingleEpisode.match(title); m.hasMatch()) {
        EpisodeEstimate e;
        e.season  = m.captured(1).toInt();
        e.episode = m.captured(2).toInt();
        out.episodes.append(e);
        return out;
    }

    // Determine the per-season episode count: explicit if title says so,
    // otherwise default (10 episodes per season).
    const int explicitCount = episodeCountFromTitle(title);
    const int perSeason     = explicitCount > 0 ? explicitCount : kDefaultEpisodesPerSeason;
    if (explicitCount > 0)
        out.hasExplicitEpisodeCount = true;

    // For season-pack / multi-season, populate episodes 1..perSeason for each
    // detected season.
    for (int season : out.detectedSeasons) {
        for (int ep = 1; ep <= perSeason; ++ep) {
            EpisodeEstimate e;
            e.season  = season;
            e.episode = ep;
            out.episodes.append(e);
        }
    }
    return out;
}

}  // namespace tankoban::stream::theatre
